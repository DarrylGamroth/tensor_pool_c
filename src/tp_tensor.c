#include "tensor_pool/tp_tensor.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/tensorHeader.h"
#include "wire/tensor_pool/dtype.h"
#include "wire/tensor_pool/majorOrder.h"
#include "wire/tensor_pool/progressUnit.h"

size_t tp_dtype_size_bytes(int16_t dtype)
{
    switch (dtype)
    {
        case tensor_pool_dtype_UINT8:
        case tensor_pool_dtype_INT8:
        case tensor_pool_dtype_BOOLEAN:
        case tensor_pool_dtype_BYTES:
        case tensor_pool_dtype_BIT:
            return 1;
        case tensor_pool_dtype_UINT16:
        case tensor_pool_dtype_INT16:
            return 2;
        case tensor_pool_dtype_UINT32:
        case tensor_pool_dtype_INT32:
        case tensor_pool_dtype_FLOAT32:
            return 4;
        case tensor_pool_dtype_UINT64:
        case tensor_pool_dtype_INT64:
        case tensor_pool_dtype_FLOAT64:
            return 8;
        default:
            return 0;
    }
}

int tp_tensor_header_decode(tp_tensor_header_t *out, const uint8_t *buffer, size_t length, tp_log_t *log)
{
    struct tensor_pool_tensorHeader header;
    struct tensor_pool_messageHeader msg_header;
    size_t required = tensor_pool_tensorHeader_sbe_block_length() + tensor_pool_messageHeader_encoded_length();
    size_t i;
    uint16_t schema_id;
    uint16_t version;
    uint16_t block_length;
    uint16_t template_id;

    if (NULL == out || NULL == buffer)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: null input");
        return -1;
    }

    if (length < required)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: headerBytes too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);
    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);

    if (schema_id != tensor_pool_tensorHeader_sbe_schema_id())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: schema id mismatch");
        return -1;
    }

    if (template_id != tensor_pool_tensorHeader_sbe_template_id())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: template id mismatch");
        return -1;
    }

    tensor_pool_tensorHeader_wrap_for_decode(
        &header,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    {
        enum tensor_pool_dtype dtype;
        if (!tensor_pool_tensorHeader_dtype(&header, &dtype))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: invalid dtype");
            return -1;
        }
        out->dtype = (int16_t)dtype;
    }

    {
        enum tensor_pool_majorOrder order;
        if (!tensor_pool_tensorHeader_majorOrder(&header, &order))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: invalid major order");
            return -1;
        }
        out->major_order = (int16_t)order;
    }

    out->ndims = tensor_pool_tensorHeader_ndims(&header);

    {
        enum tensor_pool_progressUnit progress;
        if (!tensor_pool_tensorHeader_progressUnit(&header, &progress))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: invalid progress unit");
            return -1;
        }
        out->progress_unit = (uint8_t)progress;
    }
    out->progress_stride_bytes = tensor_pool_tensorHeader_progressStrideBytes(&header);

    for (i = 0; i < TP_MAX_DIMS; i++)
    {
        int32_t value;
        if (!tensor_pool_tensorHeader_dims(&header, i, &value))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: invalid dims array");
            return -1;
        }
        out->dims[i] = value;
    }

    for (i = 0; i < TP_MAX_DIMS; i++)
    {
        int32_t value;
        if (!tensor_pool_tensorHeader_strides(&header, i, &value))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_decode: invalid strides array");
            return -1;
        }
        out->strides[i] = value;
    }

    if (NULL != log)
    {
        tp_log_emit(log, TP_LOG_DEBUG, "Decoded tensor header ndims=%u", out->ndims);
    }

    return 0;
}

static int tp_tensor_fill_strides(tp_tensor_header_t *header, size_t elem_size)
{
    if (header->major_order == tensor_pool_majorOrder_ROW)
    {
        int32_t i;

        if (header->ndims == 0)
        {
            return -1;
        }

        if (header->strides[header->ndims - 1] == 0)
        {
            header->strides[header->ndims - 1] = (int32_t)elem_size;
        }

        for (i = (int32_t)header->ndims - 2; i >= 0; i--)
        {
            int32_t expected = header->strides[i + 1] * header->dims[i + 1];
            if (header->strides[i] == 0)
            {
                header->strides[i] = expected;
            }
            else if (header->strides[i] < expected)
            {
                return -1;
            }
        }
    }
    else if (header->major_order == tensor_pool_majorOrder_COLUMN)
    {
        uint8_t i;

        if (header->ndims == 0)
        {
            return -1;
        }

        if (header->strides[0] == 0)
        {
            header->strides[0] = (int32_t)elem_size;
        }

        for (i = 1; i < header->ndims; i++)
        {
            int32_t expected = header->strides[i - 1] * header->dims[i - 1];
            if (header->strides[i] == 0)
            {
                header->strides[i] = expected;
            }
            else if (header->strides[i] < expected)
            {
                return -1;
            }
        }
    }
    else
    {
        return -1;
    }

    return 0;
}

static int tp_tensor_validate_progress(const tp_tensor_header_t *header)
{
    if (header->progress_unit == tensor_pool_progressUnit_NONE)
    {
        return 0;
    }

    if (header->progress_stride_bytes == 0)
    {
        return -1;
    }

    if (header->progress_unit == tensor_pool_progressUnit_ROWS)
    {
        if (header->ndims == 0)
        {
            return -1;
        }

        if ((uint32_t)header->strides[0] != header->progress_stride_bytes)
        {
            return -1;
        }
    }
    else if (header->progress_unit == tensor_pool_progressUnit_COLUMNS)
    {
        if (header->ndims < 2)
        {
            return -1;
        }

        if ((uint32_t)header->strides[1] != header->progress_stride_bytes)
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    return 0;
}

int tp_tensor_header_validate(tp_tensor_header_t *header, tp_log_t *log)
{
    size_t elem_size;
    uint8_t i;

    if (NULL == header)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_validate: null header");
        return -1;
    }

    if (header->ndims == 0 || header->ndims > TP_MAX_DIMS)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_validate: invalid ndims");
        return -1;
    }

    elem_size = tp_dtype_size_bytes(header->dtype);
    if (elem_size == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_validate: invalid dtype");
        return -1;
    }

    for (i = 0; i < header->ndims; i++)
    {
        if (header->dims[i] <= 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_validate: invalid dims");
            return -1;
        }
        if (header->strides[i] < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_validate: negative stride");
            return -1;
        }
    }

    if (tp_tensor_fill_strides(header, elem_size) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_validate: strides inconsistent with major order");
        return -1;
    }

    if (tp_tensor_validate_progress(header) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tensor_header_validate: invalid progress stride");
        return -1;
    }

    if (NULL != log)
    {
        tp_log_emit(log, TP_LOG_DEBUG, "Validated tensor header ndims=%u", header->ndims);
    }

    return 0;
}
