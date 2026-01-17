#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/tp_shm.h"
#include "tensor_pool/tp_types.h"

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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t superblock[TP_SUPERBLOCK_SIZE_BYTES];
    tp_shm_region_t region;
    tp_shm_expected_t expected;
    size_t offset = 0;
    int use_expected = 0;

    if (data == NULL)
    {
        return 0;
    }

    memset(superblock, 0, sizeof(superblock));
    if (size > 0)
    {
        size_t copy_len = size < sizeof(superblock) ? size : sizeof(superblock);
        memcpy(superblock, data, copy_len);
        use_expected = (data[0] & 0x1u) != 0u;
        offset = 1;
    }

    memset(&region, 0, sizeof(region));
    region.addr = superblock;
    region.length = sizeof(superblock);

    if (use_expected)
    {
        memset(&expected, 0, sizeof(expected));
        expected.stream_id = tp_fuzz_u32(data, size, &offset);
        expected.layout_version = tp_fuzz_u32(data, size, &offset);
        expected.epoch = tp_fuzz_u64(data, size, &offset);
        expected.region_type = (int16_t)tp_fuzz_u32(data, size, &offset);
        expected.pool_id = (uint16_t)tp_fuzz_u32(data, size, &offset);
        expected.nslots = tp_fuzz_u32(data, size, &offset);
        expected.slot_bytes = tp_fuzz_u32(data, size, &offset);
        expected.stride_bytes = tp_fuzz_u32(data, size, &offset);

        (void)tp_shm_validate_superblock(&region, &expected, NULL);
    }
    else
    {
        (void)tp_shm_validate_superblock(&region, NULL, NULL);
    }

    return 0;
}
