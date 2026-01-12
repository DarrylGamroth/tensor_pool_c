#ifndef TENSOR_POOL_TP_TENSOR_H
#define TENSOR_POOL_TP_TENSOR_H

#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/tp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_tensor_header_stct
{
    int16_t dtype;
    int16_t major_order;
    uint8_t ndims;
    uint8_t progress_unit;
    uint32_t progress_stride_bytes;
    int32_t dims[TP_MAX_DIMS];
    int32_t strides[TP_MAX_DIMS];
}
tp_tensor_header_t;

size_t tp_dtype_size_bytes(int16_t dtype);
int tp_tensor_header_decode(tp_tensor_header_t *out, const uint8_t *buffer, size_t length, tp_log_t *log);
int tp_tensor_header_validate(tp_tensor_header_t *header, tp_log_t *log);

#ifdef __cplusplus
}
#endif

#endif
