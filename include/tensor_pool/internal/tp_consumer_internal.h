#ifndef TENSOR_POOL_TP_CONSUMER_INTERNAL_H
#define TENSOR_POOL_TP_CONSUMER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "tensor_pool/client/tp_consumer.h"
#include "tensor_pool/internal/tp_progress_poller.h"

struct tp_consumer_stct
{
    tp_client_t *client;
    tp_consumer_context_t context;
    tp_subscription_t *descriptor_subscription;
    tp_subscription_t *control_subscription;
    uint32_t assigned_descriptor_stream_id;
    uint32_t assigned_control_stream_id;
    tp_publication_t *control_publication;
    tp_publication_t *qos_publication;
    tp_fragment_assembler_t *descriptor_assembler;
    tp_fragment_assembler_t *control_assembler;
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
    tp_driver_client_t *driver;
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
    bool conductor_poll_registered;
};

#endif
