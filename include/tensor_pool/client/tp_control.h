#ifndef TENSOR_POOL_TP_CONTROL_H
#define TENSOR_POOL_TP_CONTROL_H

#include <stdint.h>
#include <stddef.h>

#include "tensor_pool/tp_types.h"

typedef struct tp_consumer_stct tp_consumer_t;
typedef struct tp_producer_stct tp_producer_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_consumer_hello_stct
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
    const char *descriptor_channel;
    const char *control_channel;
}
tp_consumer_hello_t;

typedef struct tp_consumer_config_msg_stct
{
    uint32_t stream_id;
    uint32_t consumer_id;
    uint8_t use_shm;
    tp_mode_t mode;
    uint32_t descriptor_stream_id;
    uint32_t control_stream_id;
    const char *payload_fallback_uri;
    const char *descriptor_channel;
    const char *control_channel;
}
tp_consumer_config_msg_t;

typedef struct tp_data_source_announce_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint32_t meta_version;
    const char *name;
    const char *summary;
}
tp_data_source_announce_t;

typedef struct tp_meta_attribute_stct
{
    const char *key;
    const char *format;
    const uint8_t *value;
    uint32_t value_length;
}
tp_meta_attribute_t;

typedef struct tp_data_source_meta_stct
{
    uint32_t stream_id;
    uint32_t meta_version;
    uint64_t timestamp_ns;
    const tp_meta_attribute_t *attributes;
    size_t attribute_count;
}
tp_data_source_meta_t;

typedef struct tp_meta_blob_announce_stct
{
    uint32_t stream_id;
    uint32_t meta_version;
    uint32_t blob_type;
    uint64_t total_len;
    uint64_t checksum;
}
tp_meta_blob_announce_t;

typedef struct tp_meta_blob_chunk_stct
{
    uint32_t stream_id;
    uint32_t meta_version;
    uint64_t offset;
    const uint8_t *bytes;
    uint32_t bytes_length;
}
tp_meta_blob_chunk_t;

typedef struct tp_meta_blob_complete_stct
{
    uint32_t stream_id;
    uint32_t meta_version;
    uint64_t checksum;
}
tp_meta_blob_complete_t;

typedef struct tp_shm_pool_announce_pool_stct
{
    uint16_t pool_id;
    uint32_t pool_nslots;
    uint32_t stride_bytes;
    const char *region_uri;
}
tp_shm_pool_announce_pool_t;

typedef struct tp_shm_pool_announce_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint64_t announce_timestamp_ns;
    uint8_t announce_clock_domain;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint16_t header_slot_bytes;
    const char *header_region_uri;
    const tp_shm_pool_announce_pool_t *pools;
    size_t pool_count;
}
tp_shm_pool_announce_t;

typedef struct tp_control_response_stct
{
    int64_t correlation_id;
    tp_response_code_t code;
    const char *error_message;
}
tp_control_response_t;

int tp_consumer_send_hello(tp_consumer_t *consumer, const tp_consumer_hello_t *hello);
int tp_producer_send_consumer_config(tp_producer_t *producer, const tp_consumer_config_msg_t *config);
int tp_producer_send_data_source_announce(tp_producer_t *producer, const tp_data_source_announce_t *announce);
int tp_producer_send_data_source_meta(tp_producer_t *producer, const tp_data_source_meta_t *meta);
int tp_producer_send_meta_blob_announce(tp_producer_t *producer, const tp_meta_blob_announce_t *announce);
int tp_producer_send_meta_blob_chunk(tp_producer_t *producer, const tp_meta_blob_chunk_t *chunk);
int tp_producer_send_meta_blob_complete(tp_producer_t *producer, const tp_meta_blob_complete_t *complete);
int tp_producer_send_shm_pool_announce(tp_producer_t *producer, const tp_shm_pool_announce_t *announce);
int tp_producer_send_control_response(tp_producer_t *producer, const tp_control_response_t *response);

#ifdef __cplusplus
}
#endif

#endif
