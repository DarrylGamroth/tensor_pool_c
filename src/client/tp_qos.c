#include "tensor_pool/internal/tp_qos.h"

#include <errno.h>

#include "tensor_pool/tp_error.h"
#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/mode.h"
#include "wire/tensor_pool/qosConsumer.h"
#include "wire/tensor_pool/qosProducer.h"

int tp_qos_publish_producer(tp_producer_t *producer, uint64_t current_seq, uint32_t watermark)
{
    uint8_t buffer[128];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_qosProducer qos;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_qosProducer_sbe_block_length();
    int64_t result;

    if (NULL == producer || NULL == producer->qos_publication)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_qos_publish_producer: qos publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_qosProducer_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_qosProducer_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_qosProducer_sbe_schema_version());

    tensor_pool_qosProducer_wrap_for_encode(&qos, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_qosProducer_set_streamId(&qos, producer->stream_id);
    tensor_pool_qosProducer_set_producerId(&qos, producer->producer_id);
    tensor_pool_qosProducer_set_epoch(&qos, producer->epoch);
    tensor_pool_qosProducer_set_currentSeq(&qos, current_seq);
    tensor_pool_qosProducer_set_watermark(&qos, watermark);

    result = aeron_publication_offer(
        tp_publication_handle(producer->qos_publication),
        buffer,
        header_len + body_len,
        NULL,
        NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

int tp_qos_publish_consumer(
    tp_consumer_t *consumer,
    uint32_t consumer_id,
    uint64_t last_seq_seen,
    uint64_t drops_gap,
    uint64_t drops_late,
    tp_mode_t mode)
{
    uint8_t buffer[128];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_qosConsumer qos;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_qosConsumer_sbe_block_length();
    int64_t result;

    if (NULL == consumer || NULL == consumer->qos_publication)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_qos_publish_consumer: qos publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_qosConsumer_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_qosConsumer_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_qosConsumer_sbe_schema_version());

    tensor_pool_qosConsumer_wrap_for_encode(&qos, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_qosConsumer_set_streamId(&qos, consumer->stream_id);
    tensor_pool_qosConsumer_set_consumerId(&qos, consumer_id);
    tensor_pool_qosConsumer_set_epoch(&qos, consumer->epoch);
    tensor_pool_qosConsumer_set_lastSeqSeen(&qos, last_seq_seen);
    tensor_pool_qosConsumer_set_dropsGap(&qos, drops_gap);
    tensor_pool_qosConsumer_set_dropsLate(&qos, drops_late);
    tensor_pool_qosConsumer_set_mode(&qos, (enum tensor_pool_mode)mode);

    result = aeron_publication_offer(
        tp_publication_handle(consumer->qos_publication),
        buffer,
        header_len + body_len,
        NULL,
        NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

static void tp_qos_poller_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_qos_poller_t *poller = (tp_qos_poller_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    uint16_t template_id;
    uint16_t schema_id;
    tp_qos_event_t event;

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

    if (schema_id != tensor_pool_qosProducer_sbe_schema_id())
    {
        return;
    }

    memset(&event, 0, sizeof(event));

    if (template_id == tensor_pool_qosProducer_sbe_template_id())
    {
        struct tensor_pool_qosProducer qos;
        tensor_pool_qosProducer_wrap_for_decode(
            &qos,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            tensor_pool_qosProducer_sbe_block_length(),
            tensor_pool_qosProducer_sbe_schema_version(),
            length);
        event.type = TP_QOS_EVENT_PRODUCER;
        event.stream_id = tensor_pool_qosProducer_streamId(&qos);
        event.producer_id = tensor_pool_qosProducer_producerId(&qos);
        event.epoch = tensor_pool_qosProducer_epoch(&qos);
        event.current_seq = tensor_pool_qosProducer_currentSeq(&qos);
        event.watermark = tensor_pool_qosProducer_watermark(&qos);
    }
    else if (template_id == tensor_pool_qosConsumer_sbe_template_id())
    {
        struct tensor_pool_qosConsumer qos;
        enum tensor_pool_mode mode = tensor_pool_mode_NULL_VALUE;
        tensor_pool_qosConsumer_wrap_for_decode(
            &qos,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            tensor_pool_qosConsumer_sbe_block_length(),
            tensor_pool_qosConsumer_sbe_schema_version(),
            length);
        event.type = TP_QOS_EVENT_CONSUMER;
        event.stream_id = tensor_pool_qosConsumer_streamId(&qos);
        event.consumer_id = tensor_pool_qosConsumer_consumerId(&qos);
        event.epoch = tensor_pool_qosConsumer_epoch(&qos);
        event.last_seq_seen = tensor_pool_qosConsumer_lastSeqSeen(&qos);
        event.drops_gap = tensor_pool_qosConsumer_dropsGap(&qos);
        event.drops_late = tensor_pool_qosConsumer_dropsLate(&qos);
        if (tensor_pool_qosConsumer_mode(&qos, &mode))
        {
            event.mode = (tp_mode_t)mode;
        }
        else
        {
            event.mode = TP_MODE_NULL;
        }
    }
    else
    {
        return;
    }

    if (poller->handlers.on_qos_event)
    {
        poller->handlers.on_qos_event(poller->handlers.clientd, &event);
    }
}

#if defined(TP_ENABLE_FUZZ) || defined(TP_TESTING)
void tp_qos_poller_handle_fragment(tp_qos_poller_t *poller, const uint8_t *buffer, size_t length)
{
    tp_qos_poller_handler(poller, buffer, length, NULL);
}
#endif

int tp_qos_poller_init(tp_qos_poller_t *poller, tp_client_t *client, const tp_qos_handlers_t *handlers)
{
    if (NULL == poller || NULL == client || NULL == tp_client_qos_subscription(client))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_qos_poller_init: invalid input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->client = client;
    if (handlers)
    {
        poller->handlers = *handlers;
    }

    if (tp_fragment_assembler_create(&poller->assembler, tp_qos_poller_handler, poller) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_qos_poll(tp_qos_poller_t *poller, int fragment_limit)
{
    if (NULL == poller || NULL == poller->assembler || NULL == poller->client ||
        NULL == tp_client_qos_subscription(poller->client))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_qos_poll: poller not initialized");
        return -1;
    }

    return aeron_subscription_poll(
        tp_subscription_handle(tp_client_qos_subscription(poller->client)),
        aeron_fragment_assembler_handler,
        tp_fragment_assembler_handle(poller->assembler),
        fragment_limit);
}
