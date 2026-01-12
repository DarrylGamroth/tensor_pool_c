#include "tensor_pool/tp_consumer.h"

#include <errno.h>
#include <string.h>

#include "aeron_alloc.h"

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_types.h"

#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/tensorHeader.h"

static tp_consumer_pool_t *tp_consumer_find_pool(tp_consumer_t *consumer, uint16_t pool_id)
{
    size_t i;

    for (i = 0; i < consumer->pool_count; i++)
    {
        if (consumer->pools[i].pool_id == pool_id)
        {
            return &consumer->pools[i];
        }
    }

    return NULL;
}

static int tp_is_power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

int tp_consumer_init(tp_consumer_t *consumer, const tp_context_t *context)
{
    if (NULL == consumer || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_init: null input");
        return -1;
    }

    memset(consumer, 0, sizeof(*consumer));
    consumer->context = *context;

    if (tp_aeron_client_init(&consumer->aeron, context) < 0)
    {
        return -1;
    }

    if (context->descriptor_channel[0] != '\0' && context->descriptor_stream_id >= 0)
    {
        if (tp_aeron_add_subscription(
            &consumer->descriptor_subscription,
            &consumer->aeron,
            context->descriptor_channel,
            context->descriptor_stream_id,
            NULL,
            NULL,
            NULL,
            NULL) < 0)
        {
            tp_aeron_client_close(&consumer->aeron);
            return -1;
        }
    }

    if (context->control_channel[0] != '\0' && context->control_stream_id >= 0)
    {
        if (tp_aeron_add_publication(
            &consumer->control_publication,
            &consumer->aeron,
            context->control_channel,
            context->control_stream_id) < 0)
        {
            tp_aeron_client_close(&consumer->aeron);
            return -1;
        }
    }

    if (context->qos_channel[0] != '\0' && context->qos_stream_id >= 0)
    {
        if (tp_aeron_add_publication(
            &consumer->qos_publication,
            &consumer->aeron,
            context->qos_channel,
            context->qos_stream_id) < 0)
        {
            tp_aeron_client_close(&consumer->aeron);
            return -1;
        }
    }

    return 0;
}

int tp_consumer_attach_direct(tp_consumer_t *consumer, const tp_consumer_config_t *config)
{
    tp_shm_expected_t expected;
    size_t i;
    int result = -1;

    if (NULL == consumer || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach_direct: null input");
        return -1;
    }

    consumer->stream_id = config->stream_id;
    consumer->epoch = config->epoch;
    consumer->layout_version = config->layout_version;
    consumer->header_nslots = config->header_nslots;
    consumer->pool_count = config->pool_count;

    if (!tp_is_power_of_two(config->header_nslots))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach_direct: header_nslots must be power of two");
        return -1;
    }

    if (consumer->pool_count > 0)
    {
        if (aeron_alloc((void **)&consumer->pools, sizeof(tp_consumer_pool_t) * consumer->pool_count) < 0)
        {
            return -1;
        }
    }

    if (tp_shm_map(
        &consumer->header_region,
        config->header_uri,
        0,
        &consumer->context.allowed_paths,
        &consumer->context.log) < 0)
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

    if (tp_shm_validate_superblock(&consumer->header_region, &expected, &consumer->context.log) < 0)
    {
        goto cleanup;
    }

    for (i = 0; i < config->pool_count; i++)
    {
        const tp_consumer_pool_config_t *pool_cfg = &config->pools[i];
        tp_consumer_pool_t *pool = &consumer->pools[i];

        memset(pool, 0, sizeof(*pool));
        if (pool_cfg->nslots != config->header_nslots)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach_direct: pool nslots mismatch");
            goto cleanup;
        }
        pool->pool_id = pool_cfg->pool_id;
        pool->nslots = pool_cfg->nslots;
        pool->stride_bytes = pool_cfg->stride_bytes;

        if (tp_shm_map(
            &pool->region,
            pool_cfg->uri,
            0,
            &consumer->context.allowed_paths,
            &consumer->context.log) < 0)
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

        if (tp_shm_validate_superblock(&pool->region, &expected, &consumer->context.log) < 0)
        {
            goto cleanup;
        }
    }

    return 0;

cleanup:
    for (i = 0; i < consumer->pool_count; i++)
    {
        tp_shm_unmap(&consumer->pools[i].region, &consumer->context.log);
    }

    tp_shm_unmap(&consumer->header_region, &consumer->context.log);

    if (consumer->pools)
    {
        aeron_free(consumer->pools);
        consumer->pools = NULL;
    }

    return result;
}

int tp_consumer_read_frame(tp_consumer_t *consumer, uint64_t seq, uint32_t header_index, tp_frame_view_t *out)
{
    uint8_t *slot;
    tp_slot_view_t slot_view;
    tp_consumer_pool_t *pool;
    uint64_t seq_first;
    uint64_t seq_second;

    if (NULL == consumer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_read_frame: null input");
        return -1;
    }

    if (header_index >= consumer->header_nslots)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_read_frame: header index out of range");
        return -1;
    }

    slot = tp_slot_at(consumer->header_region.addr, header_index);

    seq_first = tp_atomic_load_u64((uint64_t *)slot);
    if (!tp_seq_is_committed(seq_first))
    {
        return 1;
    }

    if (tp_slot_decode(&slot_view, slot, TP_HEADER_SLOT_BYTES, &consumer->context.log) < 0)
    {
        return -1;
    }

    if (slot_view.payload_offset != 0)
    {
        return 1;
    }

    if (slot_view.payload_slot != header_index)
    {
        return 1;
    }

    pool = tp_consumer_find_pool(consumer, slot_view.pool_id);
    if (NULL == pool)
    {
        return 1;
    }

    if (slot_view.values_len_bytes > pool->stride_bytes)
    {
        return 1;
    }

    if (slot_view.header_bytes_length != (tensor_pool_messageHeader_encoded_length() + tensor_pool_tensorHeader_sbe_block_length()))
    {
        return 1;
    }

    if (tp_tensor_header_decode(&out->tensor, slot_view.header_bytes, slot_view.header_bytes_length, &consumer->context.log) < 0)
    {
        return 1;
    }

    if (tp_tensor_header_validate(&out->tensor, &consumer->context.log) < 0)
    {
        return 1;
    }

    out->payload_len = slot_view.values_len_bytes;
    out->payload = (const uint8_t *)pool->region.addr + TP_SUPERBLOCK_SIZE_BYTES + (slot_view.payload_slot * pool->stride_bytes);
    out->pool_id = slot_view.pool_id;
    out->payload_slot = slot_view.payload_slot;
    out->timestamp_ns = slot_view.timestamp_ns;
    out->meta_version = slot_view.meta_version;

    seq_second = tp_atomic_load_u64((uint64_t *)slot);
    if (seq_second != seq_first || !tp_seq_is_committed(seq_second))
    {
        return 1;
    }

    if (tp_seq_value(seq_second) != seq)
    {
        return 1;
    }

    return 0;
}

int tp_consumer_close(tp_consumer_t *consumer)
{
    size_t i;

    if (NULL == consumer)
    {
        return -1;
    }

    if (consumer->descriptor_subscription)
    {
        aeron_subscription_close(consumer->descriptor_subscription, NULL, NULL);
        consumer->descriptor_subscription = NULL;
    }

    if (consumer->control_publication)
    {
        aeron_publication_close(consumer->control_publication, NULL, NULL);
        consumer->control_publication = NULL;
    }

    if (consumer->qos_publication)
    {
        aeron_publication_close(consumer->qos_publication, NULL, NULL);
        consumer->qos_publication = NULL;
    }

    tp_shm_unmap(&consumer->header_region, &consumer->context.log);

    for (i = 0; i < consumer->pool_count; i++)
    {
        tp_shm_unmap(&consumer->pools[i].region, &consumer->context.log);
    }

    if (consumer->pools)
    {
        aeron_free(consumer->pools);
        consumer->pools = NULL;
    }

    tp_aeron_client_close(&consumer->aeron);

    return 0;
}
