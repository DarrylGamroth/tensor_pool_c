#ifndef TENSOR_POOL_TP_CONTEXT_H
#define TENSOR_POOL_TP_CONTEXT_H

#include <stdint.h>

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
    char qos_channel[4096];
    char metadata_channel[4096];
    int32_t descriptor_stream_id;
    int32_t control_stream_id;
    int32_t qos_stream_id;
    int32_t metadata_stream_id;
    tp_allowed_paths_t allowed_paths;
    tp_log_t log;
}
tp_context_t;

int tp_context_init(tp_context_t *context);
void tp_context_set_aeron_dir(tp_context_t *context, const char *dir);
void tp_context_set_descriptor_channel(tp_context_t *context, const char *channel, int32_t stream_id);
void tp_context_set_control_channel(tp_context_t *context, const char *channel, int32_t stream_id);
void tp_context_set_qos_channel(tp_context_t *context, const char *channel, int32_t stream_id);
void tp_context_set_metadata_channel(tp_context_t *context, const char *channel, int32_t stream_id);
void tp_context_set_allowed_paths(tp_context_t *context, const char **paths, size_t length);

#ifdef __cplusplus
}
#endif

#endif
