#ifndef TENSOR_POOL_TP_CLIENT_H
#define TENSOR_POOL_TP_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_handles.h"
#include "tensor_pool/tp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_driver_client_stct tp_driver_client_t;
typedef struct tp_client_conductor_stct tp_client_conductor_t;
typedef struct tp_control_handlers_stct tp_control_handlers_t;
typedef struct tp_metadata_handlers_stct tp_metadata_handlers_t;
typedef struct tp_qos_handlers_stct tp_qos_handlers_t;

typedef void (*tp_error_handler_t)(void *clientd, int errcode, const char *message);
typedef void (*tp_delegating_invoker_t)(void *clientd);
typedef int (*tp_client_poller_t)(void *clientd, int fragment_limit);

typedef struct tp_client_context_stct
{
    tp_context_t base;
    uint64_t driver_timeout_ns;
    uint64_t keepalive_interval_ns;
    uint64_t idle_sleep_duration_ns;
    uint32_t lease_expiry_grace_intervals;
    int64_t message_timeout_ns;
    int32_t message_retry_attempts;
    bool use_agent_invoker;
    bool owns_aeron_client;
    void *aeron;
    char client_name[256];
    tp_error_handler_t error_handler;
    void *error_handler_clientd;
    tp_delegating_invoker_t delegating_invoker;
    void *delegating_invoker_clientd;
}
tp_client_context_t;

typedef struct tp_client_stct
{
    tp_client_context_t context;
    tp_client_conductor_t *conductor;
    void *control_poller;
    void *metadata_poller;
    void *qos_poller;
    bool control_poller_registered;
    bool metadata_poller_registered;
    bool qos_poller_registered;
    tp_driver_client_t *driver_clients;
}
tp_client_t;

int tp_client_context_init(tp_client_context_t *ctx);
void tp_client_context_set_aeron_dir(tp_client_context_t *ctx, const char *dir);
void tp_client_context_set_aeron(tp_client_context_t *ctx, void *aeron);
void tp_client_context_set_owns_aeron_client(tp_client_context_t *ctx, bool owns);
void tp_client_context_set_client_name(tp_client_context_t *ctx, const char *name);
void tp_client_context_set_message_timeout_ns(tp_client_context_t *ctx, int64_t timeout_ns);
void tp_client_context_set_message_retry_attempts(tp_client_context_t *ctx, int32_t attempts);
void tp_client_context_set_error_handler(tp_client_context_t *ctx, tp_error_handler_t handler, void *clientd);
void tp_client_context_set_delegating_invoker(tp_client_context_t *ctx, tp_delegating_invoker_t invoker, void *clientd);
void tp_client_context_set_log_handler(tp_client_context_t *ctx, tp_log_func_t handler, void *clientd);
void tp_client_context_set_control_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_announce_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_descriptor_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_qos_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_metadata_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_driver_timeout_ns(tp_client_context_t *ctx, uint64_t value);
void tp_client_context_set_keepalive_interval_ns(tp_client_context_t *ctx, uint64_t value);
void tp_client_context_set_lease_expiry_grace_intervals(tp_client_context_t *ctx, uint32_t value);
void tp_client_context_set_idle_sleep_duration_ns(tp_client_context_t *ctx, uint64_t value);
void tp_client_context_set_announce_period_ns(tp_client_context_t *ctx, uint64_t value);
void tp_client_context_set_use_agent_invoker(tp_client_context_t *ctx, bool value);

int tp_client_init(tp_client_t *client, const tp_client_context_t *ctx);
int tp_client_start(tp_client_t *client);
int tp_client_do_work(tp_client_t *client);
int tp_client_close(tp_client_t *client);

int tp_client_register_driver_client(tp_client_t *client, tp_driver_client_t *driver);
int tp_client_unregister_driver_client(tp_client_t *client, tp_driver_client_t *driver);
int tp_client_register_poller(tp_client_t *client, tp_client_poller_t poller, void *clientd, int fragment_limit);
int tp_client_unregister_poller(tp_client_t *client, tp_client_poller_t poller, void *clientd);

tp_subscription_t *tp_client_control_subscription(tp_client_t *client);
tp_subscription_t *tp_client_announce_subscription(tp_client_t *client);
tp_subscription_t *tp_client_qos_subscription(tp_client_t *client);
tp_subscription_t *tp_client_metadata_subscription(tp_client_t *client);
tp_subscription_t *tp_client_descriptor_subscription(tp_client_t *client);

int tp_client_set_control_handlers(tp_client_t *client, const tp_control_handlers_t *handlers, int fragment_limit);
int tp_client_set_metadata_handlers(tp_client_t *client, const tp_metadata_handlers_t *handlers, int fragment_limit);
int tp_client_set_qos_handlers(tp_client_t *client, const tp_qos_handlers_t *handlers, int fragment_limit);

int tp_client_async_add_publication(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    tp_async_add_publication_t **out);
int tp_client_async_add_publication_poll(
    tp_publication_t **publication,
    tp_async_add_publication_t *async_add);

int tp_client_async_add_subscription(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    tp_async_add_subscription_t **out);
int tp_client_async_add_subscription_poll(
    tp_subscription_t **subscription,
    tp_async_add_subscription_t *async_add);

#ifdef __cplusplus
}
#endif

#endif
