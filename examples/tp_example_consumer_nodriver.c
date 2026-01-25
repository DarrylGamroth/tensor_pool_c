#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

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
        "Usage: %s [options] -a <aeron_dir> -c <channel> -s <stream_id> -n <max_frames> -H <header_uri> -P <pool_uri>\n"
        "Options:\n"
        "  -a <dir>     Aeron directory\n"
        "  -c <chan>    Control channel\n"
        "  -s <id>      Stream id\n"
        "  -i <id>      Client id (0 = auto-assign)\n"
        "  -n <count>   Max frames\n"
        "  -H <uri>     Header region URI\n"
        "  -P <uri>     Pool region URI\n"
        "  -p <id>      Pool id\n"
        "  -t <bytes>   Pool stride bytes\n"
        "  -N <nslots>  Header nslots\n"
        "  -e <epoch>   Epoch\n"
        "  -l <ver>     Layout version\n"
        "  -h           Show help\n"
        "Example header_uri: shm:file?path=/dev/shm/tensorpool-${USER}/demo/10000/1/header.ring\n"
        "Example pool_uri:   shm:file?path=/dev/shm/tensorpool-${USER}/demo/10000/1/1.pool\n",
        name);
}

typedef struct tp_consumer_sample_state_stct
{
    tp_consumer_t *consumer;
    int received;
    int limit;
    int errors;
}
tp_consumer_sample_state_t;

static void on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_consumer_sample_state_t *state = (tp_consumer_sample_state_t *)clientd;
    tp_frame_view_t frame;
    const float expected[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    int read_result;
    int64_t deadline;

    if (NULL == state || NULL == desc || NULL == state->consumer)
    {
        return;
    }

    deadline = tp_clock_now_ns() + 100 * 1000 * 1000LL;
    read_result = tp_consumer_read_frame(state->consumer, desc->seq, &frame);
    while (read_result == 1 && tp_clock_now_ns() < deadline)
    {
        struct timespec ts = { 0, 1000000 };
        nanosleep(&ts, NULL);
        read_result = tp_consumer_read_frame(state->consumer, desc->seq, &frame);
    }
    if (read_result == 0)
    {
        state->received++;
        fprintf(stdout, "Frame seq=%" PRIu64 " pool_id=%u payload=%u\n",
            desc->seq,
            frame.pool_id,
            frame.payload_len);
        if (frame.payload_len != sizeof(expected) ||
            frame.tensor.dtype != TP_DTYPE_FLOAT32 ||
            frame.tensor.major_order != TP_MAJOR_ORDER_ROW ||
            frame.tensor.ndims != 2 ||
            frame.tensor.dims[0] != 2 ||
            frame.tensor.dims[1] != 2 ||
            memcmp(frame.payload, expected, sizeof(expected)) != 0)
        {
            state->errors++;
            fprintf(stderr, "Validation failed for seq=%" PRIu64 "\n", desc->seq);
        }
        else
        {
            fprintf(stdout, "Validation ok for seq=%" PRIu64 "\n", desc->seq);
        }
    }
    else if (read_result == 1)
    {
        fprintf(stderr, "Frame not ready for seq=%" PRIu64 "\n", desc->seq);
    }
    else if (read_result < 0)
    {
        state->errors++;
        fprintf(stderr, "Read failed for seq=%" PRIu64 ": %s\n", desc->seq, tp_errmsg());
    }
}

static int wait_for_subscription(tp_client_t *client, tp_subscription_t *subscription)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        if (tp_subscription_is_connected(subscription))
        {
            return 0;
        }
        tp_client_do_work(client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

    return -1;
}

int main(int argc, char **argv)
{
    tp_client_context_t client_context;
    tp_client_t client;
    tp_consumer_context_t consumer_context;
    tp_consumer_t consumer;
    tp_consumer_config_t consumer_cfg;
    tp_consumer_pool_config_t pool_cfg;
    tp_consumer_sample_state_t state;
    uint32_t stream_id = 0;
    uint32_t client_id = 0;
    int max_frames = 0;
    uint32_t header_nslots = 0;
    uint32_t layout_version = 0;
    uint64_t epoch = 0;
    uint16_t pool_id = 0;
    uint32_t pool_stride = 0;
    const char *header_uri = NULL;
    const char *pool_uri = NULL;
    const char *aeron_dir = NULL;
    const char *channel = NULL;
    int result = 1;
    bool client_inited = false;
    bool consumer_inited = false;
    int opt;
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };

    while ((opt = getopt(argc, argv, "a:c:s:i:n:H:P:p:t:N:e:l:h")) != -1)
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
            case 'H':
                header_uri = optarg;
                break;
            case 'P':
                pool_uri = optarg;
                break;
            case 'p':
                pool_id = (uint16_t)strtoul(optarg, NULL, 10);
                break;
            case 't':
                pool_stride = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'N':
                header_nslots = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'e':
                epoch = (uint64_t)strtoull(optarg, NULL, 10);
                break;
            case 'l':
                layout_version = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if ((NULL == aeron_dir || NULL == channel || stream_id == 0 || max_frames == 0 ||
            header_uri == NULL || pool_uri == NULL) &&
        (argc - optind) >= 12)
    {
        aeron_dir = argv[optind++];
        channel = argv[optind++];
        stream_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        client_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        max_frames = (int)strtol(argv[optind++], NULL, 10);
        header_uri = argv[optind++];
        pool_uri = argv[optind++];
        pool_id = (uint16_t)strtoul(argv[optind++], NULL, 10);
        pool_stride = (uint32_t)strtoul(argv[optind++], NULL, 10);
        header_nslots = (uint32_t)strtoul(argv[optind++], NULL, 10);
        epoch = (uint64_t)strtoull(argv[optind++], NULL, 10);
        layout_version = (uint32_t)strtoul(argv[optind++], NULL, 10);
    }

    if (NULL == aeron_dir || NULL == channel || stream_id == 0 || max_frames == 0 ||
        header_uri == NULL || pool_uri == NULL || pool_stride == 0 || header_nslots == 0 || optind < argc)
    {
        usage(argv[0]);
        return 1;
    }

    if (tp_client_context_init(&client_context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    tp_client_context_set_aeron_dir(&client_context, aeron_dir);
    tp_client_context_set_control_channel(&client_context, channel, 1000);
    tp_client_context_set_descriptor_channel(&client_context, "aeron:ipc", 1100);
    tp_client_context_set_qos_channel(&client_context, "aeron:ipc", 1200);
    tp_context_set_allowed_paths(&client_context.base, allowed_paths, 2);

    if (tp_client_init(&client, &client_context) < 0 || tp_client_start(&client) < 0)
    {
        fprintf(stderr, "Client init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    client_inited = true;

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = pool_id;
    pool_cfg.nslots = header_nslots;
    pool_cfg.stride_bytes = pool_stride;
    pool_cfg.uri = pool_uri;

    if (tp_consumer_context_init(&consumer_context) < 0)
    {
        fprintf(stderr, "Consumer context init failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    consumer_context.stream_id = stream_id;
    consumer_context.consumer_id = client_id;

    if (tp_consumer_init(&consumer, &client, &consumer_context) < 0)
    {
        fprintf(stderr, "Consumer init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    consumer_inited = true;

    memset(&consumer_cfg, 0, sizeof(consumer_cfg));
    consumer_cfg.stream_id = stream_id;
    consumer_cfg.epoch = epoch;
    consumer_cfg.layout_version = layout_version;
    consumer_cfg.header_nslots = header_nslots;
    consumer_cfg.header_uri = header_uri;
    consumer_cfg.pools = &pool_cfg;
    consumer_cfg.pool_count = 1;

    if (tp_consumer_attach(&consumer, &consumer_cfg) < 0)
    {
        fprintf(stderr, "Consumer attach failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    if (wait_for_subscription(&client, consumer.descriptor_subscription) < 0)
    {
        fprintf(stderr, "Descriptor subscription not connected\n");
        goto cleanup;
    }

    state.consumer = &consumer;
    state.received = 0;
    state.limit = max_frames;
    state.errors = 0;

    tp_consumer_set_descriptor_handler(&consumer, on_descriptor, &state);

    {
        int64_t deadline = tp_clock_now_ns() + 5 * 1000 * 1000 * 1000LL;
        while (state.received < state.limit && tp_clock_now_ns() < deadline)
        {
            tp_consumer_poll_control(&consumer, 10);
            tp_consumer_poll_descriptors(&consumer, 10);
        }
        if (state.received == 0)
        {
            state.errors++;
            fprintf(stderr, "Timed out waiting for frames\n");
        }
        else if (state.received < state.limit)
        {
            fprintf(stderr, "Timed out with gaps: received=%d expected=%d\n", state.received, state.limit);
        }
    }

    result = state.errors == 0 ? 0 : 2;

cleanup:
    if (consumer_inited)
    {
        tp_consumer_close(&consumer);
    }
    if (client_inited)
    {
        tp_client_close(&client);
    }
    return result;
}
