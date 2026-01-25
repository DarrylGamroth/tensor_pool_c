#include "tensor_pool/internal/tp_qos.h"

#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/qosProducer.h"
#include "wire/tensor_pool/qosConsumer.h"

#include <assert.h>
#include <string.h>

typedef struct tp_qos_test_state_stct
{
    int events;
    int producers;
    int consumers;
}
tp_qos_test_state_t;

static void tp_on_qos_event(void *clientd, const tp_qos_event_t *event)
{
    tp_qos_test_state_t *state = (tp_qos_test_state_t *)clientd;

    if (NULL == state || NULL == event)
    {
        return;
    }

    state->events++;
    if (event->type == TP_QOS_EVENT_PRODUCER)
    {
        state->producers++;
    }
    else if (event->type == TP_QOS_EVENT_CONSUMER)
    {
        state->consumers++;
    }
}

static size_t encode_qos_producer(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_qosProducer qos;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_qosProducer_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_qosProducer_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_qosProducer_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_qosProducer_sbe_schema_version());

    tensor_pool_qosProducer_wrap_for_encode(&qos, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_qosProducer_set_streamId(&qos, 1);
    tensor_pool_qosProducer_set_producerId(&qos, 2);
    tensor_pool_qosProducer_set_epoch(&qos, 3);
    tensor_pool_qosProducer_set_currentSeq(&qos, 4);
    tensor_pool_qosProducer_set_watermark(&qos, 5);

    return (size_t)tensor_pool_qosProducer_sbe_position(&qos);
}

static size_t encode_qos_consumer(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_qosConsumer qos;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_qosConsumer_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_qosConsumer_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_qosConsumer_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_qosConsumer_sbe_schema_version());

    tensor_pool_qosConsumer_wrap_for_encode(&qos, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_qosConsumer_set_streamId(&qos, 1);
    tensor_pool_qosConsumer_set_consumerId(&qos, 2);
    tensor_pool_qosConsumer_set_epoch(&qos, 3);
    tensor_pool_qosConsumer_set_lastSeqSeen(&qos, 4);
    tensor_pool_qosConsumer_set_dropsGap(&qos, 0);
    tensor_pool_qosConsumer_set_dropsLate(&qos, 0);
    tensor_pool_qosConsumer_set_mode(&qos, tensor_pool_mode_STREAM);

    return (size_t)tensor_pool_qosConsumer_sbe_position(&qos);
}

static void test_qos_poller_invalid_inputs(void)
{
    tp_qos_poller_t poller;
    tp_client_t client;

    memset(&poller, 0, sizeof(poller));
    memset(&client, 0, sizeof(client));

    assert(tp_qos_poller_init(NULL, NULL, NULL) < 0);
    assert(tp_qos_poller_init(&poller, NULL, NULL) < 0);
    assert(tp_qos_poller_init(&poller, &client, NULL) < 0);
}

static void test_qos_poller_dispatch(void)
{
    tp_qos_poller_t poller;
    tp_qos_handlers_t handlers;
    tp_qos_test_state_t state;
    uint8_t buffer[256];
    size_t len;

    memset(&poller, 0, sizeof(poller));
    memset(&handlers, 0, sizeof(handlers));
    memset(&state, 0, sizeof(state));

    handlers.on_qos_event = tp_on_qos_event;
    handlers.clientd = &state;
    poller.handlers = handlers;

    tp_qos_poller_handle_fragment(&poller, buffer, 0);
    assert(state.events == 0);

    memset(buffer, 0, sizeof(buffer));
    {
        struct tensor_pool_messageHeader header;
        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, 0);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_qosProducer_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, 1234);
        tensor_pool_messageHeader_set_version(&header, tensor_pool_qosProducer_sbe_schema_version());
        tp_qos_poller_handle_fragment(&poller, buffer, tensor_pool_messageHeader_encoded_length());
        assert(state.events == 0);
    }

    len = encode_qos_producer(buffer, sizeof(buffer));
    tp_qos_poller_handle_fragment(&poller, buffer, len);
    assert(state.events == 1);
    assert(state.producers == 1);

    len = encode_qos_consumer(buffer, sizeof(buffer));
    tp_qos_poller_handle_fragment(&poller, buffer, len);
    assert(state.events == 2);
    assert(state.consumers == 1);
}

void tp_test_qos_poller(void)
{
    test_qos_poller_invalid_inputs();
    test_qos_poller_dispatch();
}
