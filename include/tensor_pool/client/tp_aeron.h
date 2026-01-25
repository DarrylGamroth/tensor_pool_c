#ifndef TENSOR_POOL_TP_AERON_H
#define TENSOR_POOL_TP_AERON_H

#include <stdint.h>

#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_aeron_client_stct
{
    void *context;
    void *aeron;
}
tp_aeron_client_t;

int tp_aeron_client_init(tp_aeron_client_t *client, const tp_context_t *context);
int tp_aeron_client_close(tp_aeron_client_t *client);

int tp_aeron_add_publication(
    tp_publication_t **pub,
    tp_aeron_client_t *client,
    const char *channel,
    int32_t stream_id);

int tp_aeron_add_subscription(
    tp_subscription_t **sub,
    tp_aeron_client_t *client,
    const char *channel,
    int32_t stream_id);

#ifdef __cplusplus
}
#endif

#endif
