#ifndef TENSOR_POOL_TP_PROGRESS_POLLER_H
#define TENSOR_POOL_TP_PROGRESS_POLLER_H

#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_producer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tp_on_progress_t)(void *clientd, const tp_frame_progress_t *progress);

typedef struct tp_progress_handlers_stct
{
    tp_on_progress_t on_progress;
    void *clientd;
}
tp_progress_handlers_t;

typedef struct tp_progress_poller_stct
{
    tp_client_t *client;
    aeron_subscription_t *subscription;
    aeron_fragment_assembler_t *assembler;
    tp_progress_handlers_t handlers;
}
tp_progress_poller_t;

int tp_progress_poller_init(tp_progress_poller_t *poller, tp_client_t *client, const tp_progress_handlers_t *handlers);
int tp_progress_poller_init_with_subscription(
    tp_progress_poller_t *poller,
    aeron_subscription_t *subscription,
    const tp_progress_handlers_t *handlers);
int tp_progress_poll(tp_progress_poller_t *poller, int fragment_limit);

#ifdef __cplusplus
}
#endif

#endif
