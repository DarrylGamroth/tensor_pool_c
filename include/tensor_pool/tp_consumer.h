#ifndef TENSOR_POOL_TP_CONSUMER_H
#define TENSOR_POOL_TP_CONSUMER_H

#include <stdint.h>

#include "tensor_pool/tp_aeron.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_shm.h"
#include "tensor_pool/tp_tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    tp_context_t context;
    tp_aeron_client_t aeron;
    aeron_subscription_t *descriptor_subscription;
    aeron_publication_t *control_publication;
    aeron_publication_t *qos_publication;
    tp_shm_region_t header_region;
    tp_consumer_pool_t *pools;
    size_t pool_count;
    uint32_t stream_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
}
tp_consumer_t;

int tp_consumer_init(tp_consumer_t *consumer, const tp_context_t *context);
int tp_consumer_attach_direct(tp_consumer_t *consumer, const tp_consumer_config_t *config);
int tp_consumer_read_frame(tp_consumer_t *consumer, uint64_t seq, uint32_t header_index, tp_frame_view_t *out);
int tp_consumer_close(tp_consumer_t *consumer);

#ifdef __cplusplus
}
#endif

#endif
