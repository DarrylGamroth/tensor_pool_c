#include "tensor_pool/internal/tp_client_internal.h"
#include "tensor_pool/internal/tp_client_conductor.h"
#include "tensor_pool/internal/tp_client_conductor_agent.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "aeron_alloc.h"
#include "aeron_agent.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/internal/tp_context.h"
#include "tensor_pool/internal/tp_control_poller.h"
#include "tensor_pool/internal/tp_metadata_poller.h"
#include "tensor_pool/internal/tp_qos.h"
#include "tensor_pool/internal/tp_driver_client_internal.h"
#include "tp_aeron_wrap.h"

enum { TP_CLIENT_DEFAULT_FRAGMENT_LIMIT = 10 };

static int tp_client_poll_control(void *clientd, int fragment_limit);
static int tp_client_poll_metadata(void *clientd, int fragment_limit);
static int tp_client_poll_qos(void *clientd, int fragment_limit);

static int tp_client_add_subscription(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    tp_client_subscription_kind_t kind)
{
    tp_async_add_subscription_t *async_add = NULL;
    int idle_count = 0;
    tp_subscription_t *subscription = NULL;

    if (NULL == channel || stream_id < 0)
    {
        return 0;
    }

    if (tp_client_async_add_subscription(
        client,
        channel,
        stream_id,
        &async_add) < 0)
    {
        return -1;
    }

    while (NULL == subscription)
    {
        if (tp_client_async_add_subscription_poll(&subscription, async_add) < 0)
        {
            return -1;
        }
        tp_client_conductor_do_work(client->conductor);
        if (++idle_count > 1000)
        {
            idle_count = 0;
        }
    }

    return tp_client_conductor_set_subscription(client->conductor, kind, subscription);
}

int tp_client_init(tp_client_t **client, tp_context_t *ctx)
{
    tp_client_t *instance = NULL;
    int result = -1;

    if (NULL == client || NULL == ctx)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_init: null input");
        return -1;
    }

    if (aeron_alloc((void **)&instance, sizeof(*instance)) < 0 || NULL == instance)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_client_init: allocation failed");
        return -1;
    }

    memset(instance, 0, sizeof(*instance));

    instance->context = ctx;
    instance->context->allowed_paths.canonical_paths = NULL;
    instance->context->allowed_paths.canonical_length = 0;

    if (tp_context_finalize_allowed_paths(instance->context) < 0)
    {
        aeron_free(instance);
        return -1;
    }

    instance->conductor = (tp_client_conductor_t *)calloc(1, sizeof(*instance->conductor));
    if (NULL == instance->conductor)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_client_init: conductor alloc failed");
        goto cleanup;
    }

    if (NULL != ctx->aeron)
    {
        if (tp_client_conductor_init_with_aeron(
            instance->conductor,
            ctx->aeron,
            ctx->use_agent_invoker,
            ctx->owns_aeron_client) < 0)
        {
            goto cleanup;
        }
    }
    else
    {
        if (tp_client_conductor_init_with_client_context(instance->conductor, ctx) < 0)
        {
            goto cleanup;
        }
    }

    result = 0;
    *client = instance;

cleanup:
    if (result < 0)
    {
        tp_context_clear_allowed_paths(instance->context);
        if (instance->conductor)
        {
            tp_client_conductor_close(instance->conductor);
            free(instance->conductor);
            instance->conductor = NULL;
        }
        aeron_free(instance);
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

    if (tp_client_conductor_start(client->conductor) < 0)
    {
        return -1;
    }

    if (!client->context->use_agent_invoker)
    {
        client->agent = (tp_client_conductor_agent_t *)calloc(1, sizeof(*client->agent));
        if (NULL == client->agent)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_client_start: agent alloc failed");
            return -1;
        }

        if (tp_client_conductor_agent_init(
            client->agent,
            client->conductor,
            client->context->idle_sleep_duration_ns) < 0)
        {
            free(client->agent);
            client->agent = NULL;
            return -1;
        }

        if (tp_client_conductor_agent_start(client->agent) < 0)
        {
            tp_client_conductor_agent_close(client->agent);
            free(client->agent);
            client->agent = NULL;
            return -1;
        }
    }

    if (tp_client_add_subscription(
        client,
        client->context->control_channel,
        client->context->control_stream_id,
        TP_CLIENT_SUB_CONTROL) < 0)
    {
        goto cleanup;
    }

    if (client->context->announce_channel[0] != '\0' &&
        client->context->announce_stream_id >= 0 &&
        (client->context->announce_stream_id != client->context->control_stream_id ||
            strcmp(client->context->announce_channel, client->context->control_channel) != 0))
    {
    if (tp_client_add_subscription(
        client,
        client->context->announce_channel,
        client->context->announce_stream_id,
        TP_CLIENT_SUB_ANNOUNCE) < 0)
    {
        goto cleanup;
    }
    }

    if (tp_client_add_subscription(
        client,
        client->context->qos_channel,
        client->context->qos_stream_id,
        TP_CLIENT_SUB_QOS) < 0)
    {
        goto cleanup;
    }

    if (tp_client_add_subscription(
        client,
        client->context->metadata_channel,
        client->context->metadata_stream_id,
        TP_CLIENT_SUB_METADATA) < 0)
    {
        goto cleanup;
    }

    if (tp_client_add_subscription(
        client,
        client->context->descriptor_channel,
        client->context->descriptor_stream_id,
        TP_CLIENT_SUB_DESCRIPTOR) < 0)
    {
        goto cleanup;
    }

    return 0;

cleanup:
    if (client->agent)
    {
        if (client->agent->runner)
        {
            tp_client_conductor_agent_stop(client->agent);
        }
        tp_client_conductor_agent_close(client->agent);
        free(client->agent);
        client->agent = NULL;
    }
    return -1;
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

    if (NULL != client->context->delegating_invoker)
    {
        client->context->delegating_invoker(client->context->delegating_invoker_clientd);
    }

    int work = tp_client_conductor_do_work(client->conductor);
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
            tp_log_emit(&client->context->log, TP_LOG_ERROR, "%s", tp_errmsg());
            if (client->context->error_handler)
            {
                client->context->error_handler(client->context->error_handler_clientd, tp_errcode(), tp_errmsg());
            }
            driver->active_lease_id = 0;
            return -1;
        }

        if (tp_driver_client_keepalive_due(driver, now_ns, client->context->keepalive_interval_ns) > 0)
        {
            if (tp_driver_keepalive(driver, now_ns) < 0)
            {
                tp_log_emit(&client->context->log, TP_LOG_ERROR, "%s", tp_errmsg());
                if (client->context->error_handler)
                {
                    client->context->error_handler(client->context->error_handler_clientd, tp_errcode(), tp_errmsg());
                }
                return -1;
            }
        }
    }

    return work;
}

int tp_client_main_do_work(tp_client_t *client)
{
    return tp_client_do_work(client);
}

int tp_client_idle(tp_client_t *client, int work_count)
{
    uint64_t sleep_ns;

    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_idle: null input");
        return -1;
    }

    sleep_ns = client->context->idle_sleep_duration_ns;
    aeron_idle_strategy_sleeping_idle(&sleep_ns, work_count);
    return 0;
}

int tp_client_close(tp_client_t *client)
{
    if (NULL == client)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_close: null input");
        return -1;
    }

    if (client->control_poller_registered)
    {
        tp_client_unregister_poller(client, tp_client_poll_control, client);
        client->control_poller_registered = false;
    }
    if (client->metadata_poller_registered)
    {
        tp_client_unregister_poller(client, tp_client_poll_metadata, client);
        client->metadata_poller_registered = false;
    }
    if (client->qos_poller_registered)
    {
        tp_client_unregister_poller(client, tp_client_poll_qos, client);
        client->qos_poller_registered = false;
    }

    if (client->control_poller)
    {
        tp_control_poller_t *poller = (tp_control_poller_t *)client->control_poller;
        tp_fragment_assembler_close(&poller->assembler);
        free(poller);
        client->control_poller = NULL;
    }
    if (client->metadata_poller)
    {
        tp_metadata_poller_t *poller = (tp_metadata_poller_t *)client->metadata_poller;
        tp_fragment_assembler_close(&poller->assembler);
        free(poller);
        client->metadata_poller = NULL;
    }
    if (client->qos_poller)
    {
        tp_qos_poller_t *poller = (tp_qos_poller_t *)client->qos_poller;
        tp_fragment_assembler_close(&poller->assembler);
        free(poller);
        client->qos_poller = NULL;
    }

    client->driver_clients = NULL;

    if (client->agent)
    {
        if (client->agent->runner)
        {
            tp_client_conductor_agent_stop(client->agent);
        }
        tp_client_conductor_agent_close(client->agent);
        free(client->agent);
        client->agent = NULL;
    }

    tp_context_clear_allowed_paths(client->context);
    if (client->conductor)
    {
        tp_client_conductor_close(client->conductor);
        free(client->conductor);
        client->conductor = NULL;
    }

    aeron_free(client);
    return 0;
}

tp_subscription_t *tp_client_control_subscription(tp_client_t *client)
{
    if (NULL == client || NULL == client->conductor)
    {
        return NULL;
    }

    return tp_client_conductor_get_subscription(client->conductor, TP_CLIENT_SUB_CONTROL);
}

tp_subscription_t *tp_client_announce_subscription(tp_client_t *client)
{
    if (NULL == client || NULL == client->conductor)
    {
        return NULL;
    }

    return tp_client_conductor_get_subscription(client->conductor, TP_CLIENT_SUB_ANNOUNCE);
}

tp_subscription_t *tp_client_qos_subscription(tp_client_t *client)
{
    if (NULL == client || NULL == client->conductor)
    {
        return NULL;
    }

    return tp_client_conductor_get_subscription(client->conductor, TP_CLIENT_SUB_QOS);
}

tp_subscription_t *tp_client_metadata_subscription(tp_client_t *client)
{
    if (NULL == client || NULL == client->conductor)
    {
        return NULL;
    }

    return tp_client_conductor_get_subscription(client->conductor, TP_CLIENT_SUB_METADATA);
}

tp_subscription_t *tp_client_descriptor_subscription(tp_client_t *client)
{
    if (NULL == client || NULL == client->conductor)
    {
        return NULL;
    }

    return tp_client_conductor_get_subscription(client->conductor, TP_CLIENT_SUB_DESCRIPTOR);
}

static int tp_client_poll_control(void *clientd, int fragment_limit)
{
    tp_client_t *client = (tp_client_t *)clientd;
    tp_control_poller_t *poller = NULL;

    if (NULL == client || NULL == client->control_poller)
    {
        return 0;
    }

    poller = (tp_control_poller_t *)client->control_poller;
    return tp_control_poll(poller, fragment_limit);
}

static int tp_client_poll_metadata(void *clientd, int fragment_limit)
{
    tp_client_t *client = (tp_client_t *)clientd;
    tp_metadata_poller_t *poller = NULL;

    if (NULL == client || NULL == client->metadata_poller)
    {
        return 0;
    }

    poller = (tp_metadata_poller_t *)client->metadata_poller;
    return tp_metadata_poll(poller, fragment_limit);
}

static int tp_client_poll_qos(void *clientd, int fragment_limit)
{
    tp_client_t *client = (tp_client_t *)clientd;
    tp_qos_poller_t *poller = NULL;

    if (NULL == client || NULL == client->qos_poller)
    {
        return 0;
    }

    poller = (tp_qos_poller_t *)client->qos_poller;
    return tp_qos_poll(poller, fragment_limit);
}

int tp_client_set_control_handlers(tp_client_t *client, const tp_control_handlers_t *handlers, int fragment_limit)
{
    tp_control_poller_t *poller = NULL;

    if (NULL == client || NULL == handlers || fragment_limit <= 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_set_control_handlers: invalid input");
        return -1;
    }

    if (NULL == client->control_poller)
    {
        poller = (tp_control_poller_t *)calloc(1, sizeof(*poller));
        if (NULL == poller)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_client_set_control_handlers: alloc failed");
            return -1;
        }
        if (tp_control_poller_init(poller, client, handlers) < 0)
        {
            free(poller);
            return -1;
        }
        client->control_poller = poller;
    }
    else
    {
        poller = (tp_control_poller_t *)client->control_poller;
        poller->handlers = *handlers;
    }

    if (client->control_poller_registered)
    {
        tp_client_unregister_poller(client, tp_client_poll_control, client);
        client->control_poller_registered = false;
    }

    if (tp_client_register_poller(client, tp_client_poll_control, client, fragment_limit) < 0)
    {
        return -1;
    }
    client->control_poller_registered = true;
    return 0;
}

int tp_client_set_metadata_handlers(tp_client_t *client, const tp_metadata_handlers_t *handlers, int fragment_limit)
{
    tp_metadata_poller_t *poller = NULL;

    if (NULL == client || NULL == handlers || fragment_limit <= 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_set_metadata_handlers: invalid input");
        return -1;
    }

    if (NULL == client->metadata_poller)
    {
        poller = (tp_metadata_poller_t *)calloc(1, sizeof(*poller));
        if (NULL == poller)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_client_set_metadata_handlers: alloc failed");
            return -1;
        }
        if (tp_metadata_poller_init(poller, client, handlers) < 0)
        {
            free(poller);
            return -1;
        }
        client->metadata_poller = poller;
    }
    else
    {
        poller = (tp_metadata_poller_t *)client->metadata_poller;
        poller->handlers = *handlers;
    }

    if (client->metadata_poller_registered)
    {
        tp_client_unregister_poller(client, tp_client_poll_metadata, client);
        client->metadata_poller_registered = false;
    }

    if (tp_client_register_poller(client, tp_client_poll_metadata, client, fragment_limit) < 0)
    {
        return -1;
    }
    client->metadata_poller_registered = true;
    return 0;
}

int tp_client_set_qos_handlers(tp_client_t *client, const tp_qos_handlers_t *handlers, int fragment_limit)
{
    tp_qos_poller_t *poller = NULL;

    if (NULL == client || NULL == handlers || fragment_limit <= 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_set_qos_handlers: invalid input");
        return -1;
    }

    if (NULL == client->qos_poller)
    {
        poller = (tp_qos_poller_t *)calloc(1, sizeof(*poller));
        if (NULL == poller)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_client_set_qos_handlers: alloc failed");
            return -1;
        }
        if (tp_qos_poller_init(poller, client, handlers) < 0)
        {
            free(poller);
            return -1;
        }
        client->qos_poller = poller;
    }
    else
    {
        poller = (tp_qos_poller_t *)client->qos_poller;
        poller->handlers = *handlers;
    }

    if (client->qos_poller_registered)
    {
        tp_client_unregister_poller(client, tp_client_poll_qos, client);
        client->qos_poller_registered = false;
    }

    if (tp_client_register_poller(client, tp_client_poll_qos, client, fragment_limit) < 0)
    {
        return -1;
    }
    client->qos_poller_registered = true;
    return 0;
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

int tp_client_register_poller(tp_client_t *client, tp_client_poller_t poller, void *clientd, int fragment_limit)
{
    if (NULL == client || NULL == poller)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_register_poller: invalid input");
        return -1;
    }

    return tp_client_conductor_register_poller(client->conductor, poller, clientd, fragment_limit);
}

int tp_client_unregister_poller(tp_client_t *client, tp_client_poller_t poller, void *clientd)
{
    if (NULL == client || NULL == poller)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_unregister_poller: invalid input");
        return -1;
    }

    return tp_client_conductor_unregister_poller(client->conductor, poller, clientd);
}

int tp_client_async_add_publication(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    tp_async_add_publication_t **out)
{
    if (NULL == client || NULL == channel || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_async_add_publication: null input");
        return -1;
    }

    return tp_client_conductor_async_add_publication(out, client->conductor, channel, stream_id);
}

int tp_client_async_add_publication_poll(
    tp_publication_t **publication,
    tp_async_add_publication_t *async_add)
{
    return tp_client_conductor_async_add_publication_poll(publication, async_add);
}

int tp_client_async_add_subscription(
    tp_client_t *client,
    const char *channel,
    int32_t stream_id,
    tp_async_add_subscription_t **out)
{
    if (NULL == client || NULL == channel || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_async_add_subscription: null input");
        return -1;
    }

    return tp_client_conductor_async_add_subscription(out, client->conductor, channel, stream_id);
}

int tp_client_async_add_subscription_poll(
    tp_subscription_t **subscription,
    tp_async_add_subscription_t *async_add)
{
    return tp_client_conductor_async_add_subscription_poll(subscription, async_add);
}
