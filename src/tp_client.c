#include "tensor_pool/tp_client.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_error.h"

static int tp_client_copy_context(tp_client_context_t *dst, const tp_client_context_t *src)
{
    if (NULL == dst || NULL == src)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_copy_context: null input");
        return -1;
    }

    *dst = *src;
    return 0;
}

int tp_client_context_init(tp_client_context_t *ctx)
{
    if (NULL == ctx)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_context_init: null input");
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    tp_context_init(&ctx->base);
    ctx->owns_aeron_client = true;
    ctx->message_timeout_ns = 0;
    ctx->message_retry_attempts = 0;
    ctx->driver_timeout_ns = 5 * 1000 * 1000 * 1000ULL;
    ctx->keepalive_interval_ns = 1000 * 1000 * 1000ULL;
    ctx->lease_expiry_grace_intervals = 3;
    return 0;
}

void tp_client_context_set_aeron_dir(tp_client_context_t *ctx, const char *dir)
{
    if (NULL == ctx)
    {
        return;
    }

    tp_context_set_aeron_dir(&ctx->base, dir);
}

void tp_client_context_set_aeron(tp_client_context_t *ctx, aeron_t *aeron)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->aeron = aeron;
}

void tp_client_context_set_owns_aeron_client(tp_client_context_t *ctx, bool owns)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->owns_aeron_client = owns;
}

void tp_client_context_set_client_name(tp_client_context_t *ctx, const char *name)
{
    if (NULL == ctx || NULL == name)
    {
        return;
    }

    strncpy(ctx->client_name, name, sizeof(ctx->client_name) - 1);
}

void tp_client_context_set_message_timeout_ns(tp_client_context_t *ctx, int64_t timeout_ns)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->message_timeout_ns = timeout_ns;
}

void tp_client_context_set_message_retry_attempts(tp_client_context_t *ctx, int32_t attempts)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->message_retry_attempts = attempts;
}

void tp_client_context_set_error_handler(tp_client_context_t *ctx, tp_error_handler_t handler, void *clientd)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->error_handler = handler;
    ctx->error_handler_clientd = clientd;
}

void tp_client_context_set_delegating_invoker(tp_client_context_t *ctx, tp_delegating_invoker_t invoker, void *clientd)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->delegating_invoker = invoker;
    ctx->delegating_invoker_clientd = clientd;
}

void tp_client_context_set_log_handler(tp_client_context_t *ctx, tp_log_func_t handler, void *clientd)
{
    if (NULL == ctx)
    {
        return;
    }

    tp_log_set_handler(&ctx->base.log, handler, clientd);
}

void tp_client_context_set_control_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id)
{
    if (NULL == ctx)
    {
        return;
    }

    tp_context_set_control_channel(&ctx->base, channel, stream_id);
}

void tp_client_context_set_descriptor_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id)
{
    if (NULL == ctx)
    {
        return;
    }

    tp_context_set_descriptor_channel(&ctx->base, channel, stream_id);
}

void tp_client_context_set_qos_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id)
{
    if (NULL == ctx)
    {
        return;
    }

    tp_context_set_qos_channel(&ctx->base, channel, stream_id);
}

void tp_client_context_set_metadata_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id)
{
    if (NULL == ctx)
    {
        return;
    }

    tp_context_set_metadata_channel(&ctx->base, channel, stream_id);
}

void tp_client_context_set_driver_timeout_ns(tp_client_context_t *ctx, uint64_t value)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->driver_timeout_ns = value;
}

void tp_client_context_set_keepalive_interval_ns(tp_client_context_t *ctx, uint64_t value)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->keepalive_interval_ns = value;
}

void tp_client_context_set_lease_expiry_grace_intervals(tp_client_context_t *ctx, uint32_t value)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->lease_expiry_grace_intervals = value;
}

void tp_client_context_set_idle_sleep_duration_ns(tp_client_context_t *ctx, uint64_t value)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->idle_sleep_duration_ns = value;
}

void tp_client_context_set_announce_period_ns(tp_client_context_t *ctx, uint64_t value)
{
    if (NULL == ctx)
    {
        return;
    }

    tp_context_set_announce_period_ns(&ctx->base, value);
}

void tp_client_context_set_use_agent_invoker(tp_client_context_t *ctx, bool value)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->use_agent_invoker = value;
}

static int tp_client_add_subscription(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    aeron_subscription_t **out_sub)
{
    aeron_async_add_subscription_t *async_add = NULL;
    int idle_count = 0;

    if (NULL == channel || stream_id < 0)
    {
        return 0;
    }

    if (tp_client_async_add_subscription(
        client,
        channel,
        stream_id,
        NULL,
        NULL,
        NULL,
        NULL,
        &async_add) < 0)
    {
        return -1;
    }

    *out_sub = NULL;
    while (NULL == *out_sub)
    {
        if (tp_client_async_add_subscription_poll(out_sub, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(client);
        if (++idle_count > 1000)
        {
            idle_count = 0;
        }
    }

    return 0;
}

int tp_client_init(tp_client_t *client, const tp_client_context_t *ctx)
{
    int result = -1;

    if (NULL == client || NULL == ctx)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_init: null input");
        return -1;
    }

    memset(client, 0, sizeof(*client));

    if (tp_client_copy_context(&client->context, ctx) < 0)
    {
        return -1;
    }

    client->context.base.allowed_paths.canonical_paths = NULL;
    client->context.base.allowed_paths.canonical_length = 0;

    if (tp_context_finalize_allowed_paths(&client->context.base) < 0)
    {
        return -1;
    }

    if (NULL != ctx->aeron)
    {
        if (tp_client_conductor_init_with_aeron(
            &client->conductor,
            ctx->aeron,
            ctx->use_agent_invoker,
            ctx->owns_aeron_client) < 0)
        {
            goto cleanup;
        }
    }
    else
    {
        if (tp_client_conductor_init_with_client_context(&client->conductor, ctx) < 0)
        {
            goto cleanup;
        }
    }

    result = 0;

cleanup:
    if (result < 0)
    {
        tp_context_clear_allowed_paths(&client->context.base);
    }
    return result;
}

int tp_client_start(tp_client_t *client)
{
    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_start: null input");
        return -1;
    }

    if (tp_client_conductor_start(&client->conductor) < 0)
    {
        return -1;
    }

    if (tp_client_add_subscription(
        client,
        client->context.base.control_channel,
        client->context.base.control_stream_id,
        &client->control_subscription) < 0)
    {
        return -1;
    }

    if (tp_client_add_subscription(
        client,
        client->context.base.qos_channel,
        client->context.base.qos_stream_id,
        &client->qos_subscription) < 0)
    {
        return -1;
    }

    if (tp_client_add_subscription(
        client,
        client->context.base.metadata_channel,
        client->context.base.metadata_stream_id,
        &client->metadata_subscription) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_client_do_work(tp_client_t *client)
{
    tp_driver_client_t *driver;
    uint64_t now_ns = 0;

    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_do_work: null input");
        return -1;
    }

    if (NULL != client->context.delegating_invoker)
    {
        client->context.delegating_invoker(client->context.delegating_invoker_clientd);
    }

    int work = tp_client_conductor_do_work(&client->conductor);
    if (work < 0)
    {
        return -1;
    }

    if (client->driver_clients)
    {
        now_ns = (uint64_t)tp_clock_now_ns();
    }

    for (driver = client->driver_clients; NULL != driver; driver = driver->next)
    {
        if (tp_driver_client_lease_expired(driver, now_ns) > 0)
        {
            TP_SET_ERR(
                ETIMEDOUT,
                "tp_client_do_work: driver lease expired stream=%u client=%u",
                driver->active_stream_id,
                driver->client_id);
            tp_log_emit(&client->context.base.log, TP_LOG_ERROR, "%s", tp_errmsg());
            if (client->context.error_handler)
            {
                client->context.error_handler(client->context.error_handler_clientd, tp_errcode(), tp_errmsg());
            }
            driver->active_lease_id = 0;
            return -1;
        }

        if (tp_driver_client_keepalive_due(driver, now_ns, client->context.keepalive_interval_ns) > 0)
        {
            if (tp_driver_keepalive(driver, now_ns) < 0)
            {
                tp_log_emit(&client->context.base.log, TP_LOG_ERROR, "%s", tp_errmsg());
                if (client->context.error_handler)
                {
                    client->context.error_handler(client->context.error_handler_clientd, tp_errcode(), tp_errmsg());
                }
                return -1;
            }
        }
    }

    return work;
}

int tp_client_close(tp_client_t *client)
{
    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_close: null input");
        return -1;
    }

    if (NULL != client->control_subscription)
    {
        aeron_subscription_close(client->control_subscription, NULL, NULL);
        client->control_subscription = NULL;
    }

    if (NULL != client->qos_subscription)
    {
        aeron_subscription_close(client->qos_subscription, NULL, NULL);
        client->qos_subscription = NULL;
    }

    if (NULL != client->metadata_subscription)
    {
        aeron_subscription_close(client->metadata_subscription, NULL, NULL);
        client->metadata_subscription = NULL;
    }

    client->driver_clients = NULL;

    tp_context_clear_allowed_paths(&client->context.base);
    return tp_client_conductor_close(&client->conductor);
}

int tp_client_register_driver_client(tp_client_t *client, tp_driver_client_t *driver)
{
    tp_driver_client_t *cursor;

    if (NULL == client || NULL == driver)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_register_driver_client: null input");
        return -1;
    }

    for (cursor = client->driver_clients; NULL != cursor; cursor = cursor->next)
    {
        if (cursor == driver)
        {
            driver->registered = true;
            return 0;
        }
    }

    driver->next = client->driver_clients;
    client->driver_clients = driver;
    driver->registered = true;
    return 0;
}

int tp_client_unregister_driver_client(tp_client_t *client, tp_driver_client_t *driver)
{
    tp_driver_client_t *cursor;
    tp_driver_client_t *prev = NULL;

    if (NULL == client || NULL == driver)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_unregister_driver_client: null input");
        return -1;
    }

    for (cursor = client->driver_clients; NULL != cursor; cursor = cursor->next)
    {
        if (cursor == driver)
        {
            if (NULL == prev)
            {
                client->driver_clients = cursor->next;
            }
            else
            {
                prev->next = cursor->next;
            }
            driver->next = NULL;
            driver->registered = false;
            return 0;
        }
        prev = cursor;
    }

    TP_SET_ERR(EINVAL, "%s", "tp_client_unregister_driver_client: driver not registered");
    return -1;
}

int tp_client_async_add_publication(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    aeron_async_add_publication_t **out)
{
    if (NULL == client || NULL == channel || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_async_add_publication: null input");
        return -1;
    }

    return tp_client_conductor_async_add_publication(out, &client->conductor, channel, stream_id);
}

int tp_client_async_add_publication_poll(
    aeron_publication_t **publication,
    aeron_async_add_publication_t *async_add)
{
    return tp_client_conductor_async_add_publication_poll(publication, async_add);
}

int tp_client_async_add_subscription(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    aeron_on_available_image_t on_available,
    void *available_clientd,
    aeron_on_unavailable_image_t on_unavailable,
    void *unavailable_clientd,
    aeron_async_add_subscription_t **out)
{
    if (NULL == client || NULL == channel || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_async_add_subscription: null input");
        return -1;
    }

    return tp_client_conductor_async_add_subscription(
        out,
        &client->conductor,
        channel,
        stream_id,
        on_available,
        available_clientd,
        on_unavailable,
        unavailable_clientd);
}

int tp_client_async_add_subscription_poll(
    aeron_subscription_t **subscription,
    aeron_async_add_subscription_t *async_add)
{
    return tp_client_conductor_async_add_subscription_poll(subscription, async_add);
}
