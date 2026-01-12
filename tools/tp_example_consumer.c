#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/responseCode.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TP_ROLE_CONSUMER tensor_pool_role_CONSUMER
#define TP_PUBLISH_MODE_EXISTING_OR_CREATE tensor_pool_publishMode_EXISTING_OR_CREATE
#define TP_HUGEPAGES_UNSPECIFIED tensor_pool_hugepagesPolicy_UNSPECIFIED
#define TP_RESPONSE_OK tensor_pool_responseCode_OK

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s <aeron_dir> <control_channel> <stream_id> <client_id> <max_frames>\n", name);
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
    size_t i;

    if (argc != 6)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);
    max_frames = (int)strtol(argv[5], NULL, 10);

    if (tp_client_context_init(&client_context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    tp_client_context_set_aeron_dir(&client_context, argv[1]);
    tp_client_context_set_control_channel(&client_context, argv[2], 1000);
    tp_client_context_set_descriptor_channel(&client_context, "aeron:ipc", 1100);
    tp_client_context_set_qos_channel(&client_context, "aeron:ipc", 1200);

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

    tp_consumer_set_descriptor_handler(&consumer, on_descriptor, &state);

    while (state.received < state.limit)
    {
        tp_consumer_poll_control(&consumer, 10);
        tp_consumer_poll_descriptors(&consumer, 10);
    }

    tp_consumer_close(&consumer);
    free(pool_cfg);
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(&driver);
    tp_client_close(&client);

    return 0;
}
