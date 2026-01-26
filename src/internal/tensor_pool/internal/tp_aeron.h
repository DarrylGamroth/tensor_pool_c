#ifndef TENSOR_POOL_TP_AERON_H
#define TENSOR_POOL_TP_AERON_H

#include <stdint.h>
#include <stdbool.h>

#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_handles.h"
#include "tensor_pool/common/tp_aeron_client.h"

#ifdef __cplusplus
extern "C" {
#endif

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

const char *tp_publication_channel(const tp_publication_t *pub);
int32_t tp_publication_stream_id(const tp_publication_t *pub);
int64_t tp_publication_channel_status(const tp_publication_t *pub);
bool tp_publication_is_connected(const tp_publication_t *pub);

int tp_subscription_image_count(const tp_subscription_t *sub);
int64_t tp_subscription_channel_status(const tp_subscription_t *sub);
bool tp_subscription_is_connected(const tp_subscription_t *sub);

#ifdef __cplusplus
}
#endif

#endif
