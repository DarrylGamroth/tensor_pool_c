#ifndef TENSOR_POOL_TP_CLIENT_CONDUCTOR_H
#define TENSOR_POOL_TP_CLIENT_CONDUCTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "tensor_pool/tp_aeron.h"
#include "tensor_pool/tp_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_client_conductor_stct
{
    tp_aeron_client_t aeron;
    bool use_agent_invoker;
    bool started;
    bool owns_aeron;
    void *state;
}
tp_client_conductor_t;

typedef enum tp_client_subscription_kind_enum
{
    TP_CLIENT_SUB_CONTROL = 1,
    TP_CLIENT_SUB_ANNOUNCE = 2,
    TP_CLIENT_SUB_QOS = 3,
    TP_CLIENT_SUB_METADATA = 4,
    TP_CLIENT_SUB_DESCRIPTOR = 5
}
tp_client_subscription_kind_t;

int tp_client_conductor_init(
    tp_client_conductor_t *conductor,
    const tp_context_t *context,
    bool use_agent_invoker);
int tp_client_conductor_init_with_client_context(
    tp_client_conductor_t *conductor,
    const tp_client_context_t *context);
int tp_client_conductor_init_with_aeron(
    tp_client_conductor_t *conductor,
    void *aeron,
    bool use_agent_invoker,
    bool owns_aeron);
int tp_client_conductor_start(tp_client_conductor_t *conductor);
int tp_client_conductor_close(tp_client_conductor_t *conductor);
int tp_client_conductor_do_work(tp_client_conductor_t *conductor);
int tp_client_conductor_set_idle_sleep_duration_ns(tp_client_conductor_t *conductor, uint64_t sleep_ns);
int tp_client_conductor_register_poller(
    tp_client_conductor_t *conductor,
    tp_client_poller_t poller,
    void *clientd,
    int fragment_limit);
int tp_client_conductor_unregister_poller(
    tp_client_conductor_t *conductor,
    tp_client_poller_t poller,
    void *clientd);

int tp_client_conductor_set_subscription(
    tp_client_conductor_t *conductor,
    tp_client_subscription_kind_t kind,
    tp_subscription_t *subscription);
tp_subscription_t *tp_client_conductor_get_subscription(
    tp_client_conductor_t *conductor,
    tp_client_subscription_kind_t kind);

int tp_client_conductor_async_add_publication(
    tp_async_add_publication_t **async,
    tp_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id);
int tp_client_conductor_async_add_publication_poll(
    tp_publication_t **publication,
    tp_async_add_publication_t *async);

int tp_client_conductor_async_add_subscription(
    tp_async_add_subscription_t **async,
    tp_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id);
int tp_client_conductor_async_add_subscription_poll(
    tp_subscription_t **subscription,
    tp_async_add_subscription_t *async);

#ifdef __cplusplus
}
#endif

#endif
