#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"

#include "wire/tensor_pool/slotHeader.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void tp_test_write_slot(
    uint8_t *slot,
    uint64_t seq_commit,
    uint16_t pool_id,
    uint32_t values_len,
    uint32_t payload_slot,
    const uint8_t *header_bytes,
    size_t header_len)
{
    struct tensor_pool_slotHeader header;

    tensor_pool_slotHeader_wrap_for_encode(
        &header,
        (char *)slot,
        0,
        TP_HEADER_SLOT_BYTES);
    tensor_pool_slotHeader_set_seqCommit(&header, seq_commit);
    tensor_pool_slotHeader_set_valuesLenBytes(&header, values_len);
    tensor_pool_slotHeader_set_payloadSlot(&header, payload_slot);
    tensor_pool_slotHeader_set_poolId(&header, pool_id);
    tensor_pool_slotHeader_set_payloadOffset(&header, 0);
    tensor_pool_slotHeader_set_timestampNs(&header, 0);
    tensor_pool_slotHeader_set_metaVersion(&header, 0);
    tensor_pool_slotHeader_put_headerBytes(&header, (const char *)header_bytes, (uint32_t)header_len);
}

static void test_consumer_read_frame_errors(void)
{
    tp_consumer_t consumer;
    tp_frame_view_t view;
    int result = -1;

    memset(&consumer, 0, sizeof(consumer));
    memset(&view, 0, sizeof(view));

    assert(tp_consumer_read_frame(NULL, 0, NULL) < 0);

    consumer.use_shm = true;
    consumer.shm_mapped = true;
    consumer.header_nslots = 0;

    if (tp_consumer_read_frame(&consumer, 0, &view) < 0)
    {
        result = 0;
    }

    assert(result == 0);
}

static void test_consumer_validate_progress_errors(void)
{
    tp_client_t client;
    tp_consumer_t consumer;
    tp_consumer_pool_t pool;
    tp_frame_progress_t progress;
    uint8_t *header_buffer = NULL;
    uint8_t *pool_buffer = NULL;
    uint8_t header_bytes[16];
    uint8_t *slot;
    size_t header_size;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&consumer, 0, sizeof(consumer));
    memset(&pool, 0, sizeof(pool));
    memset(&progress, 0, sizeof(progress));
    memset(header_bytes, 0, sizeof(header_bytes));

    assert(tp_consumer_validate_progress(NULL, NULL) < 0);

    consumer.shm_mapped = false;
    assert(tp_consumer_validate_progress(&consumer, &progress) == 1);

    consumer.shm_mapped = true;
    consumer.header_nslots = 0;
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    if (tp_context_init(&client.context.base) < 0)
    {
        goto cleanup;
    }

    consumer.client = &client;
    consumer.shm_mapped = true;
    consumer.header_nslots = 4;
    consumer.stream_id = 10;
    consumer.epoch = 20;
    consumer.pool_count = 1;
    consumer.pools = &pool;

    pool.pool_id = 7;
    pool.stride_bytes = 64;
    pool.region.addr = NULL;

    header_size = TP_SUPERBLOCK_SIZE_BYTES + consumer.header_nslots * TP_HEADER_SLOT_BYTES;
    header_buffer = calloc(1, header_size);
    if (NULL == header_buffer)
    {
        goto cleanup;
    }
    consumer.header_region.addr = header_buffer;

    pool_buffer = calloc(1, TP_SUPERBLOCK_SIZE_BYTES + pool.stride_bytes * consumer.header_nslots);
    if (NULL == pool_buffer)
    {
        goto cleanup;
    }
    pool.region.addr = pool_buffer;

    progress.stream_id = consumer.stream_id + 1;
    progress.epoch = consumer.epoch;
    progress.seq = 1;
    progress.payload_bytes_filled = 1;
    assert(tp_consumer_validate_progress(&consumer, &progress) == 1);

    progress.stream_id = consumer.stream_id;
    progress.epoch = consumer.epoch + 1;
    assert(tp_consumer_validate_progress(&consumer, &progress) == 1);

    progress.epoch = consumer.epoch;
    progress.seq = 4;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(
        slot,
        tp_seq_committed(progress.seq - 1),
        pool.pool_id,
        32,
        (uint32_t)(progress.seq & (consumer.header_nslots - 1)),
        header_bytes,
        sizeof(header_bytes));
    progress.payload_bytes_filled = 8;
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    progress.seq = 6;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(slot, tp_seq_committed(progress.seq), 99, 32, 0, header_bytes, sizeof(header_bytes));
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    progress.seq = 7;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(slot, tp_seq_committed(progress.seq), pool.pool_id, 128, 0, header_bytes, sizeof(header_bytes));
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    progress.seq = 8;
    progress.payload_bytes_filled = 80;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(slot, tp_seq_committed(progress.seq), pool.pool_id, 32, 0, header_bytes, sizeof(header_bytes));
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    progress.seq = 9;
    progress.payload_bytes_filled = 8;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(slot, tp_seq_committed(progress.seq), pool.pool_id, 32, 0, header_bytes, 1);
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    result = 0;

cleanup:
    free(header_buffer);
    free(pool_buffer);
    assert(result == 0);
}

void tp_test_consumer_errors(void)
{
    test_consumer_read_frame_errors();
    test_consumer_validate_progress_errors();
}
