#include "tensor_pool/tp_client_conductor.h"

#include "tensor_pool/tp_client.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

static int tp_client_conductor_apply_context(
    aeron_context_t *aeron_ctx,
    const tp_client_context_t *context)
{
    if (NULL == aeron_ctx || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_apply_context: null input");
        return -1;
    }

    if (context->base.aeron_dir[0] != '\0')
    {
        if (aeron_context_set_dir(aeron_ctx, context->base.aeron_dir) < 0)
        {
            return -1;
        }
    }

    if (context->client_name[0] != '\0')
    {
        if (aeron_context_set_client_name(aeron_ctx, context->client_name) < 0)
        {
            return -1;
        }
    }

    if (context->error_handler != NULL)
    {
        if (aeron_context_set_error_handler(aeron_ctx, context->error_handler, context->error_handler_clientd) < 0)
        {
            return -1;
        }
    }

    if (context->driver_timeout_ns > 0)
    {
        if (aeron_context_set_driver_timeout_ms(aeron_ctx, context->driver_timeout_ns / 1000000u) < 0)
        {
            return -1;
        }
    }

    if (context->keepalive_interval_ns > 0)
    {
        if (aeron_context_set_keepalive_interval_ns(aeron_ctx, context->keepalive_interval_ns) < 0)
        {
            return -1;
        }
    }

    if (context->idle_sleep_duration_ns > 0)
    {
        if (aeron_context_set_idle_sleep_duration_ns(aeron_ctx, context->idle_sleep_duration_ns) < 0)
        {
            return -1;
        }
    }

    if (aeron_context_set_use_conductor_agent_invoker(aeron_ctx, context->use_agent_invoker) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_client_conductor_init(
    tp_client_conductor_t *conductor,
    const tp_context_t *context,
    bool use_agent_invoker)
{
    tp_client_context_t shim;

    if (NULL == conductor || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_init: null input");
        return -1;
    }

    memset(&shim, 0, sizeof(shim));
    shim.base = *context;
    shim.use_agent_invoker = use_agent_invoker;

    return tp_client_conductor_init_with_client_context(conductor, &shim);
}

int tp_client_conductor_init_with_client_context(
    tp_client_conductor_t *conductor,
    const tp_client_context_t *context)
{
    if (NULL == conductor || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_init_with_client_context: null input");
        return -1;
    }

    memset(conductor, 0, sizeof(*conductor));

    if (aeron_context_init(&conductor->aeron.context) < 0)
    {
        return -1;
    }

    if (tp_client_conductor_apply_context(conductor->aeron.context, context) < 0)
    {
        aeron_context_close(conductor->aeron.context);
        conductor->aeron.context = NULL;
        return -1;
    }

    if (aeron_init(&conductor->aeron.aeron, conductor->aeron.context) < 0)
    {
        aeron_context_close(conductor->aeron.context);
        conductor->aeron.context = NULL;
        return -1;
    }

    conductor->use_agent_invoker = context->use_agent_invoker;
    conductor->started = false;
    conductor->owns_aeron = true;

    return 0;
}

int tp_client_conductor_init_with_aeron(
    tp_client_conductor_t *conductor,
    aeron_t *aeron,
    bool use_agent_invoker,
    bool owns_aeron)
{
    if (NULL == conductor || NULL == aeron)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_init_with_aeron: null input");
        return -1;
    }

    memset(conductor, 0, sizeof(*conductor));
    conductor->aeron.aeron = aeron;
    conductor->aeron.context = aeron_context(aeron);
    conductor->use_agent_invoker = use_agent_invoker;
    conductor->started = true;
    conductor->owns_aeron = owns_aeron;

    return 0;
}

int tp_client_conductor_start(tp_client_conductor_t *conductor)
{
    if (NULL == conductor || NULL == conductor->aeron.aeron)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_start: invalid input");
        return -1;
    }

    if (conductor->started)
    {
        return 0;
    }

    if (aeron_start(conductor->aeron.aeron) < 0)
    {
        return -1;
    }

    conductor->started = true;
    return 0;
}

int tp_client_conductor_close(tp_client_conductor_t *conductor)
{
    if (NULL == conductor)
    {
        return -1;
    }

    if (NULL != conductor->aeron.aeron && conductor->owns_aeron)
    {
        aeron_close(conductor->aeron.aeron);
    }
    conductor->aeron.aeron = NULL;

    if (NULL != conductor->aeron.context && conductor->owns_aeron)
    {
        aeron_context_close(conductor->aeron.context);
    }
    conductor->aeron.context = NULL;

    conductor->started = false;
    return 0;
}

int tp_client_conductor_do_work(tp_client_conductor_t *conductor)
{
    if (NULL == conductor || NULL == conductor->aeron.aeron)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_do_work: invalid input");
        return -1;
    }

    if (!conductor->use_agent_invoker)
    {
        return 0;
    }

    return aeron_main_do_work(conductor->aeron.aeron);
}

int tp_client_conductor_set_idle_sleep_duration_ns(tp_client_conductor_t *conductor, uint64_t sleep_ns)
{
    if (NULL == conductor || NULL == conductor->aeron.context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_set_idle_sleep_duration_ns: invalid input");
        return -1;
    }

    return aeron_context_set_idle_sleep_duration_ns(conductor->aeron.context, sleep_ns);
}

int tp_client_conductor_async_add_publication(
    aeron_async_add_publication_t **async,
    tp_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id)
{
    if (NULL == async || NULL == conductor || NULL == conductor->aeron.aeron || NULL == channel)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_async_add_publication: invalid input");
        return -1;
    }

    return aeron_async_add_publication(async, conductor->aeron.aeron, channel, stream_id);
}

int tp_client_conductor_async_add_publication_poll(
    aeron_publication_t **publication,
    aeron_async_add_publication_t *async)
{
    if (NULL == publication || NULL == async)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_async_add_publication_poll: invalid input");
        return -1;
    }

    return aeron_async_add_publication_poll(publication, async);
}

int tp_client_conductor_async_add_subscription(
    aeron_async_add_subscription_t **async,
    tp_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id,
    aeron_on_available_image_t on_available,
    void *available_clientd,
    aeron_on_unavailable_image_t on_unavailable,
    void *unavailable_clientd)
{
    if (NULL == async || NULL == conductor || NULL == conductor->aeron.aeron || NULL == channel)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_async_add_subscription: invalid input");
        return -1;
    }

    return aeron_async_add_subscription(
        async,
        conductor->aeron.aeron,
        channel,
        stream_id,
        on_available,
        available_clientd,
        on_unavailable,
        unavailable_clientd);
}

int tp_client_conductor_async_add_subscription_poll(
    aeron_subscription_t **subscription,
    aeron_async_add_subscription_t *async)
{
    if (NULL == subscription || NULL == async)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_async_add_subscription_poll: invalid input");
        return -1;
    }

    return aeron_async_add_subscription_poll(subscription, async);
}
