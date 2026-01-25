#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/internal/tp_client_internal.h"
#include "tensor_pool/internal/tp_consumer_internal.h"
#include "tensor_pool/internal/tp_producer_internal.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_types.h"

#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/slotHeader.h"
#include "wire/tensor_pool/tensorHeader.h"

static uint32_t tp_fuzz_u32(const uint8_t *data, size_t size, size_t *offset)
{
    uint32_t value = 0;
    size_t i;

    for (i = 0; i < sizeof(value) && *offset < size; i++, (*offset)++)
    {
        value |= ((uint32_t)data[*offset]) << (8u * i);
    }

    return value;
}

static uint64_t tp_fuzz_u64(const uint8_t *data, size_t size, size_t *offset)
{
    uint64_t value = 0;
    size_t i;

    for (i = 0; i < sizeof(value) && *offset < size; i++, (*offset)++)
    {
        value |= ((uint64_t)data[*offset]) << (8u * i);
    }

    return value;
}

static uint16_t tp_fuzz_u16(const uint8_t *data, size_t size, size_t *offset)
{
    uint16_t value = 0;
    size_t i;

    for (i = 0; i < sizeof(value) && *offset < size; i++, (*offset)++)
    {
        value |= ((uint16_t)data[*offset]) << (8u * i);
    }

    return value;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint32_t nslots;
    size_t header_len;
    size_t offset = 0;
    uint8_t *ring;
    size_t ring_len;
    uint32_t header_index;
    uint8_t header_bytes[128];
    struct tensor_pool_slotHeader header;
    uint64_t seq;
    uint64_t seq_commit;
    uint32_t values_len;
    uint32_t stride_bytes;
    uint16_t pool_id;
    tp_frame_progress_t progress;
    tp_consumer_t consumer;
    tp_consumer_pool_t pool;
    tp_client_t client;

    if (data == NULL || size == 0)
    {
        return 0;
    }

    nslots = 1u << ((data[0] & 0x3u) + 1u);
    header_len = tensor_pool_messageHeader_encoded_length() + tensor_pool_tensorHeader_sbe_block_length();
    if (header_len > sizeof(header_bytes))
    {
        return 0;
    }

    offset = 1;
    seq = tp_fuzz_u64(data, size, &offset);
    progress.stream_id = tp_fuzz_u32(data, size, &offset);
    progress.epoch = tp_fuzz_u64(data, size, &offset);
    stride_bytes = (tp_fuzz_u32(data, size, &offset) % 4096u) + 64u;
    values_len = tp_fuzz_u32(data, size, &offset) % (stride_bytes + 1u);
    pool_id = (uint16_t)(tp_fuzz_u16(data, size, &offset) | 1u);
    progress.seq = seq;
    progress.payload_bytes_filled = (values_len > 0) ? (tp_fuzz_u32(data, size, &offset) % (values_len + 1u)) : 0u;
    progress.state = (tp_progress_state_t)(tp_fuzz_u32(data, size, &offset) % 4u);

    ring_len = TP_SUPERBLOCK_SIZE_BYTES + (size_t)nslots * TP_HEADER_SLOT_BYTES;
    ring = (uint8_t *)calloc(1u, ring_len);
    if (ring == NULL)
    {
        return 0;
    }

    memset(&consumer, 0, sizeof(consumer));
    memset(&pool, 0, sizeof(pool));
    memset(&client, 0, sizeof(client));

    consumer.client = &client;
    consumer.shm_mapped = true;
    consumer.stream_id = progress.stream_id;
    consumer.epoch = progress.epoch;
    consumer.header_nslots = nslots;
    consumer.header_region.addr = ring;
    consumer.pools = &pool;
    consumer.pool_count = 1;

    pool.pool_id = pool_id;
    pool.stride_bytes = stride_bytes;

    header_index = (uint32_t)(seq & (nslots - 1u));
    seq_commit = (data[0] & 0x1u) ? tp_seq_committed(seq) : tp_seq_in_progress(seq);

    tensor_pool_slotHeader_wrap_for_encode(&header, (char *)tp_slot_at(ring, header_index), 0, TP_HEADER_SLOT_BYTES);
    tensor_pool_slotHeader_set_seqCommit(&header, seq_commit);
    tensor_pool_slotHeader_set_valuesLenBytes(&header, values_len);
    tensor_pool_slotHeader_set_payloadSlot(&header, header_index);
    tensor_pool_slotHeader_set_poolId(&header, pool_id);
    tensor_pool_slotHeader_set_payloadOffset(&header, 0);
    tensor_pool_slotHeader_set_timestampNs(&header, tp_fuzz_u64(data, size, &offset));
    tensor_pool_slotHeader_set_metaVersion(&header, tp_fuzz_u32(data, size, &offset));

    for (size_t i = 0; i < header_len; i++)
    {
        header_bytes[i] = (offset < size) ? data[offset++] : 0u;
    }
    (void)tensor_pool_slotHeader_put_headerBytes(&header, (const char *)header_bytes, (uint32_t)header_len);
    tp_atomic_store_u64((uint64_t *)tp_slot_at(ring, header_index), seq_commit);

    (void)tp_consumer_validate_progress(&consumer, &progress);

    free(ring);
    return 0;
}
