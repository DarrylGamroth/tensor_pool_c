#include "tensor_pool/tp_aeron.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"
#include "aeron_agent.h"

int tp_aeron_client_init(tp_aeron_client_t *client, const tp_context_t *context)
{
    if (NULL == client || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_aeron_client_init: null input");
        return -1;
    }

    memset(client, 0, sizeof(*client));

    if (aeron_context_init(&client->context) < 0)
    {
        return -1;
    }

    if (context->aeron_dir[0] != '\0')
    {
        if (aeron_context_set_dir(client->context, context->aeron_dir) < 0)
        {
            aeron_context_close(client->context);
            client->context = NULL;
            return -1;
        }
    }

    if (aeron_init(&client->aeron, client->context) < 0)
    {
        aeron_context_close(client->context);
        client->context = NULL;
        return -1;
    }

    if (aeron_start(client->aeron) < 0)
    {
        aeron_close(client->aeron);
        client->aeron = NULL;
        aeron_context_close(client->context);
        client->context = NULL;
        return -1;
    }

    return 0;
}

int tp_aeron_client_close(tp_aeron_client_t *client)
{
    if (NULL == client)
    {
        return -1;
    }

    if (NULL != client->aeron)
    {
        aeron_close(client->aeron);
        client->aeron = NULL;
    }

    if (NULL != client->context)
    {
        aeron_context_close(client->context);
        client->context = NULL;
    }

    return 0;
}

int tp_aeron_add_publication(
    aeron_publication_t **pub,
    tp_aeron_client_t *client,
    const char *channel,
    int32_t stream_id)
{
    aeron_async_add_publication_t *async_add = NULL;
    uint64_t sleep_ns = 1;

    if (NULL == pub || NULL == client || NULL == client->aeron || NULL == channel)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_aeron_add_publication: null input");
        return -1;
    }

    if (aeron_async_add_publication(&async_add, client->aeron, channel, stream_id) < 0)
    {
        return -1;
    }

    *pub = NULL;
    while (NULL == *pub)
    {
        int work = aeron_async_add_publication_poll(pub, async_add);
        if (work < 0)
        {
            return -1;
        }
        aeron_idle_strategy_sleeping_idle(&sleep_ns, work);
    }

    return 0;
}

int tp_aeron_add_subscription(
    aeron_subscription_t **sub,
    tp_aeron_client_t *client,
    const char *channel,
    int32_t stream_id,
    aeron_on_available_image_t on_available,
    void *available_clientd,
    aeron_on_unavailable_image_t on_unavailable,
    void *unavailable_clientd)
{
    aeron_async_add_subscription_t *async_add = NULL;
    uint64_t sleep_ns = 1;

    if (NULL == sub || NULL == client || NULL == client->aeron || NULL == channel)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_aeron_add_subscription: null input");
        return -1;
    }

    if (aeron_async_add_subscription(
        &async_add,
        client->aeron,
        channel,
        stream_id,
        on_available,
        available_clientd,
        on_unavailable,
        unavailable_clientd) < 0)
    {
        return -1;
    }

    *sub = NULL;
    while (NULL == *sub)
    {
        int work = aeron_async_add_subscription_poll(sub, async_add);
        if (work < 0)
        {
            return -1;
        }
        aeron_idle_strategy_sleeping_idle(&sleep_ns, work);
    }

    return 0;
}
