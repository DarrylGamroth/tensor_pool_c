#ifndef TENSOR_POOL_TP_CONTROL_ADAPTER_H
#define TENSOR_POOL_TP_CONTROL_ADAPTER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_string_view_stct
{
    const char *data;
    uint32_t length;
}
tp_string_view_t;

typedef struct tp_consumer_hello_view_stct
{
    uint32_t stream_id;
    uint32_t consumer_id;
    uint8_t supports_shm;
    uint8_t supports_progress;
    tp_mode_t mode;
    uint32_t max_rate_hz;
    uint32_t expected_layout_version;
    uint32_t progress_interval_us;
    uint32_t progress_bytes_delta;
    uint32_t progress_major_delta_units;
    uint32_t descriptor_stream_id;
    uint32_t control_stream_id;
    tp_string_view_t descriptor_channel;
    tp_string_view_t control_channel;
}
tp_consumer_hello_view_t;

typedef struct tp_consumer_config_view_stct
{
    uint32_t stream_id;
    uint32_t consumer_id;
    uint8_t use_shm;
    tp_mode_t mode;
    uint32_t descriptor_stream_id;
    uint32_t control_stream_id;
    tp_string_view_t payload_fallback_uri;
    tp_string_view_t descriptor_channel;
    tp_string_view_t control_channel;
}
tp_consumer_config_view_t;

typedef struct tp_data_source_announce_view_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint32_t meta_version;
    tp_string_view_t name;
    tp_string_view_t summary;
}
tp_data_source_announce_view_t;

typedef struct tp_data_source_meta_view_stct
{
    uint32_t stream_id;
    uint32_t meta_version;
    uint64_t timestamp_ns;
    uint32_t attribute_count;
}
tp_data_source_meta_view_t;

typedef struct tp_data_source_meta_attr_view_stct
{
    tp_string_view_t key;
    tp_string_view_t format;
    tp_string_view_t value;
}
tp_data_source_meta_attr_view_t;

typedef struct tp_meta_blob_announce_view_stct
{
    uint32_t stream_id;
    uint32_t meta_version;
    uint32_t blob_type;
    uint64_t total_len;
    uint64_t checksum;
}
tp_meta_blob_announce_view_t;

typedef struct tp_meta_blob_chunk_view_stct
{
    uint32_t stream_id;
    uint32_t meta_version;
    uint64_t offset;
    tp_string_view_t bytes;
}
tp_meta_blob_chunk_view_t;

typedef struct tp_meta_blob_complete_view_stct
{
    uint32_t stream_id;
    uint32_t meta_version;
    uint64_t checksum;
}
tp_meta_blob_complete_view_t;

typedef struct tp_control_response_view_stct
{
    int64_t correlation_id;
    tp_response_code_t code;
    tp_string_view_t error_message;
}
tp_control_response_view_t;

typedef struct tp_shm_pool_desc_stct
{
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    tp_string_view_t region_uri;
}
tp_shm_pool_desc_t;

typedef struct tp_shm_pool_announce_view_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint64_t announce_timestamp_ns;
    uint8_t announce_clock_domain;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint16_t header_slot_bytes;
    tp_string_view_t header_region_uri;
    tp_shm_pool_desc_t *pools;
    size_t pool_count;
}
tp_shm_pool_announce_view_t;

typedef void (*tp_on_consumer_hello_t)(const tp_consumer_hello_view_t *view, void *clientd);
typedef void (*tp_on_consumer_config_t)(const tp_consumer_config_view_t *view, void *clientd);
typedef void (*tp_on_data_source_announce_t)(const tp_data_source_announce_view_t *view, void *clientd);
typedef void (*tp_on_data_source_meta_begin_t)(const tp_data_source_meta_view_t *view, void *clientd);
typedef void (*tp_on_data_source_meta_attr_t)(const tp_data_source_meta_attr_view_t *attr, void *clientd);
typedef void (*tp_on_data_source_meta_end_t)(const tp_data_source_meta_view_t *view, void *clientd);
typedef void (*tp_on_meta_blob_announce_t)(const tp_meta_blob_announce_view_t *view, void *clientd);
typedef void (*tp_on_meta_blob_chunk_t)(const tp_meta_blob_chunk_view_t *view, void *clientd);
typedef void (*tp_on_meta_blob_complete_t)(const tp_meta_blob_complete_view_t *view, void *clientd);
typedef void (*tp_on_control_response_t)(const tp_control_response_view_t *view, void *clientd);
typedef void (*tp_on_shm_pool_announce_t)(const tp_shm_pool_announce_view_t *view, void *clientd);

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
    aeron_subscription_t *subscription;
    aeron_fragment_assembler_t *assembler;
    tp_control_adapter_t adapter;
}
tp_control_subscription_t;

int tp_control_subscription_init(
    tp_control_subscription_t *control,
    aeron_t *aeron,
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
