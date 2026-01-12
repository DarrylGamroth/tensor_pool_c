#ifndef TENSOR_POOL_TP_TYPES_H
#define TENSOR_POOL_TP_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/role.h"
#include "wire/tensor_pool/dtype.h"
#include "wire/tensor_pool/majorOrder.h"
#include "wire/tensor_pool/progressUnit.h"

#define TP_SUPERBLOCK_SIZE_BYTES 64u
#define TP_HEADER_SLOT_BYTES 256u
#define TP_MAX_DIMS 8u
#define TP_MAGIC_U64 0x544F504C53484D31ULL /* "TPOLSHM1" little-endian */

#define TP_NULL_U32 UINT32_MAX
#define TP_NULL_U64 UINT64_MAX
#define TP_URI_MAX_LENGTH 4096u

#define TP_PROGRESS_INTERVAL_DEFAULT_US 250u
#define TP_PROGRESS_BYTES_DELTA_DEFAULT 65536u

typedef enum tp_log_level_enum
{
    TP_LOG_ERROR = 0,
    TP_LOG_WARN = 1,
    TP_LOG_INFO = 2,
    TP_LOG_DEBUG = 3,
    TP_LOG_TRACE = 4
}
tp_log_level_t;

typedef enum tp_result_enum
{
    TP_SUCCESS = 0,
    TP_FAIL = -1
}
tp_result_t;

typedef enum tp_mode_enum
{
    TP_MODE_STREAM = 1,
    TP_MODE_RATE_LIMITED = 2,
    TP_MODE_NULL = 255
}
tp_mode_t;

typedef enum tp_progress_state_enum
{
    TP_PROGRESS_UNKNOWN = 0,
    TP_PROGRESS_STARTED = 1,
    TP_PROGRESS_PROGRESS = 2,
    TP_PROGRESS_COMPLETE = 3,
    TP_PROGRESS_NULL = 255
}
tp_progress_state_t;

#define TP_ROLE_PRODUCER tensor_pool_role_PRODUCER
#define TP_ROLE_CONSUMER tensor_pool_role_CONSUMER
#define TP_PUBLISH_MODE_EXISTING_OR_CREATE tensor_pool_publishMode_EXISTING_OR_CREATE
#define TP_PUBLISH_MODE_EXISTING tensor_pool_publishMode_EXISTING
#define TP_PUBLISH_MODE_CREATE tensor_pool_publishMode_CREATE
#define TP_HUGEPAGES_UNSPECIFIED tensor_pool_hugepagesPolicy_UNSPECIFIED
#define TP_HUGEPAGES_REQUIRED tensor_pool_hugepagesPolicy_REQUIRED
#define TP_HUGEPAGES_FORBIDDEN tensor_pool_hugepagesPolicy_FORBIDDEN
#define TP_RESPONSE_OK tensor_pool_responseCode_OK
#define TP_RESPONSE_ERROR tensor_pool_responseCode_ERROR
#define TP_DTYPE_UINT8 tensor_pool_dtype_UINT8
#define TP_DTYPE_FLOAT32 tensor_pool_dtype_FLOAT32
#define TP_MAJOR_ORDER_ROW tensor_pool_majorOrder_ROW
#define TP_MAJOR_ORDER_COLUMN tensor_pool_majorOrder_COLUMN
#define TP_PROGRESS_NONE tensor_pool_progressUnit_NONE
#define TP_PROGRESS_ROWS tensor_pool_progressUnit_ROWS
#define TP_PROGRESS_COLUMNS tensor_pool_progressUnit_COLUMNS

enum
{
    TP_BACK_PRESSURED = -2,
    TP_NOT_CONNECTED = -3,
    TP_ADMIN_ACTION = -4,
    TP_CLOSED = -5
};

#endif
