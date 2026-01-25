#ifndef TENSOR_POOL_TP_JOIN_BARRIER_H
#define TENSOR_POOL_TP_JOIN_BARRIER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/tp_merge_map.h"
#include "tensor_pool/tp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tp_join_barrier_type_enum
{
    TP_JOIN_BARRIER_SEQUENCE = 1,
    TP_JOIN_BARRIER_TIMESTAMP = 2,
    TP_JOIN_BARRIER_LATEST_VALUE = 3
}
tp_join_barrier_type_t;

typedef enum tp_latest_ordering_enum
{
    TP_LATEST_ORDERING_SEQUENCE = 1,
    TP_LATEST_ORDERING_TIMESTAMP = 2
}
tp_latest_ordering_t;

typedef struct tp_latest_selection_stct
{
    uint32_t stream_id;
    uint64_t seq;
    uint64_t timestamp_ns;
    tp_timestamp_source_t timestamp_source;
}
tp_latest_selection_t;

typedef struct tp_join_barrier_stct
{
    tp_join_barrier_type_t type;
    uint32_t out_stream_id;
    uint64_t epoch;
    uint64_t stale_timeout_ns;
    uint64_t lateness_ns;
    uint64_t last_out_time_ns;
    uint8_t clock_domain;
    bool allow_stale;
    bool require_processed;
    bool has_last_out_time;
    tp_latest_ordering_t latest_ordering;
    size_t rule_capacity;
    size_t rule_count;
    tp_sequence_merge_rule_t *sequence_rules;
    tp_timestamp_merge_rule_t *timestamp_rules;
    void *state;
}
tp_join_barrier_t;

int tp_join_barrier_init(tp_join_barrier_t *barrier, tp_join_barrier_type_t type, size_t rule_capacity);
void tp_join_barrier_close(tp_join_barrier_t *barrier);

void tp_join_barrier_set_allow_stale(tp_join_barrier_t *barrier, bool allow_stale);
void tp_join_barrier_set_require_processed(tp_join_barrier_t *barrier, bool require_processed);
void tp_join_barrier_set_latest_ordering(tp_join_barrier_t *barrier, tp_latest_ordering_t ordering);

int tp_join_barrier_apply_sequence_map(tp_join_barrier_t *barrier, const tp_sequence_merge_map_t *map);
int tp_join_barrier_apply_timestamp_map(tp_join_barrier_t *barrier, const tp_timestamp_merge_map_t *map);
int tp_join_barrier_apply_latest_value_sequence_map(tp_join_barrier_t *barrier, const tp_sequence_merge_map_t *map);
int tp_join_barrier_apply_latest_value_timestamp_map(tp_join_barrier_t *barrier, const tp_timestamp_merge_map_t *map);

int tp_join_barrier_update_observed_seq(
    tp_join_barrier_t *barrier,
    uint32_t stream_id,
    uint64_t seq,
    uint64_t now_ns);
int tp_join_barrier_update_processed_seq(
    tp_join_barrier_t *barrier,
    uint32_t stream_id,
    uint64_t seq,
    uint64_t now_ns);

int tp_join_barrier_update_observed_time(
    tp_join_barrier_t *barrier,
    uint32_t stream_id,
    uint64_t timestamp_ns,
    tp_timestamp_source_t source,
    uint8_t clock_domain,
    uint64_t now_ns);
int tp_join_barrier_update_processed_time(
    tp_join_barrier_t *barrier,
    uint32_t stream_id,
    uint64_t timestamp_ns,
    tp_timestamp_source_t source,
    uint8_t clock_domain,
    uint64_t now_ns);

int tp_join_barrier_is_ready_sequence(tp_join_barrier_t *barrier, uint64_t out_seq, uint64_t now_ns);
int tp_join_barrier_is_ready_timestamp(tp_join_barrier_t *barrier, uint64_t out_time_ns, uint8_t clock_domain, uint64_t now_ns);
int tp_join_barrier_is_ready_latest(tp_join_barrier_t *barrier, uint64_t out_seq, uint64_t out_time_ns, uint8_t clock_domain, uint64_t now_ns);
int tp_join_barrier_invalidate_latest(tp_join_barrier_t *barrier, uint32_t stream_id);
int tp_join_barrier_collect_latest(
    tp_join_barrier_t *barrier,
    tp_latest_selection_t *selections,
    size_t capacity,
    size_t *out_count);

int tp_join_barrier_collect_stale_inputs(
    tp_join_barrier_t *barrier,
    uint64_t now_ns,
    uint32_t *stream_ids,
    size_t capacity,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif
