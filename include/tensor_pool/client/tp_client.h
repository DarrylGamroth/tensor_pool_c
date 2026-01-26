#ifndef TENSOR_POOL_TP_CLIENT_H
#define TENSOR_POOL_TP_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "tensor_pool/client/tp_control_view.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_handles.h"
#include "tensor_pool/tp_join_barrier.h"
#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_tracelink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_driver_client_stct tp_driver_client_t;
typedef struct tp_client_stct tp_client_t;

typedef void (*tp_on_tracelink_set_t)(const tp_tracelink_set_t *set, void *clientd);

typedef struct tp_control_handlers_stct
{
    tp_on_shm_pool_announce_t on_shm_pool_announce;
    tp_on_consumer_hello_t on_consumer_hello;
    tp_on_consumer_config_t on_consumer_config;
    tp_on_control_response_t on_control_response;
    tp_on_data_source_announce_t on_data_source_announce;
    tp_on_data_source_meta_begin_t on_data_source_meta_begin;
    tp_on_data_source_meta_attr_t on_data_source_meta_attr;
    tp_on_data_source_meta_end_t on_data_source_meta_end;
    tp_on_tracelink_set_t on_tracelink_set;
    tp_on_driver_detach_response_t on_detach_response;
    tp_on_driver_lease_revoked_t on_lease_revoked;
    tp_on_driver_shutdown_t on_shutdown;
    tp_join_barrier_t *sequence_join_barrier;
    tp_join_barrier_t *timestamp_join_barrier;
    tp_join_barrier_t *latest_join_barrier;
    void *clientd;
}
tp_control_handlers_t;

typedef struct tp_metadata_handlers_stct
{
    tp_on_data_source_announce_t on_data_source_announce;
    tp_on_data_source_meta_begin_t on_data_source_meta_begin;
    tp_on_data_source_meta_attr_t on_data_source_meta_attr;
    tp_on_data_source_meta_end_t on_data_source_meta_end;
    tp_on_meta_blob_announce_t on_meta_blob_announce;
    tp_on_meta_blob_chunk_t on_meta_blob_chunk;
    tp_on_meta_blob_complete_t on_meta_blob_complete;
    void *clientd;
}
tp_metadata_handlers_t;

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

typedef int (*tp_client_poller_t)(void *clientd, int fragment_limit);

int tp_client_init(tp_client_t **client, tp_context_t *ctx);
int tp_client_start(tp_client_t *client);
int tp_client_do_work(tp_client_t *client);
int tp_client_main_do_work(tp_client_t *client);
int tp_client_idle(tp_client_t *client, int work_count);
int tp_client_close(tp_client_t *client);

int tp_client_register_driver_client(tp_client_t *client, tp_driver_client_t *driver);
int tp_client_unregister_driver_client(tp_client_t *client, tp_driver_client_t *driver);
int tp_client_register_poller(tp_client_t *client, tp_client_poller_t poller, void *clientd, int fragment_limit);
int tp_client_unregister_poller(tp_client_t *client, tp_client_poller_t poller, void *clientd);

tp_subscription_t *tp_client_control_subscription(tp_client_t *client);
tp_subscription_t *tp_client_announce_subscription(tp_client_t *client);
tp_subscription_t *tp_client_qos_subscription(tp_client_t *client);
tp_subscription_t *tp_client_metadata_subscription(tp_client_t *client);
tp_subscription_t *tp_client_descriptor_subscription(tp_client_t *client);

int tp_client_set_control_handlers(tp_client_t *client, const tp_control_handlers_t *handlers, int fragment_limit);
int tp_client_set_metadata_handlers(tp_client_t *client, const tp_metadata_handlers_t *handlers, int fragment_limit);
int tp_client_set_qos_handlers(tp_client_t *client, const tp_qos_handlers_t *handlers, int fragment_limit);

int tp_client_async_add_publication(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    tp_async_add_publication_t **out);
int tp_client_async_add_publication_poll(
    tp_publication_t **publication,
    tp_async_add_publication_t *async_add);

int tp_client_async_add_subscription(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    tp_async_add_subscription_t **out);
int tp_client_async_add_subscription_poll(
    tp_subscription_t **subscription,
    tp_async_add_subscription_t *async_add);

#ifdef __cplusplus
}
#endif

#endif
