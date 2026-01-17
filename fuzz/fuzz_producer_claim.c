#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"

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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_producer_t producer;
    tp_payload_pool_t pool;
    tp_buffer_claim_t claim;
    uint8_t *header_region = NULL;
    uint8_t *pool_region = NULL;
    uint32_t nslots;
    uint32_t stride_bytes;
    size_t header_len;
    size_t pool_len;
    size_t length;
    size_t offset = 0;

    if (data == NULL || size == 0)
    {
        return 0;
    }

    nslots = 1u << ((data[0] & 0x3u) + 1u);
    offset = 1;
    stride_bytes = (tp_fuzz_u32(data, size, &offset) % 4096u) + 64u;
    length = (size_t)(tp_fuzz_u32(data, size, &offset) % (stride_bytes + 1u));

    header_len = TP_SUPERBLOCK_SIZE_BYTES + (size_t)nslots * TP_HEADER_SLOT_BYTES;
    pool_len = TP_SUPERBLOCK_SIZE_BYTES + (size_t)nslots * stride_bytes;

    header_region = (uint8_t *)calloc(1u, header_len);
    pool_region = (uint8_t *)calloc(1u, pool_len);
    if (header_region == NULL || pool_region == NULL)
    {
        free(header_region);
        free(pool_region);
        return 0;
    }

    memset(&producer, 0, sizeof(producer));
    memset(&pool, 0, sizeof(pool));
    memset(&claim, 0, sizeof(claim));

    producer.header_nslots = nslots;
    producer.header_region.addr = header_region;
    producer.pool_count = 1;
    producer.pools = &pool;
    producer.next_seq = tp_fuzz_u32(data, size, &offset);

    pool.pool_id = (uint16_t)(tp_fuzz_u32(data, size, &offset) | 1u);
    pool.nslots = nslots;
    pool.stride_bytes = stride_bytes;
    pool.region.addr = pool_region;

    (void)tp_producer_try_claim(&producer, length, &claim);

    free(header_region);
    free(pool_region);

    return 0;
}
