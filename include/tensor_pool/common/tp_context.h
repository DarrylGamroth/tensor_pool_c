#ifndef TENSOR_POOL_TP_CONTEXT_H
#define TENSOR_POOL_TP_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>

#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_context_stct tp_context_t;

int tp_context_init(tp_context_t **context);
int tp_context_close(tp_context_t *context);
void tp_context_set_aeron_dir(tp_context_t *context, const char *dir);
const char *tp_context_get_aeron_dir(const tp_context_t *context);
void tp_context_set_descriptor_channel(tp_context_t *context, const char *channel, int32_t stream_id);
void tp_context_set_control_channel(tp_context_t *context, const char *channel, int32_t stream_id);
void tp_context_set_announce_channel(tp_context_t *context, const char *channel, int32_t stream_id);
void tp_context_set_qos_channel(tp_context_t *context, const char *channel, int32_t stream_id);
void tp_context_set_metadata_channel(tp_context_t *context, const char *channel, int32_t stream_id);
const char *tp_context_get_descriptor_channel(const tp_context_t *context);
const char *tp_context_get_control_channel(const tp_context_t *context);
const char *tp_context_get_announce_channel(const tp_context_t *context);
const char *tp_context_get_qos_channel(const tp_context_t *context);
const char *tp_context_get_metadata_channel(const tp_context_t *context);
int32_t tp_context_get_descriptor_stream_id(const tp_context_t *context);
int32_t tp_context_get_control_stream_id(const tp_context_t *context);
int32_t tp_context_get_announce_stream_id(const tp_context_t *context);
int32_t tp_context_get_qos_stream_id(const tp_context_t *context);
int32_t tp_context_get_metadata_stream_id(const tp_context_t *context);
void tp_context_set_allowed_paths(tp_context_t *context, const char **paths, size_t length);
void tp_context_set_announce_period_ns(tp_context_t *context, uint64_t period_ns);
uint64_t tp_context_get_announce_period_ns(const tp_context_t *context);
void tp_context_set_shm_permissions(
    tp_context_t *context,
    bool enforce,
    uint32_t expected_uid,
    uint32_t expected_gid,
    uint32_t forbidden_mode);
int tp_context_finalize_allowed_paths(tp_context_t *context);
void tp_context_clear_allowed_paths(tp_context_t *context);
tp_log_t *tp_context_log(tp_context_t *context);
const tp_allowed_paths_t *tp_context_allowed_paths(const tp_context_t *context);

#ifdef __cplusplus
}
#endif

#endif
