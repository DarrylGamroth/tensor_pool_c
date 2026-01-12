#include "tensor_pool/tp_producer.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_types.h"

#include "aeron_alloc.h"

#include "wire/tensor_pool/frameDescriptor.h"
#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/slotHeader.h"
#include "wire/tensor_pool/tensorHeader.h"
#include "wire/tensor_pool/regionType.h"

static tp_payload_pool_t *tp_find_pool(tp_producer_t *producer, uint16_t pool_id)
{
    size_t i;

    for (i = 0; i < producer->pool_count; i++)
    {
        if (producer->pools[i].pool_id == pool_id)
        {
            return &producer->pools[i];
        }
    }

    return NULL;
}

static int tp_is_power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

int tp_producer_publish_descriptor_to(
    tp_producer_t *producer,
    aeron_publication_t *publication,
    uint64_t seq,
    uint32_t header_index,
    uint64_t timestamp_ns,
    uint32_t meta_version)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameDescriptor descriptor;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_frameDescriptor_sbe_block_length();
    int64_t result;

    if (NULL == producer || NULL == publication)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_descriptor_to: publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_frameDescriptor_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_frameDescriptor_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_frameDescriptor_sbe_schema_version());

    tensor_pool_frameDescriptor_wrap_for_encode(
        &descriptor,
        (char *)buffer,
        header_len,
        sizeof(buffer));

    tensor_pool_frameDescriptor_set_streamId(&descriptor, producer->stream_id);
    tensor_pool_frameDescriptor_set_epoch(&descriptor, producer->epoch);
    tensor_pool_frameDescriptor_set_seq(&descriptor, seq);
    tensor_pool_frameDescriptor_set_headerIndex(&descriptor, header_index);
    tensor_pool_frameDescriptor_set_timestampNs(&descriptor, timestamp_ns);
    tensor_pool_frameDescriptor_set_metaVersion(&descriptor, meta_version);

    result = aeron_publication_offer(
        publication,
        buffer,
        header_len + body_len,
        NULL,
        NULL);

    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

static int tp_producer_publish_descriptor(
    tp_producer_t *producer,
    uint64_t seq,
    uint32_t header_index,
    uint64_t timestamp_ns,
    uint32_t meta_version)
{
    return tp_producer_publish_descriptor_to(
        producer,
        producer->descriptor_publication,
        seq,
        header_index,
        timestamp_ns,
        meta_version);
}

int tp_producer_init(tp_producer_t *producer, const tp_context_t *context)
{
    if (NULL == producer || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_init: null input");
        return -1;
    }

    memset(producer, 0, sizeof(*producer));
    producer->context = *context;

    if (tp_aeron_client_init(&producer->aeron, context) < 0)
    {
        return -1;
    }

    if (context->descriptor_channel[0] != '\0' && context->descriptor_stream_id >= 0)
    {
        if (tp_aeron_add_publication(
            &producer->descriptor_publication,
            &producer->aeron,
            context->descriptor_channel,
            context->descriptor_stream_id) < 0)
        {
            tp_aeron_client_close(&producer->aeron);
            return -1;
        }
    }

    if (context->control_channel[0] != '\0' && context->control_stream_id >= 0)
    {
        if (tp_aeron_add_publication(
            &producer->control_publication,
            &producer->aeron,
            context->control_channel,
            context->control_stream_id) < 0)
        {
            tp_aeron_client_close(&producer->aeron);
            return -1;
        }
    }

    if (context->qos_channel[0] != '\0' && context->qos_stream_id >= 0)
    {
        if (tp_aeron_add_publication(
            &producer->qos_publication,
            &producer->aeron,
            context->qos_channel,
            context->qos_stream_id) < 0)
        {
            tp_aeron_client_close(&producer->aeron);
            return -1;
        }
    }

    if (context->metadata_channel[0] != '\0' && context->metadata_stream_id >= 0)
    {
        if (tp_aeron_add_publication(
            &producer->metadata_publication,
            &producer->aeron,
            context->metadata_channel,
            context->metadata_stream_id) < 0)
        {
            tp_aeron_client_close(&producer->aeron);
            return -1;
        }
    }

    return 0;
}

int tp_producer_attach_direct(tp_producer_t *producer, const tp_producer_config_t *config)
{
    tp_shm_expected_t expected;
    size_t i;
    int result = -1;

    if (NULL == producer || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_attach_direct: null input");
        return -1;
    }

    producer->stream_id = config->stream_id;
    producer->producer_id = config->producer_id;
    producer->epoch = config->epoch;
    producer->layout_version = config->layout_version;
    producer->header_nslots = config->header_nslots;
    producer->pool_count = config->pool_count;

    if (!tp_is_power_of_two(config->header_nslots))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_attach_direct: header_nslots must be power of two");
        return -1;
    }

    if (producer->pool_count > 0)
    {
        if (aeron_alloc((void **)&producer->pools, sizeof(tp_payload_pool_t) * producer->pool_count) < 0)
        {
            return -1;
        }
    }

    if (tp_shm_map(
        &producer->header_region,
        config->header_uri,
        1,
        &producer->context.allowed_paths,
        &producer->context.log) < 0)
    {
        goto cleanup;
    }

    memset(&expected, 0, sizeof(expected));
    expected.stream_id = config->stream_id;
    expected.layout_version = config->layout_version;
    expected.epoch = config->epoch;
    expected.region_type = tensor_pool_regionType_HEADER_RING;
    expected.pool_id = 0;
    expected.nslots = config->header_nslots;
    expected.slot_bytes = TP_HEADER_SLOT_BYTES;
    expected.stride_bytes = TP_NULL_U32;

    if (tp_shm_validate_superblock(&producer->header_region, &expected, &producer->context.log) < 0)
    {
        goto cleanup;
    }

    for (i = 0; i < config->pool_count; i++)
    {
        const tp_payload_pool_config_t *pool_cfg = &config->pools[i];
        tp_payload_pool_t *pool = &producer->pools[i];

        memset(pool, 0, sizeof(*pool));
        if (pool_cfg->nslots != config->header_nslots)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_producer_attach_direct: pool nslots mismatch");
            goto cleanup;
        }
        pool->pool_id = pool_cfg->pool_id;
        pool->nslots = pool_cfg->nslots;
        pool->stride_bytes = pool_cfg->stride_bytes;

        if (tp_shm_map(
            &pool->region,
            pool_cfg->uri,
            1,
            &producer->context.allowed_paths,
            &producer->context.log) < 0)
        {
            goto cleanup;
        }

        memset(&expected, 0, sizeof(expected));
        expected.stream_id = config->stream_id;
        expected.layout_version = config->layout_version;
        expected.epoch = config->epoch;
        expected.region_type = tensor_pool_regionType_PAYLOAD_POOL;
        expected.pool_id = pool->pool_id;
        expected.nslots = pool->nslots;
        expected.slot_bytes = TP_NULL_U32;
        expected.stride_bytes = pool->stride_bytes;

        if (tp_shm_validate_superblock(&pool->region, &expected, &producer->context.log) < 0)
        {
            goto cleanup;
        }
    }

    return 0;

cleanup:
    for (i = 0; i < producer->pool_count; i++)
    {
        tp_shm_unmap(&producer->pools[i].region, &producer->context.log);
    }

    tp_shm_unmap(&producer->header_region, &producer->context.log);

    if (producer->pools)
    {
        aeron_free(producer->pools);
        producer->pools = NULL;
    }

    return result;
}

static int tp_encode_tensor_header(uint8_t *buffer, size_t buffer_len, const tp_tensor_header_t *tensor)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_tensorHeader header;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t i;

    if (buffer_len < header_len + tensor_pool_tensorHeader_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_encode_tensor_header: buffer too small");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_tensorHeader_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_tensorHeader_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_tensorHeader_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version());

    tensor_pool_tensorHeader_wrap_for_encode(
        &header,
        (char *)buffer,
        header_len,
        buffer_len);

    tensor_pool_tensorHeader_set_dtype(&header, tensor->dtype);
    tensor_pool_tensorHeader_set_majorOrder(&header, tensor->major_order);
    tensor_pool_tensorHeader_set_ndims(&header, tensor->ndims);
    tensor_pool_tensorHeader_set_padAlign(&header, 0);
    tensor_pool_tensorHeader_set_progressUnit(&header, tensor->progress_unit);
    tensor_pool_tensorHeader_set_progressStrideBytes(&header, tensor->progress_stride_bytes);

    for (i = 0; i < TP_MAX_DIMS; i++)
    {
        tensor_pool_tensorHeader_set_dims(&header, i, tensor->dims[i]);
        tensor_pool_tensorHeader_set_strides(&header, i, tensor->strides[i]);
    }

    return 0;
}

int tp_producer_publish_frame(
    tp_producer_t *producer,
    uint64_t seq,
    uint32_t header_index,
    const tp_tensor_header_t *tensor,
    const void *payload,
    uint32_t payload_len,
    uint16_t pool_id,
    uint64_t timestamp_ns,
    uint32_t meta_version)
{
    tp_payload_pool_t *pool;
    uint8_t *slot;
    uint8_t *payload_dst;
    struct tensor_pool_slotHeader slot_header;
    uint8_t header_bytes[TP_HEADER_SLOT_BYTES];
    uint64_t in_progress;
    uint64_t committed;

    if (NULL == producer || NULL == tensor || (NULL == payload && payload_len > 0))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_frame: null input");
        return -1;
    }

    if (header_index >= producer->header_nslots)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_frame: header index out of range");
        return -1;
    }

    pool = tp_find_pool(producer, pool_id);
    if (NULL == pool)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_frame: unknown pool id");
        return -1;
    }

    if (payload_len > pool->stride_bytes)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_frame: payload too large for pool");
        return -1;
    }

    slot = tp_slot_at(producer->header_region.addr, header_index);
    payload_dst = (uint8_t *)pool->region.addr + TP_SUPERBLOCK_SIZE_BYTES + (header_index * pool->stride_bytes);

    in_progress = tp_seq_in_progress(seq);
    committed = tp_seq_committed(seq);

    tp_atomic_store_u64((uint64_t *)slot, in_progress);
    if (payload_len > 0)
    {
        memcpy(payload_dst, payload, payload_len);
    }

    tensor_pool_slotHeader_wrap_for_encode(
        &slot_header,
        (char *)slot,
        0,
        TP_HEADER_SLOT_BYTES);

    tensor_pool_slotHeader_set_valuesLenBytes(&slot_header, payload_len);
    tensor_pool_slotHeader_set_payloadSlot(&slot_header, header_index);
    tensor_pool_slotHeader_set_poolId(&slot_header, pool_id);
    tensor_pool_slotHeader_set_payloadOffset(&slot_header, 0);
    tensor_pool_slotHeader_set_timestampNs(&slot_header, timestamp_ns);
    tensor_pool_slotHeader_set_metaVersion(&slot_header, meta_version);

    if (tp_encode_tensor_header(header_bytes, sizeof(header_bytes), tensor) < 0)
    {
        return -1;
    }

    if (tensor_pool_slotHeader_put_headerBytes(
        &slot_header,
        (const char *)header_bytes,
        tensor_pool_messageHeader_encoded_length() + tensor_pool_tensorHeader_sbe_block_length()) < 0)
    {
        return -1;
    }

    __atomic_thread_fence(__ATOMIC_RELEASE);
    tp_atomic_store_u64((uint64_t *)slot, committed);

    return tp_producer_publish_descriptor(producer, seq, header_index, timestamp_ns, meta_version);
}

int tp_producer_publish_progress_to(
    tp_producer_t *producer,
    aeron_publication_t *publication,
    uint64_t seq,
    uint32_t header_index,
    uint64_t payload_bytes_filled,
    uint8_t state)
{
    uint8_t buffer[128];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameProgress progress;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_frameProgress_sbe_block_length();
    int64_t result;

    if (NULL == producer || NULL == publication)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_progress_to: control publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_frameProgress_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_frameProgress_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_frameProgress_sbe_schema_version());

    tensor_pool_frameProgress_wrap_for_encode(&progress, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_frameProgress_set_streamId(&progress, producer->stream_id);
    tensor_pool_frameProgress_set_epoch(&progress, producer->epoch);
    tensor_pool_frameProgress_set_frameId(&progress, seq);
    tensor_pool_frameProgress_set_headerIndex(&progress, header_index);
    tensor_pool_frameProgress_set_payloadBytesFilled(&progress, payload_bytes_filled);
    tensor_pool_frameProgress_set_state(&progress, state);

    result = aeron_publication_offer(publication, buffer, header_len + body_len, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

int tp_producer_publish_progress(
    tp_producer_t *producer,
    uint64_t seq,
    uint32_t header_index,
    uint64_t payload_bytes_filled,
    uint8_t state)
{
    return tp_producer_publish_progress_to(
        producer,
        producer->control_publication,
        seq,
        header_index,
        payload_bytes_filled,
        state);
}

int tp_producer_close(tp_producer_t *producer)
{
    size_t i;

    if (NULL == producer)
    {
        return -1;
    }

    if (producer->descriptor_publication)
    {
        aeron_publication_close(producer->descriptor_publication, NULL, NULL);
        producer->descriptor_publication = NULL;
    }

    if (producer->control_publication)
    {
        aeron_publication_close(producer->control_publication, NULL, NULL);
        producer->control_publication = NULL;
    }

    if (producer->qos_publication)
    {
        aeron_publication_close(producer->qos_publication, NULL, NULL);
        producer->qos_publication = NULL;
    }

    if (producer->metadata_publication)
    {
        aeron_publication_close(producer->metadata_publication, NULL, NULL);
        producer->metadata_publication = NULL;
    }

    tp_shm_unmap(&producer->header_region, &producer->context.log);

    for (i = 0; i < producer->pool_count; i++)
    {
        tp_shm_unmap(&producer->pools[i].region, &producer->context.log);
    }

    if (producer->pools)
    {
        aeron_free(producer->pools);
        producer->pools = NULL;
    }

    tp_aeron_client_close(&producer->aeron);

    return 0;
}
