#ifndef TENSOR_POOL_TP_CONTROL_H
#define TENSOR_POOL_TP_CONTROL_H

#include <stdint.h>

#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_consumer_hello_stct
{
    uint32_t stream_id;
    uint32_t consumer_id;
    uint8_t supports_shm;
    uint8_t supports_progress;
    uint8_t mode;
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
    uint8_t mode;
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

int tp_consumer_send_hello(tp_consumer_t *consumer, const tp_consumer_hello_t *hello);
int tp_producer_send_consumer_config(tp_producer_t *producer, const tp_consumer_config_msg_t *config);
int tp_producer_send_data_source_announce(tp_producer_t *producer, const tp_data_source_announce_t *announce);
int tp_producer_send_data_source_meta(tp_producer_t *producer, const tp_data_source_meta_t *meta);

#ifdef __cplusplus
}
#endif

#endif
