#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"
#include "tp_sample_util.h"

#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options] -a <aeron_dir> -c <channel> -s <stream_id>\n"
        "Options:\n"
        "  -a <dir>     Aeron directory\n"
        "  -c <chan>    Channel\n"
        "  -s <id>      Stream id\n"
        "  -i <id>      Client id (0 = auto-assign)\n"
        "  -n <count>   Frame count (default: 1)\n"
        "  -h           Show help\n",
        name);
}

static void drive_keepalives(tp_client_t *client)
{
    const char *env = getenv("TP_EXAMPLE_KEEPALIVE_MS");
    uint64_t duration_ns = 1000ULL * 1000ULL * 1000ULL;
    struct timespec ts = { 0, 10 * 1000 * 1000 };
    uint64_t deadline;

    if (env && env[0] != '\0')
    {
        duration_ns = (uint64_t)strtoull(env, NULL, 10) * 1000ULL * 1000ULL;
    }

    if (duration_ns == 0)
    {
        return;
    }

    deadline = (uint64_t)tp_clock_now_ns() + duration_ns;
    while ((uint64_t)tp_clock_now_ns() < deadline)
    {
        tp_client_do_work(client);
        nanosleep(&ts, NULL);
    }
}

typedef enum tp_example_pattern_stct
{
    TP_EXAMPLE_PATTERN_NONE = 0,
    TP_EXAMPLE_PATTERN_SEQ,
    TP_EXAMPLE_PATTERN_INTEROP
}
tp_example_pattern_t;

static tp_example_pattern_t tp_example_pattern_from_env(void)
{
    const char *env = getenv("TP_PATTERN");

    if (NULL == env || env[0] == '\0')
    {
        return TP_EXAMPLE_PATTERN_NONE;
    }

    if (strcmp(env, "interop") == 0)
    {
        return TP_EXAMPLE_PATTERN_INTEROP;
    }

    return TP_EXAMPLE_PATTERN_SEQ;
}

static void tp_example_fill_seq_pattern(uint8_t *payload, size_t len, uint64_t seq)
{
    uint8_t value = (uint8_t)(seq & 0xffu);
    if (NULL == payload || len == 0)
    {
        return;
    }
    memset(payload, value, len);
}

static void tp_example_fill_interop_pattern(uint8_t *payload, size_t len, uint64_t seq)
{
    size_t i;
    uint64_t inv;

    if (NULL == payload || len == 0)
    {
        return;
    }

    for (i = 0; i < 8 && i < len; i++)
    {
        payload[i] = (uint8_t)((seq >> (8 * i)) & 0xffu);
    }

    inv = ~seq;
    for (i = 0; i < 8 && (8 + i) < len; i++)
    {
        payload[8 + i] = (uint8_t)((inv >> (8 * i)) & 0xffu);
    }

    for (i = 16; i < len; i++)
    {
        payload[i] = (uint8_t)((seq + i) & 0xffu);
    }
}

static void wait_for_descriptor_connection(tp_client_t *client, tp_producer_t *producer)
{
    const char *env = getenv("TP_EXAMPLE_WAIT_CONNECTED_MS");
    uint64_t wait_ms = 2000;
    struct timespec ts = { 0, 10 * 1000 * 1000 };
    uint64_t deadline;
    const int poll_limit = 10;
    tp_publication_t *publication = NULL;

    if (env && env[0] != '\0')
    {
        wait_ms = (uint64_t)strtoull(env, NULL, 10);
    }

    publication = tp_producer_descriptor_publication(producer);
    if (wait_ms == 0 || NULL == producer || NULL == publication)
    {
        return;
    }

    tp_example_log_publication_status("Descriptor", publication);
    deadline = (uint64_t)tp_clock_now_ns() + wait_ms * 1000ULL * 1000ULL;
    while ((uint64_t)tp_clock_now_ns() < deadline)
    {
        if (tp_publication_is_connected(publication))
        {
            fprintf(stderr, "Descriptor publication connected\n");
            return;
        }
        tp_producer_poll_control(producer, poll_limit);
        tp_client_do_work(client);
        nanosleep(&ts, NULL);
    }

    tp_example_log_publication_status("Descriptor", publication);
    fprintf(stderr, "Descriptor publication not connected after %" PRIu64 " ms\n", wait_ms);
}

static void wait_for_consumer(tp_producer_t *producer)
{
    const char *env = getenv("TP_EXAMPLE_WAIT_CONSUMER_MS");
    uint64_t wait_ms = 0;
    struct timespec ts = { 0, 10 * 1000 * 1000 };
    uint64_t deadline;

    if (env && env[0] != '\0')
    {
        wait_ms = (uint64_t)strtoull(env, NULL, 10);
    }

    if (wait_ms == 0 || NULL == producer)
    {
        return;
    }

    if (tp_producer_enable_consumer_manager(producer, 16) < 0)
    {
        fprintf(stderr, "Consumer manager enable failed: %s\n", tp_errmsg());
        return;
    }

    deadline = (uint64_t)tp_clock_now_ns() + wait_ms * 1000ULL * 1000ULL;
    while ((uint64_t)tp_clock_now_ns() < deadline)
    {
        tp_producer_poll_control(producer, 10);
        bool has_consumers = false;
        if (tp_producer_has_consumers(producer, &has_consumers) < 0)
        {
            fprintf(stderr, "Consumer check failed: %s\n", tp_errmsg());
            return;
        }
        if (has_consumers)
        {
            fprintf(stderr, "Consumer detected, publishing frames\n");
            return;
        }
        nanosleep(&ts, NULL);
    }

    fprintf(stderr, "No consumers detected after %" PRIu64 " ms\n", wait_ms);
}

int main(int argc, char **argv)
{
    tp_context_t *client_context = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_async_attach_t *async_attach = NULL;
    tp_payload_pool_config_t *pool_cfg = NULL;
    tp_producer_t *producer = NULL;
    tp_producer_context_t producer_context;
    tp_producer_config_t producer_cfg;
    tp_frame_t frame;
    tp_frame_metadata_t meta;
    tp_tensor_header_t header;
    float payload_floats[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    uint8_t payload_bytes[16];
    void *payload_ptr = payload_floats;
    uint32_t payload_len = (uint32_t)sizeof(payload_floats);
    uint32_t resolved_client_id = 0;
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };
    uint32_t stream_id = 0;
    uint32_t client_id = 0;
    int frame_count = 1;
    int published = 0;
    const char *aeron_dir = NULL;
    const char *channel = NULL;
    const char *verbose_env;
    const char *announce_env;
    const char *publish_progress_env;
    const char *keepalive_interval_env;
    const char *print_attach_env;
    const char *require_hugepages_env;
    const char *attach_timeout_env;
    const char *trace_env;
    int32_t announce_stream_id = 1001;
    int verbose = 0;
    int trace = 0;
    int publish_progress = 0;
    int print_attach = 0;
    int64_t attach_timeout_ns = 2 * 1000 * 1000 * 1000LL;
    int poll_result = 0;
    int result = 1;
    bool client_inited = false;
    bool driver_inited = false;
    bool attach_info_valid = false;
    bool producer_inited = false;
    int opt;
    size_t i;
    size_t pool_count = 0;

    while ((opt = getopt(argc, argv, "a:c:s:i:n:h")) != -1)
    {
        switch (opt)
        {
            case 'a':
                aeron_dir = optarg;
                break;
            case 'c':
                channel = optarg;
                break;
            case 's':
                stream_id = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'i':
                client_id = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'n':
                frame_count = (int)strtol(optarg, NULL, 10);
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if ((NULL == aeron_dir || NULL == channel || stream_id == 0) && (argc - optind) >= 4)
    {
        aeron_dir = argv[optind++];
        channel = argv[optind++];
        stream_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        client_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        if (optind < argc)
        {
            frame_count = (int)strtol(argv[optind++], NULL, 10);
        }
    }

    if (NULL == aeron_dir || NULL == channel || stream_id == 0 || optind < argc)
    {
        usage(argv[0]);
        return 1;
    }

    if (frame_count <= 0)
    {
        usage(argv[0]);
        return 1;
    }

    verbose_env = getenv("TP_EXAMPLE_VERBOSE");
    if (verbose_env && verbose_env[0] != '\0')
    {
        verbose = 1;
    }
    trace_env = getenv("TP_EXAMPLE_TRACE");
    if (trace_env && trace_env[0] != '\0')
    {
        trace = 1;
    }
    announce_env = getenv("TP_EXAMPLE_ANNOUNCE_STREAM_ID");
    if (announce_env && announce_env[0] != '\0')
    {
        announce_stream_id = (int32_t)strtol(announce_env, NULL, 10);
    }
    publish_progress_env = getenv("TP_EXAMPLE_PUBLISH_PROGRESS");
    if (publish_progress_env && publish_progress_env[0] != '\0')
    {
        publish_progress = 1;
    }
    keepalive_interval_env = getenv("TP_EXAMPLE_KEEPALIVE_INTERVAL_MS");
    print_attach_env = getenv("TP_EXAMPLE_PRINT_ATTACH");
    require_hugepages_env = getenv("TP_EXAMPLE_REQUIRE_HUGEPAGES");
    attach_timeout_env = getenv("TP_EXAMPLE_ATTACH_TIMEOUT_MS");
    if (print_attach_env && print_attach_env[0] != '\0')
    {
        print_attach = 1;
    }
    if (attach_timeout_env && attach_timeout_env[0] != '\0')
    {
        attach_timeout_ns = (int64_t)strtoll(attach_timeout_env, NULL, 10) * 1000LL * 1000LL;
    }

    if (tp_example_init_client_context(
            &client_context,
            aeron_dir,
            channel,
            announce_stream_id,
            allowed_paths,
            2) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    if (trace)
    {
        tp_log_set_level(tp_context_log(client_context), TP_LOG_TRACE);
    }
    else if (verbose)
    {
        tp_log_set_level(tp_context_log(client_context), TP_LOG_DEBUG);
    }

    if (keepalive_interval_env && keepalive_interval_env[0] != '\0')
    {
        uint64_t keepalive_ns = (uint64_t)strtoull(keepalive_interval_env, NULL, 10) * 1000ULL * 1000ULL;
        tp_context_set_keepalive_interval_ns(client_context, keepalive_ns);
    }

    if (tp_client_init(&client, client_context) < 0 || tp_client_start(client) < 0)
    {
        fprintf(stderr, "Client init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    client_inited = true;

    if (tp_driver_client_init(&driver, client) < 0)
    {
        fprintf(stderr, "Driver init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    driver_inited = true;

    memset(&request, 0, sizeof(request));
    request.correlation_id = 0;
    request.stream_id = stream_id;
    request.client_id = client_id;
    request.role = TP_ROLE_PRODUCER;
    request.expected_layout_version = 0;
    request.publish_mode = TP_PUBLISH_MODE_EXISTING_OR_CREATE;
    request.require_hugepages = TP_HUGEPAGES_UNSPECIFIED;
    if (require_hugepages_env && require_hugepages_env[0] != '\0')
    {
        request.require_hugepages = TP_HUGEPAGES_HUGEPAGES;
    }
    {
        const char *env = getenv("TP_DESIRED_NODE_ID");
        request.desired_node_id = (env && env[0] != '\0') ? (uint32_t)strtoul(env, NULL, 10) : TP_NULL_U32;
    }

    if (tp_driver_attach_async(driver, &request, &async_attach) < 0)
    {
        fprintf(stderr, "Attach async failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    {
        struct timespec ts = { 0, 10 * 1000 * 1000 };
        uint64_t deadline = (uint64_t)tp_clock_now_ns() + (uint64_t)attach_timeout_ns;

        while (true)
        {
            poll_result = tp_driver_attach_poll(async_attach, &info);
            if (poll_result < 0)
            {
                fprintf(stderr, "Attach poll failed: %s\n", tp_errmsg());
                goto cleanup;
            }
            if (poll_result > 0)
            {
                attach_info_valid = true;
                break;
            }
            if ((uint64_t)tp_clock_now_ns() >= deadline)
            {
                fprintf(stderr, "Attach timed out\n");
                goto cleanup;
            }
            tp_client_do_work(client);
            nanosleep(&ts, NULL);
        }
    }

    if (info.code != TP_RESPONSE_OK)
    {
        fprintf(stderr, "Attach rejected: code=%d error=%s\n", info.code, info.error_message);
        goto cleanup;
    }
    resolved_client_id = tp_driver_client_id(driver);
    if (resolved_client_id == 0)
    {
        resolved_client_id = request.client_id;
    }
    if (print_attach)
    {
        fprintf(stderr,
            "Attach info stream=%u epoch=%" PRIu64 " layout=%u header_nslots=%u\n",
            info.stream_id,
            info.epoch,
            info.layout_version,
            info.header_nslots);
    }

    pool_cfg = calloc(info.pool_count, sizeof(*pool_cfg));
    if (NULL == pool_cfg)
    {
        fprintf(stderr, "Allocation failed\n");
        goto cleanup;
    }

    if (tp_producer_context_init(&producer_context) < 0)
    {
        fprintf(stderr, "Producer context init failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    producer_context.stream_id = info.stream_id;
    producer_context.producer_id = resolved_client_id;
    {
        const char *env = getenv("TP_EXAMPLE_DROP_UNCONNECTED");
        if (env && env[0] != '\0')
        {
            tp_producer_context_set_drop_unconnected_descriptors(&producer_context, true);
        }
    }

    if (tp_producer_init(&producer, client, &producer_context) < 0)
    {
        fprintf(stderr, "Producer init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    producer_inited = true;

    {
        const char *env = getenv("TP_EXAMPLE_ENABLE_CONSUMER_MANAGER");
        if (env && env[0] != '\0')
        {
            if (tp_producer_enable_consumer_manager(producer, 16) < 0)
            {
                fprintf(stderr, "Consumer manager enable failed: %s\n", tp_errmsg());
                goto cleanup;
            }
        }
    }

    if (verbose)
    {
        tp_example_log_publication_status("Descriptor", tp_producer_descriptor_publication(producer));
        tp_example_log_publication_status("Control", tp_producer_control_publication(producer));
        tp_example_log_publication_status("QoS", tp_producer_qos_publication(producer));
        tp_example_log_publication_status("Metadata", tp_producer_metadata_publication(producer));
    }

    wait_for_consumer(producer);
    wait_for_descriptor_connection(client, producer);

    if (tp_driver_attach_producer_config(
            &info,
            resolved_client_id,
            pool_cfg,
            info.pool_count,
            &producer_cfg,
            &pool_count) < 0)
    {
        fprintf(stderr, "Producer config failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    if (tp_producer_attach(producer, &producer_cfg) < 0)
    {
        fprintf(stderr, "Producer attach failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    {
        tp_example_pattern_t pattern = tp_example_pattern_from_env();

        if (pattern != TP_EXAMPLE_PATTERN_NONE)
        {
            payload_ptr = payload_bytes;
            payload_len = (uint32_t)sizeof(payload_bytes);
        }

        memset(&header, 0, sizeof(header));
        header.major_order = TP_MAJOR_ORDER_ROW;
        header.progress_unit = TP_PROGRESS_NONE;

        if (pattern == TP_EXAMPLE_PATTERN_NONE)
        {
            header.dtype = TP_DTYPE_FLOAT32;
            header.ndims = 2;
            header.dims[0] = 2;
            header.dims[1] = 2;
        }
        else
        {
            header.dtype = TP_DTYPE_UINT8;
            header.ndims = 1;
            header.dims[0] = (int32_t)payload_len;
        }

        if (tp_tensor_header_validate(&header, NULL) < 0)
        {
            fprintf(stderr, "Tensor header invalid: %s\n", tp_errmsg());
        }

        memset(&frame, 0, sizeof(frame));
        frame.tensor = &header;
        frame.payload = payload_ptr;
        frame.payload_len = payload_len;
        frame.pool_id = pool_cfg[0].pool_id;
        meta.timestamp_ns = 0;
        meta.meta_version = 0;

        for (published = 0; published < frame_count; published++)
        {
            tp_frame_progress_t progress;
            uint64_t seq = (uint64_t)published;
            const int poll_limit = 10;

            if (pattern == TP_EXAMPLE_PATTERN_SEQ)
            {
                tp_example_fill_seq_pattern(payload_bytes, payload_len, seq);
            }
            else if (pattern == TP_EXAMPLE_PATTERN_INTEROP)
            {
                tp_example_fill_interop_pattern(payload_bytes, payload_len, seq);
            }

            int64_t position = tp_producer_offer_frame(producer, &frame, &meta);
            if (position < 0)
            {
                fprintf(stderr, "Publish failed: %s\n", tp_errmsg());
                goto cleanup;
            }
            printf("Published frame pool_id=%u seq=%" PRIi64 "\n", pool_cfg[0].pool_id, position);
            if (publish_progress)
            {
                progress.seq = (uint64_t)position;
                progress.stream_id = info.stream_id;
                progress.epoch = info.epoch;
                progress.payload_bytes_filled = (uint64_t)frame.payload_len;
                progress.state = TP_PROGRESS_COMPLETE;
                if (tp_producer_offer_progress(producer, &progress) < 0)
                {
                    fprintf(stderr, "Progress publish failed: %s\n", tp_errmsg());
                    goto cleanup;
                }
            }
            tp_producer_poll_control(producer, poll_limit);
            tp_client_do_work(client);
        }
    }

    tp_producer_poll_control(producer, 10);
    drive_keepalives(client);

    result = 0;

cleanup:
    if (driver_inited &&
        tp_driver_client_active_lease_id(driver) != 0 &&
        tp_driver_client_publication(driver) != NULL)
    {
        if (tp_driver_detach(
                driver,
                0,
                tp_driver_client_active_lease_id(driver),
                tp_driver_client_active_stream_id(driver),
                tp_driver_client_id(driver),
                tp_driver_client_role(driver)) < 0)
        {
            fprintf(stderr, "Driver detach failed: %s\n", tp_errmsg());
        }
    }
    if (producer_inited)
    {
        tp_producer_close(producer);
    }
    free(pool_cfg);
    if (attach_info_valid)
    {
        tp_driver_attach_info_close(&info);
    }
    if (driver_inited)
    {
        tp_driver_client_close(driver);
    }
    if (client_inited)
    {
        tp_client_close(client);
    }
    return result;
}
