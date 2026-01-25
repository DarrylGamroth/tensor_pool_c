#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"
#include "tp_sample_util.h"

#include <getopt.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options] -a <aeron_dir> -c <channel> -s <stream_id> -n <max_frames>\n"
        "Options:\n"
        "  -a <dir>     Aeron directory\n"
        "  -c <chan>    Channel\n"
        "  -s <id>      Stream id\n"
        "  -i <id>      Client id (0 = auto-assign)\n"
        "  -n <count>   Max frames to read\n"
        "  -h           Show help\n",
        name);
}

typedef struct tp_consumer_sample_state_stct
{
    tp_consumer_t *consumer;
    int received;
    int limit;
    int progress_received;
    int verbose;
}
tp_consumer_sample_state_t;

typedef struct tp_consumer_error_state_stct
{
    int lease_expired;
    int errcode;
}
tp_consumer_error_state_t;

static void on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_consumer_sample_state_t *state = (tp_consumer_sample_state_t *)clientd;
    tp_frame_view_t frame;
    int read_result;

    if (NULL == state || NULL == desc || NULL == state->consumer)
    {
        return;
    }

    read_result = tp_consumer_read_frame(state->consumer, desc->seq, &frame);
    if (state->verbose)
    {
        fprintf(stderr,
            "Descriptor seq=%" PRIu64 " ts=%" PRIu64 " meta=%u trace=%" PRIu64 "\n",
            desc->seq,
            desc->timestamp_ns,
            desc->meta_version,
            desc->trace_id);
    }
    if (read_result == 0)
    {
        state->received++;
        fprintf(stdout, "Frame seq=%" PRIu64 " pool_id=%u payload=%u\n",
            desc->seq,
            frame.pool_id,
            frame.payload_len);
    }
    else if (read_result < 0)
    {
        fprintf(stderr, "Frame seq=%" PRIu64 " read failed: %s\n", desc->seq, tp_errmsg());
    }
    else if (state->verbose)
    {
        fprintf(stderr, "Frame seq=%" PRIu64 " not ready\n", desc->seq);
    }
}

static void on_progress(void *clientd, const tp_frame_progress_t *progress)
{
    tp_consumer_sample_state_t *state = (tp_consumer_sample_state_t *)clientd;

    if (NULL == state || NULL == progress)
    {
        return;
    }

    state->progress_received++;
    if (state->verbose)
    {
        fprintf(stderr,
            "Progress seq=%" PRIu64 " bytes=%" PRIu64 " state=%u\n",
            progress->seq,
            progress->payload_bytes_filled,
            progress->state);
    }
}

static void on_error(void *clientd, int errcode, const char *message)
{
    tp_consumer_error_state_t *state = (tp_consumer_error_state_t *)clientd;

    if (NULL == state)
    {
        return;
    }

    state->errcode = errcode;
    if (errcode == ETIMEDOUT)
    {
        state->lease_expired = 1;
    }

    if (message)
    {
        fprintf(stderr, "[tp][ERROR] (%d) %s\n", errcode, message);
    }
}

static void tp_example_detach_driver(tp_driver_client_t *driver)
{
    if (NULL == driver)
    {
        return;
    }
    if (driver->active_lease_id != 0 && driver->publication != NULL)
    {
        if (tp_driver_detach(
            driver,
            0,
            driver->active_lease_id,
            driver->active_stream_id,
            driver->client_id,
            driver->role) < 0)
        {
            fprintf(stderr, "Driver detach failed: %s\n", tp_errmsg());
        }
    }
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

int main(int argc, char **argv)
{
    tp_client_context_t client_context;
    tp_client_t client;
    tp_driver_client_t driver;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_consumer_context_t consumer_context;
    tp_consumer_t consumer;
    tp_consumer_config_t consumer_cfg;
    tp_consumer_pool_config_t *pool_cfg = NULL;
    tp_consumer_sample_state_t state;
    int descriptor_connected = 0;
    int control_connected = 0;
    int desc_images = -1;
    int ctrl_images = -1;
    int64_t desc_status = INT64_MIN;
    int64_t ctrl_status = INT64_MIN;
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };
    uint32_t stream_id = 0;
    uint32_t client_id = 0;
    uint32_t resolved_client_id = 0;
    uint32_t request_desc_stream_id = 0;
    uint32_t request_ctrl_stream_id = 0;
    int max_frames = 0;
    const char *aeron_dir = NULL;
    const char *channel = NULL;
    int opt;
    const char *verbose_env;
    const char *trace_env;
    const char *announce_env;
    const char *per_consumer_env;
    const char *desc_stream_env;
    const char *ctrl_stream_env;
    const char *desc_channel_env;
    const char *ctrl_channel_env;
    const char *require_per_consumer_env;
    const char *max_wait_env;
    const char *require_progress_env;
    const char *poll_progress_env;
    const char *progress_min_env;
    const char *keepalive_interval_env;
    const char *expect_lease_env;
    const char *idle_before_work_env;
    const char *require_hugepages_env;
    const char *attach_timeout_env;
    const char *silent_attach_env;
    int32_t announce_stream_id = 1001;
    int verbose = 0;
    int trace = 0;
    int require_per_consumer = 0;
    int require_progress = 0;
    int poll_progress = 0;
    int expect_lease_expire = 0;
    int progress_min = 0;
    int silent_attach = 0;
    int64_t attach_timeout_ns = 2 * 1000 * 1000 * 1000LL;
    int result = 1;
    bool client_inited = false;
    bool driver_inited = false;
    bool attach_info_valid = false;
    bool consumer_inited = false;
    const char *request_desc_channel = NULL;
    const char *request_ctrl_channel = NULL;
    tp_subscription_t *last_descriptor_subscription = NULL;
    tp_subscription_t *last_control_subscription = NULL;
    int desc_assigned = 0;
    int ctrl_assigned = 0;
    uint64_t max_wait_ns = 0;
    uint64_t start_ns = 0;
    uint64_t idle_before_work_ms = 0;
    size_t i;
    int exit_status = 0;
    int loop_failed = 0;
    tp_consumer_error_state_t error_state;

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
                max_frames = (int)strtol(optarg, NULL, 10);
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if ((NULL == aeron_dir || NULL == channel || stream_id == 0 || max_frames == 0) && (argc - optind) >= 5)
    {
        aeron_dir = argv[optind++];
        channel = argv[optind++];
        stream_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        client_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        max_frames = (int)strtol(argv[optind++], NULL, 10);
    }

    if (NULL == aeron_dir || NULL == channel || stream_id == 0 || max_frames == 0 || optind < argc)
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
    per_consumer_env = getenv("TP_EXAMPLE_PER_CONSUMER");
    desc_stream_env = getenv("TP_EXAMPLE_DESC_STREAM_ID");
    ctrl_stream_env = getenv("TP_EXAMPLE_CTRL_STREAM_ID");
    desc_channel_env = getenv("TP_EXAMPLE_DESC_CHANNEL");
    ctrl_channel_env = getenv("TP_EXAMPLE_CTRL_CHANNEL");
    require_per_consumer_env = getenv("TP_EXAMPLE_REQUIRE_PER_CONSUMER");
    max_wait_env = getenv("TP_EXAMPLE_MAX_WAIT_MS");
    if (require_per_consumer_env && require_per_consumer_env[0] != '\0')
    {
        require_per_consumer = 1;
    }
    if (desc_stream_env && desc_stream_env[0] != '\0')
    {
        request_desc_stream_id = (uint32_t)strtoul(desc_stream_env, NULL, 10);
    }
    if (ctrl_stream_env && ctrl_stream_env[0] != '\0')
    {
        request_ctrl_stream_id = (uint32_t)strtoul(ctrl_stream_env, NULL, 10);
    }
    if (desc_channel_env && desc_channel_env[0] != '\0')
    {
        request_desc_channel = desc_channel_env;
    }
    if (ctrl_channel_env && ctrl_channel_env[0] != '\0')
    {
        request_ctrl_channel = ctrl_channel_env;
    }
    if (max_wait_env && max_wait_env[0] != '\0')
    {
        max_wait_ns = (uint64_t)strtoull(max_wait_env, NULL, 10) * 1000ULL * 1000ULL;
    }
    require_progress_env = getenv("TP_EXAMPLE_REQUIRE_PROGRESS");
    poll_progress_env = getenv("TP_EXAMPLE_POLL_PROGRESS");
    progress_min_env = getenv("TP_EXAMPLE_PROGRESS_MIN");
    keepalive_interval_env = getenv("TP_EXAMPLE_KEEPALIVE_INTERVAL_MS");
    expect_lease_env = getenv("TP_EXAMPLE_EXPECT_LEASE_EXPIRE");
    idle_before_work_env = getenv("TP_EXAMPLE_IDLE_BEFORE_WORK_MS");
    require_hugepages_env = getenv("TP_EXAMPLE_REQUIRE_HUGEPAGES");
    attach_timeout_env = getenv("TP_EXAMPLE_ATTACH_TIMEOUT_MS");
    silent_attach_env = getenv("TP_EXAMPLE_SILENT_ATTACH");
    if (require_progress_env && require_progress_env[0] != '\0')
    {
        require_progress = 1;
        poll_progress = 1;
    }
    if (poll_progress_env && poll_progress_env[0] != '\0')
    {
        poll_progress = 1;
    }
    if (progress_min_env && progress_min_env[0] != '\0')
    {
        progress_min = (int)strtol(progress_min_env, NULL, 10);
    }
    if (expect_lease_env && expect_lease_env[0] != '\0')
    {
        expect_lease_expire = 1;
    }
    if (idle_before_work_env && idle_before_work_env[0] != '\0')
    {
        idle_before_work_ms = (uint64_t)strtoull(idle_before_work_env, NULL, 10);
    }
    if (attach_timeout_env && attach_timeout_env[0] != '\0')
    {
        attach_timeout_ns = (int64_t)strtoll(attach_timeout_env, NULL, 10) * 1000LL * 1000LL;
    }
    if (silent_attach_env && silent_attach_env[0] != '\0')
    {
        silent_attach = 1;
    }

    if (per_consumer_env && per_consumer_env[0] != '\0')
    {
        if (request_desc_stream_id == 0)
        {
            request_desc_stream_id = 31001;
        }
        if (request_ctrl_stream_id == 0)
        {
            request_ctrl_stream_id = 32001;
        }
        if (NULL == request_desc_channel)
        {
            request_desc_channel = channel;
        }
        if (NULL == request_ctrl_channel)
        {
            request_ctrl_channel = channel;
        }
    }

    if (max_frames <= 0)
    {
        require_per_consumer = 0;
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
        tp_log_set_level(&client_context.base.log, TP_LOG_TRACE);
    }
    else if (verbose)
    {
        tp_log_set_level(&client_context.base.log, TP_LOG_DEBUG);
    }

    fprintf(stderr,
        "Descriptor subscription config channel=%s stream_id=%d\n",
        client_context.base.descriptor_channel,
        client_context.base.descriptor_stream_id);
    if (keepalive_interval_env && keepalive_interval_env[0] != '\0')
    {
        uint64_t keepalive_ns = (uint64_t)strtoull(keepalive_interval_env, NULL, 10) * 1000ULL * 1000ULL;
        tp_client_context_set_keepalive_interval_ns(&client_context, keepalive_ns);
    }

    memset(&state, 0, sizeof(state));
    memset(&error_state, 0, sizeof(error_state));
    tp_client_context_set_error_handler(&client_context, on_error, &error_state);

    if (tp_client_init(&client, &client_context) < 0 || tp_client_start(&client) < 0)
    {
        fprintf(stderr, "Client init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    client_inited = true;

    if (tp_driver_client_init(&driver, &client) < 0)
    {
        fprintf(stderr, "Driver init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    driver_inited = true;

    memset(&request, 0, sizeof(request));
    request.correlation_id = 0;
    request.stream_id = stream_id;
    request.client_id = client_id;
    request.role = TP_ROLE_CONSUMER;
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

    if (tp_driver_attach(&driver, &request, &info, attach_timeout_ns) < 0)
    {
        if (!silent_attach)
        {
            fprintf(stderr, "Attach failed: %s\n", tp_errmsg());
        }
        goto cleanup;
    }
    attach_info_valid = true;

    if (info.code != TP_RESPONSE_OK)
    {
        if (!silent_attach)
        {
            fprintf(stderr, "Attach rejected: code=%d error=%s\n", info.code, info.error_message);
        }
        goto cleanup;
    }

    resolved_client_id = driver.client_id;
    if (resolved_client_id == 0)
    {
        resolved_client_id = request.client_id;
    }

    pool_cfg = calloc(info.pool_count, sizeof(*pool_cfg));
    if (NULL == pool_cfg)
    {
        fprintf(stderr, "Allocation failed\n");
        goto cleanup;
    }

    for (i = 0; i < info.pool_count; i++)
    {
        pool_cfg[i].pool_id = info.pools[i].pool_id;
        pool_cfg[i].nslots = info.pools[i].nslots;
        pool_cfg[i].stride_bytes = info.pools[i].stride_bytes;
        pool_cfg[i].uri = info.pools[i].region_uri;
    }

    if (tp_consumer_context_init(&consumer_context) < 0)
    {
        fprintf(stderr, "Consumer context init failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    consumer_context.stream_id = info.stream_id;
    consumer_context.consumer_id = resolved_client_id;
    consumer_context.hello.stream_id = info.stream_id;
    consumer_context.hello.consumer_id = resolved_client_id;
    if (request_desc_stream_id != 0 && request_desc_channel)
    {
        consumer_context.hello.descriptor_stream_id = request_desc_stream_id;
        consumer_context.hello.descriptor_channel = request_desc_channel;
    }
    if (request_ctrl_stream_id != 0 && request_ctrl_channel)
    {
        consumer_context.hello.control_stream_id = request_ctrl_stream_id;
        consumer_context.hello.control_channel = request_ctrl_channel;
    }

    if (tp_consumer_init(&consumer, &client, &consumer_context) < 0)
    {
        fprintf(stderr, "Consumer init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    consumer_inited = true;

    last_descriptor_subscription = consumer.descriptor_subscription;
    last_control_subscription = consumer.control_subscription;

    if (verbose)
    {
        tp_example_log_publication_status("Control", consumer.control_publication);
        tp_example_log_publication_status("QoS", consumer.qos_publication);
    }

    memset(&consumer_cfg, 0, sizeof(consumer_cfg));
    consumer_cfg.stream_id = info.stream_id;
    consumer_cfg.epoch = info.epoch;
    consumer_cfg.layout_version = info.layout_version;
    consumer_cfg.header_nslots = info.header_nslots;
    consumer_cfg.header_uri = info.header_region_uri;
    consumer_cfg.pools = pool_cfg;
    consumer_cfg.pool_count = info.pool_count;

    if (tp_consumer_attach(&consumer, &consumer_cfg) < 0)
    {
        fprintf(stderr, "Consumer attach failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    state.consumer = &consumer;
    state.received = 0;
    state.limit = max_frames;
    state.progress_received = 0;
    state.verbose = verbose;

    tp_consumer_set_descriptor_handler(&consumer, on_descriptor, &state);
    if (poll_progress)
    {
        if (tp_consumer_set_progress_handler(&consumer, on_progress, &state) < 0)
        {
            fprintf(stderr, "Progress handler setup failed: %s\n", tp_errmsg());
            loop_failed = 1;
        }
    }

    if (require_progress && progress_min <= 0)
    {
        progress_min = (max_frames > 0) ? max_frames : 1;
    }

    if (idle_before_work_ms > 0)
    {
        struct timespec ts;
        ts.tv_sec = (time_t)(idle_before_work_ms / 1000ULL);
        ts.tv_nsec = (long)((idle_before_work_ms % 1000ULL) * 1000ULL * 1000ULL);
        nanosleep(&ts, NULL);
    }

    start_ns = (uint64_t)tp_clock_now_ns();
    while (!loop_failed && state.received < state.limit)
    {
        int work = tp_client_do_work(&client);
        int ctrl_fragments = tp_consumer_poll_control(&consumer, 10);
        int desc_fragments = tp_consumer_poll_descriptors(&consumer, 10);
        int progress_fragments = 0;
        if (poll_progress)
        {
            progress_fragments = tp_consumer_poll_progress(&consumer, 10);
        }
        if (work < 0)
        {
            if (expect_lease_expire && error_state.lease_expired)
            {
                break;
            }
            fprintf(stderr, "Client work failed: %s\n", tp_errmsg());
            loop_failed = 1;
            break;
        }
        if (ctrl_fragments < 0 || desc_fragments < 0)
        {
            fprintf(stderr, "Poll failed: %s\n", tp_errmsg());
            loop_failed = 1;
            break;
        }
        if (progress_fragments < 0)
        {
            fprintf(stderr, "Progress poll failed: %s\n", tp_errmsg());
            loop_failed = 1;
            break;
        }
        if (verbose && (ctrl_fragments > 0 || desc_fragments > 0))
        {
            fprintf(stderr, "Polled control=%d descriptor=%d\n", ctrl_fragments, desc_fragments);
        }
        if (consumer.descriptor_subscription != last_descriptor_subscription)
        {
            fprintf(stderr, "Descriptor subscription switched\n");
            last_descriptor_subscription = consumer.descriptor_subscription;
        }
        if (consumer.control_subscription != last_control_subscription && consumer.control_subscription != NULL)
        {
            fprintf(stderr, "Per-consumer control subscription active\n");
            last_control_subscription = consumer.control_subscription;
        }
        if (request_desc_stream_id != 0 &&
            consumer.assigned_descriptor_stream_id == request_desc_stream_id)
        {
            desc_assigned = 1;
        }
        if (request_ctrl_stream_id != 0 &&
            consumer.assigned_control_stream_id == request_ctrl_stream_id)
        {
            ctrl_assigned = 1;
        }
        if (!descriptor_connected && consumer.descriptor_subscription &&
            tp_subscription_is_connected(consumer.descriptor_subscription))
        {
            descriptor_connected = 1;
            fprintf(stderr, "Descriptor subscription connected\n");
        }
        if (!control_connected && tp_client_control_subscription(&client) &&
            tp_subscription_is_connected(tp_client_control_subscription(&client)))
        {
            control_connected = 1;
            fprintf(stderr, "Control subscription connected\n");
        }
        tp_example_log_subscription_status(
            "Descriptor subscription",
            consumer.descriptor_subscription,
            &desc_images,
            &desc_status);
        tp_example_log_subscription_status(
            "Control subscription",
            tp_client_control_subscription(&client),
            &ctrl_images,
            &ctrl_status);
        if (state.received >= state.limit)
        {
            break;
        }
        if (expect_lease_expire && error_state.lease_expired)
        {
            break;
        }
        if (max_wait_ns > 0 && (uint64_t)tp_clock_now_ns() - start_ns >= max_wait_ns)
        {
            fprintf(stderr, "Timed out waiting for frames\n");
            loop_failed = 1;
            break;
        }
    }

    if (!expect_lease_expire)
    {
        drive_keepalives(&client);
    }

    result = exit_status;

cleanup:
    if (consumer_inited)
    {
        tp_consumer_close(&consumer);
    }
    free(pool_cfg);
    if (attach_info_valid)
    {
        tp_driver_attach_info_close(&info);
    }
    if (driver_inited)
    {
        tp_example_detach_driver(&driver);
        tp_driver_client_close(&driver);
    }
    if (client_inited)
    {
        tp_client_close(&client);
    }

    if (require_per_consumer)
    {
        if (request_desc_stream_id != 0 && !desc_assigned)
        {
            fprintf(stderr, "Per-consumer descriptor stream not assigned\n");
            result = 1;
        }
        if (request_ctrl_stream_id != 0 && !ctrl_assigned)
        {
            fprintf(stderr, "Per-consumer control stream not assigned\n");
            result = 1;
        }
    }
    if (require_progress && state.progress_received < progress_min)
    {
        fprintf(stderr, "Progress updates missing (got=%d expected=%d)\n",
            state.progress_received,
            progress_min);
        result = 1;
    }
    if (expect_lease_expire && !error_state.lease_expired)
    {
        fprintf(stderr, "Expected lease expiry not observed\n");
        result = 1;
    }
    if (!expect_lease_expire && max_wait_ns > 0 && state.received < state.limit)
    {
        result = 1;
    }
    if (loop_failed)
    {
        result = 1;
    }

    return result;
}
