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
        "Usage:\n"
        "  %s <aeron_dir> <control_channel> <stream_id> <client_id> <max_frames>\n"
        "  %s <aeron_dir> <control_channel> <stream_id> <client_id> <max_frames> <header_uri> <pool_uri> <pool_id> <pool_stride> <header_nslots> <epoch> <layout_version>\n",
        name,
        name);
}

typedef struct tp_consumer_state_stct
{
    tp_consumer_t *consumer;
    int received;
    int limit;
}
tp_consumer_state_t;

static void on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_consumer_state_t *state = (tp_consumer_state_t *)clientd;
    tp_frame_view_t frame;

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
    tp_consumer_state_t state;
    uint32_t stream_id;
    uint32_t client_id;
    int max_frames;
    uint32_t header_nslots = 0;
    uint32_t layout_version = 0;
    uint64_t epoch = 0;
    uint16_t pool_id = 0;
    uint32_t pool_stride = 0;
    const char *header_uri = NULL;
    const char *pool_uri = NULL;
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };
    int use_driver = 1;
    size_t i;

    if (argc != 6 && argc != 13)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);
    max_frames = (int)strtol(argv[5], NULL, 10);

    if (argc == 13)
    {
        use_driver = 0;
        header_uri = argv[6];
        pool_uri = argv[7];
        pool_id = (uint16_t)strtoul(argv[8], NULL, 10);
        pool_stride = (uint32_t)strtoul(argv[9], NULL, 10);
        header_nslots = (uint32_t)strtoul(argv[10], NULL, 10);
        epoch = (uint64_t)strtoull(argv[11], NULL, 10);
        layout_version = (uint32_t)strtoul(argv[12], NULL, 10);
    }

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

    if (use_driver)
    {
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
    }
    else
    {
        pool_cfg = calloc(1, sizeof(*pool_cfg));
        if (NULL == pool_cfg)
        {
            fprintf(stderr, "Allocation failed\n");
            tp_client_close(&client);
            return 1;
        }
        pool_cfg[0].pool_id = pool_id;
        pool_cfg[0].nslots = header_nslots;
        pool_cfg[0].stride_bytes = pool_stride;
        pool_cfg[0].uri = pool_uri;
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

    consumer_context.stream_id = use_driver ? info.stream_id : stream_id;
    consumer_context.consumer_id = client_id;

    if (tp_consumer_init(&consumer, &client, &consumer_context) < 0)
    {
        fprintf(stderr, "Consumer init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        tp_client_close(&client);
        return 1;
    }

    memset(&consumer_cfg, 0, sizeof(consumer_cfg));
    consumer_cfg.stream_id = use_driver ? info.stream_id : stream_id;
    consumer_cfg.epoch = use_driver ? info.epoch : epoch;
    consumer_cfg.layout_version = use_driver ? info.layout_version : layout_version;
    consumer_cfg.header_nslots = use_driver ? info.header_nslots : header_nslots;
    consumer_cfg.header_uri = use_driver ? info.header_region_uri : header_uri;
    consumer_cfg.pools = pool_cfg;
    consumer_cfg.pool_count = use_driver ? info.pool_count : 1;

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

    tp_consumer_set_descriptor_handler(&consumer, on_descriptor, &state);

    while (state.received < state.limit)
    {
        tp_consumer_poll_control(&consumer, 10);
        tp_consumer_poll_descriptors(&consumer, 10);
    }

    tp_consumer_close(&consumer);
    free(pool_cfg);
    if (use_driver)
    {
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
    }
    tp_client_close(&client);

    return 0;
}
