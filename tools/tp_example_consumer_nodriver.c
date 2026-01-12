#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s <aeron_dir> <control_channel> <stream_id> <client_id> <max_frames> <header_uri> <pool_uri> <pool_id> <pool_stride> <header_nslots> <epoch> <layout_version>\n",
        name);
}

typedef struct tp_consumer_state_stct
{
    tp_consumer_t *consumer;
    int received;
    int limit;
    int errors;
}
tp_consumer_state_t;

static void on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_consumer_state_t *state = (tp_consumer_state_t *)clientd;
    tp_frame_view_t frame;
    const float expected[4] = { 1.0f, 2.0f, 3.0f, 4.0f };

    if (NULL == state || NULL == desc || NULL == state->consumer)
    {
        return;
    }

    if (tp_consumer_read_frame(state->consumer, desc->seq, desc->header_index, &frame) == 0)
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
}

int main(int argc, char **argv)
{
    tp_client_context_t client_context;
    tp_client_t client;
    tp_consumer_context_t consumer_context;
    tp_consumer_t consumer;
    tp_consumer_config_t consumer_cfg;
    tp_consumer_pool_config_t pool_cfg;
    tp_consumer_state_t state;
    uint32_t stream_id;
    uint32_t client_id;
    int max_frames;
    uint32_t header_nslots;
    uint32_t layout_version;
    uint64_t epoch;
    uint16_t pool_id;
    uint32_t pool_stride;
    const char *header_uri;
    const char *pool_uri;
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };

    if (argc != 13)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);
    max_frames = (int)strtol(argv[5], NULL, 10);
    header_uri = argv[6];
    pool_uri = argv[7];
    pool_id = (uint16_t)strtoul(argv[8], NULL, 10);
    pool_stride = (uint32_t)strtoul(argv[9], NULL, 10);
    header_nslots = (uint32_t)strtoul(argv[10], NULL, 10);
    epoch = (uint64_t)strtoull(argv[11], NULL, 10);
    layout_version = (uint32_t)strtoul(argv[12], NULL, 10);

    if (tp_client_context_init(&client_context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    tp_client_context_set_aeron_dir(&client_context, argv[1]);
    tp_client_context_set_control_channel(&client_context, argv[2], 1000);
    tp_client_context_set_descriptor_channel(&client_context, "aeron:ipc", 1100);
    tp_client_context_set_qos_channel(&client_context, "aeron:ipc", 1200);
    tp_context_set_allowed_paths(&client_context.base, allowed_paths, 2);

    if (tp_client_init(&client, &client_context) < 0 || tp_client_start(&client) < 0)
    {
        fprintf(stderr, "Client init failed: %s\n", tp_errmsg());
        return 1;
    }

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = pool_id;
    pool_cfg.nslots = header_nslots;
    pool_cfg.stride_bytes = pool_stride;
    pool_cfg.uri = pool_uri;

    if (tp_consumer_context_init(&consumer_context) < 0)
    {
        fprintf(stderr, "Consumer context init failed: %s\n", tp_errmsg());
        tp_client_close(&client);
        return 1;
    }

    consumer_context.stream_id = stream_id;
    consumer_context.consumer_id = client_id;

    if (tp_consumer_init(&consumer, &client, &consumer_context) < 0)
    {
        fprintf(stderr, "Consumer init failed: %s\n", tp_errmsg());
        tp_client_close(&client);
        return 1;
    }

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
        tp_consumer_close(&consumer);
        tp_client_close(&client);
        return 1;
    }

    state.consumer = &consumer;
    state.received = 0;
    state.limit = max_frames;
    state.errors = 0;

    tp_consumer_set_descriptor_handler(&consumer, on_descriptor, &state);

    while (state.received < state.limit)
    {
        tp_consumer_poll_control(&consumer, 10);
        tp_consumer_poll_descriptors(&consumer, 10);
    }

    tp_consumer_close(&consumer);
    tp_client_close(&client);

    return state.errors == 0 ? 0 : 2;
}
