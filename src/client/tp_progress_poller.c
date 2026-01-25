#include "tensor_pool/internal/tp_progress_poller.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/internal/tp_consumer_internal.h"
#include "tensor_pool/tp_error.h"
#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/messageHeader.h"

static int tp_progress_poller_resize(tp_progress_poller_t *poller, size_t capacity);

static int tp_progress_poller_validate(tp_progress_poller_t *poller, const tp_frame_progress_t *view)
{
    size_t i;
    int validation = 0;

    if (NULL == poller || NULL == view)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_validate: null input");
        return -1;
    }

    if (poller->max_payload_bytes > 0 && view->payload_bytes_filled > poller->max_payload_bytes)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_validate: payload bytes exceed max");
        return -1;
    }

    if (poller->header_nslots > 0 && poller->tracker_capacity < poller->header_nslots)
    {
        if (tp_progress_poller_resize(poller, poller->header_nslots) < 0)
        {
            return -1;
        }
    }

    if (NULL == poller->tracker || poller->tracker_capacity == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_validate: tracker unavailable");
        return -1;
    }

    if (poller->header_nslots > 0)
    {
        if (poller->tracker_capacity < poller->header_nslots)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_validate: tracker capacity too small");
            return -1;
        }

        size_t index = (size_t)(view->seq & (poller->header_nslots - 1));
        tp_progress_tracker_entry_t *entry = &poller->tracker[index];

        if (entry->in_use &&
            entry->seq == view->seq &&
            entry->stream_id == view->stream_id &&
            entry->epoch == view->epoch)
        {
            if (view->payload_bytes_filled < entry->last_bytes)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_validate: payload bytes regressed");
                return -1;
            }
            if (poller->validator)
            {
                validation = poller->validator(poller->validator_clientd, view);
                if (validation != 0)
                {
                    return -1;
                }
            }
            entry->last_bytes = view->payload_bytes_filled;
            return 0;
        }

        if (poller->validator)
        {
            validation = poller->validator(poller->validator_clientd, view);
            if (validation != 0)
            {
                return -1;
            }
        }

        entry->stream_id = view->stream_id;
        entry->epoch = view->epoch;
        entry->seq = view->seq;
        entry->last_bytes = view->payload_bytes_filled;
        entry->in_use = 1;
        return 0;
    }

    for (i = 0; i < poller->tracker_capacity; i++)
    {
        tp_progress_tracker_entry_t *entry = &poller->tracker[i];
        if (entry->in_use &&
            entry->seq == view->seq &&
            entry->stream_id == view->stream_id &&
            entry->epoch == view->epoch)
        {
            if (view->payload_bytes_filled < entry->last_bytes)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_validate: payload bytes regressed");
                return -1;
            }
            if (poller->validator)
            {
                validation = poller->validator(poller->validator_clientd, view);
                if (validation != 0)
                {
                    return -1;
                }
            }
            entry->last_bytes = view->payload_bytes_filled;
            return 0;
        }
    }

    if (poller->validator)
    {
        validation = poller->validator(poller->validator_clientd, view);
        if (validation != 0)
        {
            return -1;
        }
    }

    poller->tracker[poller->tracker_cursor].stream_id = view->stream_id;
    poller->tracker[poller->tracker_cursor].epoch = view->epoch;
    poller->tracker[poller->tracker_cursor].seq = view->seq;
    poller->tracker[poller->tracker_cursor].last_bytes = view->payload_bytes_filled;
    poller->tracker[poller->tracker_cursor].in_use = 1;
    poller->tracker_cursor = (poller->tracker_cursor + 1) % poller->tracker_capacity;
    return 0;
}

static int tp_progress_poller_resize(tp_progress_poller_t *poller, size_t capacity)
{
    tp_progress_tracker_entry_t *next = NULL;
    size_t i;

    if (NULL == poller || capacity == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_resize: invalid input");
        return -1;
    }

    if (poller->tracker_capacity >= capacity)
    {
        return 0;
    }

    next = calloc(capacity, sizeof(*next));
    if (NULL == next)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_progress_poller_resize: allocation failed");
        return -1;
    }

    if (poller->tracker)
    {
        for (i = 0; i < poller->tracker_capacity; i++)
        {
            next[i] = poller->tracker[i];
        }
        free(poller->tracker);
    }

    poller->tracker = next;
    poller->tracker_capacity = capacity;
    if (poller->tracker_cursor >= poller->tracker_capacity)
    {
        poller->tracker_cursor = 0;
    }

    return 0;
}

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
    view.stream_id = tensor_pool_frameProgress_streamId(&progress);
    view.epoch = tensor_pool_frameProgress_epoch(&progress);
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

    if (tp_progress_poller_validate(poller, &view) != 0)
    {
        return;
    }

    if (poller->handlers.on_progress)
    {
        poller->handlers.on_progress(poller->handlers.clientd, &view);
    }
}

int tp_progress_poller_init(tp_progress_poller_t *poller, tp_client_t *client, const tp_progress_handlers_t *handlers)
{
    if (NULL == poller || NULL == client || NULL == tp_client_control_subscription(client))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_init: invalid input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->client = client;
    poller->subscription = tp_client_control_subscription(client);
    poller->header_nslots = 0;

    if (handlers)
    {
        poller->handlers = *handlers;
    }

    poller->tracker = calloc(TP_PROGRESS_TRACKER_CAPACITY, sizeof(*poller->tracker));
    if (NULL == poller->tracker)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_progress_poller_init: tracker allocation failed");
        return -1;
    }
    poller->tracker_capacity = TP_PROGRESS_TRACKER_CAPACITY;
    poller->tracker_cursor = 0;

    if (tp_fragment_assembler_create(&poller->assembler, tp_progress_poller_handler, poller) < 0)
    {
        free(poller->tracker);
        poller->tracker = NULL;
        poller->tracker_capacity = 0;
        return -1;
    }

    return 0;
}

int tp_progress_poller_init_with_subscription(
    tp_progress_poller_t *poller,
    tp_subscription_t *subscription,
    const tp_progress_handlers_t *handlers)
{
    if (NULL == poller || NULL == subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poller_init_with_subscription: invalid input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->subscription = subscription;
    poller->header_nslots = 0;
    if (handlers)
    {
        poller->handlers = *handlers;
    }

    poller->tracker = calloc(TP_PROGRESS_TRACKER_CAPACITY, sizeof(*poller->tracker));
    if (NULL == poller->tracker)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_progress_poller_init_with_subscription: tracker allocation failed");
        return -1;
    }
    poller->tracker_capacity = TP_PROGRESS_TRACKER_CAPACITY;
    poller->tracker_cursor = 0;

    if (tp_fragment_assembler_create(&poller->assembler, tp_progress_poller_handler, poller) < 0)
    {
        free(poller->tracker);
        poller->tracker = NULL;
        poller->tracker_capacity = 0;
        return -1;
    }

    return 0;
}

void tp_progress_poller_set_max_payload_bytes(tp_progress_poller_t *poller, uint64_t max_payload_bytes)
{
    if (NULL == poller)
    {
        return;
    }

    poller->max_payload_bytes = max_payload_bytes;
}

static int tp_progress_validate_consumer(void *clientd, const tp_frame_progress_t *progress)
{
    const tp_consumer_t *consumer = (const tp_consumer_t *)clientd;

    return tp_consumer_validate_progress(consumer, progress);
}

void tp_progress_poller_set_validator(
    tp_progress_poller_t *poller,
    tp_progress_validator_t validator,
    void *clientd)
{
    if (NULL == poller)
    {
        return;
    }

    poller->validator = validator;
    poller->validator_clientd = clientd;
}

void tp_progress_poller_set_consumer(tp_progress_poller_t *poller, tp_consumer_t *consumer)
{
    if (NULL == poller)
    {
        return;
    }

    poller->validator = tp_progress_validate_consumer;
    poller->validator_clientd = consumer;

    if (consumer && consumer->header_nslots > 0)
    {
        poller->header_nslots = consumer->header_nslots;
        if (tp_progress_poller_resize(poller, consumer->header_nslots) < 0)
        {
            if (poller->tracker)
            {
                free(poller->tracker);
                poller->tracker = NULL;
            }
            poller->tracker_capacity = 0;
            poller->tracker_cursor = 0;
            poller->header_nslots = 0;
        }
    }
    else
    {
        poller->header_nslots = 0;
    }
}

int tp_progress_poll(tp_progress_poller_t *poller, int fragment_limit)
{
    tp_subscription_t *subscription = NULL;

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
        subscription = tp_client_control_subscription(poller->client);
    }

    if (NULL == subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_progress_poll: subscription not available");
        return -1;
    }

    return aeron_subscription_poll(
        tp_subscription_handle(subscription),
        aeron_fragment_assembler_handler,
        tp_fragment_assembler_handle(poller->assembler),
        fragment_limit);
}

#if defined(TP_ENABLE_FUZZ) || defined(TP_TESTING)
void tp_progress_poller_handle_fragment(tp_progress_poller_t *poller, const uint8_t *buffer, size_t length)
{
    tp_progress_poller_handler(poller, buffer, length, NULL);
}
#endif
