#ifndef TENSOR_POOL_TP_TYPES_H
#define TENSOR_POOL_TP_TYPES_H

#include <stdint.h>
#include <stdbool.h>

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

#endif
