#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_tensor.h"
#include "tensor_pool/tp_version.h"

#include "wire/tensor_pool/dtype.h"
#include "wire/tensor_pool/majorOrder.h"
#include "wire/tensor_pool/progressUnit.h"
#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/shmRegionSuperblock.h"
#include "wire/tensor_pool/slotHeader.h"
#include "wire/tensor_pool/tensorHeader.h"

#include "aeronc.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static size_t tp_payload_flush_last_len = 0;

static void tp_test_payload_flush(void *clientd, void *payload, size_t length)
{
    int *calls = (int *)clientd;

    (void)payload;

    if (calls)
    {
        (*calls)++;
    }
    tp_payload_flush_last_len = length;
}

static void tp_test_write_superblock(
    int fd,
    uint32_t stream_id,
    uint64_t epoch,
    int16_t region_type,
    uint16_t pool_id,
    uint32_t nslots,
    uint32_t slot_bytes,
    uint32_t stride_bytes)
{
    uint8_t buffer[TP_SUPERBLOCK_SIZE_BYTES];
    struct tensor_pool_shmRegionSuperblock block;

    memset(buffer, 0, sizeof(buffer));

    tensor_pool_shmRegionSuperblock_wrap_for_encode(&block, (char *)buffer, 0, sizeof(buffer));
    tensor_pool_shmRegionSuperblock_set_magic(&block, TP_MAGIC_U64);
    tensor_pool_shmRegionSuperblock_set_layoutVersion(&block, 1);
    tensor_pool_shmRegionSuperblock_set_epoch(&block, epoch);
    tensor_pool_shmRegionSuperblock_set_streamId(&block, stream_id);
    tensor_pool_shmRegionSuperblock_set_regionType(&block, region_type);
    tensor_pool_shmRegionSuperblock_set_poolId(&block, pool_id);
    tensor_pool_shmRegionSuperblock_set_nslots(&block, nslots);
    tensor_pool_shmRegionSuperblock_set_slotBytes(&block, slot_bytes);
    tensor_pool_shmRegionSuperblock_set_strideBytes(&block, stride_bytes);
    tensor_pool_shmRegionSuperblock_set_pid(&block, (uint64_t)getpid());
    tensor_pool_shmRegionSuperblock_set_startTimestampNs(&block, 1);
    tensor_pool_shmRegionSuperblock_set_activityTimestampNs(&block, (uint64_t)tp_clock_now_ns());

    assert(pwrite(fd, buffer, sizeof(buffer), 0) == (ssize_t)sizeof(buffer));
}

static int tp_test_wait_for_connected(tp_client_t *client, aeron_publication_t *publication)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        if (aeron_publication_is_connected(publication))
        {
            return 0;
        }
        tp_client_do_work(client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

    return -1;
}

static int tp_test_offer_frame_retry(
    tp_producer_t *producer,
    const tp_frame_t *frame,
    tp_frame_metadata_t *meta,
    uint64_t *out_seq)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    if (NULL != out_seq)
    {
        *out_seq = 0;
    }

    while (tp_clock_now_ns() < deadline)
    {
        int64_t result = tp_producer_offer_frame(producer, frame, meta);
        if (result >= 0)
        {
            if (NULL != out_seq)
            {
                *out_seq = (uint64_t)result;
            }
            return 0;
        }
        tp_client_do_work(producer->client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

    return -1;
}

static int tp_test_add_subscription(tp_client_t *client, const char *channel, int32_t stream_id, aeron_subscription_t **out)
{
    aeron_async_add_subscription_t *async_add = NULL;

    if (tp_client_async_add_subscription(client, channel, stream_id, NULL, NULL, NULL, NULL, &async_add) < 0)
    {
        return -1;
    }

    *out = NULL;
    while (NULL == *out)
    {
        if (tp_client_async_add_subscription_poll(out, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(client);
    }

    return 0;
}

static int tp_test_start_client(tp_client_t *client, tp_client_context_t *ctx, const char *aeron_dir)
{
    const char *allowed_paths[] = { "/tmp" };

    if (NULL == aeron_dir)
    {
        return -1;
    }

    if (tp_client_context_init(ctx) < 0)
    {
        return -1;
    }

    tp_client_context_set_aeron_dir(ctx, aeron_dir);
    tp_client_context_set_descriptor_channel(ctx, "aeron:ipc", 1100);
    tp_client_context_set_use_agent_invoker(ctx, true);
    tp_context_set_allowed_paths(&ctx->base, allowed_paths, 1);

    if (tp_client_init(client, ctx) < 0)
    {
        return -1;
    }

    if (tp_client_start(client) < 0)
    {
        tp_client_close(client);
        return -1;
    }

    return 0;
}

static int tp_test_init_producer(
    tp_client_t *client,
    const tp_producer_context_t *producer_ctx,
    const char *header_uri,
    const char *pool_uri,
    tp_producer_t *out_producer)
{
    tp_producer_config_t config;
    tp_payload_pool_config_t pool_cfg;

    memset(out_producer, 0, sizeof(*out_producer));

    if (tp_producer_init(out_producer, client, producer_ctx) < 0)
    {
        return -1;
    }

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = 1;
    pool_cfg.nslots = 4;
    pool_cfg.stride_bytes = 128;
    pool_cfg.uri = pool_uri;

    memset(&config, 0, sizeof(config));
    config.stream_id = 7;
    config.producer_id = 9;
    config.epoch = 1;
    config.layout_version = 1;
    config.header_nslots = 4;
    config.header_uri = header_uri;
    config.pools = &pool_cfg;
    config.pool_count = 1;

    if (tp_producer_attach(out_producer, &config) < 0)
    {
        tp_producer_close(out_producer);
        return -1;
    }

    return 0;
}

static void tp_test_claim_lifecycle(bool fixed_pool_mode)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    aeron_subscription_t *descriptor_sub = NULL;
    tp_buffer_claim_t claim;
    tp_frame_metadata_t meta;
    uint8_t *slot;
    uint64_t observed;
    struct tensor_pool_slotHeader slot_header;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_header_claimXXXXXX";
    char pool_path[] = "/tmp/tp_pool_claimXXXXXX";
    char header_uri[512];
    char pool_uri[512];
    size_t header_size = TP_SUPERBLOCK_SIZE_BYTES + TP_HEADER_SLOT_BYTES * 4;
    size_t pool_size = TP_SUPERBLOCK_SIZE_BYTES + 128 * 4;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));
    memset(&claim, 0, sizeof(claim));

    header_fd = mkstemp(header_path);
    pool_fd = mkstemp(pool_path);
    if (header_fd < 0 || pool_fd < 0)
    {
        goto cleanup;
    }

    if (ftruncate(header_fd, (off_t)header_size) != 0 || ftruncate(pool_fd, (off_t)pool_size) != 0)
    {
        goto cleanup;
    }

    tp_test_write_superblock(header_fd, 7, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 7, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 128);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    {
        const char *candidates[] = { getenv("AERON_DIR"), "/dev/shm/aeron-dgamroth", "/dev/shm/aeron" };
        size_t i;
        int started = 0;

        for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        {
            if (tp_test_start_client(&client, &ctx, candidates[i]) == 0)
            {
                started = 1;
                break;
            }
        }

        if (!started)
        {
            result = 0;
            goto cleanup;
        }
    }

    if (tp_test_add_subscription(&client, "aeron:ipc", 1100, &descriptor_sub) < 0)
    {
        goto cleanup;
    }

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }

    tp_producer_context_set_fixed_pool_mode(&producer_ctx, fixed_pool_mode);

    if (tp_test_init_producer(&client, &producer_ctx, header_uri, pool_uri, &producer) < 0)
    {
        goto cleanup;
    }

    if (tp_test_wait_for_connected(&client, producer.descriptor_publication) < 0)
    {
        goto cleanup;
    }

    if (tp_producer_try_claim(&producer, 32, &claim) < 0)
    {
        goto cleanup;
    }

    slot = tp_slot_at(producer.header_region.addr, claim.header_index);
    observed = tp_atomic_load_u64((uint64_t *)slot);
    assert(!tp_seq_is_committed(observed));
    assert(tp_seq_value(observed) == claim.seq);

    memset(&claim.tensor, 0, sizeof(claim.tensor));
    claim.tensor.dtype = tensor_pool_dtype_FLOAT32;
    claim.tensor.major_order = tensor_pool_majorOrder_ROW;
    claim.tensor.ndims = 1;
    claim.tensor.progress_unit = tensor_pool_progressUnit_NONE;
    claim.tensor.dims[0] = 8;
    claim.tensor.strides[0] = 4;
    claim.tensor.dims[1] = 99;
    claim.tensor.strides[1] = 123;
    memset(claim.payload, 0xAB, claim.payload_len);

    memset(&meta, 0, sizeof(meta));
    meta.timestamp_ns = 55;
    meta.meta_version = 2;

    if (tp_producer_commit_claim(&producer, &claim, &meta) < 0)
    {
        goto cleanup;
    }

    observed = tp_atomic_load_u64((uint64_t *)slot);
    assert(tp_seq_is_committed(observed));
    assert(tp_seq_value(observed) == claim.seq);

    tensor_pool_slotHeader_wrap_for_decode(
        &slot_header,
        (char *)slot,
        0,
        tensor_pool_slotHeader_sbe_block_length(),
        tensor_pool_slotHeader_sbe_schema_version(),
        TP_HEADER_SLOT_BYTES);
    assert(tensor_pool_slotHeader_valuesLenBytes(&slot_header) == claim.payload_len);
    assert(tensor_pool_slotHeader_poolId(&slot_header) == claim.pool_id);
    {
        size_t pad_offset = tensor_pool_slotHeader_pad_encoding_offset();
        size_t i;
        for (i = 0; i < 26; i++)
        {
            assert(slot[pad_offset + i] == 0);
        }
    }
    {
        struct tensor_pool_messageHeader msg_header;
        struct tensor_pool_tensorHeader tensor_header;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t pad_offset = tensor_pool_tensorHeader_pad_encoding_offset();
        size_t pad_len = tensor_pool_tensorHeader_sbe_block_length() - pad_offset;
        size_t i;
        uint8_t *header_bytes = slot + tensor_pool_slotHeader_sbe_block_length() + sizeof(uint32_t);

        tensor_pool_messageHeader_wrap(
            &msg_header,
            (char *)header_bytes,
            0,
            tensor_pool_messageHeader_sbe_schema_version(),
            header_len + tensor_pool_tensorHeader_sbe_block_length());
        tensor_pool_tensorHeader_wrap_for_decode(
            &tensor_header,
            (char *)header_bytes,
            header_len,
            tensor_pool_tensorHeader_sbe_block_length(),
            tensor_pool_tensorHeader_sbe_schema_version(),
            header_len + tensor_pool_tensorHeader_sbe_block_length());

        for (i = 0; i < pad_len; i++)
        {
            assert(header_bytes[header_len + pad_offset + i] == 0);
        }

        for (i = claim.tensor.ndims; i < TP_MAX_DIMS; i++)
        {
            int32_t dim = 0;
            int32_t stride = 0;

            assert(tensor_pool_tensorHeader_dims(&tensor_header, i, &dim));
            assert(tensor_pool_tensorHeader_strides(&tensor_header, i, &stride));
            assert(dim == 0);
            assert(stride == 0);
        }
    }

    if (fixed_pool_mode)
    {
        int64_t queued = tp_producer_queue_claim(&producer, &claim);
        assert(queued >= 0);
        observed = tp_atomic_load_u64((uint64_t *)slot);
        assert(!tp_seq_is_committed(observed));
        assert(tp_seq_value(observed) == claim.seq);
    }
    else
    {
        int64_t queued = tp_producer_queue_claim(&producer, &claim);
        assert(queued == TP_ADMIN_ACTION);
    }

    result = 0;

cleanup:
    if (producer.client)
    {
        tp_producer_close(&producer);
    }
    if (descriptor_sub)
    {
        aeron_subscription_close(descriptor_sub, NULL, NULL);
    }
    if (client.context.base.aeron_dir[0] != '\0')
    {
        tp_client_close(&client);
    }
    if (header_fd >= 0)
    {
        close(header_fd);
        unlink(header_path);
    }
    if (pool_fd >= 0)
    {
        close(pool_fd);
        unlink(pool_path);
    }

    assert(result == 0);
}

static void tp_test_producer_invalid_tensor_header(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    aeron_subscription_t *descriptor_sub = NULL;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_header_invalidXXXXXX";
    char pool_path[] = "/tmp/tp_pool_invalidXXXXXX";
    char header_uri[512];
    char pool_uri[512];
    size_t header_size = TP_SUPERBLOCK_SIZE_BYTES + TP_HEADER_SLOT_BYTES * 4;
    size_t pool_size = TP_SUPERBLOCK_SIZE_BYTES + 128 * 4;
    tp_tensor_header_t tensor;
    tp_frame_t frame;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));

    header_fd = mkstemp(header_path);
    pool_fd = mkstemp(pool_path);
    if (header_fd < 0 || pool_fd < 0)
    {
        goto cleanup;
    }

    if (ftruncate(header_fd, (off_t)header_size) != 0 || ftruncate(pool_fd, (off_t)pool_size) != 0)
    {
        goto cleanup;
    }

    tp_test_write_superblock(header_fd, 7, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 7, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 128);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    {
        const char *candidates[] = { getenv("AERON_DIR"), "/dev/shm/aeron-dgamroth", "/dev/shm/aeron" };
        size_t i;
        int started = 0;

        for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        {
            if (tp_test_start_client(&client, &ctx, candidates[i]) == 0)
            {
                started = 1;
                break;
            }
        }

        if (!started)
        {
            result = 0;
            goto cleanup;
        }
    }

    if (tp_test_add_subscription(&client, "aeron:ipc", 1100, &descriptor_sub) < 0)
    {
        goto cleanup;
    }

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_test_init_producer(&client, &producer_ctx, header_uri, pool_uri, &producer) < 0)
    {
        goto cleanup;
    }

    if (tp_test_wait_for_connected(&client, producer.descriptor_publication) < 0)
    {
        goto cleanup;
    }

    memset(&frame, 0, sizeof(frame));
    frame.tensor = &tensor;
    frame.pool_id = 1;

    memset(&tensor, 0, sizeof(tensor));
    tensor.dtype = tensor_pool_dtype_FLOAT32;
    tensor.major_order = tensor_pool_majorOrder_ROW;
    tensor.progress_unit = tensor_pool_progressUnit_NONE;

    tensor.ndims = 0;
    assert(tp_producer_offer_frame(&producer, &frame, NULL) < 0);

    tensor.ndims = 1;
    tensor.dims[0] = 8;
    tensor.strides[0] = -4;
    assert(tp_producer_offer_frame(&producer, &frame, NULL) < 0);

    tensor.strides[0] = 4;
    tensor.progress_unit = tensor_pool_progressUnit_ROWS;
    tensor.progress_stride_bytes = 8;
    assert(tp_producer_offer_frame(&producer, &frame, NULL) < 0);

    result = 0;

cleanup:
    if (producer.client)
    {
        tp_producer_close(&producer);
    }
    if (descriptor_sub)
    {
        aeron_subscription_close(descriptor_sub, NULL, NULL);
    }
    if (client.context.base.aeron_dir[0] != '\0')
    {
        tp_client_close(&client);
    }
    if (header_fd >= 0)
    {
        close(header_fd);
        unlink(header_path);
    }
    if (pool_fd >= 0)
    {
        close(pool_fd);
        unlink(pool_path);
    }

    assert(result == 0);
}

static void tp_test_producer_offer_pool_selection(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    aeron_subscription_t *descriptor_sub = NULL;
    tp_payload_pool_config_t pools[2];
    tp_producer_config_t config;
    tp_frame_t frame;
    tp_frame_metadata_t meta;
    tp_tensor_header_t tensor;
    uint8_t payload[128];
    tp_slot_view_t slot_view;
    uint32_t header_index;
    uint64_t seq;
    int header_fd = -1;
    int pool1_fd = -1;
    int pool2_fd = -1;
    char header_path[] = "/tmp/tp_header_offerXXXXXX";
    char pool1_path[] = "/tmp/tp_pool_offer1XXXXXX";
    char pool2_path[] = "/tmp/tp_pool_offer2XXXXXX";
    char header_uri[512];
    char pool1_uri[512];
    char pool2_uri[512];
    size_t header_size = TP_SUPERBLOCK_SIZE_BYTES + TP_HEADER_SLOT_BYTES * 4;
    size_t pool1_size = TP_SUPERBLOCK_SIZE_BYTES + 64 * 4;
    size_t pool2_size = TP_SUPERBLOCK_SIZE_BYTES + 128 * 4;
    int result = -1;
    int step = 0;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));

    {
        const char *candidates[] = { getenv("AERON_DIR"), "/dev/shm/aeron-dgamroth", "/dev/shm/aeron" };
        size_t i;
        int started = 0;

        for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        {
            if (NULL == candidates[i])
            {
                continue;
            }
            if (tp_test_start_client(&client, &ctx, candidates[i]) == 0)
            {
                started = 1;
                break;
            }
        }

        if (!started)
        {
            result = 0;
            goto cleanup;
        }
    }

    step = 1;
    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_subscription(&client, "aeron:ipc", 1100, &descriptor_sub) < 0)
    {
        goto cleanup;
    }

    header_fd = mkstemp(header_path);
    pool1_fd = mkstemp(pool1_path);
    pool2_fd = mkstemp(pool2_path);
    if (header_fd < 0 || pool1_fd < 0 || pool2_fd < 0)
    {
        goto cleanup;
    }

    step = 4;
    if (ftruncate(header_fd, (off_t)header_size) != 0 ||
        ftruncate(pool1_fd, (off_t)pool1_size) != 0 ||
        ftruncate(pool2_fd, (off_t)pool2_size) != 0)
    {
        goto cleanup;
    }

    tp_test_write_superblock(header_fd, 7, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool1_fd, 7, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 64);
    tp_test_write_superblock(pool2_fd, 7, 1, tensor_pool_regionType_PAYLOAD_POOL, 2, 4, TP_NULL_U32, 128);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool1_uri, sizeof(pool1_uri), "shm:file?path=%s", pool1_path);
    snprintf(pool2_uri, sizeof(pool2_uri), "shm:file?path=%s", pool2_path);

    memset(&config, 0, sizeof(config));
    config.stream_id = 7;
    config.producer_id = 9;
    config.epoch = 1;
    config.layout_version = 1;
    config.header_nslots = 4;
    config.header_uri = header_uri;
    config.pools = pools;
    config.pool_count = 2;

    memset(pools, 0, sizeof(pools));
    pools[0].pool_id = 1;
    pools[0].nslots = 4;
    pools[0].stride_bytes = 64;
    pools[0].uri = pool1_uri;
    pools[1].pool_id = 2;
    pools[1].nslots = 4;
    pools[1].stride_bytes = 128;
    pools[1].uri = pool2_uri;

    step = 6;
    if (tp_producer_init(&producer, &client, &producer_ctx) < 0)
    {
        goto cleanup;
    }

    step = 7;
    if (tp_producer_attach(&producer, &config) < 0)
    {
        goto cleanup;
    }

    step = 8;
    if (tp_test_wait_for_connected(&client, producer.descriptor_publication) < 0)
    {
        goto cleanup;
    }

    memset(&tensor, 0, sizeof(tensor));
    tensor.dtype = tensor_pool_dtype_UINT8;
    tensor.major_order = tensor_pool_majorOrder_ROW;
    tensor.ndims = 1;
    tensor.progress_unit = tensor_pool_progressUnit_NONE;
    tensor.dims[0] = 32;

    memset(&frame, 0, sizeof(frame));
    frame.tensor = &tensor;
    frame.payload = payload;
    frame.payload_len = 32;
    frame.pool_id = 99;

    memset(&meta, 0, sizeof(meta));

    step = 9;
    if (tp_test_offer_frame_retry(&producer, &frame, &meta, &seq) < 0)
    {
        goto cleanup;
    }

    header_index = (uint32_t)(seq & (config.header_nslots - 1));
    if (tp_slot_decode(&slot_view, tp_slot_at(producer.header_region.addr, header_index),
        TP_HEADER_SLOT_BYTES, &client.context.base.log) < 0)
    {
        goto cleanup;
    }
    assert(slot_view.pool_id == 1);

    frame.payload_len = 80;
    tensor.dims[0] = 80;
    if (tp_test_offer_frame_retry(&producer, &frame, &meta, &seq) < 0)
    {
        goto cleanup;
    }
    header_index = (uint32_t)(seq & (config.header_nslots - 1));
    if (tp_slot_decode(&slot_view, tp_slot_at(producer.header_region.addr, header_index),
        TP_HEADER_SLOT_BYTES, &client.context.base.log) < 0)
    {
        goto cleanup;
    }
    assert(slot_view.pool_id == 2);

    frame.payload_len = 256;
    tensor.dims[0] = 256;
    step = 11;
    if (tp_producer_offer_frame(&producer, &frame, &meta) >= 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    if (producer.client)
    {
        tp_producer_close(&producer);
    }
    if (descriptor_sub)
    {
        aeron_subscription_close(descriptor_sub, NULL, NULL);
    }
    if (client.context.base.aeron_dir[0] != '\0')
    {
        tp_client_close(&client);
    }
    if (header_fd >= 0)
    {
        close(header_fd);
        unlink(header_path);
    }
    if (pool1_fd >= 0)
    {
        close(pool1_fd);
        unlink(pool1_path);
    }
    if (pool2_fd >= 0)
    {
        close(pool2_fd);
        unlink(pool2_path);
    }

    if (result != 0)
    {
        fprintf(stderr, "tp_test_producer_offer_pool_selection failed at step %d: %s\n", step, tp_errmsg());
    }
    assert(result == 0);
}

static void tp_test_producer_payload_flush(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    aeron_subscription_t *descriptor_sub = NULL;
    tp_frame_metadata_t meta;
    tp_frame_t frame;
    tp_tensor_header_t tensor;
    tp_buffer_claim_t claim;
    uint8_t payload[32];
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_header_flushXXXXXX";
    char pool_path[] = "/tmp/tp_pool_flushXXXXXX";
    char header_uri[512];
    char pool_uri[512];
    size_t header_size = TP_SUPERBLOCK_SIZE_BYTES + TP_HEADER_SLOT_BYTES * 4;
    size_t pool_size = TP_SUPERBLOCK_SIZE_BYTES + 128 * 4;
    int flush_calls = 0;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));
    memset(&claim, 0, sizeof(claim));

    header_fd = mkstemp(header_path);
    pool_fd = mkstemp(pool_path);
    if (header_fd < 0 || pool_fd < 0)
    {
        goto cleanup;
    }

    if (ftruncate(header_fd, (off_t)header_size) != 0 || ftruncate(pool_fd, (off_t)pool_size) != 0)
    {
        goto cleanup;
    }

    tp_test_write_superblock(header_fd, 7, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 7, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 128);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    {
        const char *candidates[] = { getenv("AERON_DIR"), "/dev/shm/aeron-dgamroth", "/dev/shm/aeron" };
        size_t i;
        int started = 0;

        for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        {
            if (tp_test_start_client(&client, &ctx, candidates[i]) == 0)
            {
                started = 1;
                break;
            }
        }

        if (!started)
        {
            result = 0;
            goto cleanup;
        }
    }

    if (tp_test_add_subscription(&client, "aeron:ipc", 1100, &descriptor_sub) < 0)
    {
        goto cleanup;
    }

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }

    tp_producer_context_set_payload_flush(&producer_ctx, tp_test_payload_flush, &flush_calls);

    if (tp_test_init_producer(&client, &producer_ctx, header_uri, pool_uri, &producer) < 0)
    {
        goto cleanup;
    }

    if (tp_test_wait_for_connected(&client, producer.descriptor_publication) < 0)
    {
        goto cleanup;
    }

    memset(&tensor, 0, sizeof(tensor));
    tensor.dtype = tensor_pool_dtype_UINT8;
    tensor.major_order = tensor_pool_majorOrder_ROW;
    tensor.ndims = 1;
    tensor.progress_unit = tensor_pool_progressUnit_NONE;
    tensor.dims[0] = (int32_t)sizeof(payload);
    tensor.strides[0] = 1;

    memset(payload, 0xCD, sizeof(payload));
    memset(&frame, 0, sizeof(frame));
    frame.tensor = &tensor;
    frame.payload = payload;
    frame.payload_len = (uint32_t)sizeof(payload);

    if (tp_test_offer_frame_retry(&producer, &frame, &meta, NULL) < 0)
    {
        goto cleanup;
    }

    assert(flush_calls >= 1);
    assert(tp_payload_flush_last_len == sizeof(payload));

    if (tp_producer_try_claim(&producer, 16, &claim) < 0)
    {
        goto cleanup;
    }

    memset(&claim.tensor, 0, sizeof(claim.tensor));
    claim.tensor.dtype = tensor_pool_dtype_UINT8;
    claim.tensor.major_order = tensor_pool_majorOrder_ROW;
    claim.tensor.ndims = 1;
    claim.tensor.progress_unit = tensor_pool_progressUnit_NONE;
    claim.tensor.dims[0] = 16;
    claim.tensor.strides[0] = 1;
    memset(claim.payload, 0xEF, claim.payload_len);

    memset(&meta, 0, sizeof(meta));
    meta.timestamp_ns = 101;

    if (tp_producer_commit_claim(&producer, &claim, &meta) < 0)
    {
        goto cleanup;
    }

    assert(flush_calls >= 2);
    assert(tp_payload_flush_last_len == claim.payload_len);

    result = 0;

cleanup:
    if (producer.client)
    {
        tp_producer_close(&producer);
    }
    if (descriptor_sub)
    {
        aeron_subscription_close(descriptor_sub, NULL, NULL);
    }
    if (client.context.base.aeron_dir[0] != '\0')
    {
        tp_client_close(&client);
    }
    if (header_fd >= 0)
    {
        close(header_fd);
    }
    if (pool_fd >= 0)
    {
        close(pool_fd);
    }
    unlink(header_path);
    unlink(pool_path);

    assert(result == 0);
}

void tp_test_producer_claim_lifecycle(void)
{
    tp_test_claim_lifecycle(true);
    tp_test_claim_lifecycle(false);
    tp_test_producer_invalid_tensor_header();
    tp_test_producer_offer_pool_selection();
    tp_test_producer_payload_flush();
}
