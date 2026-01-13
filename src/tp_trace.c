#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_trace.h"

#include <errno.h>
#include <string.h>

#include "concurrent/aeron_thread.h"

#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_error.h"

static uint64_t tp_trace_default_clock_ms(void *clientd)
{
    (void)clientd;
    return (uint64_t)(tp_clock_now_realtime_ns() / 1000000LL);
}

int tp_trace_id_generator_init(
    tp_trace_id_generator_t *generator,
    uint8_t node_id_bits,
    uint8_t sequence_bits,
    uint64_t node_id,
    uint64_t timestamp_offset_ms,
    tp_trace_clock_ms_func_t clock,
    void *clock_clientd)
{
    uint64_t now_ms;
    uint64_t max_node_id;
    uint8_t node_id_and_sequence_bits;

    if (NULL == generator)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_trace_id_generator_init: null input");
        return -1;
    }

    if ((int)node_id_bits < 0 || (int)sequence_bits < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_trace_id_generator_init: invalid bit sizes");
        return -1;
    }

    node_id_and_sequence_bits = (uint8_t)(node_id_bits + sequence_bits);
    if (node_id_and_sequence_bits > TP_TRACE_MAX_NODE_AND_SEQUENCE_BITS)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_trace_id_generator_init: too many bits used");
        return -1;
    }

    max_node_id = (uint64_t)((1ULL << node_id_bits) - 1);
    if (node_id > max_node_id)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_trace_id_generator_init: node_id out of range");
        return -1;
    }

    if (timestamp_offset_ms > tp_trace_default_clock_ms(NULL))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_trace_id_generator_init: timestamp offset in future");
        return -1;
    }

    memset(generator, 0, sizeof(*generator));
    generator->node_id_bits = node_id_bits;
    generator->sequence_bits = sequence_bits;
    generator->node_id_and_sequence_bits = node_id_and_sequence_bits;
    generator->max_node_id = max_node_id;
    generator->max_sequence = (uint64_t)((1ULL << sequence_bits) - 1);
    generator->node_bits = node_id << sequence_bits;
    generator->timestamp_offset_ms = timestamp_offset_ms;
    generator->clock = (clock != NULL) ? clock : tp_trace_default_clock_ms;
    generator->clock_clientd = clock_clientd;

    now_ms = generator->clock(generator->clock_clientd);
    if (timestamp_offset_ms > now_ms)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_trace_id_generator_init: offset beyond clock");
        return -1;
    }

    atomic_init(&generator->timestamp_sequence, 0);
    return 0;
}

int tp_trace_id_generator_init_default(tp_trace_id_generator_t *generator, uint64_t node_id)
{
    return tp_trace_id_generator_init(
        generator,
        TP_TRACE_NODE_ID_BITS_DEFAULT,
        TP_TRACE_SEQUENCE_BITS_DEFAULT,
        node_id,
        0,
        NULL,
        NULL);
}

uint64_t tp_trace_id_generator_next(tp_trace_id_generator_t *generator)
{
    uint64_t old_timestamp_sequence;

    if (NULL == generator || NULL == generator->clock)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_trace_id_generator_next: generator not initialized");
        return 0;
    }

    for (;;)
    {
        uint64_t timestamp_ms;
        uint64_t old_timestamp_ms;

        old_timestamp_sequence = atomic_load_explicit(&generator->timestamp_sequence, memory_order_relaxed);
        timestamp_ms = generator->clock(generator->clock_clientd) - generator->timestamp_offset_ms;
        old_timestamp_ms = old_timestamp_sequence >> generator->node_id_and_sequence_bits;

        if (timestamp_ms > old_timestamp_ms)
        {
            uint64_t new_timestamp_sequence = timestamp_ms << generator->node_id_and_sequence_bits;
            if (atomic_compare_exchange_weak_explicit(
                &generator->timestamp_sequence,
                &old_timestamp_sequence,
                new_timestamp_sequence,
                memory_order_acq_rel,
                memory_order_relaxed))
            {
                return new_timestamp_sequence | generator->node_bits;
            }
        }
        else
        {
            uint64_t old_sequence = old_timestamp_sequence & generator->max_sequence;
            if (old_sequence < generator->max_sequence)
            {
                uint64_t new_timestamp_sequence = old_timestamp_sequence + 1;
                if (atomic_compare_exchange_weak_explicit(
                    &generator->timestamp_sequence,
                    &old_timestamp_sequence,
                    new_timestamp_sequence,
                    memory_order_acq_rel,
                    memory_order_relaxed))
                {
                    return new_timestamp_sequence | generator->node_bits;
                }
            }
        }

        proc_yield();
    }
}

uint64_t tp_trace_id_extract_timestamp(const tp_trace_id_generator_t *generator, uint64_t trace_id)
{
    if (NULL == generator)
    {
        return 0;
    }

    return trace_id >> generator->node_id_and_sequence_bits;
}

uint64_t tp_trace_id_extract_node_id(const tp_trace_id_generator_t *generator, uint64_t trace_id)
{
    if (NULL == generator)
    {
        return 0;
    }

    return (trace_id >> generator->sequence_bits) & generator->max_node_id;
}

uint64_t tp_trace_id_extract_sequence(const tp_trace_id_generator_t *generator, uint64_t trace_id)
{
    if (NULL == generator)
    {
        return 0;
    }

    return trace_id & generator->max_sequence;
}
