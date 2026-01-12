#ifndef TENSOR_POOL_TP_PRODUCER_H
#define TENSOR_POOL_TP_PRODUCER_H

#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/tp_aeron.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_shm.h"
#include "tensor_pool/tp_tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct tp_producer_stct
{
    tp_context_t context;
    tp_aeron_client_t aeron;
    aeron_publication_t *descriptor_publication;
    aeron_publication_t *control_publication;
    aeron_publication_t *qos_publication;
    aeron_publication_t *metadata_publication;
    tp_shm_region_t header_region;
    tp_payload_pool_t *pools;
    size_t pool_count;
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
}
tp_producer_t;

int tp_producer_init(tp_producer_t *producer, const tp_context_t *context);
int tp_producer_attach_direct(tp_producer_t *producer, const tp_producer_config_t *config);
int tp_producer_publish_frame(
    tp_producer_t *producer,
    uint64_t seq,
    uint32_t header_index,
    const tp_tensor_header_t *tensor,
    const void *payload,
    uint32_t payload_len,
    uint16_t pool_id,
    uint64_t timestamp_ns,
    uint32_t meta_version);
int tp_producer_publish_descriptor_to(
    tp_producer_t *producer,
    aeron_publication_t *publication,
    uint64_t seq,
    uint32_t header_index,
    uint64_t timestamp_ns,
    uint32_t meta_version);
int tp_producer_publish_progress(
    tp_producer_t *producer,
    uint64_t seq,
    uint32_t header_index,
    uint64_t payload_bytes_filled,
    uint8_t state);
int tp_producer_publish_progress_to(
    tp_producer_t *producer,
    aeron_publication_t *publication,
    uint64_t seq,
    uint32_t header_index,
    uint64_t payload_bytes_filled,
    uint8_t state);
int tp_producer_close(tp_producer_t *producer);

#ifdef __cplusplus
}
#endif

#endif
