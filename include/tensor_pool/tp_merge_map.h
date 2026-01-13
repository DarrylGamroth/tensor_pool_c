#ifndef TENSOR_POOL_TP_MERGE_MAP_H
#define TENSOR_POOL_TP_MERGE_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tp_merge_map_kind_enum
{
    TP_MERGE_MAP_SEQUENCE = 1,
    TP_MERGE_MAP_TIMESTAMP = 2
}
tp_merge_map_kind_t;

typedef struct tp_sequence_merge_rule_stct
{
    uint32_t input_stream_id;
    tp_merge_rule_type_t rule_type;
    int32_t offset;
    uint32_t window_size;
}
tp_sequence_merge_rule_t;

typedef struct tp_timestamp_merge_rule_stct
{
    uint32_t input_stream_id;
    tp_merge_time_rule_type_t rule_type;
    tp_timestamp_source_t timestamp_source;
    int64_t offset_ns;
    uint64_t window_ns;
}
tp_timestamp_merge_rule_t;

typedef struct tp_sequence_merge_map_stct
{
    uint32_t out_stream_id;
    uint64_t epoch;
    uint64_t stale_timeout_ns;
    const tp_sequence_merge_rule_t *rules;
    size_t rule_count;
}
tp_sequence_merge_map_t;

typedef struct tp_timestamp_merge_map_stct
{
    uint32_t out_stream_id;
    uint64_t epoch;
    uint64_t stale_timeout_ns;
    uint8_t clock_domain;
    uint64_t lateness_ns;
    const tp_timestamp_merge_rule_t *rules;
    size_t rule_count;
}
tp_timestamp_merge_map_t;

typedef struct tp_merge_map_entry_stct
{
    bool in_use;
    tp_merge_map_kind_t kind;
    uint32_t out_stream_id;
    uint64_t epoch;
    uint64_t last_announce_ns;
    tp_sequence_merge_rule_t *sequence_rules;
    tp_timestamp_merge_rule_t *timestamp_rules;
    size_t rule_count;
    tp_sequence_merge_map_t sequence_map;
    tp_timestamp_merge_map_t timestamp_map;
}
tp_merge_map_entry_t;

typedef struct tp_merge_map_registry_stct
{
    tp_merge_map_entry_t *entries;
    size_t capacity;
    tp_log_t *log;
}
tp_merge_map_registry_t;

int tp_sequence_merge_map_encode(
    uint8_t *buffer,
    size_t length,
    const tp_sequence_merge_map_t *map,
    size_t *out_len);
int tp_sequence_merge_map_decode(
    const uint8_t *buffer,
    size_t length,
    tp_sequence_merge_map_t *out,
    tp_sequence_merge_rule_t *rules,
    size_t max_rules);
int tp_sequence_merge_map_request_encode(
    uint8_t *buffer,
    size_t length,
    uint32_t out_stream_id,
    uint64_t epoch,
    size_t *out_len);
int tp_sequence_merge_map_request_decode(
    const uint8_t *buffer,
    size_t length,
    uint32_t *out_stream_id,
    uint64_t *epoch);

int tp_timestamp_merge_map_encode(
    uint8_t *buffer,
    size_t length,
    const tp_timestamp_merge_map_t *map,
    size_t *out_len);
int tp_timestamp_merge_map_decode(
    const uint8_t *buffer,
    size_t length,
    tp_timestamp_merge_map_t *out,
    tp_timestamp_merge_rule_t *rules,
    size_t max_rules);
int tp_timestamp_merge_map_request_encode(
    uint8_t *buffer,
    size_t length,
    uint32_t out_stream_id,
    uint64_t epoch,
    size_t *out_len);
int tp_timestamp_merge_map_request_decode(
    const uint8_t *buffer,
    size_t length,
    uint32_t *out_stream_id,
    uint64_t *epoch);

int tp_merge_map_registry_init(tp_merge_map_registry_t *registry, size_t capacity, tp_log_t *log);
void tp_merge_map_registry_set_log(tp_merge_map_registry_t *registry, tp_log_t *log);
void tp_merge_map_registry_close(tp_merge_map_registry_t *registry);
int tp_merge_map_registry_upsert_sequence(
    tp_merge_map_registry_t *registry,
    const tp_sequence_merge_map_t *map,
    uint64_t now_ns);
int tp_merge_map_registry_upsert_timestamp(
    tp_merge_map_registry_t *registry,
    const tp_timestamp_merge_map_t *map,
    uint64_t now_ns);
const tp_sequence_merge_map_t *tp_merge_map_registry_find_sequence(
    const tp_merge_map_registry_t *registry,
    uint32_t out_stream_id,
    uint64_t epoch);
const tp_timestamp_merge_map_t *tp_merge_map_registry_find_timestamp(
    const tp_merge_map_registry_t *registry,
    uint32_t out_stream_id,
    uint64_t epoch);

#ifdef __cplusplus
}
#endif

#endif
