#include "tensor_pool/tp_control_adapter.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "aeron_agent.h"

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"
#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/shmPoolAnnounce.h"
#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/controlResponse.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/metaBlobAnnounce.h"
#include "wire/tensor_pool/metaBlobChunk.h"
#include "wire/tensor_pool/metaBlobComplete.h"
#include "wire/tensor_pool/messageHeader.h"

static tp_string_view_t tp_string_view_from_parts(const char *data, size_t length)
{
    tp_string_view_t out;

    out.data = data;
    out.length = (uint32_t)length;

    if (out.length == 0)
    {
        out.data = NULL;
    }

    return out;
}

static int tp_decode_var_data(const char *buffer, size_t length, uint64_t *position, tp_string_view_t *out)
{
    uint32_t field_len;

    if (NULL == buffer || NULL == position || NULL == out)
    {
        return -1;
    }

    if (*position + 4 > length)
    {
        return -1;
    }

    memcpy(&field_len, buffer + *position, sizeof(field_len));
    field_len = SBE_LITTLE_ENDIAN_ENCODE_32(field_len);

    if (*position + 4 + field_len > length)
    {
        return -1;
    }

    out->data = (field_len > 0) ? buffer + *position + 4 : NULL;
    out->length = field_len;
    *position += 4 + field_len;
    return 0;
}

int tp_control_decode_consumer_hello(const uint8_t *buffer, size_t length, tp_consumer_hello_view_t *out)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerHello hello;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    uint32_t opt_value;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_consumerHello_sbe_schema_id() ||
        template_id != tensor_pool_consumerHello_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_consumerHello_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_consumerHello_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: block length mismatch");
        return -1;
    }

    tensor_pool_consumerHello_wrap_for_decode(
        &hello,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->stream_id = tensor_pool_consumerHello_streamId(&hello);
    out->consumer_id = tensor_pool_consumerHello_consumerId(&hello);
    {
        enum tensor_pool_bool bool_val;
        if (!tensor_pool_consumerHello_supportsShm(&hello, &bool_val))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: invalid supportsShm");
            return -1;
        }
        out->supports_shm = (uint8_t)bool_val;
    }
    {
        enum tensor_pool_bool bool_val;
        if (!tensor_pool_consumerHello_supportsProgress(&hello, &bool_val))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: invalid supportsProgress");
            return -1;
        }
        out->supports_progress = (uint8_t)bool_val;
    }
    {
        enum tensor_pool_mode mode_val;
        if (!tensor_pool_consumerHello_mode(&hello, &mode_val))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: invalid mode");
            return -1;
        }
        out->mode = (tp_mode_t)mode_val;
    }
    out->max_rate_hz = tensor_pool_consumerHello_maxRateHz(&hello);
    out->expected_layout_version = tensor_pool_consumerHello_expectedLayoutVersion(&hello);

    opt_value = tensor_pool_consumerHello_progressIntervalUs(&hello);
    out->progress_interval_us = (opt_value == tensor_pool_consumerHello_progressIntervalUs_null_value()) ? TP_NULL_U32 : opt_value;

    opt_value = tensor_pool_consumerHello_progressBytesDelta(&hello);
    out->progress_bytes_delta = (opt_value == tensor_pool_consumerHello_progressBytesDelta_null_value()) ? TP_NULL_U32 : opt_value;

    opt_value = tensor_pool_consumerHello_progressMajorDeltaUnits(&hello);
    out->progress_major_delta_units =
        (opt_value == tensor_pool_consumerHello_progressMajorDeltaUnits_null_value()) ? TP_NULL_U32 : opt_value;

    out->descriptor_stream_id = tensor_pool_consumerHello_descriptorStreamId(&hello);
    out->control_stream_id = tensor_pool_consumerHello_controlStreamId(&hello);

    {
        tp_string_view_t channel_view;
        uint64_t pos = tensor_pool_consumerHello_sbe_position(&hello);
        if (tp_decode_var_data((const char *)buffer, length, &pos, &channel_view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: invalid descriptor channel");
            return -1;
        }
        out->descriptor_channel = channel_view;

        if (tp_decode_var_data((const char *)buffer, length, &pos, &channel_view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_hello: invalid control channel");
            return -1;
        }
        out->control_channel = channel_view;
    }

    return 0;
}

int tp_control_decode_consumer_config(const uint8_t *buffer, size_t length, tp_consumer_config_view_t *out)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerConfig cfg;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_consumerConfig_sbe_schema_id() ||
        template_id != tensor_pool_consumerConfig_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_consumerConfig_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_consumerConfig_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: block length mismatch");
        return -1;
    }

    tensor_pool_consumerConfig_wrap_for_decode(
        &cfg,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->stream_id = tensor_pool_consumerConfig_streamId(&cfg);
    out->consumer_id = tensor_pool_consumerConfig_consumerId(&cfg);
    {
        enum tensor_pool_bool bool_val;
        if (!tensor_pool_consumerConfig_useShm(&cfg, &bool_val))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: invalid useShm");
            return -1;
        }
        out->use_shm = (uint8_t)bool_val;
    }
    {
        enum tensor_pool_mode mode_val;
        if (!tensor_pool_consumerConfig_mode(&cfg, &mode_val))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: invalid mode");
            return -1;
        }
        out->mode = (tp_mode_t)mode_val;
    }
    out->descriptor_stream_id = tensor_pool_consumerConfig_descriptorStreamId(&cfg);
    out->control_stream_id = tensor_pool_consumerConfig_controlStreamId(&cfg);

    {
        tp_string_view_t uri_view;
        uint64_t pos = tensor_pool_consumerConfig_sbe_position(&cfg);

        if (tp_decode_var_data((const char *)buffer, length, &pos, &uri_view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: invalid payload uri");
            return -1;
        }
        out->payload_fallback_uri = uri_view;

        if (tp_decode_var_data((const char *)buffer, length, &pos, &uri_view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: invalid descriptor channel");
            return -1;
        }
        out->descriptor_channel = uri_view;

        if (tp_decode_var_data((const char *)buffer, length, &pos, &uri_view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_consumer_config: invalid control channel");
            return -1;
        }
        out->control_channel = uri_view;
    }

    return 0;
}

int tp_control_decode_data_source_announce(const uint8_t *buffer, size_t length, tp_data_source_announce_view_t *out)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_dataSourceAnnounce announce;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_announce: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_announce: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_dataSourceAnnounce_sbe_schema_id() ||
        template_id != tensor_pool_dataSourceAnnounce_sbe_template_id())
    {
        return 1;
    }

    tensor_pool_dataSourceAnnounce_wrap_for_decode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->stream_id = tensor_pool_dataSourceAnnounce_streamId(&announce);
    out->producer_id = tensor_pool_dataSourceAnnounce_producerId(&announce);
    out->epoch = tensor_pool_dataSourceAnnounce_epoch(&announce);
    out->meta_version = tensor_pool_dataSourceAnnounce_metaVersion(&announce);
    {
        tp_string_view_t view;
        uint64_t pos = tensor_pool_dataSourceAnnounce_sbe_position(&announce);

        if (tp_decode_var_data((const char *)buffer, length, &pos, &view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_announce: invalid name");
            return -1;
        }
        out->name = view;

        if (tp_decode_var_data((const char *)buffer, length, &pos, &view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_announce: invalid summary");
            return -1;
        }
        out->summary = view;
    }

    return 0;
}

int tp_control_decode_shm_pool_announce(const uint8_t *buffer, size_t length, tp_shm_pool_announce_view_t *out)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmPoolAnnounce announce;
    struct tensor_pool_shmPoolAnnounce_payloadPools pools;
    tp_string_view_t header_uri;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    size_t pool_count;
    size_t i = 0;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: buffer too short");
        return -1;
    }

    memset(out, 0, sizeof(*out));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_shmPoolAnnounce_sbe_schema_id() ||
        template_id != tensor_pool_shmPoolAnnounce_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_shmPoolAnnounce_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_shmPoolAnnounce_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: block length mismatch");
        return -1;
    }

    tensor_pool_shmPoolAnnounce_wrap_for_decode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    out->stream_id = tensor_pool_shmPoolAnnounce_streamId(&announce);
    out->producer_id = tensor_pool_shmPoolAnnounce_producerId(&announce);
    out->epoch = tensor_pool_shmPoolAnnounce_epoch(&announce);
    out->announce_timestamp_ns = tensor_pool_shmPoolAnnounce_announceTimestampNs(&announce);
    out->layout_version = tensor_pool_shmPoolAnnounce_layoutVersion(&announce);
    out->header_nslots = tensor_pool_shmPoolAnnounce_headerNslots(&announce);
    out->header_slot_bytes = tensor_pool_shmPoolAnnounce_headerSlotBytes(&announce);
    {
        enum tensor_pool_clockDomain clock_domain;
        if (!tensor_pool_shmPoolAnnounce_announceClockDomain(&announce, &clock_domain))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: invalid clock domain");
            return -1;
        }
        out->announce_clock_domain = (uint8_t)clock_domain;
    }

    if (NULL == tensor_pool_shmPoolAnnounce_payloadPools_wrap_for_decode(
        &pools,
        (char *)buffer,
        tensor_pool_shmPoolAnnounce_sbe_position_ptr(&announce),
        version,
        length))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: payload pools truncated");
        return -1;
    }

    pool_count = (size_t)tensor_pool_shmPoolAnnounce_payloadPools_count(&pools);
    out->pool_count = pool_count;
    if (pool_count > 0)
    {
        out->pools = calloc(pool_count, sizeof(*out->pools));
        if (NULL == out->pools)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_control_decode_shm_pool_announce: pool allocation failed");
            return -1;
        }
    }

    for (i = 0; i < pool_count; i++)
    {
        tp_shm_pool_desc_t *pool = &out->pools[i];
        tp_string_view_t region_uri;

        if (!tensor_pool_shmPoolAnnounce_payloadPools_next(&pools))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: payload pools truncated");
            tp_control_shm_pool_announce_close(out);
            return -1;
        }

        pool->pool_id = tensor_pool_shmPoolAnnounce_payloadPools_poolId(&pools);
        pool->nslots = tensor_pool_shmPoolAnnounce_payloadPools_poolNslots(&pools);
        pool->stride_bytes = tensor_pool_shmPoolAnnounce_payloadPools_strideBytes(&pools);

        if (tp_decode_var_data(pools.buffer, pools.buffer_length, pools.position_ptr, &region_uri) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: invalid pool uri");
            tp_control_shm_pool_announce_close(out);
            return -1;
        }
        pool->region_uri = region_uri;
    }

    {
        uint64_t pos = *tensor_pool_shmPoolAnnounce_sbe_position_ptr(&announce);
        if (tp_decode_var_data((const char *)buffer, length, &pos, &header_uri) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_shm_pool_announce: invalid header uri");
            tp_control_shm_pool_announce_close(out);
            return -1;
        }
        out->header_region_uri = header_uri;
    }

    return 0;
}

int tp_control_decode_control_response(const uint8_t *buffer, size_t length, tp_control_response_view_t *out)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_controlResponse resp;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    tp_string_view_t error_view;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_control_response: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_control_response: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_controlResponse_sbe_schema_id() ||
        template_id != tensor_pool_controlResponse_sbe_template_id())
    {
        return 1;
    }

    tensor_pool_controlResponse_wrap_for_decode(
        &resp,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->correlation_id = tensor_pool_controlResponse_correlationId(&resp);
    {
        enum tensor_pool_responseCode code;
        if (!tensor_pool_controlResponse_code(&resp, &code))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_control_response: invalid response code");
            return -1;
        }
        out->code = (tp_response_code_t)code;
    }

    {
        uint64_t pos = tensor_pool_controlResponse_sbe_position(&resp);
        if (tp_decode_var_data((const char *)buffer, length, &pos, &error_view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_control_response: invalid error message");
            return -1;
        }
        out->error_message = error_view;
    }

    return 0;
}

int tp_control_decode_meta_blob_announce(const uint8_t *buffer, size_t length, tp_meta_blob_announce_view_t *out)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_metaBlobAnnounce msg;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_announce: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_announce: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_metaBlobAnnounce_sbe_schema_id() ||
        template_id != tensor_pool_metaBlobAnnounce_sbe_template_id())
    {
        return 1;
    }

    tensor_pool_metaBlobAnnounce_wrap_for_decode(
        &msg,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->stream_id = tensor_pool_metaBlobAnnounce_streamId(&msg);
    out->meta_version = tensor_pool_metaBlobAnnounce_metaVersion(&msg);
    out->blob_type = tensor_pool_metaBlobAnnounce_blobType(&msg);
    out->total_len = tensor_pool_metaBlobAnnounce_totalLen(&msg);
    out->checksum = tensor_pool_metaBlobAnnounce_checksum(&msg);

    return 0;
}

int tp_control_decode_meta_blob_chunk(const uint8_t *buffer, size_t length, tp_meta_blob_chunk_view_t *out)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_metaBlobChunk msg;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    tp_string_view_t bytes_view;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_chunk: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_chunk: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_metaBlobChunk_sbe_schema_id() ||
        template_id != tensor_pool_metaBlobChunk_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_metaBlobChunk_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_chunk: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_metaBlobChunk_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_chunk: block length mismatch");
        return -1;
    }

    tensor_pool_metaBlobChunk_wrap_for_decode(
        &msg,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->stream_id = tensor_pool_metaBlobChunk_streamId(&msg);
    out->meta_version = tensor_pool_metaBlobChunk_metaVersion(&msg);
    out->offset = tensor_pool_metaBlobChunk_chunkOffset(&msg);
    {
        uint64_t pos = tensor_pool_metaBlobChunk_sbe_position(&msg);
        if (tp_decode_var_data((const char *)buffer, length, &pos, &bytes_view) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_chunk: invalid bytes");
            return -1;
        }
        out->bytes = bytes_view;
    }

    return 0;
}

int tp_control_decode_meta_blob_complete(const uint8_t *buffer, size_t length, tp_meta_blob_complete_view_t *out)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_metaBlobComplete msg;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_complete: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_meta_blob_complete: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_metaBlobComplete_sbe_schema_id() ||
        template_id != tensor_pool_metaBlobComplete_sbe_template_id())
    {
        return 1;
    }

    tensor_pool_metaBlobComplete_wrap_for_decode(
        &msg,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->stream_id = tensor_pool_metaBlobComplete_streamId(&msg);
    out->meta_version = tensor_pool_metaBlobComplete_metaVersion(&msg);
    out->checksum = tensor_pool_metaBlobComplete_checksum(&msg);

    return 0;
}

void tp_control_shm_pool_announce_close(tp_shm_pool_announce_view_t *view)
{
    if (NULL == view)
    {
        return;
    }

    if (view->pools)
    {
        free(view->pools);
    }
    memset(view, 0, sizeof(*view));
}

int tp_control_decode_data_source_meta(
    const uint8_t *buffer,
    size_t length,
    tp_data_source_meta_view_t *out,
    tp_on_data_source_meta_attr_t on_attr,
    void *clientd)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_dataSourceMeta meta;
    struct tensor_pool_dataSourceMeta_attributes attrs;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    uint32_t attr_count;

    if (NULL == buffer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_meta: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_meta: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);
    version = tensor_pool_messageHeader_version(&header);

    if (schema_id != tensor_pool_dataSourceMeta_sbe_schema_id() ||
        template_id != tensor_pool_dataSourceMeta_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_dataSourceMeta_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_meta: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_dataSourceMeta_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_meta: block length mismatch");
        return -1;
    }

    tensor_pool_dataSourceMeta_wrap_for_decode(
        &meta,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->stream_id = tensor_pool_dataSourceMeta_streamId(&meta);
    out->meta_version = tensor_pool_dataSourceMeta_metaVersion(&meta);
    out->timestamp_ns = tensor_pool_dataSourceMeta_timestampNs(&meta);

    if (NULL == tensor_pool_dataSourceMeta_attributes_wrap_for_decode(
        &attrs,
        (char *)buffer,
        tensor_pool_dataSourceMeta_sbe_position_ptr(&meta),
        version,
        length))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_meta: attributes truncated");
        return -1;
    }

    attr_count = (uint32_t)tensor_pool_dataSourceMeta_attributes_count(&attrs);
    out->attribute_count = attr_count;

    if (NULL != on_attr)
    {
        uint32_t i;
        for (i = 0; i < attr_count; i++)
        {
            tp_data_source_meta_attr_view_t attr_view;
            tp_string_view_t key_view;
            tp_string_view_t fmt_view;
            tp_string_view_t val_view;

            if (NULL == tensor_pool_dataSourceMeta_attributes_next(&attrs))
            {
                TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_meta: attributes truncated");
                return -1;
            }

            if (tp_decode_var_data(attrs.buffer, attrs.buffer_length, attrs.position_ptr, &key_view) < 0 ||
                tp_decode_var_data(attrs.buffer, attrs.buffer_length, attrs.position_ptr, &fmt_view) < 0 ||
                tp_decode_var_data(attrs.buffer, attrs.buffer_length, attrs.position_ptr, &val_view) < 0)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_control_decode_data_source_meta: invalid attribute");
                return -1;
            }

            attr_view.key = key_view;
            attr_view.format = fmt_view;
            attr_view.value = val_view;

            on_attr(&attr_view, clientd);
        }
    }

    return 0;
}

static void tp_control_fragment_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_control_subscription_t *control = (tp_control_subscription_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    uint16_t template_id;
    uint16_t schema_id;

    (void)header;

    if (NULL == control || NULL == buffer || length < tensor_pool_messageHeader_encoded_length())
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

    if (schema_id != tensor_pool_consumerHello_sbe_schema_id())
    {
        return;
    }

    if (template_id == tensor_pool_shmPoolAnnounce_sbe_template_id() && control->adapter.on_shm_pool_announce)
    {
        tp_shm_pool_announce_view_t view;
        if (tp_control_decode_shm_pool_announce(buffer, length, &view) == 0)
        {
            control->adapter.on_shm_pool_announce(&view, control->adapter.clientd);
            tp_control_shm_pool_announce_close(&view);
        }
        return;
    }

    if (template_id == tensor_pool_consumerHello_sbe_template_id() && control->adapter.on_consumer_hello)
    {
        tp_consumer_hello_view_t view;
        if (tp_control_decode_consumer_hello(buffer, length, &view) == 0)
        {
            control->adapter.on_consumer_hello(&view, control->adapter.clientd);
        }
        return;
    }

    if (template_id == tensor_pool_consumerConfig_sbe_template_id() && control->adapter.on_consumer_config)
    {
        tp_consumer_config_view_t view;
        if (tp_control_decode_consumer_config(buffer, length, &view) == 0)
        {
            control->adapter.on_consumer_config(&view, control->adapter.clientd);
        }
        return;
    }

    if (template_id == tensor_pool_controlResponse_sbe_template_id() && control->adapter.on_control_response)
    {
        tp_control_response_view_t view;
        if (tp_control_decode_control_response(buffer, length, &view) == 0)
        {
            control->adapter.on_control_response(&view, control->adapter.clientd);
        }
        return;
    }

    if (template_id == tensor_pool_dataSourceAnnounce_sbe_template_id() && control->adapter.on_data_source_announce)
    {
        tp_data_source_announce_view_t view;
        if (tp_control_decode_data_source_announce(buffer, length, &view) == 0)
        {
            control->adapter.on_data_source_announce(&view, control->adapter.clientd);
        }
        return;
    }

    if (template_id == tensor_pool_dataSourceMeta_sbe_template_id() && control->adapter.on_data_source_meta_begin)
    {
        tp_data_source_meta_view_t view;
        if (tp_control_decode_data_source_meta(buffer, length, &view, NULL, NULL) == 0)
        {
            control->adapter.on_data_source_meta_begin(&view, control->adapter.clientd);
            if (control->adapter.on_data_source_meta_attr)
            {
                tp_control_decode_data_source_meta(
                    buffer,
                    length,
                    &view,
                    control->adapter.on_data_source_meta_attr,
                    control->adapter.clientd);
            }
            if (control->adapter.on_data_source_meta_end)
            {
                control->adapter.on_data_source_meta_end(&view, control->adapter.clientd);
            }
        }
    }
}

int tp_control_subscription_init(
    tp_control_subscription_t *control,
    void *aeron,
    const char *channel,
    int32_t stream_id,
    const tp_control_adapter_t *adapter)
{
    aeron_async_add_subscription_t *async_add = NULL;
    aeron_subscription_t *raw_sub = NULL;
    aeron_t *aeron_client = NULL;

    if (NULL == control || NULL == aeron || NULL == channel || NULL == adapter)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_subscription_init: null input");
        return -1;
    }

    memset(control, 0, sizeof(*control));
    control->adapter = *adapter;

    aeron_client = (aeron_t *)aeron;
    if (aeron_async_add_subscription(
        &async_add,
        aeron_client,
        channel,
        stream_id,
        NULL,
        NULL,
        NULL,
        NULL) < 0)
    {
        return -1;
    }

    while (NULL == control->subscription)
    {
        if (aeron_async_add_subscription_poll(&raw_sub, async_add) < 0)
        {
            return -1;
        }
        if (raw_sub && tp_subscription_wrap(&control->subscription, raw_sub) < 0)
        {
            aeron_subscription_close(raw_sub, NULL, NULL);
            return -1;
        }
        aeron_idle_strategy_sleeping_idle(NULL, 0);
    }

    if (tp_fragment_assembler_create(&control->assembler, tp_control_fragment_handler, control) < 0)
    {
        tp_subscription_close(&control->subscription);
        return -1;
    }

    return 0;
}

int tp_control_subscription_close(tp_control_subscription_t *control)
{
    if (NULL == control)
    {
        return -1;
    }

    tp_subscription_close(&control->subscription);
    tp_fragment_assembler_close(&control->assembler);

    return 0;
}

int tp_control_subscription_poll(tp_control_subscription_t *control, int fragment_limit)
{
    if (NULL == control || NULL == control->subscription || NULL == control->assembler)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_subscription_poll: subscription not initialized");
        return -1;
    }

    return aeron_subscription_poll(
        tp_subscription_handle(control->subscription),
        aeron_fragment_assembler_handler,
        tp_fragment_assembler_handle(control->assembler),
        fragment_limit);
}
