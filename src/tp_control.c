#include "tensor_pool/tp_control.h"

#include "aeronc.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_producer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/controlResponse.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/metaBlobAnnounce.h"
#include "wire/tensor_pool/metaBlobChunk.h"
#include "wire/tensor_pool/metaBlobComplete.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/shmPoolAnnounce.h"

static int tp_offer_message(aeron_publication_t *pub, const uint8_t *buffer, size_t length)
{
    int64_t result = aeron_publication_offer(pub, buffer, length, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }
    return 0;
}

static size_t tp_control_strlen(const char *value)
{
    return value ? strlen(value) : 0;
}

int tp_consumer_send_hello(tp_consumer_t *consumer, const tp_consumer_hello_t *hello)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_consumerHello hello_msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_consumerHello_sbe_block_length();

    if (NULL == consumer || NULL == consumer->control_publication || NULL == hello)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_send_hello: control publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_consumerHello_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_consumerHello_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_consumerHello_sbe_schema_version());

    tensor_pool_consumerHello_wrap_for_encode(&hello_msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_consumerHello_set_streamId(&hello_msg, hello->stream_id);
    tensor_pool_consumerHello_set_consumerId(&hello_msg, hello->consumer_id);
    tensor_pool_consumerHello_set_supportsShm(&hello_msg, hello->supports_shm);
    tensor_pool_consumerHello_set_supportsProgress(&hello_msg, hello->supports_progress);
    tensor_pool_consumerHello_set_mode(&hello_msg, (enum tensor_pool_mode)hello->mode);
    tensor_pool_consumerHello_set_maxRateHz(&hello_msg, hello->max_rate_hz);
    tensor_pool_consumerHello_set_expectedLayoutVersion(&hello_msg, hello->expected_layout_version);

    if (hello->progress_interval_us == TP_NULL_U32)
    {
        tensor_pool_consumerHello_set_progressIntervalUs(&hello_msg, tensor_pool_consumerHello_progressIntervalUs_null_value());
    }
    else
    {
        tensor_pool_consumerHello_set_progressIntervalUs(&hello_msg, hello->progress_interval_us);
    }

    if (hello->progress_bytes_delta == TP_NULL_U32)
    {
        tensor_pool_consumerHello_set_progressBytesDelta(&hello_msg, tensor_pool_consumerHello_progressBytesDelta_null_value());
    }
    else
    {
        tensor_pool_consumerHello_set_progressBytesDelta(&hello_msg, hello->progress_bytes_delta);
    }

    if (hello->progress_major_delta_units == TP_NULL_U32)
    {
        tensor_pool_consumerHello_set_progressMajorDeltaUnits(&hello_msg, tensor_pool_consumerHello_progressMajorDeltaUnits_null_value());
    }
    else
    {
        tensor_pool_consumerHello_set_progressMajorDeltaUnits(&hello_msg, hello->progress_major_delta_units);
    }

    tensor_pool_consumerHello_set_descriptorStreamId(&hello_msg, hello->descriptor_stream_id);
    tensor_pool_consumerHello_set_controlStreamId(&hello_msg, hello->control_stream_id);

    if (NULL != hello->descriptor_channel)
    {
        if (tensor_pool_consumerHello_put_descriptorChannel(
            &hello_msg,
            hello->descriptor_channel,
            strlen(hello->descriptor_channel)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_consumerHello_put_descriptorChannel(&hello_msg, "", 0);
    }

    if (NULL != hello->control_channel)
    {
        if (tensor_pool_consumerHello_put_controlChannel(
            &hello_msg,
            hello->control_channel,
            strlen(hello->control_channel)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_consumerHello_put_controlChannel(&hello_msg, "", 0);
    }

    return tp_offer_message(consumer->control_publication, buffer, tensor_pool_consumerHello_sbe_position(&hello_msg));
}

int tp_producer_send_consumer_config(tp_producer_t *producer, const tp_consumer_config_msg_t *config)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_consumerConfig cfg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_consumerConfig_sbe_block_length();

    if (NULL == producer || NULL == producer->control_publication || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_consumer_config: control publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_consumerConfig_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_consumerConfig_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_consumerConfig_sbe_schema_version());

    tensor_pool_consumerConfig_wrap_for_encode(&cfg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_consumerConfig_set_streamId(&cfg, config->stream_id);
    tensor_pool_consumerConfig_set_consumerId(&cfg, config->consumer_id);
    tensor_pool_consumerConfig_set_useShm(&cfg, config->use_shm);
    tensor_pool_consumerConfig_set_mode(&cfg, (enum tensor_pool_mode)config->mode);
    tensor_pool_consumerConfig_set_descriptorStreamId(&cfg, config->descriptor_stream_id);
    tensor_pool_consumerConfig_set_controlStreamId(&cfg, config->control_stream_id);

    if (NULL != config->payload_fallback_uri)
    {
        if (tensor_pool_consumerConfig_put_payloadFallbackUri(&cfg, config->payload_fallback_uri, strlen(config->payload_fallback_uri)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_consumerConfig_put_payloadFallbackUri(&cfg, "", 0);
    }

    if (NULL != config->descriptor_channel)
    {
        if (tensor_pool_consumerConfig_put_descriptorChannel(&cfg, config->descriptor_channel, strlen(config->descriptor_channel)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_consumerConfig_put_descriptorChannel(&cfg, "", 0);
    }

    if (NULL != config->control_channel)
    {
        if (tensor_pool_consumerConfig_put_controlChannel(&cfg, config->control_channel, strlen(config->control_channel)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_consumerConfig_put_controlChannel(&cfg, "", 0);
    }

    return tp_offer_message(producer->control_publication, buffer, tensor_pool_consumerConfig_sbe_position(&cfg));
}

int tp_producer_send_data_source_announce(tp_producer_t *producer, const tp_data_source_announce_t *announce)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_dataSourceAnnounce msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_dataSourceAnnounce_sbe_block_length();

    if (NULL == producer || NULL == producer->metadata_publication || NULL == announce)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_data_source_announce: metadata publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_dataSourceAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_dataSourceAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_dataSourceAnnounce_sbe_schema_version());

    tensor_pool_dataSourceAnnounce_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_dataSourceAnnounce_set_streamId(&msg, announce->stream_id);
    tensor_pool_dataSourceAnnounce_set_producerId(&msg, announce->producer_id);
    tensor_pool_dataSourceAnnounce_set_epoch(&msg, announce->epoch);
    tensor_pool_dataSourceAnnounce_set_metaVersion(&msg, announce->meta_version);

    if (NULL != announce->name)
    {
        if (tensor_pool_dataSourceAnnounce_put_name(&msg, announce->name, strlen(announce->name)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_dataSourceAnnounce_put_name(&msg, "", 0);
    }

    if (NULL != announce->summary)
    {
        if (tensor_pool_dataSourceAnnounce_put_summary(&msg, announce->summary, strlen(announce->summary)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_dataSourceAnnounce_put_summary(&msg, "", 0);
    }

    return tp_offer_message(producer->metadata_publication, buffer, tensor_pool_dataSourceAnnounce_sbe_position(&msg));
}

int tp_producer_send_data_source_meta(tp_producer_t *producer, const tp_data_source_meta_t *meta)
{
    uint8_t buffer[2048];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_dataSourceMeta msg;
    struct tensor_pool_dataSourceMeta_attributes attrs;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_dataSourceMeta_sbe_block_length();
    size_t i;

    if (NULL == producer || NULL == producer->metadata_publication || NULL == meta)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_data_source_meta: metadata publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_dataSourceMeta_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_dataSourceMeta_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_dataSourceMeta_sbe_schema_version());

    tensor_pool_dataSourceMeta_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_dataSourceMeta_set_streamId(&msg, meta->stream_id);
    tensor_pool_dataSourceMeta_set_metaVersion(&msg, meta->meta_version);
    tensor_pool_dataSourceMeta_set_timestampNs(&msg, meta->timestamp_ns);

    if (NULL == tensor_pool_dataSourceMeta_attributes_wrap_for_encode(
        &attrs,
        (char *)buffer,
        (uint16_t)meta->attribute_count,
        tensor_pool_dataSourceMeta_sbe_position_ptr(&msg),
        tensor_pool_dataSourceMeta_sbe_schema_version(),
        sizeof(buffer)))
    {
        return -1;
    }

    for (i = 0; i < meta->attribute_count; i++)
    {
        const tp_meta_attribute_t *attr = &meta->attributes[i];

        if (NULL == tensor_pool_dataSourceMeta_attributes_next(&attrs))
        {
            return -1;
        }

        if (tensor_pool_dataSourceMeta_attributes_put_key(&attrs, attr->key, strlen(attr->key)) < 0)
        {
            return -1;
        }

        if (tensor_pool_dataSourceMeta_attributes_put_format(&attrs, attr->format, strlen(attr->format)) < 0)
        {
            return -1;
        }

        if (tensor_pool_dataSourceMeta_attributes_put_value(
            &attrs,
            (const char *)attr->value,
            attr->value_length) < 0)
        {
            return -1;
        }
    }

    return tp_offer_message(producer->metadata_publication, buffer, tensor_pool_dataSourceMeta_sbe_position(&msg));
}

int tp_producer_send_meta_blob_announce(tp_producer_t *producer, const tp_meta_blob_announce_t *announce)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_metaBlobAnnounce msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_metaBlobAnnounce_sbe_block_length();

    if (NULL == producer || NULL == producer->metadata_publication || NULL == announce)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_meta_blob_announce: metadata publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_metaBlobAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_metaBlobAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_metaBlobAnnounce_sbe_schema_version());

    tensor_pool_metaBlobAnnounce_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_metaBlobAnnounce_set_streamId(&msg, announce->stream_id);
    tensor_pool_metaBlobAnnounce_set_metaVersion(&msg, announce->meta_version);
    tensor_pool_metaBlobAnnounce_set_blobType(&msg, announce->blob_type);
    tensor_pool_metaBlobAnnounce_set_totalLen(&msg, announce->total_len);
    tensor_pool_metaBlobAnnounce_set_checksum(&msg, announce->checksum);

    return tp_offer_message(producer->metadata_publication, buffer, tensor_pool_metaBlobAnnounce_sbe_position(&msg));
}

int tp_producer_send_meta_blob_chunk(tp_producer_t *producer, const tp_meta_blob_chunk_t *chunk)
{
    uint8_t buffer[4096];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_metaBlobChunk msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_metaBlobChunk_sbe_block_length();

    if (NULL == producer || NULL == producer->metadata_publication || NULL == chunk || NULL == chunk->bytes)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_meta_blob_chunk: metadata publication unavailable");
        return -1;
    }

    if (chunk->bytes_length == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_meta_blob_chunk: empty chunk");
        return -1;
    }

    if (chunk->bytes_length > sizeof(buffer) - header_len - body_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_meta_blob_chunk: chunk too large");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_metaBlobChunk_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_metaBlobChunk_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_metaBlobChunk_sbe_schema_version());

    tensor_pool_metaBlobChunk_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_metaBlobChunk_set_streamId(&msg, chunk->stream_id);
    tensor_pool_metaBlobChunk_set_metaVersion(&msg, chunk->meta_version);
    tensor_pool_metaBlobChunk_set_chunkOffset(&msg, chunk->offset);
    if (tensor_pool_metaBlobChunk_put_bytes(&msg, (const char *)chunk->bytes, chunk->bytes_length) < 0)
    {
        return -1;
    }

    return tp_offer_message(producer->metadata_publication, buffer, tensor_pool_metaBlobChunk_sbe_position(&msg));
}

int tp_producer_send_meta_blob_complete(tp_producer_t *producer, const tp_meta_blob_complete_t *complete)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_metaBlobComplete msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_metaBlobComplete_sbe_block_length();

    if (NULL == producer || NULL == producer->metadata_publication || NULL == complete)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_meta_blob_complete: metadata publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_metaBlobComplete_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_metaBlobComplete_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_metaBlobComplete_sbe_schema_version());

    tensor_pool_metaBlobComplete_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_metaBlobComplete_set_streamId(&msg, complete->stream_id);
    tensor_pool_metaBlobComplete_set_metaVersion(&msg, complete->meta_version);
    tensor_pool_metaBlobComplete_set_checksum(&msg, complete->checksum);

    return tp_offer_message(producer->metadata_publication, buffer, tensor_pool_metaBlobComplete_sbe_position(&msg));
}

int tp_producer_send_shm_pool_announce(tp_producer_t *producer, const tp_shm_pool_announce_t *announce)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmPoolAnnounce msg;
    struct tensor_pool_shmPoolAnnounce_payloadPools pools;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_shmPoolAnnounce_sbe_block_length();
    size_t pool_count;
    size_t buffer_len;
    size_t i;
    uint8_t *buffer = NULL;
    int result = -1;

    if (NULL == producer || NULL == producer->control_publication || NULL == announce)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_shm_pool_announce: control publication unavailable");
        return -1;
    }

    pool_count = announce->pool_count;
    if (pool_count == 0 || NULL == announce->pools)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_shm_pool_announce: missing payload pools");
        return -1;
    }

    if (NULL == announce->header_region_uri || announce->header_region_uri[0] == '\0')
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_shm_pool_announce: missing header region uri");
        return -1;
    }

    buffer_len = header_len + body_len +
        tensor_pool_shmPoolAnnounce_payloadPools_sbe_header_size() +
        tensor_pool_shmPoolAnnounce_headerRegionUri_header_length() +
        tp_control_strlen(announce->header_region_uri);

    for (i = 0; i < pool_count; i++)
    {
        const tp_shm_pool_announce_pool_t *pool = &announce->pools[i];
        buffer_len += tensor_pool_shmPoolAnnounce_payloadPools_sbe_block_length() +
            tensor_pool_shmPoolAnnounce_payloadPools_regionUri_header_length() +
            tp_control_strlen(pool->region_uri);
    }

    buffer = calloc(1, buffer_len);
    if (NULL == buffer)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_producer_send_shm_pool_announce: buffer allocation failed");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmPoolAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmPoolAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmPoolAnnounce_sbe_schema_version());

    tensor_pool_shmPoolAnnounce_wrap_for_encode(&msg, (char *)buffer, header_len, buffer_len);
    tensor_pool_shmPoolAnnounce_set_streamId(&msg, announce->stream_id);
    tensor_pool_shmPoolAnnounce_set_producerId(&msg, announce->producer_id);
    tensor_pool_shmPoolAnnounce_set_epoch(&msg, announce->epoch);
    tensor_pool_shmPoolAnnounce_set_announceTimestampNs(&msg, announce->announce_timestamp_ns);
    tensor_pool_shmPoolAnnounce_set_layoutVersion(&msg, announce->layout_version);
    tensor_pool_shmPoolAnnounce_set_headerNslots(&msg, announce->header_nslots);
    tensor_pool_shmPoolAnnounce_set_headerSlotBytes(&msg, announce->header_slot_bytes);
    tensor_pool_shmPoolAnnounce_set_announceClockDomain(&msg, (enum tensor_pool_clockDomain)announce->announce_clock_domain);

    if (NULL == tensor_pool_shmPoolAnnounce_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        (uint16_t)pool_count,
        tensor_pool_shmPoolAnnounce_sbe_position_ptr(&msg),
        tensor_pool_shmPoolAnnounce_sbe_schema_version(),
        buffer_len))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_shm_pool_announce: payload pool wrap failed");
        goto cleanup;
    }

    for (i = 0; i < pool_count; i++)
    {
        const tp_shm_pool_announce_pool_t *pool = &announce->pools[i];

        if (!tensor_pool_shmPoolAnnounce_payloadPools_next(&pools))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_producer_send_shm_pool_announce: payload pool next failed");
            goto cleanup;
        }

        tensor_pool_shmPoolAnnounce_payloadPools_set_poolId(&pools, pool->pool_id);
        tensor_pool_shmPoolAnnounce_payloadPools_set_poolNslots(&pools, pool->pool_nslots);
        tensor_pool_shmPoolAnnounce_payloadPools_set_strideBytes(&pools, pool->stride_bytes);

        if (tensor_pool_shmPoolAnnounce_payloadPools_put_regionUri(
            &pools,
            pool->region_uri ? pool->region_uri : "",
            tp_control_strlen(pool->region_uri)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_producer_send_shm_pool_announce: payload pool uri encode failed");
            goto cleanup;
        }
    }

    if (tensor_pool_shmPoolAnnounce_put_headerRegionUri(
        &msg,
        announce->header_region_uri,
        tp_control_strlen(announce->header_region_uri)) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_shm_pool_announce: header uri encode failed");
        goto cleanup;
    }

    result = tp_offer_message(producer->control_publication, buffer, tensor_pool_shmPoolAnnounce_sbe_position(&msg));

cleanup:
    free(buffer);
    return result;
}

int tp_producer_send_control_response(tp_producer_t *producer, const tp_control_response_t *response)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_controlResponse resp;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_controlResponse_sbe_block_length();

    if (NULL == producer || NULL == producer->control_publication || NULL == response)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_control_response: control publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_controlResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_controlResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_controlResponse_sbe_schema_version());

    tensor_pool_controlResponse_wrap_for_encode(&resp, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_controlResponse_set_correlationId(&resp, response->correlation_id);
    tensor_pool_controlResponse_set_code(&resp, (enum tensor_pool_responseCode)response->code);

    if (NULL != response->error_message)
    {
        if (tensor_pool_controlResponse_put_errorMessage(&resp, response->error_message, strlen(response->error_message)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_controlResponse_put_errorMessage(&resp, "", 0);
    }

    return tp_offer_message(producer->control_publication, buffer, tensor_pool_controlResponse_sbe_position(&resp));
}
