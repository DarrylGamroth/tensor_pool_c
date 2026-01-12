#ifndef TENSOR_POOL_TP_CLIENT_CONDUCTOR_H
#define TENSOR_POOL_TP_CLIENT_CONDUCTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "aeronc.h"

#include "tensor_pool/tp_aeron.h"
#include "tensor_pool/tp_context.h"

typedef struct tp_client_context_stct tp_client_context_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_client_conductor_stct
{
    tp_aeron_client_t aeron;
    bool use_agent_invoker;
    bool started;
    bool owns_aeron;
}
tp_client_conductor_t;

int tp_client_conductor_init(
    tp_client_conductor_t *conductor,
    const tp_context_t *context,
    bool use_agent_invoker);
int tp_client_conductor_init_with_client_context(
    tp_client_conductor_t *conductor,
    const tp_client_context_t *context);
int tp_client_conductor_init_with_aeron(
    tp_client_conductor_t *conductor,
    aeron_t *aeron,
    bool use_agent_invoker,
    bool owns_aeron);
int tp_client_conductor_start(tp_client_conductor_t *conductor);
int tp_client_conductor_close(tp_client_conductor_t *conductor);
int tp_client_conductor_do_work(tp_client_conductor_t *conductor);
int tp_client_conductor_set_idle_sleep_duration_ns(tp_client_conductor_t *conductor, uint64_t sleep_ns);

int tp_client_conductor_async_add_publication(
    aeron_async_add_publication_t **async,
    tp_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id);
int tp_client_conductor_async_add_publication_poll(
    aeron_publication_t **publication,
    aeron_async_add_publication_t *async);

int tp_client_conductor_async_add_subscription(
    aeron_async_add_subscription_t **async,
    tp_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id,
    aeron_on_available_image_t on_available,
    void *available_clientd,
    aeron_on_unavailable_image_t on_unavailable,
    void *unavailable_clientd);
int tp_client_conductor_async_add_subscription_poll(
    aeron_subscription_t **subscription,
    aeron_async_add_subscription_t *async);

#ifdef __cplusplus
}
#endif

#endif
