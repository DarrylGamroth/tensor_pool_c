#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s <aeron_dir> <channel> <stream_id> <client_id> <max_frames>\n", name);
}

typedef struct tp_consumer_sample_state_stct
{
    tp_consumer_t *consumer;
    int received;
    int limit;
    int verbose;
}
tp_consumer_sample_state_t;

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

static void log_subscription_status(
    const char *label,
    aeron_subscription_t *subscription,
    int *last_images,
    int64_t *last_status)
{
    int image_count;
    int64_t status;

    if (NULL == subscription)
    {
        return;
    }

    image_count = aeron_subscription_image_count(subscription);
    status = aeron_subscription_channel_status(subscription);
    if (image_count != *last_images || status != *last_status)
    {
        fprintf(stderr, "%s images=%d channel_status=%" PRId64 "\n", label, image_count, status);
        *last_images = image_count;
        *last_status = status;
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
    uint32_t stream_id;
    uint32_t client_id;
    int max_frames;
    const char *verbose_env;
    const char *announce_env;
    int32_t announce_stream_id = 1001;
    int verbose = 0;
    size_t i;

    if (argc != 6)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);
    max_frames = (int)strtol(argv[5], NULL, 10);
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
    fprintf(stderr,
        "Descriptor subscription config channel=%s stream_id=%d\n",
        client_context.base.descriptor_channel,
        client_context.base.descriptor_stream_id);
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
    request.role = TP_ROLE_CONSUMER;
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

    if (tp_consumer_context_init(&consumer_context) < 0)
    {
        fprintf(stderr, "Consumer context init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    consumer_context.stream_id = info.stream_id;
    consumer_context.consumer_id = client_id;
    consumer_context.hello.stream_id = info.stream_id;
    consumer_context.hello.consumer_id = client_id;

    if (tp_consumer_init(&consumer, &client, &consumer_context) < 0)
    {
        fprintf(stderr, "Consumer init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    if (verbose)
    {
        log_publication_status("Control", consumer.control_publication);
        log_publication_status("QoS", consumer.qos_publication);
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
        tp_consumer_close(&consumer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    state.consumer = &consumer;
    state.received = 0;
    state.limit = max_frames;
    state.verbose = verbose;

    tp_consumer_set_descriptor_handler(&consumer, on_descriptor, &state);

    while (state.received < state.limit)
    {
        int ctrl_fragments = tp_consumer_poll_control(&consumer, 10);
        int desc_fragments = tp_consumer_poll_descriptors(&consumer, 10);
        if (ctrl_fragments < 0 || desc_fragments < 0)
        {
            fprintf(stderr, "Poll failed: %s\n", tp_errmsg());
            break;
        }
        if (verbose && (ctrl_fragments > 0 || desc_fragments > 0))
        {
            fprintf(stderr, "Polled control=%d descriptor=%d\n", ctrl_fragments, desc_fragments);
        }
        if (!descriptor_connected && consumer.descriptor_subscription &&
            aeron_subscription_is_connected(consumer.descriptor_subscription))
        {
            descriptor_connected = 1;
            fprintf(stderr, "Descriptor subscription connected\n");
        }
        if (!control_connected && client.control_subscription &&
            aeron_subscription_is_connected(client.control_subscription))
        {
            control_connected = 1;
            fprintf(stderr, "Control subscription connected\n");
        }
        log_subscription_status(
            "Descriptor subscription",
            consumer.descriptor_subscription,
            &desc_images,
            &desc_status);
        log_subscription_status(
            "Control subscription",
            client.control_subscription,
            &ctrl_images,
            &ctrl_status);
    }

    drive_keepalives(&client);

    tp_consumer_close(&consumer);
    free(pool_cfg);
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(&driver);
    tp_client_close(&client);

    return 0;
}
