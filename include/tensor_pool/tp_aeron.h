#ifndef TENSOR_POOL_TP_AERON_H
#define TENSOR_POOL_TP_AERON_H

#include <stdint.h>

#include "aeronc.h"

#include "tensor_pool/tp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_aeron_client_stct
{
    aeron_context_t *context;
    aeron_t *aeron;
}
tp_aeron_client_t;

int tp_aeron_client_init(tp_aeron_client_t *client, const tp_context_t *context);
int tp_aeron_client_close(tp_aeron_client_t *client);

int tp_aeron_add_publication(
    aeron_publication_t **pub,
    tp_aeron_client_t *client,
    const char *channel,
    int32_t stream_id);

int tp_aeron_add_subscription(
    aeron_subscription_t **sub,
    tp_aeron_client_t *client,
    const char *channel,
    int32_t stream_id,
    aeron_on_available_image_t on_available,
    void *available_clientd,
    aeron_on_unavailable_image_t on_unavailable,
    void *unavailable_clientd);

#ifdef __cplusplus
}
#endif

#endif
