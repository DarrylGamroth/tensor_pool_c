#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_driver_client.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "aeron_alloc.h"
#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_types.h"

#include "driver/tensor_pool/messageHeader.h"
#include "driver/tensor_pool/shmAttachRequest.h"
#include "driver/tensor_pool/shmAttachResponse.h"
#include "driver/tensor_pool/shmDetachRequest.h"
#include "driver/tensor_pool/shmLeaseKeepalive.h"
#include "driver/tensor_pool/responseCode.h"

typedef struct tp_driver_response_ctx_stct
{
    int64_t correlation_id;
    tp_driver_attach_info_t *out;
    int done;
}
tp_driver_response_ctx_t;

static int64_t tp_time_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
        return 0;
    }

    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void tp_driver_response_handler(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_driver_response_ctx_t *ctx = (tp_driver_response_ctx_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmAttachResponse response;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    (void)header;

    if (NULL == ctx || NULL == buffer || length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);

    if (schema_id != tensor_pool_shmAttachResponse_sbe_schema_id())
    {
        return;
    }

    if (template_id != tensor_pool_shmAttachResponse_sbe_template_id())
    {
        return;
    }

    tensor_pool_shmAttachResponse_wrap_for_decode(
        &response,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    if (tensor_pool_shmAttachResponse_correlationId(&response) != ctx->correlation_id)
    {
        return;
    }

    {
        enum tensor_pool_responseCode code;
        if (!tensor_pool_shmAttachResponse_code(&response, &code))
        {
            ctx->out->code = tensor_pool_responseCode_INTERNAL_ERROR;
        }
        else
        {
            ctx->out->code = code;
        }
    }

    if (ctx->out->code == tensor_pool_responseCode_OK)
    {
        struct tensor_pool_shmAttachResponse_payloadPools pools;
        size_t pool_count = 0;
        size_t i = 0;

        ctx->out->lease_id = tensor_pool_shmAttachResponse_leaseId(&response);
        ctx->out->lease_expiry_timestamp_ns = tensor_pool_shmAttachResponse_leaseExpiryTimestampNs(&response);
        ctx->out->stream_id = tensor_pool_shmAttachResponse_streamId(&response);
        ctx->out->epoch = tensor_pool_shmAttachResponse_epoch(&response);
        ctx->out->layout_version = tensor_pool_shmAttachResponse_layoutVersion(&response);
        ctx->out->header_nslots = tensor_pool_shmAttachResponse_headerNslots(&response);
        ctx->out->header_slot_bytes = tensor_pool_shmAttachResponse_headerSlotBytes(&response);
        ctx->out->max_dims = tensor_pool_shmAttachResponse_maxDims(&response);

        if (ctx->out->stream_id == tensor_pool_shmAttachResponse_streamId_null_value() ||
            ctx->out->epoch == tensor_pool_shmAttachResponse_epoch_null_value() ||
            ctx->out->layout_version == tensor_pool_shmAttachResponse_layoutVersion_null_value() ||
            ctx->out->header_nslots == tensor_pool_shmAttachResponse_headerNslots_null_value() ||
            ctx->out->header_slot_bytes == tensor_pool_shmAttachResponse_headerSlotBytes_null_value() ||
            ctx->out->max_dims == tensor_pool_shmAttachResponse_maxDims_null_value())
        {
            ctx->out->code = tensor_pool_responseCode_INTERNAL_ERROR;
            strncpy(ctx->out->error_message, "attach response missing required fields", sizeof(ctx->out->error_message) - 1);
            ctx->out->error_message[sizeof(ctx->out->error_message) - 1] = '\0';
            ctx->done = 1;
            return;
        }

        if (ctx->out->header_slot_bytes != TP_HEADER_SLOT_BYTES)
        {
            ctx->out->code = tensor_pool_responseCode_INVALID_PARAMS;
            strncpy(ctx->out->error_message, "attach response header_slot_bytes mismatch", sizeof(ctx->out->error_message) - 1);
            ctx->out->error_message[sizeof(ctx->out->error_message) - 1] = '\0';
            ctx->done = 1;
            return;
        }

        {
            const char *uri = tensor_pool_shmAttachResponse_headerRegionUri(&response);
            uint32_t len = tensor_pool_shmAttachResponse_headerRegionUri_length(&response);
            if (len >= sizeof(ctx->out->header_region_uri))
            {
                len = sizeof(ctx->out->header_region_uri) - 1;
            }
            memcpy(ctx->out->header_region_uri, uri, len);
            ctx->out->header_region_uri[len] = '\0';
            if (len == 0)
            {
                ctx->out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(ctx->out->error_message, "attach response missing header region uri", sizeof(ctx->out->error_message) - 1);
                ctx->out->error_message[sizeof(ctx->out->error_message) - 1] = '\0';
                ctx->done = 1;
                return;
            }
        }

        tensor_pool_shmAttachResponse_payloadPools_wrap_for_decode(
            &pools,
            (char *)buffer,
            tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
            version,
            length);

        pool_count = (size_t)tensor_pool_shmAttachResponse_payloadPools_count(&pools);
        ctx->out->pool_count = pool_count;

        if (pool_count == 0)
        {
            ctx->out->code = tensor_pool_responseCode_INVALID_PARAMS;
            strncpy(ctx->out->error_message, "attach response missing payload pools", sizeof(ctx->out->error_message) - 1);
            ctx->out->error_message[sizeof(ctx->out->error_message) - 1] = '\0';
            ctx->done = 1;
            return;
        }

        if (pool_count > 0)
        {
            if (aeron_alloc((void **)&ctx->out->pools, sizeof(tp_driver_pool_info_t) * pool_count) < 0)
            {
                ctx->out->pool_count = 0;
                ctx->done = 1;
                return;
            }
        }

        for (i = 0; i < pool_count; i++)
        {
            tp_driver_pool_info_t *pool = &ctx->out->pools[i];
            const char *uri;
            uint32_t len;

            if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
            {
                break;
            }

            pool->pool_id = tensor_pool_shmAttachResponse_payloadPools_poolId(&pools);
            pool->nslots = tensor_pool_shmAttachResponse_payloadPools_poolNslots(&pools);
            pool->stride_bytes = tensor_pool_shmAttachResponse_payloadPools_strideBytes(&pools);

            if (pool->nslots != ctx->out->header_nslots)
            {
                ctx->out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(ctx->out->error_message, "attach response pool nslots mismatch", sizeof(ctx->out->error_message) - 1);
                ctx->out->error_message[sizeof(ctx->out->error_message) - 1] = '\0';
                ctx->done = 1;
                return;
            }

            uri = tensor_pool_shmAttachResponse_payloadPools_regionUri(&pools);
            len = tensor_pool_shmAttachResponse_payloadPools_regionUri_length(&pools);
            if (len >= sizeof(pool->region_uri))
            {
                len = sizeof(pool->region_uri) - 1;
            }
            memcpy(pool->region_uri, uri, len);
            pool->region_uri[len] = '\0';
            if (len == 0)
            {
                ctx->out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(ctx->out->error_message, "attach response missing payload uri", sizeof(ctx->out->error_message) - 1);
                ctx->out->error_message[sizeof(ctx->out->error_message) - 1] = '\0';
                ctx->done = 1;
                return;
            }
        }
    }
    else
    {
        const char *err = tensor_pool_shmAttachResponse_errorMessage(&response);
        uint32_t len = tensor_pool_shmAttachResponse_errorMessage_length(&response);

        if (len >= sizeof(ctx->out->error_message))
        {
            len = sizeof(ctx->out->error_message) - 1;
        }
        memcpy(ctx->out->error_message, err, len);
        ctx->out->error_message[len] = '\0';
    }

    ctx->done = 1;
}

int tp_driver_client_init(tp_driver_client_t *client, const tp_context_t *context)
{
    if (NULL == client || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_init: null input");
        return -1;
    }

    memset(client, 0, sizeof(*client));

    if (tp_aeron_client_init(&client->aeron, context) < 0)
    {
        return -1;
    }

    if (context->control_channel[0] == '\0' || context->control_stream_id < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_init: control channel not configured");
        return -1;
    }

    if (tp_aeron_add_publication(
        &client->publication,
        &client->aeron,
        context->control_channel,
        context->control_stream_id) < 0)
    {
        tp_aeron_client_close(&client->aeron);
        return -1;
    }

    if (tp_aeron_add_subscription(
        &client->subscription,
        &client->aeron,
        context->control_channel,
        context->control_stream_id,
        NULL,
        NULL,
        NULL,
        NULL) < 0)
    {
        tp_aeron_client_close(&client->aeron);
        return -1;
    }

    return 0;
}

int tp_driver_client_close(tp_driver_client_t *client)
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

static int tp_driver_send_attach(tp_driver_client_t *client, const tp_driver_attach_request_t *request)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmAttachRequest attach;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_shmAttachRequest_sbe_block_length();
    int64_t result;

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmAttachRequest_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmAttachRequest_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmAttachRequest_sbe_schema_version());

    tensor_pool_shmAttachRequest_wrap_for_encode(
        &attach,
        (char *)buffer,
        header_len,
        sizeof(buffer));

    tensor_pool_shmAttachRequest_set_correlationId(&attach, request->correlation_id);
    tensor_pool_shmAttachRequest_set_streamId(&attach, request->stream_id);
    tensor_pool_shmAttachRequest_set_clientId(&attach, request->client_id);
    tensor_pool_shmAttachRequest_set_role(&attach, request->role);
    tensor_pool_shmAttachRequest_set_expectedLayoutVersion(&attach, request->expected_layout_version);
    tensor_pool_shmAttachRequest_set_maxDims(&attach, 0);
    tensor_pool_shmAttachRequest_set_publishMode(&attach, request->publish_mode);
    tensor_pool_shmAttachRequest_set_requireHugepages(&attach, request->require_hugepages);

    result = aeron_publication_offer(client->publication, buffer, header_len + body_len, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

int tp_driver_attach(
    tp_driver_client_t *client,
    const tp_driver_attach_request_t *request,
    tp_driver_attach_info_t *out,
    int64_t timeout_ns)
{
    tp_driver_response_ctx_t ctx;
    aeron_fragment_assembler_t *assembler = NULL;
    int64_t deadline = tp_time_now_ns() + timeout_ns;

    if (NULL == client || NULL == request || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_attach: null input");
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->code = tensor_pool_responseCode_INTERNAL_ERROR;

    if (tp_driver_send_attach(client, request) < 0)
    {
        return -1;
    }

    ctx.correlation_id = request->correlation_id;
    ctx.out = out;
    ctx.done = 0;

    if (aeron_fragment_assembler_create(&assembler, tp_driver_response_handler, &ctx) < 0)
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
            return -1;
        }

        if (tp_time_now_ns() > deadline)
        {
            TP_SET_ERR(ETIMEDOUT, "%s", "tp_driver_attach: timeout");
            aeron_fragment_assembler_delete(assembler);
            return -1;
        }
    }

    aeron_fragment_assembler_delete(assembler);
    return 0;
}

int tp_driver_keepalive(tp_driver_client_t *client, uint64_t lease_id, uint32_t stream_id, uint32_t client_id, uint8_t role, uint64_t timestamp_ns)
{
    uint8_t buffer[128];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmLeaseKeepalive keepalive;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_shmLeaseKeepalive_sbe_block_length();
    int64_t result;

    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_keepalive: null client");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmLeaseKeepalive_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmLeaseKeepalive_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmLeaseKeepalive_sbe_schema_version());

    tensor_pool_shmLeaseKeepalive_wrap_for_encode(&keepalive, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_shmLeaseKeepalive_set_leaseId(&keepalive, lease_id);
    tensor_pool_shmLeaseKeepalive_set_streamId(&keepalive, stream_id);
    tensor_pool_shmLeaseKeepalive_set_clientId(&keepalive, client_id);
    tensor_pool_shmLeaseKeepalive_set_role(&keepalive, role);
    tensor_pool_shmLeaseKeepalive_set_clientTimestampNs(&keepalive, timestamp_ns);

    result = aeron_publication_offer(client->publication, buffer, header_len + body_len, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

int tp_driver_detach(tp_driver_client_t *client, int64_t correlation_id, uint64_t lease_id, uint32_t stream_id, uint32_t client_id, uint8_t role)
{
    uint8_t buffer[128];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmDetachRequest detach;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_shmDetachRequest_sbe_block_length();
    int64_t result;

    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_detach: null client");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmDetachRequest_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmDetachRequest_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmDetachRequest_sbe_schema_version());

    tensor_pool_shmDetachRequest_wrap_for_encode(&detach, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_shmDetachRequest_set_correlationId(&detach, correlation_id);
    tensor_pool_shmDetachRequest_set_leaseId(&detach, lease_id);
    tensor_pool_shmDetachRequest_set_streamId(&detach, stream_id);
    tensor_pool_shmDetachRequest_set_clientId(&detach, client_id);
    tensor_pool_shmDetachRequest_set_role(&detach, role);

    result = aeron_publication_offer(client->publication, buffer, header_len + body_len, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

void tp_driver_attach_info_close(tp_driver_attach_info_t *info)
{
    if (NULL == info)
    {
        return;
    }

    if (info->pools)
    {
        aeron_free(info->pools);
        info->pools = NULL;
    }

    info->pool_count = 0;
}
