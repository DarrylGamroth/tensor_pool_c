#ifndef TENSOR_POOL_TP_HANDLES_H
#define TENSOR_POOL_TP_HANDLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_publication_stct tp_publication_t;
typedef struct tp_subscription_stct tp_subscription_t;
typedef struct tp_fragment_assembler_stct tp_fragment_assembler_t;
typedef struct tp_async_add_publication_stct tp_async_add_publication_t;
typedef struct tp_async_add_subscription_stct tp_async_add_subscription_t;

const char *tp_publication_channel(const tp_publication_t *publication);
int32_t tp_publication_stream_id(const tp_publication_t *publication);
int64_t tp_publication_channel_status(const tp_publication_t *publication);

const char *tp_subscription_channel(const tp_subscription_t *subscription);
int32_t tp_subscription_stream_id(const tp_subscription_t *subscription);
int64_t tp_subscription_channel_status(const tp_subscription_t *subscription);

#ifdef __cplusplus
}
#endif

#endif
