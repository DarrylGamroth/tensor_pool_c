#ifndef TENSOR_POOL_TP_DRIVER_CLIENT_INTERNAL_H
#define TENSOR_POOL_TP_DRIVER_CLIENT_INTERNAL_H

#include <stdbool.h>

#include "tensor_pool/client/tp_driver_client.h"

struct tp_driver_client_stct
{
    tp_client_t *client;
    tp_publication_t *publication;
    tp_subscription_t *subscription;
    uint64_t active_lease_id;
    uint64_t lease_expiry_timestamp_ns;
    uint64_t last_keepalive_ns;
    uint32_t active_stream_id;
    uint32_t client_id;
    uint8_t role;
    bool registered;
    struct tp_driver_client_stct *next;
};

#endif
