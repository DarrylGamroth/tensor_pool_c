#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/tp_join_barrier.h"
#include "tensor_pool/tp_merge_map.h"

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
    tp_sequence_merge_rule_t seq_rules[16];
    tp_timestamp_merge_rule_t ts_rules[16];
    tp_sequence_merge_map_t seq_map;
    tp_timestamp_merge_map_t ts_map;
    tp_merge_map_registry_t registry;
    tp_join_barrier_t barrier;
    uint32_t out_stream_id = 0;
    uint64_t epoch = 0;
    size_t offset = 0;
    int decoded;

    if (data == NULL)
    {
        return 0;
    }

    decoded = tp_sequence_merge_map_decode(data, size, &seq_map, seq_rules, 16);
    if (decoded == 0)
    {
        if (tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_SEQUENCE, 16) == 0)
        {
            (void)tp_join_barrier_apply_sequence_map(&barrier, &seq_map);
            tp_join_barrier_close(&barrier);
        }

        if (tp_merge_map_registry_init(&registry, 4, NULL) == 0)
        {
            uint64_t now_ns = tp_fuzz_u64(data, size, &offset);
            (void)tp_merge_map_registry_upsert_sequence(&registry, &seq_map, now_ns);
            tp_merge_map_registry_close(&registry);
        }
    }

    decoded = tp_timestamp_merge_map_decode(data, size, &ts_map, ts_rules, 16);
    if (decoded == 0)
    {
        if (tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_TIMESTAMP, 16) == 0)
        {
            (void)tp_join_barrier_apply_timestamp_map(&barrier, &ts_map);
            tp_join_barrier_close(&barrier);
        }

        if (tp_merge_map_registry_init(&registry, 4, NULL) == 0)
        {
            uint64_t now_ns = tp_fuzz_u64(data, size, &offset);
            (void)tp_merge_map_registry_upsert_timestamp(&registry, &ts_map, now_ns);
            tp_merge_map_registry_close(&registry);
        }
    }

    (void)tp_sequence_merge_map_request_decode(data, size, &out_stream_id, &epoch);
    (void)tp_timestamp_merge_map_request_decode(data, size, &out_stream_id, &epoch);

    return 0;
}
