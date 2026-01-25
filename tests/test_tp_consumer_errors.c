#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_control.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/slotHeader.h"
#include "wire/tensor_pool/tensorHeader.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void tp_test_write_slot(
    uint8_t *slot,
    uint64_t seq_commit,
    uint16_t pool_id,
    uint32_t values_len,
    uint32_t payload_slot,
    uint32_t payload_offset,
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
    tensor_pool_slotHeader_set_payloadOffset(&header, payload_offset);
    tensor_pool_slotHeader_set_timestampNs(&header, 0);
    tensor_pool_slotHeader_set_metaVersion(&header, 0);
    tensor_pool_slotHeader_put_headerBytes(&header, (const char *)header_bytes, (uint32_t)header_len);
}

static size_t tp_test_encode_tensor_header(uint8_t *buffer, size_t buffer_len)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_tensorHeader tensor_header;

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_tensorHeader_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_tensorHeader_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_tensorHeader_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version());

    tensor_pool_tensorHeader_wrap_for_encode(
        &tensor_header,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        buffer_len);
    tensor_pool_tensorHeader_set_dtype(&tensor_header, tensor_pool_dtype_UINT8);
    tensor_pool_tensorHeader_set_majorOrder(&tensor_header, tensor_pool_majorOrder_ROW);
    tensor_pool_tensorHeader_set_ndims(&tensor_header, 1);
    tensor_pool_tensorHeader_set_padAlign(&tensor_header, 0);
    tensor_pool_tensorHeader_set_progressUnit(&tensor_header, tensor_pool_progressUnit_NONE);
    tensor_pool_tensorHeader_set_progressStrideBytes(&tensor_header, 0);
    tensor_pool_tensorHeader_set_dims(&tensor_header, 0, 1);
    tensor_pool_tensorHeader_set_strides(&tensor_header, 0, 1);

    return tensor_pool_messageHeader_encoded_length() + tensor_pool_tensorHeader_sbe_block_length();
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

static void test_consumer_read_frame_validation_failures(void)
{
    tp_client_t client;
    tp_consumer_t consumer;
    tp_consumer_pool_t pool;
    tp_frame_view_t view;
    uint8_t *header_buffer = NULL;
    uint8_t *pool_buffer = NULL;
    uint8_t header_bytes[TP_HEADER_SLOT_BYTES];
    uint8_t *slot;
    size_t header_size;
    size_t pool_size;
    size_t header_len;
    uint64_t seq;
    uint32_t header_index;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&consumer, 0, sizeof(consumer));
    memset(&pool, 0, sizeof(pool));
    memset(&view, 0, sizeof(view));
    memset(header_bytes, 0, sizeof(header_bytes));

    if (tp_context_init(&client.context.base) < 0)
    {
        goto cleanup;
    }

    consumer.client = &client;
    consumer.use_shm = true;
    consumer.shm_mapped = true;
    consumer.header_nslots = 4;
    consumer.pool_count = 1;
    consumer.pools = &pool;

    pool.pool_id = 5;
    pool.stride_bytes = 64;

    header_size = TP_SUPERBLOCK_SIZE_BYTES + (consumer.header_nslots * TP_HEADER_SLOT_BYTES);
    header_buffer = calloc(1, header_size);
    if (NULL == header_buffer)
    {
        goto cleanup;
    }
    consumer.header_region.addr = header_buffer;

    pool_size = TP_SUPERBLOCK_SIZE_BYTES + (pool.stride_bytes * consumer.header_nslots);
    pool_buffer = calloc(1, pool_size);
    if (NULL == pool_buffer)
    {
        goto cleanup;
    }
    pool.region.addr = pool_buffer;

    header_len = tp_test_encode_tensor_header(header_bytes, sizeof(header_bytes));

    seq = 6;
    header_index = (uint32_t)(seq & (consumer.header_nslots - 1));
    slot = tp_slot_at(consumer.header_region.addr, header_index);

    tp_test_write_slot(
        slot,
        tp_seq_in_progress(seq),
        pool.pool_id,
        16,
        header_index,
        0,
        header_bytes,
        header_len);
    assert(tp_consumer_read_frame(&consumer, seq, &view) == 1);

    tp_test_write_slot(
        slot,
        tp_seq_committed(seq),
        pool.pool_id,
        16,
        header_index,
        4,
        header_bytes,
        header_len);
    assert(tp_consumer_read_frame(&consumer, seq, &view) == 1);

    tp_test_write_slot(
        slot,
        tp_seq_committed(seq),
        pool.pool_id,
        16,
        (header_index + 1) & (consumer.header_nslots - 1),
        0,
        header_bytes,
        header_len);
    assert(tp_consumer_read_frame(&consumer, seq, &view) == 1);

    tp_test_write_slot(
        slot,
        tp_seq_committed(seq),
        pool.pool_id + 1,
        16,
        header_index,
        0,
        header_bytes,
        header_len);
    assert(tp_consumer_read_frame(&consumer, seq, &view) == 1);

    tp_test_write_slot(
        slot,
        tp_seq_committed(seq),
        pool.pool_id,
        pool.stride_bytes + 1,
        header_index,
        0,
        header_bytes,
        header_len);
    assert(tp_consumer_read_frame(&consumer, seq, &view) == 1);

    tp_test_write_slot(
        slot,
        tp_seq_committed(seq),
        pool.pool_id,
        16,
        header_index,
        0,
        header_bytes,
        1);
    assert(tp_consumer_read_frame(&consumer, seq, &view) == 1);

    {
        struct tensor_pool_messageHeader msg_header;

        tp_test_encode_tensor_header(header_bytes, sizeof(header_bytes));
        tensor_pool_messageHeader_wrap(
            &msg_header,
            (char *)header_bytes,
            0,
            tensor_pool_messageHeader_sbe_schema_version(),
            sizeof(header_bytes));
        tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version() + 1);
        tp_test_write_slot(
            slot,
            tp_seq_committed(seq),
            pool.pool_id,
            16,
            header_index,
            0,
            header_bytes,
            header_len);
        assert(tp_consumer_read_frame(&consumer, seq, &view) == 1);
    }

    tp_test_write_slot(
        slot,
        tp_seq_committed(seq + 1),
        pool.pool_id,
        16,
        header_index,
        0,
        header_bytes,
        header_len);
    assert(tp_consumer_read_frame(&consumer, seq, &view) == 1);

    result = 0;

cleanup:
    free(header_buffer);
    free(pool_buffer);
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
        0,
        header_bytes,
        sizeof(header_bytes));
    progress.payload_bytes_filled = 8;
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    progress.seq = 6;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(slot, tp_seq_committed(progress.seq), 99, 32, 0, 0, header_bytes, sizeof(header_bytes));
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    progress.seq = 7;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(slot, tp_seq_committed(progress.seq), pool.pool_id, 128, 0, 0, header_bytes, sizeof(header_bytes));
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    progress.seq = 8;
    progress.payload_bytes_filled = 80;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(slot, tp_seq_committed(progress.seq), pool.pool_id, 32, 0, 0, header_bytes, sizeof(header_bytes));
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    progress.seq = 9;
    progress.payload_bytes_filled = 8;
    slot = tp_slot_at(consumer.header_region.addr, (uint32_t)(progress.seq & (consumer.header_nslots - 1)));
    tp_test_write_slot(slot, tp_seq_committed(progress.seq), pool.pool_id, 32, 0, 0, header_bytes, 1);
    assert(tp_consumer_validate_progress(&consumer, &progress) < 0);

    result = 0;

cleanup:
    free(header_buffer);
    free(pool_buffer);
    assert(result == 0);
}

static void test_consumer_hello_request_validation(void)
{
    tp_consumer_t consumer;
    tp_consumer_hello_t hello;
    tp_publication_t *publication = NULL;
    int result = -1;

    memset(&consumer, 0, sizeof(consumer));
    memset(&hello, 0, sizeof(hello));

    publication = (tp_publication_t *)calloc(1, sizeof(*publication));
    if (NULL == publication)
    {
        goto cleanup;
    }
    consumer.control_publication = publication;

    hello.stream_id = 1;
    hello.consumer_id = 2;

    hello.descriptor_channel = "aeron:ipc";
    hello.descriptor_stream_id = 0;
    assert(tp_consumer_send_hello(&consumer, &hello) < 0);

    hello.descriptor_channel = "";
    hello.descriptor_stream_id = 100;
    assert(tp_consumer_send_hello(&consumer, &hello) < 0);

    hello.descriptor_channel = "";
    hello.descriptor_stream_id = 0;
    hello.control_channel = "aeron:ipc";
    hello.control_stream_id = 0;
    assert(tp_consumer_send_hello(&consumer, &hello) < 0);

    hello.control_channel = "";
    hello.control_stream_id = 42;
    assert(tp_consumer_send_hello(&consumer, &hello) < 0);

    result = 0;

cleanup:
    tp_publication_close(&publication);
    assert(result == 0);
}

void tp_test_consumer_errors(void)
{
    test_consumer_read_frame_errors();
    test_consumer_read_frame_validation_failures();
    test_consumer_validate_progress_errors();
    test_consumer_hello_request_validation();
}
