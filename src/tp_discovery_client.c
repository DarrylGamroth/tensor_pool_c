#include "tensor_pool/tp_discovery_client.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "aeron_alloc.h"
#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"

#include "discovery/tensor_pool/messageHeader.h"
#include "discovery/tensor_pool/discoveryRequest.h"
#include "discovery/tensor_pool/discoveryResponse.h"
#include "discovery/tensor_pool/discoveryStatus.h"

typedef struct tp_discovery_response_ctx_stct
{
    uint64_t request_id;
    tp_discovery_response_t *out;
    int done;
}
tp_discovery_response_ctx_t;

static int64_t tp_discovery_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
        return 0;
    }

    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void tp_discovery_response_handler(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_discovery_response_ctx_t *ctx = (tp_discovery_response_ctx_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_discoveryResponse response;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    (void)header;

    if (NULL == ctx || NULL == buffer || length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(&msg_header, (char *)buffer, 0, length);
    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);

    if (schema_id != tensor_pool_discoveryResponse_sbe_schema_id())
    {
        return;
    }

    if (template_id != tensor_pool_discoveryResponse_sbe_template_id())
    {
        return;
    }

    tensor_pool_discoveryResponse_wrap_for_decode(
        &response,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    if (tensor_pool_discoveryResponse_requestId(&response) != ctx->request_id)
    {
        return;
    }

    ctx->out->request_id = ctx->request_id;
    ctx->out->status = tensor_pool_discoveryResponse_status(&response);

    if (ctx->out->status != tensor_pool_discoveryStatus_OK)
    {
        const char *err = tensor_pool_discoveryResponse_errorMessage(&response);
        uint32_t len = tensor_pool_discoveryResponse_errorMessage_length(&response);
        if (len >= sizeof(ctx->out->error_message))
        {
            len = sizeof(ctx->out->error_message) - 1;
        }
        memcpy(ctx->out->error_message, err, len);
        ctx->out->error_message[len] = '\0';
        ctx->done = 1;
        return;
    }

    {
        struct tensor_pool_discoveryResponse_results results;
        size_t result_count = 0;
        size_t i;

        tensor_pool_discoveryResponse_results_wrap_for_decode(
            &results,
            (char *)buffer,
            tensor_pool_discoveryResponse_sbe_position(&response),
            version,
            length);

        result_count = (size_t)tensor_pool_discoveryResponse_results_count(&results);
        ctx->out->result_count = result_count;

        if (result_count > 0)
        {
            if (aeron_alloc((void **)&ctx->out->results, sizeof(tp_discovery_result_t) * result_count) < 0)
            {
                ctx->out->result_count = 0;
                ctx->done = 1;
                return;
            }
        }

        for (i = 0; i < result_count; i++)
        {
            tp_discovery_result_t *result = &ctx->out->results[i];
            struct tensor_pool_discoveryResponse_results_payloadPools pools;
            size_t pool_count;
            size_t p;

            if (NULL == tensor_pool_discoveryResponse_results_next(&results))
            {
                break;
            }

            memset(result, 0, sizeof(*result));
            result->stream_id = tensor_pool_discoveryResponse_results_streamId(&results);
            result->producer_id = tensor_pool_discoveryResponse_results_producerId(&results);
            result->epoch = tensor_pool_discoveryResponse_results_epoch(&results);
            result->layout_version = tensor_pool_discoveryResponse_results_layoutVersion(&results);
            result->header_nslots = tensor_pool_discoveryResponse_results_headerNslots(&results);
            result->header_slot_bytes = tensor_pool_discoveryResponse_results_headerSlotBytes(&results);
            result->max_dims = tensor_pool_discoveryResponse_results_maxDims(&results);
            result->data_source_id = tensor_pool_discoveryResponse_results_dataSourceId(&results);
            result->driver_control_stream_id = tensor_pool_discoveryResponse_results_driverControlStreamId(&results);

            {
                const char *uri = tensor_pool_discoveryResponse_results_headerRegionUri(&results);
                uint32_t len = tensor_pool_discoveryResponse_results_headerRegionUri_length(&results);
                if (len >= sizeof(result->header_region_uri))
                {
                    len = sizeof(result->header_region_uri) - 1;
                }
                memcpy(result->header_region_uri, uri, len);
                result->header_region_uri[len] = '\0';
            }

            {
                const char *name = tensor_pool_discoveryResponse_results_dataSourceName(&results);
                uint32_t len = tensor_pool_discoveryResponse_results_dataSourceName_length(&results);
                if (len >= sizeof(result->data_source_name))
                {
                    len = sizeof(result->data_source_name) - 1;
                }
                memcpy(result->data_source_name, name, len);
                result->data_source_name[len] = '\0';
            }

            {
                const char *inst = tensor_pool_discoveryResponse_results_driverInstanceId(&results);
                uint32_t len = tensor_pool_discoveryResponse_results_driverInstanceId_length(&results);
                if (len >= sizeof(result->driver_instance_id))
                {
                    len = sizeof(result->driver_instance_id) - 1;
                }
                memcpy(result->driver_instance_id, inst, len);
                result->driver_instance_id[len] = '\0';
            }

            {
                const char *channel = tensor_pool_discoveryResponse_results_driverControlChannel(&results);
                uint32_t len = tensor_pool_discoveryResponse_results_driverControlChannel_length(&results);
                if (len >= sizeof(result->driver_control_channel))
                {
                    len = sizeof(result->driver_control_channel) - 1;
                }
                memcpy(result->driver_control_channel, channel, len);
                result->driver_control_channel[len] = '\0';
            }

            tensor_pool_discoveryResponse_results_payloadPools_wrap_for_decode(
                &pools,
                (char *)buffer,
                tensor_pool_discoveryResponse_results_sbe_position(&results),
                version,
                length);

            pool_count = (size_t)tensor_pool_discoveryResponse_results_payloadPools_count(&pools);
            result->pool_count = pool_count;

            if (pool_count > 0)
            {
                if (aeron_alloc((void **)&result->pools, sizeof(tp_discovery_pool_info_t) * pool_count) < 0)
                {
                    result->pool_count = 0;
                    continue;
                }
            }

            for (p = 0; p < pool_count; p++)
            {
                tp_discovery_pool_info_t *pool = &result->pools[p];
                const char *uri;
                uint32_t len;

                if (NULL == tensor_pool_discoveryResponse_results_payloadPools_next(&pools))
                {
                    break;
                }

                pool->pool_id = tensor_pool_discoveryResponse_results_payloadPools_poolId(&pools);
                pool->nslots = tensor_pool_discoveryResponse_results_payloadPools_poolNslots(&pools);
                pool->stride_bytes = tensor_pool_discoveryResponse_results_payloadPools_strideBytes(&pools);

                uri = tensor_pool_discoveryResponse_results_payloadPools_regionUri(&pools);
                len = tensor_pool_discoveryResponse_results_payloadPools_regionUri_length(&pools);
                if (len >= sizeof(pool->region_uri))
                {
                    len = sizeof(pool->region_uri) - 1;
                }
                memcpy(pool->region_uri, uri, len);
                pool->region_uri[len] = '\0';
            }
        }
    }

    ctx->done = 1;
}

int tp_discovery_client_init(\n    tp_discovery_client_t *client,\n    const tp_context_t *context,\n    const char *request_channel,\n    int32_t request_stream_id,\n    const char *response_channel,\n    int32_t response_stream_id)
{
    if (NULL == client || NULL == context || NULL == request_channel || NULL == response_channel)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_client_init: null input");
        return -1;
    }

    memset(client, 0, sizeof(*client));

    if (tp_aeron_client_init(&client->aeron, context) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_publication(&client->publication, &client->aeron, request_channel, request_stream_id) < 0)
    {
        tp_aeron_client_close(&client->aeron);
        return -1;
    }

    if (tp_aeron_add_subscription(&client->subscription, &client->aeron, response_channel, response_stream_id, NULL, NULL, NULL, NULL) < 0)
    {
        tp_aeron_client_close(&client->aeron);
        return -1;
    }

    return 0;
}

int tp_discovery_client_close(tp_discovery_client_t *client)
{
    if (NULL == client)
    {
        return -1;
    }

    if (client->publication)
    {
        aeron_publication_close(client->publication, NULL, NULL);
        client->publication = NULL;
    }

    if (client->subscription)
    {
        aeron_subscription_close(client->subscription, NULL, NULL);
        client->subscription = NULL;
    }

    tp_aeron_client_close(&client->aeron);

    return 0;
}

int tp_discovery_request(tp_discovery_client_t *client, const tp_discovery_request_t *request)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_discoveryRequest req;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_discoveryRequest_sbe_block_length();
    int64_t result;

    if (NULL == client || NULL == request)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_request: null input");
        return -1;
    }

    if (NULL == request->response_channel || request->response_stream_id == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_request: response channel required");
        return -1;
    }

    tensor_pool_messageHeader_wrap(&msg_header, (char *)buffer, 0, sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_discoveryRequest_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_discoveryRequest_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_discoveryRequest_sbe_schema_version());

    tensor_pool_discoveryRequest_wrap_for_encode(&req, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_discoveryRequest_set_requestId(&req, request->request_id);
    tensor_pool_discoveryRequest_set_clientId(&req, request->client_id);
    tensor_pool_discoveryRequest_set_responseStreamId(&req, request->response_stream_id);
    if (request->stream_id == TP_NULL_U32)
    {
        tensor_pool_discoveryRequest_set_streamId(&req, tensor_pool_discoveryRequest_streamId_null_value());
    }
    else
    {
        tensor_pool_discoveryRequest_set_streamId(&req, request->stream_id);
    }

    if (request->producer_id == TP_NULL_U32)
    {
        tensor_pool_discoveryRequest_set_producerId(&req, tensor_pool_discoveryRequest_producerId_null_value());
    }
    else
    {
        tensor_pool_discoveryRequest_set_producerId(&req, request->producer_id);
    }

    if (request->data_source_id == TP_NULL_U64)
    {
        tensor_pool_discoveryRequest_set_dataSourceId(&req, tensor_pool_discoveryRequest_dataSourceId_null_value());
    }
    else
    {
        tensor_pool_discoveryRequest_set_dataSourceId(&req, request->data_source_id);
    }

    if (tensor_pool_discoveryRequest_put_responseChannel(&req, request->response_channel, strlen(request->response_channel)) < 0)
    {
        return -1;
    }

    if (NULL != request->data_source_name && strlen(request->data_source_name) > 0)
    {
        if (tensor_pool_discoveryRequest_put_dataSourceName(&req, request->data_source_name, strlen(request->data_source_name)) < 0)
        {
            return -1;
        }
    }

    result = aeron_publication_offer(client->publication, buffer, tensor_pool_discoveryRequest_sbe_position(&req), NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

int tp_discovery_poll(tp_discovery_client_t *client, uint64_t request_id, tp_discovery_response_t *out, int64_t timeout_ns)
{
    tp_discovery_response_ctx_t ctx;
    aeron_fragment_assembler_t *assembler = NULL;
    int64_t deadline = tp_discovery_now_ns() + timeout_ns;

    if (NULL == client || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_poll: null input");
        return -1;
    }

    memset(out, 0, sizeof(*out));

    ctx.request_id = request_id;
    ctx.out = out;
    ctx.done = 0;

    if (aeron_fragment_assembler_create(&assembler, tp_discovery_response_handler, &ctx) < 0)
    {
        return -1;
    }

    while (!ctx.done)
    {
        int fragments = aeron_subscription_poll(
            client->subscription,
            aeron_fragment_assembler_handler,
            assembler,
            10);

        if (fragments < 0)
        {
            aeron_fragment_assembler_delete(assembler);
            return -1;
        }

        if (tp_discovery_now_ns() > deadline)
        {
            TP_SET_ERR(ETIMEDOUT, "%s", "tp_discovery_poll: timeout");
            aeron_fragment_assembler_delete(assembler);
            return -1;
        }
    }

    aeron_fragment_assembler_delete(assembler);
    return 0;
}

void tp_discovery_response_close(tp_discovery_response_t *response)
{
    size_t i;

    if (NULL == response)
    {
        return;
    }

    for (i = 0; i < response->result_count; i++)
    {
        tp_discovery_result_t *result = &response->results[i];
        if (result->pools)
        {
            aeron_free(result->pools);
            result->pools = NULL;
        }
    }

    if (response->results)
    {
        aeron_free(response->results);
        response->results = NULL;
    }

    response->result_count = 0;
}
