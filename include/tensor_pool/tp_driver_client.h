#ifndef TENSOR_POOL_TP_DRIVER_CLIENT_H
#define TENSOR_POOL_TP_DRIVER_CLIENT_H

#include <stdint.h>

#include "tensor_pool/tp_aeron.h"
#include "tensor_pool/tp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_driver_pool_info_stct
{
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    char region_uri[4096];
}
tp_driver_pool_info_t;

typedef struct tp_driver_attach_info_stct
{
    int32_t code;
    char error_message[1024];
    uint64_t lease_id;
    uint64_t lease_expiry_timestamp_ns;
    uint32_t stream_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint16_t header_slot_bytes;
    uint8_t max_dims;
    char header_region_uri[4096];
    tp_driver_pool_info_t *pools;
    size_t pool_count;
}
tp_driver_attach_info_t;

typedef struct tp_driver_attach_request_stct
{
    int64_t correlation_id;
    uint32_t stream_id;
    uint32_t client_id;
    uint8_t role;
    uint32_t expected_layout_version;
    uint8_t publish_mode;
    uint8_t require_hugepages;
}
tp_driver_attach_request_t;

typedef struct tp_driver_client_stct
{
    tp_aeron_client_t aeron;
    aeron_publication_t *publication;
    aeron_subscription_t *subscription;
}
tp_driver_client_t;

int tp_driver_client_init(tp_driver_client_t *client, const tp_context_t *context);
int tp_driver_client_close(tp_driver_client_t *client);

int tp_driver_attach(
    tp_driver_client_t *client,
    const tp_driver_attach_request_t *request,
    tp_driver_attach_info_t *out,
    int64_t timeout_ns);

int tp_driver_keepalive(tp_driver_client_t *client, uint64_t lease_id, uint32_t stream_id, uint32_t client_id, uint8_t role, uint64_t timestamp_ns);
int tp_driver_detach(tp_driver_client_t *client, int64_t correlation_id, uint64_t lease_id, uint32_t stream_id, uint32_t client_id, uint8_t role);

void tp_driver_attach_info_close(tp_driver_attach_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
