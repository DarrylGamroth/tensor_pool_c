#ifndef TENSOR_POOL_TP_CLIENT_INTERNAL_H
#define TENSOR_POOL_TP_CLIENT_INTERNAL_H

#include <stdbool.h>

#include "tensor_pool/client/tp_client.h"

typedef struct tp_client_conductor_stct tp_client_conductor_t;
typedef struct tp_client_conductor_agent_stct tp_client_conductor_agent_t;

struct tp_client_stct
{
    tp_context_t *context;
    tp_client_conductor_t *conductor;
    tp_client_conductor_agent_t *agent;
    void *control_poller;
    void *metadata_poller;
    void *qos_poller;
    bool control_poller_registered;
    bool metadata_poller_registered;
    bool qos_poller_registered;
    tp_driver_client_t *driver_clients;
};

#endif
