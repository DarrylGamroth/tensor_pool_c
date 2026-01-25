#include "tensor_pool/internal/tp_client_internal.h"
#include "tensor_pool/internal/tp_consumer_internal.h"
#include "tensor_pool/internal/tp_producer_internal.h"
#include "tensor_pool/tp_tensor.h"
#include "tensor_pool/tp_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int tp_producer_publish_frame(
    tp_producer_t *producer,
    uint64_t seq,
    const tp_tensor_header_t *tensor,
    const void *payload,
    uint32_t payload_len,
    uint16_t pool_id,
    uint64_t timestamp_ns,
    uint32_t meta_version,
    uint64_t trace_id);

static size_t tp_test_flush_calls = 0;
static size_t tp_test_flush_len = 0;

static void tp_test_payload_flush(void *clientd, void *payload, size_t length)
{
    (void)clientd;
    (void)payload;

    tp_test_flush_calls++;
    tp_test_flush_len = length;
}

void tp_test_shm_roundtrip(void)
{
    tp_client_t client;
    tp_producer_t producer;
    tp_consumer_t consumer;
    tp_payload_pool_t producer_pool;
    tp_consumer_pool_t consumer_pool;
    tp_tensor_header_t header;
    tp_frame_t frame;
    tp_frame_view_t view;
    tp_frame_progress_t progress;
    const float payload[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    uint8_t *header_region = NULL;
    uint8_t *pool_region = NULL;
    uint32_t header_nslots = 4;
    uint32_t stride_bytes = 64;
    size_t header_size = TP_SUPERBLOCK_SIZE_BYTES + (header_nslots * TP_HEADER_SLOT_BYTES);
    size_t pool_size = TP_SUPERBLOCK_SIZE_BYTES + (header_nslots * stride_bytes);
    uint64_t seq = 5;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));
    memset(&consumer, 0, sizeof(consumer));
    memset(&producer_pool, 0, sizeof(producer_pool));
    memset(&consumer_pool, 0, sizeof(consumer_pool));
    memset(&header, 0, sizeof(header));
    memset(&frame, 0, sizeof(frame));
    memset(&view, 0, sizeof(view));
    memset(&progress, 0, sizeof(progress));

    if (tp_client_context_init(&client.context) < 0)
    {
        goto cleanup;
    }

    header_region = calloc(1, header_size);
    pool_region = calloc(1, pool_size);
    if (NULL == header_region || NULL == pool_region)
    {
        goto cleanup;
    }

    producer.client = &client;
    producer.header_region.addr = header_region;
    producer.header_nslots = header_nslots;
    producer.pool_count = 1;
    producer.pools = &producer_pool;
    producer.stream_id = 1;
    producer.epoch = 1;
    producer.context.payload_flush = tp_test_payload_flush;

    producer_pool.pool_id = 1;
    producer_pool.nslots = header_nslots;
    producer_pool.stride_bytes = stride_bytes;
    producer_pool.region.addr = pool_region;

    consumer.client = &client;
    consumer.use_shm = true;
    consumer.shm_mapped = true;
    consumer.header_region.addr = header_region;
    consumer.header_nslots = header_nslots;
    consumer.pool_count = 1;
    consumer.pools = &consumer_pool;
    consumer.stream_id = 1;
    consumer.epoch = 1;

    consumer_pool.pool_id = 1;
    consumer_pool.nslots = header_nslots;
    consumer_pool.stride_bytes = stride_bytes;
    consumer_pool.region.addr = pool_region;

    header.dtype = TP_DTYPE_FLOAT32;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.ndims = 2;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = 2;
    header.dims[1] = 2;

    frame.tensor = &header;
    frame.payload = payload;
    frame.payload_len = sizeof(payload);
    frame.pool_id = producer_pool.pool_id;

    if (tp_producer_publish_frame(
        &producer,
        seq,
        &header,
        payload,
        sizeof(payload),
        producer_pool.pool_id,
        123,
        7,
        0) >= 0)
    {
        goto cleanup;
    }

    if (tp_test_flush_calls != 1 || tp_test_flush_len != sizeof(payload))
    {
        goto cleanup;
    }

    if ( tp_consumer_read_frame(&consumer, seq, &view) != 0)
    {
        goto cleanup;
    }

    if (view.payload_len != sizeof(payload) ||
        view.pool_id != producer_pool.pool_id ||
        view.timestamp_ns != 123 ||
        view.meta_version != 7 ||
        memcmp(view.payload, payload, sizeof(payload)) != 0)
    {
        goto cleanup;
    }

    progress.stream_id = consumer.stream_id;
    progress.epoch = consumer.epoch;
    progress.seq = seq;
    progress.payload_bytes_filled = sizeof(payload);
    progress.state = TP_PROGRESS_COMPLETE;
    if (tp_consumer_validate_progress(&consumer, &progress) != 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    free(header_region);
    free(pool_region);
    assert(result == 0);
}
