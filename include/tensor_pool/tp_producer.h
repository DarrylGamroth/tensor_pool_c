#ifndef TENSOR_POOL_TP_PRODUCER_H
#define TENSOR_POOL_TP_PRODUCER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_control.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_shm.h"
#include "tensor_pool/tp_tensor.h"
#include "tensor_pool/tp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_consumer_manager_stct tp_consumer_manager_t;

typedef struct tp_payload_pool_config_stct
{
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    const char *uri;
}
tp_payload_pool_config_t;

typedef struct tp_payload_pool_stct
{
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    tp_shm_region_t region;
}
tp_payload_pool_t;

typedef struct tp_producer_config_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    const char *header_uri;
    const tp_payload_pool_config_t *pools;
    size_t pool_count;
}
tp_producer_config_t;

typedef struct tp_producer_context_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    bool use_driver;
    bool fixed_pool_mode;
    tp_driver_attach_request_t driver_request;
}
tp_producer_context_t;

typedef struct tp_frame_metadata_stct
{
    uint64_t timestamp_ns;
    uint32_t meta_version;
    uint64_t trace_id;
}
tp_frame_metadata_t;

typedef struct tp_meta_attribute_owned_stct
{
    char *key;
    char *format;
    uint8_t *value;
    uint32_t value_length;
}
tp_meta_attribute_owned_t;

typedef struct tp_frame_stct
{
    const tp_tensor_header_t *tensor;
    const void *payload;
    uint32_t payload_len;
    uint16_t pool_id;
}
tp_frame_t;

typedef struct tp_buffer_claim_stct
{
    uint64_t seq;
    uint32_t header_index;
    uint16_t pool_id;
    uint32_t payload_len;
    uint8_t *payload;
    tp_tensor_header_t tensor;
}
tp_buffer_claim_t;

typedef struct tp_frame_progress_stct
{
    uint64_t seq;
    uint64_t payload_bytes_filled;
    tp_progress_state_t state;
}
tp_frame_progress_t;

typedef struct tp_producer_stct
{
    tp_client_t *client;
    tp_producer_context_t context;
    aeron_publication_t *descriptor_publication;
    aeron_publication_t *control_publication;
    aeron_publication_t *qos_publication;
    aeron_publication_t *metadata_publication;
    aeron_fragment_assembler_t *control_assembler;
    tp_shm_region_t header_region;
    tp_payload_pool_t *pools;
    size_t pool_count;
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint64_t next_seq;
    tp_driver_client_t driver;
    tp_driver_attach_info_t driver_attach;
    bool driver_initialized;
    bool driver_attached;
    tp_consumer_manager_t *consumer_manager;
    uint64_t last_consumer_sweep_ns;
    uint64_t last_qos_ns;
    uint64_t last_announce_ns;
    uint64_t last_meta_ns;
    tp_data_source_announce_t cached_announce;
    bool has_announce;
    tp_data_source_meta_t cached_meta;
    tp_meta_attribute_owned_t *cached_attrs;
    size_t cached_attr_count;
    bool has_meta;
}
tp_producer_t;

int tp_producer_context_init(tp_producer_context_t *ctx);
void tp_producer_context_set_fixed_pool_mode(tp_producer_context_t *ctx, bool enabled);

int tp_producer_init(tp_producer_t *producer, tp_client_t *client, const tp_producer_context_t *ctx);
int tp_producer_attach(tp_producer_t *producer, const tp_producer_config_t *config);
int64_t tp_producer_offer_frame(tp_producer_t *producer, const tp_frame_t *frame, tp_frame_metadata_t *meta);
int64_t tp_producer_try_claim(tp_producer_t *producer, size_t length, tp_buffer_claim_t *claim);
int tp_producer_commit_claim(tp_producer_t *producer, tp_buffer_claim_t *claim, const tp_frame_metadata_t *meta);
int tp_producer_abort_claim(tp_producer_t *producer, tp_buffer_claim_t *claim);
int64_t tp_producer_queue_claim(tp_producer_t *producer, tp_buffer_claim_t *claim);
int tp_producer_offer_progress(tp_producer_t *producer, const tp_frame_progress_t *progress);
int tp_producer_set_data_source_announce(tp_producer_t *producer, const tp_data_source_announce_t *announce);
int tp_producer_set_data_source_meta(tp_producer_t *producer, const tp_data_source_meta_t *meta);
void tp_producer_clear_data_source_announce(tp_producer_t *producer);
void tp_producer_clear_data_source_meta(tp_producer_t *producer);
int tp_producer_enable_consumer_manager(tp_producer_t *producer, size_t capacity);
int tp_producer_poll_control(tp_producer_t *producer, int fragment_limit);
int tp_producer_close(tp_producer_t *producer);

#ifdef __cplusplus
}
#endif

#endif
