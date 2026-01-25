#ifndef TENSOR_POOL_TP_QOS_H
#define TENSOR_POOL_TP_QOS_H

#include <stdint.h>

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

int tp_qos_publish_producer(tp_producer_t *producer, uint64_t current_seq, uint32_t watermark);
int tp_qos_publish_consumer(
    tp_consumer_t *consumer,
    uint32_t consumer_id,
    uint64_t last_seq_seen,
    uint64_t drops_gap,
    uint64_t drops_late,
    tp_mode_t mode);

typedef enum tp_qos_event_type_enum
{
    TP_QOS_EVENT_PRODUCER,
    TP_QOS_EVENT_CONSUMER
}
tp_qos_event_type_t;

typedef struct tp_qos_event_stct
{
    tp_qos_event_type_t type;
    uint32_t stream_id;
    uint32_t producer_id;
    uint32_t consumer_id;
    uint64_t epoch;
    uint64_t current_seq;
    uint32_t watermark;
    uint64_t last_seq_seen;
    uint64_t drops_gap;
    uint64_t drops_late;
    tp_mode_t mode;
}
tp_qos_event_t;

typedef void (*tp_on_qos_event_t)(void *clientd, const tp_qos_event_t *event);

typedef struct tp_qos_handlers_stct
{
    tp_on_qos_event_t on_qos_event;
    void *clientd;
}
tp_qos_handlers_t;

typedef struct tp_qos_poller_stct
{
    tp_client_t *client;
    tp_fragment_assembler_t *assembler;
    tp_qos_handlers_t handlers;
}
tp_qos_poller_t;

int tp_qos_poller_init(tp_qos_poller_t *poller, tp_client_t *client, const tp_qos_handlers_t *handlers);
int tp_qos_poll(tp_qos_poller_t *poller, int fragment_limit);

#if defined(TP_ENABLE_FUZZ) || defined(TP_TESTING)
void tp_qos_poller_handle_fragment(tp_qos_poller_t *poller, const uint8_t *buffer, size_t length);
#endif

#ifdef __cplusplus
}
#endif

#endif
