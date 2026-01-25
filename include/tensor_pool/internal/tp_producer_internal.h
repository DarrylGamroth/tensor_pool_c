#ifndef TENSOR_POOL_TP_PRODUCER_INTERNAL_H
#define TENSOR_POOL_TP_PRODUCER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "tensor_pool/client/tp_producer.h"
#include "tensor_pool/internal/tp_qos.h"

typedef struct tp_consumer_manager_stct tp_consumer_manager_t;
typedef struct tp_tracelink_entry_stct tp_tracelink_entry_t;

struct tp_producer_stct
{
    tp_client_t *client;
    tp_producer_context_t context;
    tp_publication_t *descriptor_publication;
    tp_publication_t *control_publication;
    tp_publication_t *qos_publication;
    tp_publication_t *metadata_publication;
    tp_fragment_assembler_t *control_assembler;
    tp_qos_poller_t *qos_poller;
    tp_shm_region_t header_region;
    char *header_uri;
    tp_payload_pool_t *pools;
    char **pool_uris;
    size_t pool_uri_count;
    size_t pool_count;
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint64_t next_seq;
    tp_driver_client_t *driver;
    tp_driver_attach_info_t driver_attach;
    bool driver_initialized;
    bool driver_attached;
    tp_consumer_manager_t *consumer_manager;
    uint64_t last_consumer_sweep_ns;
    uint64_t last_qos_ns;
    uint64_t last_activity_ns;
    uint64_t last_shm_announce_ns;
    uint64_t last_announce_ns;
    uint64_t last_meta_ns;
    tp_data_source_announce_t cached_announce;
    bool has_announce;
    tp_data_source_meta_t cached_meta;
    tp_meta_attribute_owned_t *cached_attrs;
    size_t cached_attr_count;
    bool has_meta;
    tp_trace_id_generator_t *trace_id_generator;
    tp_tracelink_entry_t *tracelink_entries;
    size_t tracelink_entry_count;
    tp_tracelink_validate_t tracelink_validator;
    void *tracelink_validator_clientd;
    uint64_t next_attach_ns;
    uint32_t attach_failures;
    bool reattach_requested;
    bool conductor_poll_registered;
};

#endif
