#include "tensor_pool/tp_control.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/messageHeader.h"

static int tp_offer_message(aeron_publication_t *pub, const uint8_t *buffer, size_t length)
{
    int64_t result = aeron_publication_offer(pub, buffer, length, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }
    return 0;
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
    tensor_pool_consumerHello_set_mode(&hello_msg, hello->mode);
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
    tensor_pool_consumerConfig_set_mode(&cfg, config->mode);
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
