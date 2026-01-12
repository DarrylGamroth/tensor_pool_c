#ifndef TENSOR_POOL_TP_CONTROL_POLLER_H
#define TENSOR_POOL_TP_CONTROL_POLLER_H

#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_control_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_control_handlers_stct
{
    tp_on_consumer_hello_t on_consumer_hello;
    tp_on_consumer_config_t on_consumer_config;
    tp_on_data_source_announce_t on_data_source_announce;
    tp_on_data_source_meta_begin_t on_data_source_meta_begin;
    tp_on_data_source_meta_attr_t on_data_source_meta_attr;
    tp_on_data_source_meta_end_t on_data_source_meta_end;
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

#ifdef __cplusplus
}
#endif

#endif
