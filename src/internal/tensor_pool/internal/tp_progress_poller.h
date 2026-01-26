#ifndef TENSOR_POOL_TP_PROGRESS_POLLER_H
#define TENSOR_POOL_TP_PROGRESS_POLLER_H

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_handles.h"

typedef struct tp_consumer_stct tp_consumer_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tp_on_progress_t)(void *clientd, const tp_frame_progress_t *progress);
typedef int (*tp_progress_validator_t)(void *clientd, const tp_frame_progress_t *progress);

#define TP_PROGRESS_TRACKER_CAPACITY 64

typedef struct tp_progress_tracker_entry_stct
{
    uint32_t stream_id;
    uint64_t epoch;
    uint64_t seq;
    uint64_t last_bytes;
    uint8_t in_use;
}
tp_progress_tracker_entry_t;

typedef struct tp_progress_handlers_stct
{
    tp_on_progress_t on_progress;
    void *clientd;
}
tp_progress_handlers_t;

typedef struct tp_progress_poller_stct
{
    tp_client_t *client;
    tp_subscription_t *subscription;
    tp_fragment_assembler_t *assembler;
    tp_progress_handlers_t handlers;
    tp_progress_tracker_entry_t *tracker;
    size_t tracker_capacity;
    size_t tracker_cursor;
    uint32_t header_nslots;
    uint64_t max_payload_bytes;
    tp_progress_validator_t validator;
    void *validator_clientd;
}
tp_progress_poller_t;

int tp_progress_poller_init(tp_progress_poller_t *poller, tp_client_t *client, const tp_progress_handlers_t *handlers);
int tp_progress_poller_init_with_subscription(
    tp_progress_poller_t *poller,
    tp_subscription_t *subscription,
    const tp_progress_handlers_t *handlers);
void tp_progress_poller_set_max_payload_bytes(tp_progress_poller_t *poller, uint64_t max_payload_bytes);
void tp_progress_poller_set_validator(
    tp_progress_poller_t *poller,
    tp_progress_validator_t validator,
    void *clientd);
void tp_progress_poller_set_consumer(tp_progress_poller_t *poller, tp_consumer_t *consumer);
int tp_progress_poll(tp_progress_poller_t *poller, int fragment_limit);

#if defined(TP_ENABLE_FUZZ) || defined(TP_TESTING)
void tp_progress_poller_handle_fragment(tp_progress_poller_t *poller, const uint8_t *buffer, size_t length);
#endif

#ifdef __cplusplus
}
#endif

#endif
