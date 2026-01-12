#include "tensor_pool/tp_metadata_poller.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

static void tp_metadata_poller_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_metadata_poller_t *poller = (tp_metadata_poller_t *)clientd;
    tp_data_source_announce_view_t announce;
    tp_data_source_meta_view_t meta;

    (void)header;

    if (NULL == poller || NULL == buffer)
    {
        return;
    }

    if (poller->handlers.on_data_source_announce &&
        tp_control_decode_data_source_announce(buffer, length, &announce) == 0)
    {
        poller->handlers.on_data_source_announce(&announce, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.on_data_source_meta_begin ||
        poller->handlers.on_data_source_meta_attr ||
        poller->handlers.on_data_source_meta_end)
    {
        if (tp_control_decode_data_source_meta(
            buffer,
            length,
            &meta,
            poller->handlers.on_data_source_meta_attr,
            poller->handlers.clientd) == 0)
        {
            if (poller->handlers.on_data_source_meta_begin)
            {
                poller->handlers.on_data_source_meta_begin(&meta, poller->handlers.clientd);
            }
            if (poller->handlers.on_data_source_meta_end)
            {
                poller->handlers.on_data_source_meta_end(&meta, poller->handlers.clientd);
            }
        }
    }
}

int tp_metadata_poller_init(tp_metadata_poller_t *poller, tp_client_t *client, const tp_metadata_handlers_t *handlers)
{
    if (NULL == poller || NULL == client || NULL == client->metadata_subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_poller_init: invalid input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->client = client;
    if (handlers)
    {
        poller->handlers = *handlers;
    }

    if (aeron_fragment_assembler_create(&poller->assembler, tp_metadata_poller_handler, poller) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_metadata_poll(tp_metadata_poller_t *poller, int fragment_limit)
{
    if (NULL == poller || NULL == poller->assembler || NULL == poller->client || NULL == poller->client->metadata_subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_poll: poller not initialized");
        return -1;
    }

    return aeron_subscription_poll(
        poller->client->metadata_subscription,
        aeron_fragment_assembler_handler,
        poller->assembler,
        fragment_limit);
}
