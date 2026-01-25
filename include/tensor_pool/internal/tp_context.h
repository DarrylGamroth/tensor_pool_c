#ifndef TENSOR_POOL_INTERNAL_TP_CONTEXT_H
#define TENSOR_POOL_INTERNAL_TP_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>

#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_context_stct
{
    char aeron_dir[4096];
    char descriptor_channel[4096];
    char control_channel[4096];
    char announce_channel[4096];
    char qos_channel[4096];
    char metadata_channel[4096];
    int32_t descriptor_stream_id;
    int32_t control_stream_id;
    int32_t announce_stream_id;
    int32_t qos_stream_id;
    int32_t metadata_stream_id;
    tp_allowed_paths_t allowed_paths;
    uint64_t announce_period_ns;
    tp_log_t log;
}
tp_context_t;

#ifdef __cplusplus
}
#endif

#endif
