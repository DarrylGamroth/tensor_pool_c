#include "tensor_pool/tp_progress_poller.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/messageHeader.h"

static void tp_progress_poller_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_progress_poller_t *poller = (tp_progress_poller_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameProgress progress;
    uint16_t template_id;
    uint16_t schema_id;
    tp_frame_progress_t view;
    enum tensor_pool_frameProgressState state = tensor_pool_frameProgressState_UNKNOWN;

    (void)header;

    if (NULL == poller || NULL == buffer || length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);

    if (schema_id != tensor_pool_frameProgress_sbe_schema_id() ||
        template_id != tensor_pool_frameProgress_sbe_template_id())
    {
        return;
    }

    tensor_pool_frameProgress_wrap_for_decode(
        &progress,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        tensor_pool_frameProgress_sbe_block_length(),
        tensor_pool_frameProgress_sbe_schema_version(),
        length);

    memset(&view, 0, sizeof(view));
    view.seq = tensor_pool_frameProgress_seq(&progress);
    view.payload_bytes_filled = tensor_pool_frameProgress_payloadBytesFilled(&progress);
    if (tensor_pool_frameProgress_state(&progress, &state))
    {
        view.state = (tp_progress_state_t)state;
    }
    else
    {
        view.state = TP_PROGRESS_UNKNOWN;
    }

    if (poller->handlers.on_progress)
    {
        poller->handlers.on_progress(poller->handlers.clientd, &view);
    }
}

int tp_progress_poller_init(tp_progress_poller_t *poller, tp_client_t *client, const tp_progress_handlers_t *handlers)
{
    if (NULL == poller || NULL == client || NULL == client->control_subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_init: invalid input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->client = client;
    poller->subscription = client->control_subscription;

    if (handlers)
    {
        poller->handlers = *handlers;
    }

    if (aeron_fragment_assembler_create(&poller->assembler, tp_progress_poller_handler, poller) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_progress_poller_init_with_subscription(
    tp_progress_poller_t *poller,
    aeron_subscription_t *subscription,
    const tp_progress_handlers_t *handlers)
{
    if (NULL == poller || NULL == subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_init_with_subscription: invalid input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->subscription = subscription;
    if (handlers)
    {
        poller->handlers = *handlers;
    }

    if (aeron_fragment_assembler_create(&poller->assembler, tp_progress_poller_handler, poller) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_progress_poll(tp_progress_poller_t *poller, int fragment_limit)
{
    aeron_subscription_t *subscription = NULL;

    if (NULL == poller || NULL == poller->assembler)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poll: poller not initialized");
        return -1;
    }

    if (poller->subscription)
    {
        subscription = poller->subscription;
    }
    else if (poller->client)
    {
        subscription = poller->client->control_subscription;
    }

    if (NULL == subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poll: subscription not available");
        return -1;
    }

    return aeron_subscription_poll(
        subscription,
        aeron_fragment_assembler_handler,
        poller->assembler,
        fragment_limit);
}
