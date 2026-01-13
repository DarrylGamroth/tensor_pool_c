#ifndef TENSOR_POOL_TP_TRACE_H
#define TENSOR_POOL_TP_TRACE_H

#include <stdint.h>
#include <stdatomic.h>

#include "tensor_pool/tp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TP_TRACE_UNUSED_BITS 1
#define TP_TRACE_EPOCH_BITS 41
#define TP_TRACE_MAX_NODE_AND_SEQUENCE_BITS 22
#define TP_TRACE_NODE_ID_BITS_DEFAULT 10
#define TP_TRACE_SEQUENCE_BITS_DEFAULT 12

typedef uint64_t (*tp_trace_clock_ms_func_t)(void *clientd);

typedef struct tp_trace_id_generator_stct
{
    atomic_uint_fast64_t timestamp_sequence;
    uint8_t node_id_bits;
    uint8_t sequence_bits;
    uint8_t node_id_and_sequence_bits;
    uint64_t max_node_id;
    uint64_t max_sequence;
    uint64_t node_bits;
    uint64_t timestamp_offset_ms;
    tp_trace_clock_ms_func_t clock;
    void *clock_clientd;
}
tp_trace_id_generator_t;

int tp_trace_id_generator_init(
    tp_trace_id_generator_t *generator,
    uint8_t node_id_bits,
    uint8_t sequence_bits,
    uint64_t node_id,
    uint64_t timestamp_offset_ms,
    tp_trace_clock_ms_func_t clock,
    void *clock_clientd);
int tp_trace_id_generator_init_default(tp_trace_id_generator_t *generator, uint64_t node_id);
uint64_t tp_trace_id_generator_next(tp_trace_id_generator_t *generator);

uint64_t tp_trace_id_extract_timestamp(const tp_trace_id_generator_t *generator, uint64_t trace_id);
uint64_t tp_trace_id_extract_node_id(const tp_trace_id_generator_t *generator, uint64_t trace_id);
uint64_t tp_trace_id_extract_sequence(const tp_trace_id_generator_t *generator, uint64_t trace_id);

#ifdef __cplusplus
}
#endif

#endif
