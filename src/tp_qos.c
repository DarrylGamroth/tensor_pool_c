#include "tensor_pool/tp_qos.h"

#include <errno.h>

#include "tensor_pool/tp_error.h"

#include "wire/tensor_pool/messageHeader.h"
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

    result = aeron_publication_offer(producer->qos_publication, buffer, header_len + body_len, NULL, NULL);
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
    uint8_t mode)
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
    tensor_pool_qosConsumer_set_mode(&qos, mode);

    result = aeron_publication_offer(consumer->qos_publication, buffer, header_len + body_len, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}
