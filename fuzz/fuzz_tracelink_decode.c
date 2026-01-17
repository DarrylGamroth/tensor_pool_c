#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/tp_tracelink.h"

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
    uint64_t parents[TP_TRACELINK_MAX_PARENTS];
    tp_tracelink_set_t decoded;
    tp_tracelink_set_t encoded;
    uint8_t buffer[4096];
    size_t out_len = 0;
    size_t offset = 0;

    if (data == NULL)
    {
        return 0;
    }

    memset(parents, 0, sizeof(parents));
    (void)tp_tracelink_set_decode(data, size, &decoded, parents, TP_TRACELINK_MAX_PARENTS);

    if (size > 0)
    {
        size_t i;
        size_t parent_count = data[0] % TP_TRACELINK_MAX_PARENTS;

        offset = 1;
        encoded.stream_id = (uint32_t)tp_fuzz_u64(data, size, &offset);
        encoded.epoch = tp_fuzz_u64(data, size, &offset);
        encoded.seq = tp_fuzz_u64(data, size, &offset);
        encoded.trace_id = tp_fuzz_u64(data, size, &offset);
        encoded.parent_count = parent_count;
        encoded.parents = parents;

        for (i = 0; i < parent_count; i++)
        {
            parents[i] = tp_fuzz_u64(data, size, &offset);
        }

        (void)tp_tracelink_set_encode(buffer, sizeof(buffer), &encoded, &out_len);
    }

    return 0;
}
