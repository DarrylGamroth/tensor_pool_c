#ifndef TENSOR_POOL_TP_TRACELINK_H
#define TENSOR_POOL_TP_TRACELINK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tp_producer_stct;

#define TP_TRACELINK_MAX_PARENTS 256

typedef struct tp_tracelink_set_stct
{
    uint32_t stream_id;
    uint64_t epoch;
    uint64_t seq;
    uint64_t trace_id;
    const uint64_t *parents;
    size_t parent_count;
}
tp_tracelink_set_t;

typedef int (*tp_tracelink_validate_t)(const tp_tracelink_set_t *set, void *clientd);

int tp_tracelink_set_encode(
    uint8_t *buffer,
    size_t length,
    const tp_tracelink_set_t *set,
    size_t *out_len);
int tp_tracelink_set_decode(
    const uint8_t *buffer,
    size_t length,
    tp_tracelink_set_t *out,
    uint64_t *parents,
    size_t max_parents);

int tp_producer_send_tracelink_set(struct tp_producer_stct *producer, const tp_tracelink_set_t *set);

#ifdef __cplusplus
}
#endif

#endif
