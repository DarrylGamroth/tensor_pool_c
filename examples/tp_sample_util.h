#ifndef TENSOR_POOL_TP_SAMPLE_UTIL_H
#define TENSOR_POOL_TP_SAMPLE_UTIL_H

#include <stdint.h>
#include <stddef.h>

#include "tensor_pool/tp.h"

#ifdef __cplusplus
extern "C" {
#endif

int tp_example_init_client_context(
    tp_client_context_t *ctx,
    const char *aeron_dir,
    const char *channel,
    int32_t announce_stream_id,
    const char **allowed_paths,
    size_t allowed_path_count);

int tp_example_init_client_context_nodriver(
    tp_client_context_t *ctx,
    const char *aeron_dir,
    const char *channel,
    const char **allowed_paths,
    size_t allowed_path_count);

void tp_example_log_publication_status(const char *label, tp_publication_t *publication);
void tp_example_log_subscription_status(
    const char *label,
    tp_subscription_t *subscription,
    int *last_images,
    int64_t *last_status);

int tp_example_wait_for_publication(tp_client_t *client, tp_publication_t *publication, int64_t timeout_ns);
int tp_example_wait_for_subscription(tp_client_t *client, tp_subscription_t *subscription, int64_t timeout_ns);

#ifdef __cplusplus
}
#endif

#endif
