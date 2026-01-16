#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"
#include "tensor_pool/tp_consumer_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s <aeron_dir> <channel> <stream_id> <client_id> [frame_count]\n", name);
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

static void log_publication_status(const char *label, aeron_publication_t *publication)
{
    if (NULL == publication)
    {
        fprintf(stderr, "%s publication unavailable\n", label);
        return;
    }

    fprintf(stderr,
        "%s publication channel=%s stream_id=%d status=%" PRId64 " connected=%d\n",
        label,
        aeron_publication_channel(publication),
        aeron_publication_stream_id(publication),
        aeron_publication_channel_status(publication),
        aeron_publication_is_connected(publication) ? 1 : 0);
}

static void wait_for_descriptor_connection(tp_producer_t *producer)
{
    const char *env = getenv("TP_EXAMPLE_WAIT_CONNECTED_MS");
    uint64_t wait_ms = 2000;
    struct timespec ts = { 0, 10 * 1000 * 1000 };
    uint64_t deadline;

    if (env && env[0] != '\0')
    {
        wait_ms = (uint64_t)strtoull(env, NULL, 10);
    }

    if (wait_ms == 0 || NULL == producer || NULL == producer->descriptor_publication)
    {
        return;
    }

    log_publication_status("Descriptor", producer->descriptor_publication);
    deadline = (uint64_t)tp_clock_now_ns() + wait_ms * 1000ULL * 1000ULL;
    while ((uint64_t)tp_clock_now_ns() < deadline)
    {
        if (aeron_publication_is_connected(producer->descriptor_publication))
        {
            fprintf(stderr, "Descriptor publication connected\n");
            return;
        }
        tp_producer_poll_control(producer, 0);
        tp_client_do_work(producer->client);
        nanosleep(&ts, NULL);
    }

    log_publication_status("Descriptor", producer->descriptor_publication);
    fprintf(stderr, "Descriptor publication not connected after %" PRIu64 " ms\n", wait_ms);
}

static int tp_producer_has_consumers(const tp_producer_t *producer)
{
    const tp_consumer_registry_t *registry;
    size_t i;

    if (NULL == producer || NULL == producer->consumer_manager)
    {
        return 0;
    }

    registry = &producer->consumer_manager->registry;
    for (i = 0; i < registry->capacity; i++)
    {
        if (registry->entries[i].in_use)
        {
            return 1;
        }
    }

    return 0;
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

    if (!producer->consumer_manager)
    {
        if (tp_producer_enable_consumer_manager(producer, 16) < 0)
        {
            fprintf(stderr, "Consumer manager enable failed: %s\n", tp_errmsg());
            return;
        }
    }

    deadline = (uint64_t)tp_clock_now_ns() + wait_ms * 1000ULL * 1000ULL;
    while ((uint64_t)tp_clock_now_ns() < deadline)
    {
        tp_producer_poll_control(producer, 10);
        if (tp_producer_has_consumers(producer))
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
    tp_client_context_t client_context;
    tp_client_t client;
    tp_driver_client_t driver;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_payload_pool_config_t *pool_cfg = NULL;
    tp_producer_t producer;
    tp_producer_context_t producer_context;
    tp_producer_config_t producer_cfg;
    tp_frame_t frame;
    tp_frame_metadata_t meta;
    tp_tensor_header_t header;
    float payload[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };
    uint32_t stream_id;
    uint32_t client_id;
    int frame_count = 1;
    int published = 0;
    const char *verbose_env;
    const char *announce_env;
    int32_t announce_stream_id = 1001;
    int verbose = 0;
    size_t i;

    if (argc < 5 || argc > 6)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);
    if (argc == 6)
    {
        frame_count = (int)strtol(argv[5], NULL, 10);
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
    announce_env = getenv("TP_EXAMPLE_ANNOUNCE_STREAM_ID");
    if (announce_env && announce_env[0] != '\0')
    {
        announce_stream_id = (int32_t)strtol(announce_env, NULL, 10);
    }

    if (tp_client_context_init(&client_context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    if (verbose)
    {
        tp_log_set_level(&client_context.base.log, TP_LOG_DEBUG);
    }

    tp_client_context_set_aeron_dir(&client_context, argv[1]);
    tp_client_context_set_control_channel(&client_context, argv[2], 1000);
    tp_client_context_set_announce_channel(&client_context, argv[2], announce_stream_id);
    tp_client_context_set_descriptor_channel(&client_context, argv[2], 1100);
    tp_client_context_set_qos_channel(&client_context, argv[2], 1200);
    tp_client_context_set_metadata_channel(&client_context, argv[2], 1300);
    tp_context_set_allowed_paths(&client_context.base, allowed_paths, 2);

    if (tp_client_init(&client, &client_context) < 0 || tp_client_start(&client) < 0)
    {
        fprintf(stderr, "Client init failed: %s\n", tp_errmsg());
        return 1;
    }

    if (tp_driver_client_init(&driver, &client) < 0)
    {
        fprintf(stderr, "Driver init failed: %s\n", tp_errmsg());
        tp_client_close(&client);
        return 1;
    }

    memset(&request, 0, sizeof(request));
    request.correlation_id = 1;
    request.stream_id = stream_id;
    request.client_id = client_id;
    request.role = TP_ROLE_PRODUCER;
    request.expected_layout_version = 0;
    request.publish_mode = TP_PUBLISH_MODE_EXISTING_OR_CREATE;
    request.require_hugepages = TP_HUGEPAGES_UNSPECIFIED;
    {
        const char *env = getenv("TP_DESIRED_NODE_ID");
        request.desired_node_id = (env && env[0] != '\0') ? (uint32_t)strtoul(env, NULL, 10) : TP_NULL_U32;
    }

    if (tp_driver_attach(&driver, &request, &info, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        fprintf(stderr, "Attach failed: %s\n", tp_errmsg());
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    if (info.code != TP_RESPONSE_OK)
    {
        fprintf(stderr, "Attach rejected: code=%d error=%s\n", info.code, info.error_message);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    pool_cfg = calloc(info.pool_count, sizeof(*pool_cfg));
    if (NULL == pool_cfg)
    {
        fprintf(stderr, "Allocation failed\n");
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    for (i = 0; i < info.pool_count; i++)
    {
        pool_cfg[i].pool_id = info.pools[i].pool_id;
        pool_cfg[i].nslots = info.pools[i].nslots;
        pool_cfg[i].stride_bytes = info.pools[i].stride_bytes;
        pool_cfg[i].uri = info.pools[i].region_uri;
    }

    if (tp_producer_context_init(&producer_context) < 0)
    {
        fprintf(stderr, "Producer context init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    producer_context.stream_id = info.stream_id;
    producer_context.producer_id = client_id;
    {
        const char *env = getenv("TP_EXAMPLE_DROP_UNCONNECTED");
        if (env && env[0] != '\0')
        {
            tp_producer_context_set_drop_unconnected_descriptors(&producer_context, true);
        }
    }

    if (tp_producer_init(&producer, &client, &producer_context) < 0)
    {
        fprintf(stderr, "Producer init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    if (verbose)
    {
        log_publication_status("Descriptor", producer.descriptor_publication);
        log_publication_status("Control", producer.control_publication);
        log_publication_status("QoS", producer.qos_publication);
        log_publication_status("Metadata", producer.metadata_publication);
    }

    wait_for_consumer(&producer);
    wait_for_descriptor_connection(&producer);

    memset(&producer_cfg, 0, sizeof(producer_cfg));
    producer_cfg.stream_id = info.stream_id;
    producer_cfg.producer_id = client_id;
    producer_cfg.epoch = info.epoch;
    producer_cfg.layout_version = info.layout_version;
    producer_cfg.header_nslots = info.header_nslots;
    producer_cfg.header_uri = info.header_region_uri;
    producer_cfg.pools = pool_cfg;
    producer_cfg.pool_count = info.pool_count;

    if (tp_producer_attach(&producer, &producer_cfg) < 0)
    {
        fprintf(stderr, "Producer attach failed: %s\n", tp_errmsg());
        tp_producer_close(&producer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    memset(&header, 0, sizeof(header));
    header.dtype = TP_DTYPE_FLOAT32;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.ndims = 2;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = 2;
    header.dims[1] = 2;

    if (tp_tensor_header_validate(&header, NULL) < 0)
    {
        fprintf(stderr, "Tensor header invalid: %s\n", tp_errmsg());
    }
    memset(&frame, 0, sizeof(frame));
    frame.tensor = &header;
    frame.payload = payload;
    frame.payload_len = sizeof(payload);
    frame.pool_id = pool_cfg[0].pool_id;
    meta.timestamp_ns = 0;
    meta.meta_version = 0;

    for (published = 0; published < frame_count; published++)
    {
        int64_t position = tp_producer_offer_frame(&producer, &frame, &meta);
        if (position < 0)
        {
            fprintf(stderr, "Publish failed: %s\n", tp_errmsg());
            break;
        }
        printf("Published frame pool_id=%u seq=%" PRIi64 "\n", pool_cfg[0].pool_id, position);
        tp_producer_poll_control(&producer, 0);
        tp_client_do_work(&client);
    }

    tp_producer_poll_control(&producer, 0);
    drive_keepalives(&client);

    tp_producer_close(&producer);
    free(pool_cfg);
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(&driver);
    tp_client_close(&client);

    return 0;
}
