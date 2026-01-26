#ifndef TENSOR_POOL_TP_CONSUMER_MANAGER_H
#define TENSOR_POOL_TP_CONSUMER_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "tensor_pool/internal/tp_consumer_registry.h"
#include "tensor_pool/internal/tp_control_adapter.h"
#include "tensor_pool/tp_producer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_progress_tracker_stct
{
    uint64_t last_timestamp_ns;
    uint64_t last_bytes;
}
tp_progress_tracker_t;

typedef struct tp_consumer_manager_stct
{
    tp_producer_t *producer;
    tp_consumer_registry_t registry;
    tp_progress_policy_t progress_policy;
    char payload_fallback_uri[TP_URI_MAX_LENGTH];
    bool force_no_shm;
}
tp_consumer_manager_t;

int tp_consumer_manager_init(tp_consumer_manager_t *manager, tp_producer_t *producer, size_t capacity);
int tp_consumer_manager_close(tp_consumer_manager_t *manager);
void tp_consumer_manager_set_payload_fallback_uri(tp_consumer_manager_t *manager, const char *uri);
void tp_consumer_manager_set_force_no_shm(tp_consumer_manager_t *manager, bool enabled);

int tp_consumer_manager_handle_hello(
    tp_consumer_manager_t *manager,
    const tp_consumer_hello_view_t *hello,
    uint64_t now_ns);
int tp_consumer_manager_touch(tp_consumer_manager_t *manager, uint32_t consumer_id, uint64_t now_ns);
int tp_consumer_manager_sweep(tp_consumer_manager_t *manager, uint64_t now_ns, uint64_t stale_ns);

int tp_consumer_manager_refresh_progress_policy(tp_consumer_manager_t *manager);
int tp_consumer_manager_get_progress_policy(const tp_consumer_manager_t *manager, tp_progress_policy_t *out);

int tp_consumer_manager_should_publish_progress(
    const tp_consumer_manager_t *manager,
    tp_progress_tracker_t *state,
    uint64_t now_ns,
    uint64_t payload_bytes_filled,
    int *out_should_publish);

int tp_consumer_manager_publish_descriptor(
    tp_consumer_manager_t *manager,
    uint32_t consumer_id,
    uint64_t seq,
    uint64_t timestamp_ns,
    uint32_t meta_version,
    uint64_t trace_id);

int tp_consumer_manager_publish_progress(
    tp_consumer_manager_t *manager,
    uint32_t consumer_id,
    uint64_t seq,
    uint64_t payload_bytes_filled,
    tp_progress_state_t state);

#ifdef __cplusplus
}
#endif

#endif
