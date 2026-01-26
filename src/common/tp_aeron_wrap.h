#ifndef TENSOR_POOL_TP_AERON_WRAP_H
#define TENSOR_POOL_TP_AERON_WRAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "aeron_fragment_assembler.h"
#include "aeronc.h"

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_handles.h"

typedef enum tp_async_status_enum
{
    TP_ASYNC_PENDING = 0,
    TP_ASYNC_COMPLETE = 1,
    TP_ASYNC_ERROR = 2
}
tp_async_status_t;

struct tp_publication_stct
{
    aeron_publication_t *aeron;
    char *channel;
    int32_t stream_id;
};

struct tp_subscription_stct
{
    aeron_subscription_t *aeron;
    char *channel;
    int32_t stream_id;
};

struct tp_fragment_assembler_stct
{
    aeron_fragment_assembler_t *aeron;
};

struct tp_async_add_publication_stct
{
    atomic_int status;
    tp_publication_t *publication;
    aeron_async_add_publication_t *aeron_async;
    char *channel;
    int32_t stream_id;
    int errcode;
    char errmsg[256];
    struct tp_async_add_publication_stct *next;
};

struct tp_async_add_subscription_stct
{
    atomic_int status;
    tp_subscription_t *subscription;
    aeron_async_add_subscription_t *aeron_async;
    char *channel;
    int32_t stream_id;
    int errcode;
    char errmsg[256];
    struct tp_async_add_subscription_stct *next;
};

int tp_publication_wrap(tp_publication_t **out, aeron_publication_t *pub, const char *channel, int32_t stream_id);
int tp_subscription_wrap(tp_subscription_t **out, aeron_subscription_t *sub, const char *channel, int32_t stream_id);
int tp_fragment_assembler_wrap(tp_fragment_assembler_t **out, aeron_fragment_assembler_t *assembler);
int tp_fragment_assembler_create(tp_fragment_assembler_t **out, aeron_fragment_handler_t handler, void *clientd);

void tp_publication_close(tp_publication_t **pub);
void tp_subscription_close(tp_subscription_t **sub);
void tp_fragment_assembler_close(tp_fragment_assembler_t **assembler);

static inline aeron_publication_t *tp_publication_handle(tp_publication_t *pub)
{
    return NULL != pub ? pub->aeron : NULL;
}

static inline aeron_subscription_t *tp_subscription_handle(tp_subscription_t *sub)
{
    return NULL != sub ? sub->aeron : NULL;
}

static inline aeron_fragment_assembler_t *tp_fragment_assembler_handle(tp_fragment_assembler_t *assembler)
{
    return NULL != assembler ? assembler->aeron : NULL;
}

#endif
