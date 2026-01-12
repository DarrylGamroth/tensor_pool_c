#ifndef TENSOR_POOL_TP_SLOT_H
#define TENSOR_POOL_TP_SLOT_H

#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/tp_tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_slot_view_stct
{
    uint64_t seq_commit;
    uint32_t values_len_bytes;
    uint32_t payload_slot;
    uint16_t pool_id;
    uint32_t payload_offset;
    uint64_t timestamp_ns;
    uint32_t meta_version;
    uint32_t header_bytes_length;
    const uint8_t *header_bytes;
}
tp_slot_view_t;

uint8_t *tp_slot_at(void *base, uint32_t index);
int tp_slot_decode(tp_slot_view_t *view, const uint8_t *slot, size_t slot_length, tp_log_t *log);

#ifdef __cplusplus
}
#endif

#endif
