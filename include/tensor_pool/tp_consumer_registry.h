#ifndef TENSOR_POOL_TP_CONSUMER_REGISTRY_H
#define TENSOR_POOL_TP_CONSUMER_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aeron_publication.h"

#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_progress_policy_stct
{
    uint32_t interval_us;
    uint32_t bytes_delta;
    uint32_t major_delta_units;
}
tp_progress_policy_t;

typedef struct tp_consumer_entry_stct
{
    bool in_use;
    uint32_t consumer_id;
    uint64_t last_seen_ns;
    uint8_t mode;
    uint32_t max_rate_hz;
    uint8_t supports_progress;
    uint32_t progress_interval_us;
    uint32_t progress_bytes_delta;
    uint32_t progress_major_delta_units;
    uint32_t descriptor_stream_id;
    char descriptor_channel[TP_URI_MAX_LENGTH];
    uint32_t control_stream_id;
    char control_channel[TP_URI_MAX_LENGTH];
    aeron_publication_t *descriptor_publication;
    aeron_publication_t *control_publication;
}
tp_consumer_entry_t;

typedef struct tp_consumer_registry_stct
{
    tp_consumer_entry_t *entries;
    size_t capacity;
}
tp_consumer_registry_t;

typedef struct tp_consumer_request_stct
{
    bool descriptor_requested;
    bool control_requested;
}
tp_consumer_request_t;

int tp_consumer_request_validate(const tp_consumer_hello_view_t *hello, tp_consumer_request_t *out);

int tp_consumer_registry_init(tp_consumer_registry_t *registry, size_t capacity);
void tp_consumer_registry_close(tp_consumer_registry_t *registry);

int tp_consumer_registry_update(
    tp_consumer_registry_t *registry,
    const tp_consumer_hello_view_t *hello,
    uint64_t now_ns,
    tp_consumer_entry_t **out_entry);
int tp_consumer_registry_sweep(tp_consumer_registry_t *registry, uint64_t now_ns, uint64_t stale_ns);

int tp_consumer_registry_aggregate_progress(
    const tp_consumer_registry_t *registry,
    tp_progress_policy_t *out_policy);

#ifdef __cplusplus
}
#endif

#endif
