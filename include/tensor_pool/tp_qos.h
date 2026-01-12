#ifndef TENSOR_POOL_TP_QOS_H
#define TENSOR_POOL_TP_QOS_H

#include <stdint.h>

#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_producer.h"

#ifdef __cplusplus
extern "C" {
#endif

int tp_qos_publish_producer(tp_producer_t *producer, uint64_t current_seq, uint32_t watermark);
int tp_qos_publish_consumer(
    tp_consumer_t *consumer,
    uint32_t consumer_id,
    uint64_t last_seq_seen,
    uint64_t drops_gap,
    uint64_t drops_late,
    uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif
