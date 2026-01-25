#include "tensor_pool/internal/tp_client_internal.h"
#include "tensor_pool/internal/tp_producer_internal.h"
#include "tensor_pool/tp_tensor.h"
#include "tensor_pool/tp_context.h"

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

static void test_producer_offer_errors(void)
{
    tp_producer_t producer;
    tp_frame_t frame;
    tp_tensor_header_t tensor;

    memset(&producer, 0, sizeof(producer));
    memset(&frame, 0, sizeof(frame));
    memset(&tensor, 0, sizeof(tensor));

    assert(tp_producer_offer_frame(NULL, NULL, NULL) < 0);

    frame.tensor = &tensor;
    frame.payload = NULL;
    frame.payload_len = 0;
    frame.pool_id = 0;

    assert( tp_producer_offer_frame(&producer, &frame, NULL) < 0);

    producer.header_nslots = 4;
    producer.pool_count = 0;
    producer.header_region.addr = NULL;
    assert( tp_producer_offer_frame(&producer, &frame, NULL) < 0);
}

static void test_producer_claim_errors(void)
{
    tp_producer_t producer;
    tp_buffer_claim_t claim;

    memset(&producer, 0, sizeof(producer));
    memset(&claim, 0, sizeof(claim));

    assert(tp_producer_try_claim(NULL, 0, NULL) < 0);
    assert( tp_producer_try_claim(&producer, 64, &claim) < 0);

    assert(tp_producer_commit_claim(NULL, NULL, NULL) < 0);
    assert( tp_producer_commit_claim(&producer, NULL, NULL) < 0);
    assert(tp_producer_abort_claim(NULL, NULL) < 0);
    assert(tp_producer_abort_claim(&producer, NULL) < 0);
    assert(tp_producer_queue_claim(NULL, NULL) < 0);
    assert( tp_producer_queue_claim(&producer, NULL) < 0);
}

static void test_producer_publication_errors(void)
{
    tp_producer_t producer;
    tp_frame_progress_t progress;

    memset(&producer, 0, sizeof(producer));
    memset(&progress, 0, sizeof(progress));

    assert( tp_producer_offer_progress(&producer, &progress) < 0);
}

static void test_producer_publish_frame_errors(void)
{
    tp_client_t client;
    tp_producer_t producer;
    tp_payload_pool_t pool;
    tp_tensor_header_t tensor;
    uint8_t *header_buffer = NULL;
    uint8_t *pool_buffer = NULL;
    uint8_t payload[64];
    size_t header_size;
    size_t pool_size;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));
    memset(&pool, 0, sizeof(pool));
    memset(&tensor, 0, sizeof(tensor));
    memset(payload, 0, sizeof(payload));

    assert(tp_producer_publish_frame(NULL, 0, NULL, NULL, 0, 0, 0, 0, 0) < 0);
    assert(tp_producer_publish_frame(&producer, 0, &tensor, NULL, 1, 0, 0, 0, 0) < 0);
    assert(tp_producer_publish_frame(&producer, 0, &tensor, payload, 0, 0, 0, 0, 0) < 0);

    if (tp_context_init(&client.context.base) < 0)
    {
        goto cleanup;
    }

    producer.client = &client;
    producer.header_nslots = 4;
    producer.pool_count = 1;
    producer.pools = &pool;

    pool.pool_id = 3;
    pool.stride_bytes = 32;

    header_size = TP_SUPERBLOCK_SIZE_BYTES + (producer.header_nslots * TP_HEADER_SLOT_BYTES);
    header_buffer = calloc(1, header_size);
    if (NULL == header_buffer)
    {
        goto cleanup;
    }
    producer.header_region.addr = header_buffer;

    pool_size = TP_SUPERBLOCK_SIZE_BYTES + (pool.stride_bytes * producer.header_nslots);
    pool_buffer = calloc(1, pool_size);
    if (NULL == pool_buffer)
    {
        goto cleanup;
    }
    pool.region.addr = pool_buffer;

    tensor.dtype = tensor_pool_dtype_UINT8;
    tensor.major_order = tensor_pool_majorOrder_ROW;
    tensor.ndims = 1;
    tensor.progress_unit = tensor_pool_progressUnit_NONE;
    tensor.dims[0] = 1;
    tensor.strides[0] = 1;

    assert(tp_producer_publish_frame(&producer, 1, &tensor, payload, 0, 99, 0, 0, 0) < 0);
    assert(tp_producer_publish_frame(&producer, 1, &tensor, payload, pool.stride_bytes + 1, pool.pool_id, 0, 0, 0) < 0);

    tensor.ndims = 0;
    assert(tp_producer_publish_frame(&producer, 1, &tensor, payload, 0, pool.pool_id, 0, 0, 0) < 0);

    result = 0;

cleanup:
    free(header_buffer);
    free(pool_buffer);
    assert(result == 0);
}

void tp_test_producer_errors(void)
{
    test_producer_offer_errors();
    test_producer_claim_errors();
    test_producer_publication_errors();
    test_producer_publish_frame_errors();
}
