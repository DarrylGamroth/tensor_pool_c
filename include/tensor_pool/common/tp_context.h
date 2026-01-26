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

typedef void (*tp_error_handler_t)(void *clientd, int errcode, const char *message);
typedef void (*tp_delegating_invoker_t)(void *clientd);

int tp_context_init(tp_context_t **context);
int tp_context_close(tp_context_t *context);
void tp_context_set_aeron_dir(tp_context_t *context, const char *dir);
const char *tp_context_get_aeron_dir(const tp_context_t *context);
void tp_context_set_aeron(tp_context_t *context, void *aeron);
void *tp_context_get_aeron(const tp_context_t *context);
void tp_context_set_owns_aeron_client(tp_context_t *context, bool owns);
bool tp_context_get_owns_aeron_client(const tp_context_t *context);
void tp_context_set_client_name(tp_context_t *context, const char *name);
const char *tp_context_get_client_name(const tp_context_t *context);
void tp_context_set_message_timeout_ns(tp_context_t *context, int64_t timeout_ns);
int64_t tp_context_get_message_timeout_ns(const tp_context_t *context);
void tp_context_set_message_retry_attempts(tp_context_t *context, int32_t attempts);
int32_t tp_context_get_message_retry_attempts(const tp_context_t *context);
void tp_context_set_error_handler(tp_context_t *context, tp_error_handler_t handler, void *clientd);
tp_error_handler_t tp_context_get_error_handler(const tp_context_t *context);
void *tp_context_get_error_handler_clientd(const tp_context_t *context);
void tp_context_set_delegating_invoker(tp_context_t *context, tp_delegating_invoker_t invoker, void *clientd);
tp_delegating_invoker_t tp_context_get_delegating_invoker(const tp_context_t *context);
void *tp_context_get_delegating_invoker_clientd(const tp_context_t *context);
void tp_context_set_log_handler(tp_context_t *context, tp_log_func_t handler, void *clientd);
tp_log_func_t tp_context_get_log_handler(const tp_context_t *context);
void *tp_context_get_log_handler_clientd(const tp_context_t *context);
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
int tp_context_set_default_channels(tp_context_t *context, const char *channel, int32_t announce_stream_id);
void tp_context_set_allowed_paths(tp_context_t *context, const char **paths, size_t length);
void tp_context_set_driver_timeout_ns(tp_context_t *context, uint64_t value);
uint64_t tp_context_get_driver_timeout_ns(const tp_context_t *context);
void tp_context_set_keepalive_interval_ns(tp_context_t *context, uint64_t value);
uint64_t tp_context_get_keepalive_interval_ns(const tp_context_t *context);
void tp_context_set_lease_expiry_grace_intervals(tp_context_t *context, uint32_t value);
uint32_t tp_context_get_lease_expiry_grace_intervals(const tp_context_t *context);
void tp_context_set_idle_sleep_duration_ns(tp_context_t *context, uint64_t value);
uint64_t tp_context_get_idle_sleep_duration_ns(const tp_context_t *context);
void tp_context_set_idle_strategy(tp_context_t *context, uint64_t sleep_ns);
void tp_context_set_announce_period_ns(tp_context_t *context, uint64_t period_ns);
uint64_t tp_context_get_announce_period_ns(const tp_context_t *context);
void tp_context_set_use_agent_invoker(tp_context_t *context, bool value);
void tp_context_set_use_conductor_agent_invoker(tp_context_t *context, bool value);
bool tp_context_get_use_agent_invoker(const tp_context_t *context);
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
