#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_tensor.h"
#include "tensor_pool/tp_types.h"

#include <assert.h>
#include <string.h>

void tp_test_tensor_header_validate_extra(void)
{
    tp_tensor_header_t header;
    tp_log_t log;

    assert(tp_tensor_header_validate(NULL, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.ndims = 0;
    assert(tp_tensor_header_validate(&header, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.ndims = 1;
    header.dtype = 999;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = 1;
    assert(tp_tensor_header_validate(&header, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.ndims = 1;
    header.dtype = TP_DTYPE_UINT8;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = -1;
    assert(tp_tensor_header_validate(&header, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.ndims = 1;
    header.dtype = TP_DTYPE_UINT8;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = 1;
    header.strides[0] = -1;
    assert(tp_tensor_header_validate(&header, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.ndims = 2;
    header.dtype = TP_DTYPE_UINT8;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = 2;
    header.dims[1] = 2;
    header.strides[1] = 1;
    header.strides[0] = 1;
    assert(tp_tensor_header_validate(&header, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.ndims = 1;
    header.dtype = TP_DTYPE_UINT8;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.progress_unit = TP_PROGRESS_ROWS;
    header.progress_stride_bytes = 8;
    header.dims[0] = 1;
    header.strides[0] = 4;
    assert(tp_tensor_header_validate(&header, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.ndims = 1;
    header.dtype = TP_DTYPE_UINT8;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.progress_unit = 99;
    header.progress_stride_bytes = 0;
    header.dims[0] = 1;
    header.strides[0] = 1;
    assert(tp_tensor_header_validate(&header, NULL) < 0);

    tp_log_init(&log);
    memset(&header, 0, sizeof(header));
    header.ndims = 2;
    header.dtype = TP_DTYPE_FLOAT32;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = 1;
    header.dims[1] = 2;
    header.strides[0] = 0;
    header.strides[1] = 0;
    assert(tp_tensor_header_validate(&header, &log) == 0);
}
