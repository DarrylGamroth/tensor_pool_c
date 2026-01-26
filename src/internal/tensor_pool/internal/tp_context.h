#ifndef TENSOR_POOL_INTERNAL_TP_CONTEXT_H
#define TENSOR_POOL_INTERNAL_TP_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>

#include "tensor_pool/tp_context.h"
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
    uint64_t driver_timeout_ns;
    uint64_t keepalive_interval_ns;
    uint64_t idle_sleep_duration_ns;
    uint32_t lease_expiry_grace_intervals;
    int64_t message_timeout_ns;
    int32_t message_retry_attempts;
    bool use_agent_invoker;
    bool owns_aeron_client;
    void *aeron;
    char client_name[256];
    tp_error_handler_t error_handler;
    void *error_handler_clientd;
    tp_delegating_invoker_t delegating_invoker;
    void *delegating_invoker_clientd;
    tp_allowed_paths_t allowed_paths;
    uint64_t announce_period_ns;
    tp_log_t log;
}
tp_context_t;

#ifdef __cplusplus
}
#endif

#endif
