#ifndef TENSOR_POOL_TP_CONTROL_ADAPTER_H
#define TENSOR_POOL_TP_CONTROL_ADAPTER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "tensor_pool/client/tp_control_view.h"
#include "tensor_pool/tp_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_control_adapter_stct
{
    tp_on_shm_pool_announce_t on_shm_pool_announce;
    tp_on_consumer_hello_t on_consumer_hello;
    tp_on_consumer_config_t on_consumer_config;
    tp_on_data_source_announce_t on_data_source_announce;
    tp_on_data_source_meta_begin_t on_data_source_meta_begin;
    tp_on_data_source_meta_attr_t on_data_source_meta_attr;
    tp_on_data_source_meta_end_t on_data_source_meta_end;
    tp_on_control_response_t on_control_response;
    void *clientd;
}
tp_control_adapter_t;

typedef struct tp_control_subscription_stct
{
    tp_subscription_t *subscription;
    tp_fragment_assembler_t *assembler;
    tp_control_adapter_t adapter;
}
tp_control_subscription_t;

int tp_control_subscription_init(
    tp_control_subscription_t *control,
    void *aeron,
    const char *channel,
    int32_t stream_id,
    const tp_control_adapter_t *adapter);
int tp_control_subscription_close(tp_control_subscription_t *control);
int tp_control_subscription_poll(tp_control_subscription_t *control, int fragment_limit);

int tp_control_decode_consumer_hello(const uint8_t *buffer, size_t length, tp_consumer_hello_view_t *out);
int tp_control_decode_consumer_config(const uint8_t *buffer, size_t length, tp_consumer_config_view_t *out);
int tp_control_decode_data_source_announce(const uint8_t *buffer, size_t length, tp_data_source_announce_view_t *out);
int tp_control_decode_shm_pool_announce(const uint8_t *buffer, size_t length, tp_shm_pool_announce_view_t *out);
int tp_control_decode_control_response(const uint8_t *buffer, size_t length, tp_control_response_view_t *out);
void tp_control_shm_pool_announce_close(tp_shm_pool_announce_view_t *view);
int tp_control_decode_meta_blob_announce(const uint8_t *buffer, size_t length, tp_meta_blob_announce_view_t *out);
int tp_control_decode_meta_blob_chunk(const uint8_t *buffer, size_t length, tp_meta_blob_chunk_view_t *out);
int tp_control_decode_meta_blob_complete(const uint8_t *buffer, size_t length, tp_meta_blob_complete_view_t *out);
int tp_control_decode_data_source_meta(
    const uint8_t *buffer,
    size_t length,
    tp_data_source_meta_view_t *out,
    tp_on_data_source_meta_attr_t on_attr,
    void *clientd);

#ifdef __cplusplus
}
#endif

#endif
