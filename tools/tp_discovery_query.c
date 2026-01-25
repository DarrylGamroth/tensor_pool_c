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
        "Usage: %s <aeron_dir> <request_channel> <request_stream_id> <response_channel> <response_stream_id> [stream_id]\n",
        name);
}

static void print_json(const tp_discovery_response_t *resp)
{
    size_t i;
    size_t j;

    printf("{\"request_id\":%" PRIu64 ",\"status\":%u,\"error\":\"%s\",\"results\":[",
        resp->request_id, resp->status, resp->error_message);

    for (i = 0; i < resp->result_count; i++)
    {
        const tp_discovery_result_t *res = &resp->results[i];
        if (i > 0)
        {
            printf(",");
        }
        printf("{\"stream_id\":%u,\"producer_id\":%u,\"epoch\":%" PRIu64 ",\"layout_version\":%u,"
               "\"header_nslots\":%u,\"header_slot_bytes\":%u,\"max_dims\":%u,"
               "\"driver_instance_id\":\"%s\",\"driver_control_channel\":\"%s\",\"driver_control_stream_id\":%u,"
               "\"header_region_uri\":\"%s\",\"data_source_name\":\"%s\",\"pools\":[",
            res->stream_id,
            res->producer_id,
            res->epoch,
            res->layout_version,
            res->header_nslots,
            res->header_slot_bytes,
            res->max_dims,
            res->driver_instance_id,
            res->driver_control_channel,
            res->driver_control_stream_id,
            res->header_region_uri,
            res->data_source_name);

        for (j = 0; j < res->pool_count; j++)
        {
            if (j > 0)
            {
                printf(",");
            }
            printf("{\"pool_id\":%u,\"nslots\":%u,\"stride_bytes\":%u,\"region_uri\":\"%s\"}",
                res->pools[j].pool_id,
                res->pools[j].nslots,
                res->pools[j].stride_bytes,
                res->pools[j].region_uri);
        }
        printf("]}");
    }
    printf("]}\n");
}

int main(int argc, char **argv)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_discovery_context_t disco_ctx;
    tp_discovery_client_t disco;
    tp_discovery_request_t request;
    tp_discovery_response_t response;
    uint64_t request_id = 1;
    uint32_t stream_id = TP_NULL_U32;
    int64_t timeout_ns = 5 * 1000 * 1000 * 1000LL;

    if (argc < 6)
    {
        usage(argv[0]);
        return 1;
    }

    if (argc > 6)
    {
        stream_id = (uint32_t)strtoul(argv[6], NULL, 10);
    }

    tp_client_context_init(&ctx);
    tp_client_context_set_aeron_dir(&ctx, argv[1]);

    tp_client_init(&client, &ctx);
    if (tp_client_start(&client) < 0)
    {
        fprintf(stderr, "client start failed: %s\n", tp_errmsg());
        return 1;
    }

    tp_discovery_context_init(&disco_ctx);
    tp_discovery_context_set_channel(&disco_ctx, argv[2], (int32_t)strtol(argv[3], NULL, 10));
    tp_discovery_context_set_response_channel(&disco_ctx, argv[4], (int32_t)strtol(argv[5], NULL, 10));

    if (tp_discovery_client_init(&disco, &client, &disco_ctx) < 0)
    {
        fprintf(stderr, "discovery client init failed: %s\n", tp_errmsg());
        tp_client_close(&client);
        return 1;
    }

    tp_discovery_request_init(&request);
    request.request_id = request_id;
    request.client_id = 1;
    request.response_channel = argv[4];
    request.response_stream_id = (uint32_t)strtoul(argv[5], NULL, 10);
    request.stream_id = stream_id;

    if (tp_discovery_request(&disco, &request) < 0)
    {
        fprintf(stderr, "discovery request failed: %s\n", tp_errmsg());
        tp_discovery_client_close(&disco);
        tp_client_close(&client);
        return 1;
    }

    if (tp_discovery_poll(&disco, request_id, &response, timeout_ns) < 0)
    {
        fprintf(stderr, "discovery poll failed: %s\n", tp_errmsg());
        tp_discovery_client_close(&disco);
        tp_client_close(&client);
        return 1;
    }

    print_json(&response);
    tp_discovery_response_close(&response);
    tp_discovery_client_close(&disco);
    tp_client_close(&client);
    return 0;
}
