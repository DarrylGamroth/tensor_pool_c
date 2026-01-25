#include "tensor_pool/tp_aeron.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"
#include "tp_aeron_wrap.h"
#include "aeron_agent.h"

int tp_aeron_client_init(tp_aeron_client_t *client, const tp_context_t *context)
{
    if (NULL == client || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_aeron_client_init: null input");
        return -1;
    }

    memset(client, 0, sizeof(*client));

    aeron_context_t *ctx = NULL;
    aeron_t *aeron = NULL;

    if (aeron_context_init(&ctx) < 0)
    {
        return -1;
    }

    if (context->aeron_dir[0] != '\0')
    {
        if (aeron_context_set_dir(ctx, context->aeron_dir) < 0)
        {
            aeron_context_close(ctx);
            return -1;
        }
    }

    if (aeron_init(&aeron, ctx) < 0)
    {
        aeron_context_close(ctx);
        return -1;
    }

    if (aeron_start(aeron) < 0)
    {
        aeron_close(aeron);
        aeron_context_close(ctx);
        return -1;
    }

    client->context = ctx;
    client->aeron = aeron;
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
        aeron_close((aeron_t *)client->aeron);
        client->aeron = NULL;
    }

    if (NULL != client->context)
    {
        aeron_context_close((aeron_context_t *)client->context);
        client->context = NULL;
    }

    return 0;
}

int tp_aeron_add_publication(
    tp_publication_t **pub,
    tp_aeron_client_t *client,
    const char *channel,
    int32_t stream_id)
{
    aeron_async_add_publication_t *async_add = NULL;
    uint64_t sleep_ns = 1;
    aeron_publication_t *raw_pub = NULL;
    aeron_t *aeron = NULL;

    if (NULL == pub || NULL == client || NULL == client->aeron || NULL == channel)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_aeron_add_publication: null input");
        return -1;
    }

    aeron = (aeron_t *)client->aeron;
    if (aeron_async_add_publication(&async_add, aeron, channel, stream_id) < 0)
    {
        return -1;
    }

    *pub = NULL;
    while (NULL == *pub)
    {
        int work = aeron_async_add_publication_poll(&raw_pub, async_add);
        if (work < 0)
        {
            return -1;
        }
        if (raw_pub && tp_publication_wrap(pub, raw_pub) < 0)
        {
            aeron_publication_close(raw_pub, NULL, NULL);
            return -1;
        }
        aeron_idle_strategy_sleeping_idle(&sleep_ns, work);
    }

    return 0;
}

int tp_aeron_add_subscription(
    tp_subscription_t **sub,
    tp_aeron_client_t *client,
    const char *channel,
    int32_t stream_id)
{
    aeron_async_add_subscription_t *async_add = NULL;
    uint64_t sleep_ns = 1;
    aeron_subscription_t *raw_sub = NULL;
    aeron_t *aeron = NULL;

    if (NULL == sub || NULL == client || NULL == client->aeron || NULL == channel)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_aeron_add_subscription: null input");
        return -1;
    }

    aeron = (aeron_t *)client->aeron;
    if (aeron_async_add_subscription(
        &async_add,
        aeron,
        channel,
        stream_id,
        NULL,
        NULL,
        NULL,
        NULL) < 0)
    {
        return -1;
    }

    *sub = NULL;
    while (NULL == *sub)
    {
        int work = aeron_async_add_subscription_poll(&raw_sub, async_add);
        if (work < 0)
        {
            return -1;
        }
        if (raw_sub && tp_subscription_wrap(sub, raw_sub) < 0)
        {
            aeron_subscription_close(raw_sub, NULL, NULL);
            return -1;
        }
        aeron_idle_strategy_sleeping_idle(&sleep_ns, work);
    }

    return 0;
}
