#include "tensor_pool/tp_consumer.h"

#include <errno.h>
#include <string.h>

#include "aeron_alloc.h"
#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_types.h"

#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/frameDescriptor.h"
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

static void tp_consumer_descriptor_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_consumer_t *consumer = (tp_consumer_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameDescriptor descriptor;
    tp_frame_descriptor_t view;
    uint16_t template_id;
    uint16_t schema_id;

    (void)header;

    if (NULL == consumer || NULL == buffer)
    {
        return;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
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

    if (schema_id != tensor_pool_frameDescriptor_sbe_schema_id() ||
        template_id != tensor_pool_frameDescriptor_sbe_template_id())
    {
        return;
    }

    tensor_pool_frameDescriptor_wrap_for_decode(
        &descriptor,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        tensor_pool_frameDescriptor_sbe_block_length(),
        tensor_pool_frameDescriptor_sbe_schema_version(),
        length);

    view.seq = tensor_pool_frameDescriptor_seq(&descriptor);
    view.header_index = tensor_pool_frameDescriptor_headerIndex(&descriptor);
    view.timestamp_ns = tensor_pool_frameDescriptor_timestampNs(&descriptor);
    view.meta_version = tensor_pool_frameDescriptor_metaVersion(&descriptor);

    if (consumer->descriptor_handler)
    {
        consumer->descriptor_handler(consumer->descriptor_clientd, &view);
    }
}

static void tp_consumer_control_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_consumer_t *consumer = (tp_consumer_t *)clientd;
    tp_consumer_config_view_t view;

    (void)header;

    if (NULL == consumer || NULL == buffer)
    {
        return;
    }

    if (tp_control_decode_consumer_config(buffer, length, &view) < 0)
    {
        return;
    }

    if (view.consumer_id != consumer->context.consumer_id)
    {
        return;
    }

    if (view.descriptor_channel.length > 0 && view.descriptor_stream_id > 0)
    {
        char channel[4096];
        size_t copy_len = view.descriptor_channel.length;
        if (copy_len >= sizeof(channel))
        {
            copy_len = sizeof(channel) - 1;
        }
        memcpy(channel, view.descriptor_channel.data, copy_len);
        channel[copy_len] = '\0';

        if (consumer->descriptor_subscription)
        {
            aeron_subscription_close(consumer->descriptor_subscription, NULL, NULL);
            consumer->descriptor_subscription = NULL;
        }

        tp_consumer_add_subscription(
            consumer,
            channel,
            (int32_t)view.descriptor_stream_id,
            &consumer->descriptor_subscription);
    }
}

static int tp_consumer_add_publication(
    tp_consumer_t *consumer,
    const char *channel,
    int32_t stream_id,
    aeron_publication_t **out_pub)
{
    aeron_async_add_publication_t *async_add = NULL;

    if (NULL == consumer || NULL == consumer->client || NULL == out_pub || NULL == channel || stream_id < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_add_publication: invalid input");
        return -1;
    }

    if (tp_client_async_add_publication(consumer->client, channel, stream_id, &async_add) < 0)
    {
        return -1;
    }

    *out_pub = NULL;
    while (NULL == *out_pub)
    {
        if (tp_client_async_add_publication_poll(out_pub, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(consumer->client);
    }

    return 0;
}

static int tp_consumer_add_subscription(
    tp_consumer_t *consumer,
    const char *channel,
    int32_t stream_id,
    aeron_subscription_t **out_sub)
{
    aeron_async_add_subscription_t *async_add = NULL;

    if (NULL == consumer || NULL == consumer->client || NULL == out_sub || NULL == channel || stream_id < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_add_subscription: invalid input");
        return -1;
    }

    if (tp_client_async_add_subscription(
        consumer->client,
        channel,
        stream_id,
        NULL,
        NULL,
        NULL,
        NULL,
        &async_add) < 0)
    {
        return -1;
    }

    *out_sub = NULL;
    while (NULL == *out_sub)
    {
        if (tp_client_async_add_subscription_poll(out_sub, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(consumer->client);
    }

    return 0;
}

int tp_consumer_context_init(tp_consumer_context_t *ctx)
{
    if (NULL == ctx)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_context_init: null input");
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    return 0;
}

int tp_consumer_init(tp_consumer_t *consumer, tp_client_t *client, const tp_consumer_context_t *context)
{
    if (NULL == consumer || NULL == client || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_init: null input");
        return -1;
    }

    memset(consumer, 0, sizeof(*consumer));
    consumer->client = client;
    consumer->context = *context;

    if (client->context.base.descriptor_channel[0] != '\0' && client->context.base.descriptor_stream_id >= 0)
    {
        if (tp_consumer_add_subscription(
            consumer,
            client->context.base.descriptor_channel,
            client->context.base.descriptor_stream_id,
            &consumer->descriptor_subscription) < 0)
        {
            return -1;
        }
    }

    if (client->context.base.control_channel[0] != '\0' && client->context.base.control_stream_id >= 0)
    {
        if (tp_consumer_add_publication(
            consumer,
            client->context.base.control_channel,
            client->context.base.control_stream_id,
            &consumer->control_publication) < 0)
        {
            return -1;
        }
    }

    if (client->context.base.qos_channel[0] != '\0' && client->context.base.qos_stream_id >= 0)
    {
        if (tp_consumer_add_publication(
            consumer,
            client->context.base.qos_channel,
            client->context.base.qos_stream_id,
            &consumer->qos_publication) < 0)
        {
            return -1;
        }
    }

    return 0;
}

void tp_consumer_set_descriptor_handler(tp_consumer_t *consumer, tp_frame_descriptor_handler_t handler, void *clientd)
{
    if (NULL == consumer)
    {
        return;
    }

    consumer->descriptor_handler = handler;
    consumer->descriptor_clientd = clientd;
}

int tp_consumer_attach(tp_consumer_t *consumer, const tp_consumer_config_t *config)
{
    tp_shm_expected_t expected;
    size_t i;
    int result = -1;

    if (NULL == consumer || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach: null input");
        return -1;
    }

    consumer->stream_id = config->stream_id;
    consumer->epoch = config->epoch;
    consumer->layout_version = config->layout_version;
    consumer->header_nslots = config->header_nslots;
    consumer->pool_count = config->pool_count;

    if (!tp_is_power_of_two(config->header_nslots))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach: header_nslots must be power of two");
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
        &consumer->client->context.base.allowed_paths,
        &consumer->client->context.base.log) < 0)
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

    if (tp_shm_validate_superblock(&consumer->header_region, &expected, &consumer->client->context.base.log) < 0)
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
            TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach: pool nslots mismatch");
            goto cleanup;
        }
        pool->pool_id = pool_cfg->pool_id;
        pool->nslots = pool_cfg->nslots;
        pool->stride_bytes = pool_cfg->stride_bytes;

        if (tp_shm_map(
            &pool->region,
            pool_cfg->uri,
            0,
            &consumer->client->context.base.allowed_paths,
            &consumer->client->context.base.log) < 0)
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

        if (tp_shm_validate_superblock(&pool->region, &expected, &consumer->client->context.base.log) < 0)
        {
            goto cleanup;
        }
    }

    return 0;

cleanup:
    for (i = 0; i < consumer->pool_count; i++)
    {
        tp_shm_unmap(&consumer->pools[i].region, &consumer->client->context.base.log);
    }

    tp_shm_unmap(&consumer->header_region, &consumer->client->context.base.log);

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

    if (tp_slot_decode(&slot_view, slot, TP_HEADER_SLOT_BYTES, &consumer->client->context.base.log) < 0)
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

    if (tp_tensor_header_decode(&out->tensor, slot_view.header_bytes, slot_view.header_bytes_length, &consumer->client->context.base.log) < 0)
    {
        return 1;
    }

    if (tp_tensor_header_validate(&out->tensor, &consumer->client->context.base.log) < 0)
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

int tp_consumer_poll_descriptors(tp_consumer_t *consumer, int fragment_limit)
{
    if (NULL == consumer || NULL == consumer->descriptor_subscription)
    {
        return 0;
    }

    if (NULL == consumer->descriptor_assembler)
    {
        if (aeron_fragment_assembler_create(
            &consumer->descriptor_assembler,
            tp_consumer_descriptor_handler,
            consumer) < 0)
        {
            return -1;
        }
    }

    return aeron_subscription_poll(
        consumer->descriptor_subscription,
        aeron_fragment_assembler_handler,
        consumer->descriptor_assembler,
        fragment_limit);
}

int tp_consumer_poll_control(tp_consumer_t *consumer, int fragment_limit)
{
    if (NULL == consumer || NULL == consumer->client || NULL == consumer->client->control_subscription)
    {
        return 0;
    }

    if (NULL == consumer->control_assembler)
    {
        if (aeron_fragment_assembler_create(
            &consumer->control_assembler,
            tp_consumer_control_handler,
            consumer) < 0)
        {
            return -1;
        }
    }

    return aeron_subscription_poll(
        consumer->client->control_subscription,
        aeron_fragment_assembler_handler,
        consumer->control_assembler,
        fragment_limit);
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

    if (consumer->descriptor_assembler)
    {
        aeron_fragment_assembler_delete(consumer->descriptor_assembler);
        consumer->descriptor_assembler = NULL;
    }

    if (consumer->control_assembler)
    {
        aeron_fragment_assembler_delete(consumer->control_assembler);
        consumer->control_assembler = NULL;
    }

    tp_shm_unmap(&consumer->header_region, &consumer->client->context.base.log);

    for (i = 0; i < consumer->pool_count; i++)
    {
        tp_shm_unmap(&consumer->pools[i].region, &consumer->client->context.base.log);
    }

    if (consumer->pools)
    {
        aeron_free(consumer->pools);
        consumer->pools = NULL;
    }

    return 0;
}
