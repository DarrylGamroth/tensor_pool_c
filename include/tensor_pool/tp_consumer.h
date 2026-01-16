#ifndef TENSOR_POOL_TP_CONSUMER_H
#define TENSOR_POOL_TP_CONSUMER_H

#include <stdint.h>
#include <stdbool.h>

#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_control.h"
#include "tensor_pool/tp_shm.h"
#include "tensor_pool/tp_tensor.h"
#include "tensor_pool/tp_types.h"
#include "tensor_pool/tp_progress_poller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_frame_progress_stct tp_frame_progress_t;

typedef struct tp_consumer_pool_config_stct
{
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    const char *uri;
}
tp_consumer_pool_config_t;

typedef struct tp_consumer_pool_stct
{
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    tp_shm_region_t region;
}
tp_consumer_pool_t;

typedef struct tp_consumer_config_stct
{
    uint32_t stream_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    const char *header_uri;
    const tp_consumer_pool_config_t *pools;
    size_t pool_count;
}
tp_consumer_config_t;

typedef struct tp_consumer_context_stct
{
    uint32_t stream_id;
    uint32_t consumer_id;
    bool use_driver;
    tp_driver_attach_request_t driver_request;
    tp_consumer_hello_t hello;
}
tp_consumer_context_t;

typedef enum tp_consumer_state_enum
{
    TP_CONSUMER_STATE_UNMAPPED = 0,
    TP_CONSUMER_STATE_MAPPED = 1,
    TP_CONSUMER_STATE_FALLBACK = 2
}
tp_consumer_state_t;

typedef struct tp_frame_descriptor_stct
{
    uint64_t seq;
    uint64_t timestamp_ns;
    uint32_t meta_version;
    uint64_t trace_id;
}
tp_frame_descriptor_t;

typedef void (*tp_frame_descriptor_handler_t)(void *clientd, const tp_frame_descriptor_t *desc);
typedef void (*tp_frame_progress_handler_t)(void *clientd, const tp_frame_progress_t *progress);

typedef struct tp_frame_view_stct
{
    tp_tensor_header_t tensor;
    const uint8_t *payload;
    uint32_t payload_len;
    uint16_t pool_id;
    uint32_t payload_slot;
    uint64_t timestamp_ns;
    uint32_t meta_version;
}
tp_frame_view_t;

typedef struct tp_consumer_stct
{
    tp_client_t *client;
    tp_consumer_context_t context;
    aeron_subscription_t *descriptor_subscription;
    aeron_subscription_t *control_subscription;
    uint32_t assigned_descriptor_stream_id;
    uint32_t assigned_control_stream_id;
    aeron_publication_t *control_publication;
    aeron_publication_t *qos_publication;
    aeron_fragment_assembler_t *descriptor_assembler;
    aeron_fragment_assembler_t *control_assembler;
    tp_progress_poller_t progress_poller;
    bool progress_poller_initialized;
    tp_frame_descriptor_handler_t descriptor_handler;
    void *descriptor_clientd;
    tp_frame_progress_handler_t progress_handler;
    void *progress_clientd;
    tp_shm_region_t header_region;
    tp_consumer_pool_t *pools;
    size_t pool_count;
    uint32_t stream_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint64_t next_seq;
    tp_driver_client_t driver;
    tp_driver_attach_info_t driver_attach;
    bool driver_initialized;
    bool driver_attached;
    tp_consumer_state_t state;
    bool use_shm;
    char payload_fallback_uri[TP_URI_MAX_LENGTH];
    bool shm_mapped;
    uint64_t mapped_epoch;
    uint64_t attach_time_ns;
    uint64_t last_seq_seen;
    uint64_t drops_gap;
    uint64_t drops_late;
    uint64_t last_qos_ns;
    uint64_t announce_join_time_ns;
    uint64_t last_announce_rx_ns;
    uint64_t last_announce_timestamp_ns;
    uint8_t last_announce_clock_domain;
    uint64_t last_announce_epoch;
    uint64_t next_attach_ns;
    uint32_t attach_failures;
    bool reattach_requested;
}
tp_consumer_t;

int tp_consumer_context_init(tp_consumer_context_t *ctx);
int tp_consumer_init(tp_consumer_t *consumer, tp_client_t *client, const tp_consumer_context_t *context);
int tp_consumer_attach(tp_consumer_t *consumer, const tp_consumer_config_t *config);
void tp_consumer_schedule_reattach(tp_consumer_t *consumer, uint64_t now_ns);
int tp_consumer_reattach_due(const tp_consumer_t *consumer, uint64_t now_ns);
void tp_consumer_clear_reattach(tp_consumer_t *consumer);
void tp_consumer_set_descriptor_handler(tp_consumer_t *consumer, tp_frame_descriptor_handler_t handler, void *clientd);
int tp_consumer_read_frame(tp_consumer_t *consumer, uint64_t seq, tp_frame_view_t *out);
int tp_consumer_validate_progress(const tp_consumer_t *consumer, const tp_frame_progress_t *progress);
int tp_consumer_get_drop_counts(const tp_consumer_t *consumer, uint64_t *drops_gap, uint64_t *drops_late, uint64_t *last_seq_seen);
int tp_consumer_poll_descriptors(tp_consumer_t *consumer, int fragment_limit);
int tp_consumer_poll_control(tp_consumer_t *consumer, int fragment_limit);
int tp_consumer_set_progress_handler(tp_consumer_t *consumer, tp_frame_progress_handler_t handler, void *clientd);
int tp_consumer_poll_progress(tp_consumer_t *consumer, int fragment_limit);
const char *tp_consumer_payload_fallback_uri(const tp_consumer_t *consumer);
bool tp_consumer_uses_shm(const tp_consumer_t *consumer);
int tp_consumer_close(tp_consumer_t *consumer);

#ifdef __cplusplus
}
#endif

#endif
