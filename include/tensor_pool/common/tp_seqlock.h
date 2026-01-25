#ifndef TENSOR_POOL_TP_SEQLOCK_H
#define TENSOR_POOL_TP_SEQLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

static inline uint64_t tp_seq_in_progress(uint64_t seq)
{
    return (seq << 1u) | 0u;
}

static inline uint64_t tp_seq_committed(uint64_t seq)
{
    return (seq << 1u) | 1u;
}

static inline bool tp_seq_is_committed(uint64_t seq_commit)
{
    return (seq_commit & 1u) != 0u;
}

static inline uint64_t tp_seq_value(uint64_t seq_commit)
{
    return seq_commit >> 1u;
}

static inline uint64_t tp_atomic_load_u64(const uint64_t *value)
{
    return atomic_load_explicit((_Atomic uint64_t *)value, memory_order_acquire);
}

static inline void tp_atomic_store_u64(uint64_t *value, uint64_t v)
{
    atomic_store_explicit((_Atomic uint64_t *)value, v, memory_order_release);
}

#endif
