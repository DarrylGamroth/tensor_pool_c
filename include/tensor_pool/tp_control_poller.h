#ifndef TENSOR_POOL_TP_CONTROL_POLLER_H
#define TENSOR_POOL_TP_CONTROL_POLLER_H

#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_join_barrier.h"
#include "tensor_pool/tp_tracelink.h"

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct tp_control_poller_stct
{
    tp_client_t *client;
    aeron_fragment_assembler_t *assembler;
    tp_control_handlers_t handlers;
}
tp_control_poller_t;

int tp_control_poller_init(tp_control_poller_t *poller, tp_client_t *client, const tp_control_handlers_t *handlers);
int tp_control_poll(tp_control_poller_t *poller, int fragment_limit);

#if defined(TP_ENABLE_FUZZ) || defined(TP_TESTING)
void tp_control_poller_handle_fragment(tp_control_poller_t *poller, const uint8_t *buffer, size_t length);
#endif

#ifdef __cplusplus
}
#endif

#endif
