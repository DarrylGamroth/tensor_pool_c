#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/internal/tp_progress_poller.h"
#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/messageHeader.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void tp_test_progress_poller_misc(void)
{
    tp_progress_poller_t poller;
    tp_consumer_t consumer;
    int result = -1;

    memset(&poller, 0, sizeof(poller));
    memset(&consumer, 0, sizeof(consumer));

    tp_progress_poller_set_max_payload_bytes(NULL, 1);
    tp_progress_poller_set_validator(NULL, NULL, NULL);
    tp_progress_poller_set_consumer(NULL, NULL);

    poller.tracker = calloc(1, sizeof(*poller.tracker));
    if (NULL == poller.tracker)
    {
        goto cleanup;
    }
    poller.tracker_capacity = 1;
    poller.tracker_cursor = 5;

    consumer.header_nslots = 4;
    tp_progress_poller_set_consumer(&poller, &consumer);

    assert(poller.tracker_capacity == 4);
    assert(poller.tracker_cursor == 0);
    assert(poller.validator_clientd == &consumer);

    if (tp_progress_poll(&poller, 1) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    free(poller.tracker);
    assert(result == 0);
}

static void tp_on_progress_count(void *clientd, const tp_frame_progress_t *progress)
{
    int *count = (int *)clientd;
    (void)progress;
    if (count)
    {
        (*count)++;
    }
}

static size_t tp_test_encode_progress(
    uint8_t *buffer,
    size_t capacity,
    uint32_t stream_id,
    uint64_t epoch,
    uint64_t seq,
    uint64_t payload_bytes_filled)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameProgress progress;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_frameProgress_sbe_block_length();

    if (capacity < header_len + body_len)
    {
        return 0;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        capacity);
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_frameProgress_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_frameProgress_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_frameProgress_sbe_schema_version());

    tensor_pool_frameProgress_wrap_for_encode(&progress, (char *)buffer, header_len, capacity);
    tensor_pool_frameProgress_set_streamId(&progress, stream_id);
    tensor_pool_frameProgress_set_epoch(&progress, epoch);
    tensor_pool_frameProgress_set_seq(&progress, seq);
    tensor_pool_frameProgress_set_payloadBytesFilled(&progress, payload_bytes_filled);
    tensor_pool_frameProgress_set_state(&progress, tensor_pool_frameProgressState_PROGRESS);

    return header_len + body_len;
}

void tp_test_progress_poller_monotonic_capacity(void)
{
    tp_progress_poller_t poller;
    tp_progress_handlers_t handlers;
    tp_consumer_t consumer;
    tp_subscription_t *subscription = NULL;
    uint8_t buffer[128];
    uint32_t stream_id = 1;
    uint64_t epoch = 1;
    uint64_t seq;
    size_t len;
    int count = 0;
    int result = -1;

    memset(&poller, 0, sizeof(poller));
    memset(&handlers, 0, sizeof(handlers));
    memset(&consumer, 0, sizeof(consumer));

    handlers.on_progress = tp_on_progress_count;
    handlers.clientd = &count;

    subscription = (tp_subscription_t *)calloc(1, sizeof(*subscription));
    if (NULL == subscription)
    {
        goto cleanup;
    }

    if (tp_progress_poller_init_with_subscription(&poller, subscription, &handlers) < 0)
    {
        goto cleanup;
    }

    consumer.header_nslots = 128;
    tp_progress_poller_set_consumer(&poller, &consumer);
    tp_progress_poller_set_validator(&poller, NULL, NULL);

    for (seq = 0; seq < consumer.header_nslots; seq++)
    {
        len = tp_test_encode_progress(buffer, sizeof(buffer), stream_id, epoch, seq, 10);
        assert(len > 0);
        tp_progress_poller_handle_fragment(&poller, buffer, len);
    }

    assert(count == (int)consumer.header_nslots);

    len = tp_test_encode_progress(buffer, sizeof(buffer), stream_id, epoch, 0, 5);
    assert(len > 0);
    tp_progress_poller_handle_fragment(&poller, buffer, len);

    if (count != (int)consumer.header_nslots)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_subscription_close(&subscription);
    tp_fragment_assembler_close(&poller.assembler);
    free(poller.tracker);
    assert(result == 0);
}
