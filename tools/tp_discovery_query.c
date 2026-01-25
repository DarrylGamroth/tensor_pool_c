#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s -a <aeron_dir> -r <request_channel> -s <request_stream_id> \\\n"
        "          -R <response_channel> -S <response_stream_id> [-t <stream_id>] [-i <client_id>]\n"
        "Options:\n"
        "  -a <dir>     Aeron directory\n"
        "  -r <chan>    Discovery request channel\n"
        "  -s <id>      Discovery request stream id\n"
        "  -R <chan>    Response channel\n"
        "  -S <id>      Response stream id\n"
        "  -t <id>      Stream id filter (optional)\n"
        "  -i <id>      Client id (optional, default 1)\n"
        "  -h           Show help\n",
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
    tp_client_t *client = NULL;
    tp_discovery_context_t disco_ctx;
    tp_discovery_client_t disco;
    tp_discovery_request_t request;
    tp_discovery_response_t response;
    uint64_t request_id = 1;
    uint32_t stream_id = TP_NULL_U32;
    uint32_t client_id = 1;
    int64_t timeout_ns = 5 * 1000 * 1000 * 1000LL;
    const char *aeron_dir = NULL;
    const char *request_channel = NULL;
    const char *response_channel = NULL;
    int32_t request_stream_id = 0;
    int32_t response_stream_id = 0;
    int opt;

    while ((opt = getopt(argc, argv, "a:r:s:R:S:t:i:h")) != -1)
    {
        switch (opt)
        {
            case 'a':
                aeron_dir = optarg;
                break;
            case 'r':
                request_channel = optarg;
                break;
            case 's':
                request_stream_id = (int32_t)strtol(optarg, NULL, 10);
                break;
            case 'R':
                response_channel = optarg;
                break;
            case 'S':
                response_stream_id = (int32_t)strtol(optarg, NULL, 10);
                break;
            case 't':
                stream_id = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'i':
                client_id = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if ((NULL == aeron_dir || NULL == request_channel || NULL == response_channel ||
            request_stream_id == 0 || response_stream_id == 0) &&
        (argc - optind) >= 5)
    {
        aeron_dir = argv[optind++];
        request_channel = argv[optind++];
        request_stream_id = (int32_t)strtol(argv[optind++], NULL, 10);
        response_channel = argv[optind++];
        response_stream_id = (int32_t)strtol(argv[optind++], NULL, 10);
        if (optind < argc)
        {
            stream_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        }
    }

    if (NULL == aeron_dir || NULL == request_channel || NULL == response_channel ||
        request_stream_id == 0 || response_stream_id == 0 || optind < argc)
    {
        usage(argv[0]);
        return 1;
    }

    tp_client_context_init(&ctx);
    tp_client_context_set_aeron_dir(&ctx, aeron_dir);

    tp_client_init(&client, &ctx);
    if (tp_client_start(client) < 0)
    {
        fprintf(stderr, "client start failed: %s\n", tp_errmsg());
        return 1;
    }

    tp_discovery_context_init(&disco_ctx);
    tp_discovery_context_set_channel(&disco_ctx, request_channel, request_stream_id);
    tp_discovery_context_set_response_channel(&disco_ctx, response_channel, response_stream_id);

    if (tp_discovery_client_init(&disco, client, &disco_ctx) < 0)
    {
        fprintf(stderr, "discovery client init failed: %s\n", tp_errmsg());
        tp_client_close(client);
        return 1;
    }

    tp_discovery_request_init(&request);
    request.request_id = request_id;
    request.client_id = client_id;
    request.response_channel = response_channel;
    request.response_stream_id = (uint32_t)response_stream_id;
    request.stream_id = stream_id;

    if (tp_discovery_request(&disco, &request) < 0)
    {
        fprintf(stderr, "discovery request failed: %s\n", tp_errmsg());
        tp_discovery_client_close(&disco);
        tp_client_close(client);
        return 1;
    }

    if (tp_discovery_poll(&disco, request_id, &response, timeout_ns) < 0)
    {
        fprintf(stderr, "discovery poll failed: %s\n", tp_errmsg());
        tp_discovery_client_close(&disco);
        tp_client_close(client);
        return 1;
    }

    print_json(&response);
    tp_discovery_response_close(&response);
    tp_discovery_client_close(&disco);
    tp_client_close(client);
    return 0;
}
