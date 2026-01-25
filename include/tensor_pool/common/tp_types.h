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
#include "wire/tensor_pool/clockDomain.h"
#include "wire/tensor_pool/responseCode.h"

#define TP_SUPERBLOCK_SIZE_BYTES 64u
#define TP_HEADER_SLOT_BYTES 256u
#define TP_MAX_DIMS 8u
#define TP_LAYOUT_VERSION 1u
#define TP_MAGIC_U64 0x544F504C53484D31ULL /* "TPOLSHM1" little-endian */

#define TP_NULL_U32 UINT32_MAX
#define TP_NULL_U64 UINT64_MAX
#define TP_URI_MAX_LENGTH 4096u

#define TP_PROGRESS_INTERVAL_DEFAULT_US 250u
#define TP_PROGRESS_BYTES_DELTA_DEFAULT 65536u
#define TP_ANNOUNCE_PERIOD_DEFAULT_NS 1000000000ULL
#define TP_ANNOUNCE_FRESHNESS_MULTIPLIER 3u

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

typedef enum tp_response_code_enum
{
    TP_RESPONSE_OK = tensor_pool_responseCode_OK,
    TP_RESPONSE_UNSUPPORTED = tensor_pool_responseCode_UNSUPPORTED,
    TP_RESPONSE_INVALID_PARAMS = tensor_pool_responseCode_INVALID_PARAMS,
    TP_RESPONSE_REJECTED = tensor_pool_responseCode_REJECTED,
    TP_RESPONSE_INTERNAL_ERROR = tensor_pool_responseCode_INTERNAL_ERROR,
    TP_RESPONSE_NULL = tensor_pool_responseCode_NULL_VALUE
}
tp_response_code_t;

#define TP_ROLE_PRODUCER tensor_pool_role_PRODUCER
#define TP_ROLE_CONSUMER tensor_pool_role_CONSUMER
#define TP_ROLE_NULL tensor_pool_role_NULL_VALUE

#define TP_PUBLISH_MODE_REQUIRE_EXISTING tensor_pool_publishMode_REQUIRE_EXISTING
#define TP_PUBLISH_MODE_EXISTING_OR_CREATE tensor_pool_publishMode_EXISTING_OR_CREATE
#define TP_PUBLISH_MODE_NULL tensor_pool_publishMode_NULL_VALUE

#define TP_HUGEPAGES_UNSPECIFIED tensor_pool_hugepagesPolicy_UNSPECIFIED
#define TP_HUGEPAGES_STANDARD tensor_pool_hugepagesPolicy_STANDARD
#define TP_HUGEPAGES_HUGEPAGES tensor_pool_hugepagesPolicy_HUGEPAGES
#define TP_HUGEPAGES_NULL tensor_pool_hugepagesPolicy_NULL_VALUE

#define TP_DTYPE_UNKNOWN tensor_pool_dtype_UNKNOWN
#define TP_DTYPE_UINT8 tensor_pool_dtype_UINT8
#define TP_DTYPE_INT8 tensor_pool_dtype_INT8
#define TP_DTYPE_UINT16 tensor_pool_dtype_UINT16
#define TP_DTYPE_INT16 tensor_pool_dtype_INT16
#define TP_DTYPE_UINT32 tensor_pool_dtype_UINT32
#define TP_DTYPE_INT32 tensor_pool_dtype_INT32
#define TP_DTYPE_UINT64 tensor_pool_dtype_UINT64
#define TP_DTYPE_INT64 tensor_pool_dtype_INT64
#define TP_DTYPE_FLOAT32 tensor_pool_dtype_FLOAT32
#define TP_DTYPE_FLOAT64 tensor_pool_dtype_FLOAT64
#define TP_DTYPE_BOOLEAN tensor_pool_dtype_BOOLEAN
#define TP_DTYPE_BYTES tensor_pool_dtype_BYTES
#define TP_DTYPE_BIT tensor_pool_dtype_BIT
#define TP_DTYPE_NULL tensor_pool_dtype_NULL_VALUE

#define TP_MAJOR_ORDER_UNKNOWN tensor_pool_majorOrder_UNKNOWN
#define TP_MAJOR_ORDER_ROW tensor_pool_majorOrder_ROW
#define TP_MAJOR_ORDER_COLUMN tensor_pool_majorOrder_COLUMN
#define TP_MAJOR_ORDER_NULL tensor_pool_majorOrder_NULL_VALUE

#define TP_PROGRESS_NONE tensor_pool_progressUnit_NONE
#define TP_PROGRESS_ROWS tensor_pool_progressUnit_ROWS
#define TP_PROGRESS_COLUMNS tensor_pool_progressUnit_COLUMNS
#define TP_PROGRESS_NULL tensor_pool_progressUnit_NULL_VALUE

#define TP_CLOCK_DOMAIN_MONOTONIC tensor_pool_clockDomain_MONOTONIC
#define TP_CLOCK_DOMAIN_REALTIME_SYNCED tensor_pool_clockDomain_REALTIME_SYNCED

typedef enum tp_merge_rule_type_enum
{
    TP_MERGE_RULE_OFFSET = 0,
    TP_MERGE_RULE_WINDOW = 1
}
tp_merge_rule_type_t;

typedef enum tp_merge_time_rule_type_enum
{
    TP_MERGE_TIME_OFFSET_NS = 0,
    TP_MERGE_TIME_WINDOW_NS = 1
}
tp_merge_time_rule_type_t;

typedef enum tp_timestamp_source_enum
{
    TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR = 1,
    TP_TIMESTAMP_SOURCE_SLOT_HEADER = 2
}
tp_timestamp_source_t;

enum
{
    TP_BACK_PRESSURED = -2,
    TP_NOT_CONNECTED = -3,
    TP_ADMIN_ACTION = -4,
    TP_CLOSED = -5
};

#endif
