#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_driver_client.h"

#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include "aeron_alloc.h"
#include "aeron_fragment_assembler.h"
#include "aeron_agent.h"

#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_types.h"

#include "driver/tensor_pool/messageHeader.h"
#include "driver/tensor_pool/shmAttachRequest.h"
#include "driver/tensor_pool/shmAttachResponse.h"
#include "driver/tensor_pool/shmDetachResponse.h"
#include "driver/tensor_pool/shmDetachRequest.h"
#include "driver/tensor_pool/shmDriverShutdown.h"
#include "driver/tensor_pool/shmLeaseKeepalive.h"
#include "driver/tensor_pool/shmLeaseRevoked.h"
#include "driver/tensor_pool/leaseRevokeReason.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/shutdownReason.h"

typedef struct tp_driver_response_ctx_stct
{
    int64_t correlation_id;
    tp_driver_attach_info_t *out;
    tp_driver_client_t *client;
    int done;
}
tp_driver_response_ctx_t;

static void tp_copy_ascii(char *dst, size_t dst_len, const char *src, uint32_t src_len)
{
    uint32_t len = src_len;

    if (dst_len == 0)
    {
        return;
    }

    if (NULL == src)
    {
        dst[0] = '\0';
        return;
    }

    if (len >= dst_len)
    {
        len = (uint32_t)dst_len - 1;
    }

    if (len > 0)
    {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

static int tp_driver_send_attach(tp_driver_client_t *client, const tp_driver_attach_request_t *request);
static int tp_driver_client_ready(tp_driver_client_t *client);
static int tp_driver_wait_connected(tp_driver_client_t *client, int64_t deadline_ns);

int tp_driver_decode_attach_response(
    const uint8_t *buffer,
    size_t length,
    int64_t correlation_id,
    tp_driver_attach_info_t *out)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmAttachResponse response;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    if (NULL == buffer || NULL == out || length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_attach_response: invalid input");
        return -1;
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

    if (schema_id != tensor_pool_shmAttachResponse_sbe_schema_id() ||
        template_id != tensor_pool_shmAttachResponse_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_shmAttachResponse_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_attach_response: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_shmAttachResponse_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_attach_response: block length mismatch");
        return -1;
    }

    tensor_pool_shmAttachResponse_wrap_for_decode(
        &response,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    if (tensor_pool_shmAttachResponse_correlationId(&response) != correlation_id)
    {
        return 1;
    }

    {
        enum tensor_pool_responseCode code;
        if (!tensor_pool_shmAttachResponse_code(&response, &code))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_attach_response: invalid response code");
            return -1;
        }
        else
        {
            out->code = code;
        }
    }

    if (out->code == tensor_pool_responseCode_OK)
    {
        struct tensor_pool_shmAttachResponse_payloadPools pools;
        size_t pool_count = 0;
        size_t i = 0;

        out->lease_id = tensor_pool_shmAttachResponse_leaseId(&response);
        out->lease_expiry_timestamp_ns = tensor_pool_shmAttachResponse_leaseExpiryTimestampNs(&response);
        if (out->lease_expiry_timestamp_ns == tensor_pool_shmAttachResponse_leaseExpiryTimestampNs_null_value())
        {
            out->lease_expiry_timestamp_ns = TP_NULL_U64;
        }
        out->stream_id = tensor_pool_shmAttachResponse_streamId(&response);
        out->epoch = tensor_pool_shmAttachResponse_epoch(&response);
        out->layout_version = tensor_pool_shmAttachResponse_layoutVersion(&response);
        out->header_nslots = tensor_pool_shmAttachResponse_headerNslots(&response);
        out->header_slot_bytes = tensor_pool_shmAttachResponse_headerSlotBytes(&response);
        out->node_id = tensor_pool_shmAttachResponse_nodeId(&response);
        if (out->node_id == tensor_pool_shmAttachResponse_nodeId_null_value())
        {
            out->node_id = TP_NULL_U32;
        }

        if (out->lease_id == tensor_pool_shmAttachResponse_leaseId_null_value() ||
            out->stream_id == tensor_pool_shmAttachResponse_streamId_null_value() ||
            out->epoch == tensor_pool_shmAttachResponse_epoch_null_value() ||
            out->layout_version == tensor_pool_shmAttachResponse_layoutVersion_null_value() ||
            out->header_nslots == tensor_pool_shmAttachResponse_headerNslots_null_value() ||
            out->header_slot_bytes == tensor_pool_shmAttachResponse_headerSlotBytes_null_value())
        {
            out->code = tensor_pool_responseCode_INTERNAL_ERROR;
            strncpy(out->error_message, "attach response missing required fields", sizeof(out->error_message) - 1);
            out->error_message[sizeof(out->error_message) - 1] = '\0';
            return 0;
        }

        if (out->header_slot_bytes != TP_HEADER_SLOT_BYTES)
        {
            out->code = tensor_pool_responseCode_INVALID_PARAMS;
            strncpy(out->error_message, "attach response header_slot_bytes mismatch", sizeof(out->error_message) - 1);
            out->error_message[sizeof(out->error_message) - 1] = '\0';
            return 0;
        }

        if (out->header_nslots == 0)
        {
            out->code = tensor_pool_responseCode_INVALID_PARAMS;
            strncpy(out->error_message, "attach response header_nslots invalid", sizeof(out->error_message) - 1);
            out->error_message[sizeof(out->error_message) - 1] = '\0';
            return 0;
        }

        tensor_pool_shmAttachResponse_payloadPools_wrap_for_decode(
            &pools,
            (char *)buffer,
            tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
            version,
            length);

        pool_count = (size_t)tensor_pool_shmAttachResponse_payloadPools_count(&pools);
        out->pool_count = pool_count;

        if (pool_count == 0)
        {
            out->code = tensor_pool_responseCode_INVALID_PARAMS;
            strncpy(out->error_message, "attach response missing payload pools", sizeof(out->error_message) - 1);
            out->error_message[sizeof(out->error_message) - 1] = '\0';
            return 0;
        }

        if (pool_count > 0)
        {
            if (aeron_alloc((void **)&out->pools, sizeof(tp_driver_pool_info_t) * pool_count) < 0)
            {
                out->pool_count = 0;
                out->code = tensor_pool_responseCode_INTERNAL_ERROR;
                strncpy(out->error_message, "attach response pool allocation failed", sizeof(out->error_message) - 1);
                out->error_message[sizeof(out->error_message) - 1] = '\0';
                return 0;
            }
        }

        for (i = 0; i < pool_count; i++)
        {
            tp_driver_pool_info_t *pool = &out->pools[i];
            const char *uri;
            uint32_t len;

            if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
            {
                break;
            }

            pool->pool_id = tensor_pool_shmAttachResponse_payloadPools_poolId(&pools);
            pool->nslots = tensor_pool_shmAttachResponse_payloadPools_poolNslots(&pools);
            pool->stride_bytes = tensor_pool_shmAttachResponse_payloadPools_strideBytes(&pools);

            if (pool->nslots == tensor_pool_shmAttachResponse_payloadPools_poolNslots_null_value() ||
                pool->stride_bytes == tensor_pool_shmAttachResponse_payloadPools_strideBytes_null_value())
            {
                out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(out->error_message, "attach response pool missing required fields", sizeof(out->error_message) - 1);
                out->error_message[sizeof(out->error_message) - 1] = '\0';
                return 0;
            }

            if (pool->nslots != out->header_nslots)
            {
                out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(out->error_message, "attach response pool nslots mismatch", sizeof(out->error_message) - 1);
                out->error_message[sizeof(out->error_message) - 1] = '\0';
                return 0;
            }

            len = tensor_pool_shmAttachResponse_payloadPools_regionUri_length(&pools);
            uri = tensor_pool_shmAttachResponse_payloadPools_regionUri(&pools);
            if (NULL == uri)
            {
                out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(out->error_message, "attach response invalid payload uri", sizeof(out->error_message) - 1);
                out->error_message[sizeof(out->error_message) - 1] = '\0';
                return 0;
            }
            tp_copy_ascii(pool->region_uri, sizeof(pool->region_uri), uri, len);
            if (pool->region_uri[0] == '\0')
            {
                out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(out->error_message, "attach response missing payload uri", sizeof(out->error_message) - 1);
                out->error_message[sizeof(out->error_message) - 1] = '\0';
                return 0;
            }
        }

        {
            uint32_t len = tensor_pool_shmAttachResponse_headerRegionUri_length(&response);
            const char *uri = tensor_pool_shmAttachResponse_headerRegionUri(&response);
            if (NULL == uri)
            {
                out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(out->error_message, "attach response invalid header uri", sizeof(out->error_message) - 1);
                out->error_message[sizeof(out->error_message) - 1] = '\0';
                return 0;
            }
            tp_copy_ascii(out->header_region_uri, sizeof(out->header_region_uri), uri, len);
            if (out->header_region_uri[0] == '\0')
            {
                out->code = tensor_pool_responseCode_INVALID_PARAMS;
                strncpy(out->error_message, "attach response missing header region uri", sizeof(out->error_message) - 1);
                out->error_message[sizeof(out->error_message) - 1] = '\0';
                return 0;
            }
        }
    }
    else
    {
        struct tensor_pool_shmAttachResponse_payloadPools pools;
        size_t pool_count = 0;

        tensor_pool_shmAttachResponse_payloadPools_wrap_for_decode(
            &pools,
            (char *)buffer,
            tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
            version,
            length);
        pool_count = (size_t)tensor_pool_shmAttachResponse_payloadPools_count(&pools);
        for (size_t i = 0; i < pool_count; i++)
        {
            if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
            {
                break;
            }
        }

        {
            uint32_t header_len = tensor_pool_shmAttachResponse_headerRegionUri_length(&response);
            const char *header_uri = tensor_pool_shmAttachResponse_headerRegionUri(&response);
            (void)header_len;
            (void)header_uri;
        }

        {
            uint32_t len = tensor_pool_shmAttachResponse_errorMessage_length(&response);
            const char *err = tensor_pool_shmAttachResponse_errorMessage(&response);
            tp_copy_ascii(out->error_message, sizeof(out->error_message), err, len);
        }
    }

    return 0;
}

int tp_driver_decode_detach_response(
    const uint8_t *buffer,
    size_t length,
    tp_driver_detach_info_t *out)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmDetachResponse response;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    if (NULL == buffer || NULL == out || length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_detach_response: invalid input");
        return -1;
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

    if (schema_id != tensor_pool_shmDetachResponse_sbe_schema_id() ||
        template_id != tensor_pool_shmDetachResponse_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_shmDetachResponse_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_detach_response: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_shmDetachResponse_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_detach_response: block length mismatch");
        return -1;
    }

    tensor_pool_shmDetachResponse_wrap_for_decode(
        &response,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    out->correlation_id = tensor_pool_shmDetachResponse_correlationId(&response);
    {
        enum tensor_pool_responseCode code;
        if (!tensor_pool_shmDetachResponse_code(&response, &code))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_detach_response: invalid response code");
            return -1;
        }
        else
        {
            out->code = code;
        }
    }

    {
        uint32_t len = tensor_pool_shmDetachResponse_errorMessage_length(&response);
        const char *err = tensor_pool_shmDetachResponse_errorMessage(&response);
        tp_copy_ascii(out->error_message, sizeof(out->error_message), err, len);
    }

    return 0;
}

int tp_driver_decode_lease_revoked(
    const uint8_t *buffer,
    size_t length,
    tp_driver_lease_revoked_t *out)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmLeaseRevoked revoked;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    if (NULL == buffer || NULL == out || length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_lease_revoked: invalid input");
        return -1;
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

    if (schema_id != tensor_pool_shmLeaseRevoked_sbe_schema_id() ||
        template_id != tensor_pool_shmLeaseRevoked_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_shmLeaseRevoked_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_lease_revoked: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_shmLeaseRevoked_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_lease_revoked: block length mismatch");
        return -1;
    }

    tensor_pool_shmLeaseRevoked_wrap_for_decode(
        &revoked,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    out->timestamp_ns = tensor_pool_shmLeaseRevoked_timestampNs(&revoked);
    out->lease_id = tensor_pool_shmLeaseRevoked_leaseId(&revoked);
    out->stream_id = tensor_pool_shmLeaseRevoked_streamId(&revoked);
    out->client_id = tensor_pool_shmLeaseRevoked_clientId(&revoked);
    {
        enum tensor_pool_role role;
        if (!tensor_pool_shmLeaseRevoked_role(&revoked, &role))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_lease_revoked: invalid role");
            return -1;
        }
        else
        {
            out->role = (uint8_t)role;
        }
    }
    {
        enum tensor_pool_leaseRevokeReason reason;
        if (!tensor_pool_shmLeaseRevoked_reason(&revoked, &reason))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_lease_revoked: invalid reason");
            return -1;
        }
        else
        {
            out->reason = (uint8_t)reason;
        }
    }
    {
        uint32_t len = tensor_pool_shmLeaseRevoked_errorMessage_length(&revoked);
        const char *err = tensor_pool_shmLeaseRevoked_errorMessage(&revoked);
        tp_copy_ascii(out->error_message, sizeof(out->error_message), err, len);
    }

    return 0;
}

int tp_driver_decode_shutdown(
    const uint8_t *buffer,
    size_t length,
    tp_driver_shutdown_t *out)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmDriverShutdown shutdown;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    if (NULL == buffer || NULL == out || length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_shutdown: invalid input");
        return -1;
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

    if (schema_id != tensor_pool_shmDriverShutdown_sbe_schema_id() ||
        template_id != tensor_pool_shmDriverShutdown_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_shmDriverShutdown_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_shutdown: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_shmDriverShutdown_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_shutdown: block length mismatch");
        return -1;
    }

    tensor_pool_shmDriverShutdown_wrap_for_decode(
        &shutdown,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    out->timestamp_ns = tensor_pool_shmDriverShutdown_timestampNs(&shutdown);
    {
        enum tensor_pool_shutdownReason reason;
        if (!tensor_pool_shmDriverShutdown_reason(&shutdown, &reason))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_decode_shutdown: invalid shutdown reason");
            return -1;
        }
        else
        {
            out->reason = (uint8_t)reason;
        }
    }
    {
        uint32_t len = tensor_pool_shmDriverShutdown_errorMessage_length(&shutdown);
        const char *err = tensor_pool_shmDriverShutdown_errorMessage(&shutdown);
        tp_copy_ascii(out->error_message, sizeof(out->error_message), err, len);
    }

    return 0;
}

static void tp_driver_response_handler(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_driver_response_ctx_t *ctx = (tp_driver_response_ctx_t *)clientd;
    int decode_result;
    tp_log_t *log = NULL;

    (void)header;

    if (NULL == ctx || NULL == buffer)
    {
        return;
    }

    if (ctx->client && ctx->client->client)
    {
        log = &ctx->client->client->context.base.log;
    }

    if (log && log->min_level <= TP_LOG_DEBUG && length >= tensor_pool_messageHeader_encoded_length())
    {
        struct tensor_pool_messageHeader msg_header;
        tensor_pool_messageHeader_wrap(
            &msg_header,
            (char *)buffer,
            0,
            tensor_pool_messageHeader_sbe_schema_version(),
            length);
        tp_log_emit(
            log,
            TP_LOG_DEBUG,
            "driver control fragment schema=%u template=%u block_length=%u version=%u length=%zu",
            (unsigned)tensor_pool_messageHeader_schemaId(&msg_header),
            (unsigned)tensor_pool_messageHeader_templateId(&msg_header),
            (unsigned)tensor_pool_messageHeader_blockLength(&msg_header),
            (unsigned)tensor_pool_messageHeader_version(&msg_header),
            length);
    }

    decode_result = tp_driver_decode_attach_response(buffer, length, ctx->correlation_id, ctx->out);
    if (decode_result == 0 && log)
    {
        tp_log_emit(
            log,
            TP_LOG_DEBUG,
            "driver attach response correlation=%" PRIi64 " code=%u lease=%" PRIu64 " stream=%u epoch=%" PRIu64 " layout=%u",
            ctx->correlation_id,
            (unsigned)ctx->out->code,
            ctx->out->lease_id,
            ctx->out->stream_id,
            ctx->out->epoch,
            ctx->out->layout_version);
    }
    if (decode_result == 0 || decode_result < 0)
    {
        ctx->done = 1;
    }
}

static void tp_driver_async_attach_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_async_attach_t *async = (tp_async_attach_t *)clientd;

    (void)header;

    if (NULL == async || NULL == buffer)
    {
        return;
    }

    if (tp_driver_decode_attach_response(buffer, length, async->request.correlation_id, &async->response) == 0)
    {
        async->done = 1;
    }
}

static void tp_driver_async_detach_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_async_detach_t *async = (tp_async_detach_t *)clientd;

    (void)header;

    if (NULL == async || NULL == buffer)
    {
        return;
    }

    if (tp_driver_decode_detach_response(buffer, length, &async->response) == 0)
    {
        async->done = 1;
    }
}

int tp_driver_client_init(tp_driver_client_t *client, tp_client_t *base)
{
    if (NULL == client || NULL == base)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_init: null input");
        return -1;
    }

    memset(client, 0, sizeof(*client));
    client->client = base;
    client->subscription = base->control_subscription;

    if (base->context.base.control_channel[0] == '\0' || base->context.base.control_stream_id < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_init: control channel not configured");
        return -1;
    }

    if (NULL == client->subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_init: control subscription unavailable");
        return -1;
    }

    aeron_async_add_publication_t *async_add = NULL;

    if (tp_client_async_add_publication(
        base,
        base->context.base.control_channel,
        base->context.base.control_stream_id,
        &async_add) < 0)
    {
        return -1;
    }

    while (NULL == client->publication)
    {
        if (tp_client_async_add_publication_poll(&client->publication, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(base);
    }

    if (tp_client_register_driver_client(base, client) < 0)
    {
        aeron_publication_close(client->publication, NULL, NULL);
        client->publication = NULL;
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

    if (client->registered && client->client)
    {
        tp_client_unregister_driver_client(client->client, client);
    }

    client->subscription = NULL;

    return 0;
}

int tp_driver_attach_async(
    tp_driver_client_t *client,
    const tp_driver_attach_request_t *request,
    tp_async_attach_t **out)
{
    tp_async_attach_t *async = NULL;

    if (NULL == client || NULL == request || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_attach_async: null input");
        return -1;
    }

    if (aeron_alloc((void **)&async, sizeof(*async)) < 0)
    {
        return -1;
    }

    memset(async, 0, sizeof(*async));
    async->client = client;
    async->request = *request;
    async->response.code = tensor_pool_responseCode_INTERNAL_ERROR;
    *out = async;
    return 0;
}

int tp_driver_attach_poll(tp_async_attach_t *async, tp_driver_attach_info_t *out)
{
    int fragments;

    if (NULL == async || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_attach_poll: null input");
        return -1;
    }

    if (!async->sent)
    {
        int ready = tp_driver_client_ready(async->client);
        if (ready < 0)
        {
            return -1;
        }
        if (ready == 0)
        {
            return 0;
        }
        if (tp_driver_send_attach(async->client, &async->request) < 0)
        {
            return -1;
        }
        async->sent = 1;
    }

    if (NULL == async->assembler)
    {
        if (aeron_fragment_assembler_create(&async->assembler, tp_driver_async_attach_handler, async) < 0)
        {
            return -1;
        }
    }

    fragments = aeron_subscription_poll(
        async->client->subscription,
        aeron_fragment_assembler_handler,
        async->assembler,
        10);

    if (fragments < 0)
    {
        return -1;
    }

    if (!async->done)
    {
        return 0;
    }

    *out = async->response;
    if (out->code == tensor_pool_responseCode_OK)
    {
        if (tp_driver_client_update_lease(async->client, out, async->request.client_id, async->request.role) < 0)
        {
            return -1;
        }
    }

    if (async->assembler)
    {
        aeron_fragment_assembler_delete(async->assembler);
        async->assembler = NULL;
    }

    return 1;
}

int tp_driver_detach_async(tp_driver_client_t *client, tp_async_detach_t **out)
{
    tp_async_detach_t *async = NULL;

    if (NULL == client || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_detach_async: null input");
        return -1;
    }

    if (aeron_alloc((void **)&async, sizeof(*async)) < 0)
    {
        return -1;
    }

    memset(async, 0, sizeof(*async));
    async->client = client;
    async->response.correlation_id = tp_clock_now_ns();
    *out = async;
    return 0;
}

int tp_driver_detach_poll(tp_async_detach_t *async, tp_driver_detach_info_t *out)
{
    int fragments;

    if (NULL == async || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_detach_poll: null input");
        return -1;
    }

    if (!async->sent)
    {
        if (tp_driver_detach(
            async->client,
            async->response.correlation_id,
            async->client->active_lease_id,
            async->client->active_stream_id,
            async->client->client_id,
            async->client->role) < 0)
        {
            return -1;
        }
        async->sent = 1;
    }

    if (NULL == async->assembler)
    {
        if (aeron_fragment_assembler_create(&async->assembler, tp_driver_async_detach_handler, async) < 0)
        {
            return -1;
        }
    }

    fragments = aeron_subscription_poll(
        async->client->subscription,
        aeron_fragment_assembler_handler,
        async->assembler,
        10);

    if (fragments < 0)
    {
        return -1;
    }

    if (!async->done)
    {
        return 0;
    }

    *out = async->response;
    if (async->assembler)
    {
        aeron_fragment_assembler_delete(async->assembler);
        async->assembler = NULL;
    }
    return 1;
}

static int tp_driver_send_attach(tp_driver_client_t *client, const tp_driver_attach_request_t *request)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmAttachRequest attach;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_shmAttachRequest_sbe_block_length();
    int64_t result;
    tp_log_t *log = NULL;

    if (client && client->client)
    {
        log = &client->client->context.base.log;
    }

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
    tensor_pool_shmAttachRequest_set_publishMode(&attach, request->publish_mode);
    tensor_pool_shmAttachRequest_set_requireHugepages(&attach, request->require_hugepages);
    if (request->desired_node_id == TP_NULL_U32)
    {
        tensor_pool_shmAttachRequest_set_desiredNodeId(
            &attach,
            tensor_pool_shmAttachRequest_desiredNodeId_null_value());
    }
    else
    {
        tensor_pool_shmAttachRequest_set_desiredNodeId(&attach, request->desired_node_id);
    }

    if (log)
    {
        tp_log_emit(
            log,
            TP_LOG_DEBUG,
            "driver attach send correlation=%" PRIi64 " stream=%u client=%u role=%u mode=%u layout=%u hugepages=%u desired_node_id=%u",
            request->correlation_id,
            request->stream_id,
            request->client_id,
            request->role,
            request->publish_mode,
            request->expected_layout_version,
            request->require_hugepages,
            request->desired_node_id == TP_NULL_U32 ? 0U : request->desired_node_id);
    }

    result = aeron_publication_offer(client->publication, buffer, header_len + body_len, NULL, NULL);
    if (result < 0)
    {
        if (log)
        {
            tp_log_emit(
                log,
                TP_LOG_DEBUG,
                "driver attach offer failed correlation=%" PRIi64 " result=%" PRId64,
                request->correlation_id,
                result);
        }
        return (int)result;
    }

    return 0;
}

static int tp_driver_client_ready(tp_driver_client_t *client)
{
    if (NULL == client || NULL == client->subscription || NULL == client->publication)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_ready: invalid client");
        return -1;
    }

    if (!aeron_publication_is_connected(client->publication))
    {
        if (client->client)
        {
            tp_client_do_work(client->client);
        }
        return 0;
    }
    if (!aeron_subscription_is_connected(client->subscription))
    {
        if (client->client)
        {
            tp_client_do_work(client->client);
        }
        return 0;
    }

    return 1;
}

static int tp_driver_wait_connected(tp_driver_client_t *client, int64_t deadline_ns)
{
    int ready;

    while ((ready = tp_driver_client_ready(client)) == 0)
    {
        if (deadline_ns > 0 && tp_clock_now_ns() > deadline_ns)
        {
            TP_SET_ERR(ETIMEDOUT, "%s", "tp_driver_attach: control stream not connected");
            return -1;
        }
        aeron_idle_strategy_sleeping_idle(NULL, 0);
    }

    return ready < 0 ? -1 : 0;
}

int tp_driver_attach(
    tp_driver_client_t *client,
    const tp_driver_attach_request_t *request,
    tp_driver_attach_info_t *out,
    int64_t timeout_ns)
{
    tp_driver_response_ctx_t ctx;
    aeron_fragment_assembler_t *assembler = NULL;
    int64_t deadline = 0;
    int64_t last_send_ns = 0;
    const int64_t retry_interval_ns = 200 * 1000 * 1000LL;
    int use_deadline = (timeout_ns > 0);
    tp_log_t *log = NULL;

    if (NULL == client || NULL == request || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_attach: null input");
        return -1;
    }

    if (client->client)
    {
        log = &client->client->context.base.log;
    }

    memset(out, 0, sizeof(*out));
    out->code = tensor_pool_responseCode_INTERNAL_ERROR;

    if (use_deadline)
    {
        deadline = tp_clock_now_ns() + timeout_ns;
    }

    if (tp_driver_wait_connected(client, deadline) < 0)
    {
        return -1;
    }

    last_send_ns = tp_clock_now_ns();
    (void)tp_driver_send_attach(client, request);

    ctx.correlation_id = request->correlation_id;
    ctx.out = out;
    ctx.client = client;
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
        int64_t now_ns = tp_clock_now_ns();

        if (fragments < 0)
        {
            return -1;
        }

        if (use_deadline && now_ns > deadline)
        {
            TP_SET_ERR(ETIMEDOUT, "%s", "tp_driver_attach: timeout");
            aeron_fragment_assembler_delete(assembler);
            return -1;
        }
        if (now_ns - last_send_ns >= retry_interval_ns)
        {
            last_send_ns = now_ns;
            if (log)
            {
                tp_log_emit(
                    log,
                    TP_LOG_DEBUG,
                    "driver attach retry correlation=%" PRIi64 " stream=%u client=%u",
                    request->correlation_id,
                    request->stream_id,
                    request->client_id);
            }
            (void)tp_driver_send_attach(client, request);
        }
    }

    aeron_fragment_assembler_delete(assembler);

    if (out->code == tensor_pool_responseCode_OK)
    {
        if (tp_driver_client_update_lease(client, out, request->client_id, request->role) < 0)
        {
            return -1;
        }
    }

    return 0;
}

int tp_driver_keepalive(tp_driver_client_t *client, uint64_t timestamp_ns)
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
    tensor_pool_shmLeaseKeepalive_set_leaseId(&keepalive, client->active_lease_id);
    tensor_pool_shmLeaseKeepalive_set_streamId(&keepalive, client->active_stream_id);
    tensor_pool_shmLeaseKeepalive_set_clientId(&keepalive, client->client_id);
    tensor_pool_shmLeaseKeepalive_set_role(&keepalive, client->role);
    tensor_pool_shmLeaseKeepalive_set_clientTimestampNs(&keepalive, timestamp_ns);

    result = aeron_publication_offer(client->publication, buffer, header_len + body_len, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }

    if (tp_driver_client_record_keepalive(client, timestamp_ns) < 0)
    {
        return -1;
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

static void tp_driver_event_handler(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_driver_event_poller_t *poller = (tp_driver_event_poller_t *)clientd;
    tp_driver_detach_info_t detach_info;
    tp_driver_lease_revoked_t revoked;
    tp_driver_shutdown_t shutdown;

    (void)header;

    if (NULL == poller || NULL == buffer)
    {
        return;
    }

    if (poller->handlers.on_detach_response)
    {
        if (tp_driver_decode_detach_response(buffer, length, &detach_info) == 0)
        {
            poller->handlers.on_detach_response(&detach_info, poller->handlers.clientd);
            return;
        }
    }

    if (poller->handlers.on_lease_revoked)
    {
        if (tp_driver_decode_lease_revoked(buffer, length, &revoked) == 0)
        {
            poller->handlers.on_lease_revoked(&revoked, poller->handlers.clientd);
            return;
        }
    }

    if (poller->handlers.on_shutdown)
    {
        if (tp_driver_decode_shutdown(buffer, length, &shutdown) == 0)
        {
            poller->handlers.on_shutdown(&shutdown, poller->handlers.clientd);
            return;
        }
    }
}

int tp_driver_event_poller_init(
    tp_driver_event_poller_t *poller,
    tp_driver_client_t *client,
    const tp_driver_event_handlers_t *handlers)
{
    if (NULL == poller || NULL == client || NULL == client->subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_event_poller_init: null input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->subscription = client->subscription;
    poller->owns_subscription = false;
    if (NULL != handlers)
    {
        poller->handlers = *handlers;
    }

    if (aeron_fragment_assembler_create(&poller->assembler, tp_driver_event_handler, poller) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_driver_event_poller_close(tp_driver_event_poller_t *poller)
{
    if (NULL == poller)
    {
        return -1;
    }

    if (poller->assembler)
    {
        aeron_fragment_assembler_delete(poller->assembler);
        poller->assembler = NULL;
    }

    poller->subscription = NULL;
    return 0;
}

int tp_driver_event_poll(tp_driver_event_poller_t *poller, int fragment_limit)
{
    if (NULL == poller || NULL == poller->assembler || NULL == poller->subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_event_poll: poller not initialized");
        return -1;
    }

    return aeron_subscription_poll(
        poller->subscription,
        aeron_fragment_assembler_handler,
        poller->assembler,
        fragment_limit);
}

int tp_driver_client_update_lease(
    tp_driver_client_t *client,
    const tp_driver_attach_info_t *attach_info,
    uint32_t client_id,
    uint8_t role)
{
    if (NULL == client || NULL == attach_info)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_update_lease: null input");
        return -1;
    }

    client->active_lease_id = attach_info->lease_id;
    client->lease_expiry_timestamp_ns = attach_info->lease_expiry_timestamp_ns;
    client->active_stream_id = attach_info->stream_id;
    client->client_id = client_id;
    client->role = role;
    return 0;
}

int tp_driver_client_record_keepalive(tp_driver_client_t *client, uint64_t now_ns)
{
    tp_client_t *owner;
    uint64_t interval_ns;
    uint64_t grace_intervals;

    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_record_keepalive: null input");
        return -1;
    }

    client->last_keepalive_ns = now_ns;
    if (client->lease_expiry_timestamp_ns != TP_NULL_U64)
    {
        owner = client->client;
        interval_ns = owner ? owner->context.keepalive_interval_ns : 0;
        grace_intervals = owner ? owner->context.lease_expiry_grace_intervals : 0;
        if (interval_ns > 0 && grace_intervals > 0)
        {
            uint64_t extension = interval_ns * grace_intervals;
            uint64_t next_expiry = now_ns + extension;
            if (next_expiry > client->lease_expiry_timestamp_ns)
            {
                client->lease_expiry_timestamp_ns = next_expiry;
            }
        }
    }
    return 0;
}

int tp_driver_client_keepalive_due(const tp_driver_client_t *client, uint64_t now_ns, uint64_t interval_ns)
{
    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_keepalive_due: null input");
        return -1;
    }

    if (client->active_lease_id == 0 || interval_ns == 0)
    {
        return 0;
    }

    return (now_ns - client->last_keepalive_ns >= interval_ns) ? 1 : 0;
}

int tp_driver_client_lease_expired(const tp_driver_client_t *client, uint64_t now_ns)
{
    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_client_lease_expired: null input");
        return -1;
    }

    if (client->active_lease_id == 0)
    {
        return 0;
    }

    if (client->lease_expiry_timestamp_ns == TP_NULL_U64)
    {
        return 0;
    }

    return (now_ns >= client->lease_expiry_timestamp_ns) ? 1 : 0;
}
