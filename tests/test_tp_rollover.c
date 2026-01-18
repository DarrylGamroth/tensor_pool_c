#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_tensor.h"

#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/shmRegionSuperblock.h"

#include "aeronc.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct tp_rollover_state_stct
{
    tp_consumer_t *consumer;
    int received;
    int errors;
}
tp_rollover_state_t;

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

static int tp_test_wait_for_publication(tp_client_t *client, aeron_publication_t *publication)
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

static int tp_test_wait_for_subscription(tp_client_t *client, aeron_subscription_t *subscription)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        if (aeron_subscription_is_connected(subscription))
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

static void tp_on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_rollover_state_t *state = (tp_rollover_state_t *)clientd;
    tp_frame_view_t frame;
    const float expected[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    int read_result;

    if (NULL == state || NULL == desc || NULL == state->consumer)
    {
        return;
    }

    read_result = tp_consumer_read_frame(state->consumer, desc->seq, &frame);
    if (read_result == 0)
    {
        state->received++;
        if (frame.payload_len != sizeof(expected) ||
            frame.tensor.dtype != TP_DTYPE_FLOAT32 ||
            frame.tensor.major_order != TP_MAJOR_ORDER_ROW ||
            frame.tensor.ndims != 2 ||
            frame.tensor.dims[0] != 2 ||
            frame.tensor.dims[1] != 2 ||
            memcmp(frame.payload, expected, sizeof(expected)) != 0)
        {
            state->errors++;
        }
    }
}

void tp_test_rollover(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    tp_consumer_context_t consumer_ctx;
    tp_consumer_t consumer;
    tp_producer_config_t producer_cfg;
    tp_consumer_config_t consumer_cfg;
    tp_payload_pool_config_t pool_cfg;
    tp_consumer_pool_config_t consumer_pool_cfg;
    tp_frame_t frame;
    tp_frame_metadata_t meta;
    tp_tensor_header_t header;
    tp_rollover_state_t state;
    const char *allowed_paths[] = { "/tmp" };
    const char *aeron_dir = NULL;
    char default_dir[AERON_MAX_PATH];
    char header_path[] = "/tmp/tp_rollover_headerXXXXXX";
    char pool_path[] = "/tmp/tp_rollover_poolXXXXXX";
    char header_uri[512];
    char pool_uri[512];
    int header_fd = -1;
    int pool_fd = -1;
    int result = -1;
    size_t header_size;
    size_t pool_size;
    const uint32_t stream_id = 10000;
    const uint16_t pool_id = 1;
    const uint32_t header_nslots = 4;
    const uint32_t pool_stride = 64;
    const uint32_t frame_count = 32;
    const uint64_t epoch = 1;
    const uint32_t layout_version = 1;
    size_t i;

    memset(&producer, 0, sizeof(producer));
    memset(&consumer, 0, sizeof(consumer));
    memset(&state, 0, sizeof(state));

    default_dir[0] = '\0';
    aeron_dir = getenv("AERON_DIR");
    if (NULL == aeron_dir || aeron_dir[0] == '\0')
    {
        if (aeron_default_path(default_dir, sizeof(default_dir)) >= 0 && default_dir[0] != '\0')
        {
            aeron_dir = default_dir;
        }
    }
    if (NULL == aeron_dir || aeron_dir[0] == '\0')
    {
        return;
    }

    header_fd = mkstemp(header_path);
    pool_fd = mkstemp(pool_path);
    if (header_fd < 0 || pool_fd < 0)
    {
        goto cleanup;
    }

    header_size = TP_SUPERBLOCK_SIZE_BYTES + (header_nslots * TP_HEADER_SLOT_BYTES);
    pool_size = TP_SUPERBLOCK_SIZE_BYTES + (header_nslots * pool_stride);

    if (ftruncate(header_fd, (off_t)header_size) != 0 || ftruncate(pool_fd, (off_t)pool_size) != 0)
    {
        goto cleanup;
    }

    tp_test_write_superblock(header_fd, stream_id, epoch, tensor_pool_regionType_HEADER_RING, 0, header_nslots, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, stream_id, epoch, tensor_pool_regionType_PAYLOAD_POOL, pool_id, header_nslots, TP_NULL_U32, pool_stride);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    {
        int started = 0;

        if (tp_client_context_init(&ctx) < 0)
        {
            goto cleanup;
        }
        tp_client_context_set_aeron_dir(&ctx, aeron_dir);
        tp_client_context_set_descriptor_channel(&ctx, "aeron:ipc", 1100);
        tp_context_set_allowed_paths(&ctx.base, allowed_paths, 1);
        if (tp_client_init(&client, &ctx) == 0 && tp_client_start(&client) == 0)
        {
            started = 1;
        }
        if (!started)
        {
            result = 0;
            goto cleanup;
        }
    }

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }
    producer_ctx.stream_id = stream_id;
    producer_ctx.producer_id = 1;

    if (tp_producer_init(&producer, &client, &producer_ctx) < 0)
    {
        goto cleanup;
    }

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = pool_id;
    pool_cfg.nslots = header_nslots;
    pool_cfg.stride_bytes = pool_stride;
    pool_cfg.uri = pool_uri;

    memset(&producer_cfg, 0, sizeof(producer_cfg));
    producer_cfg.stream_id = stream_id;
    producer_cfg.producer_id = 1;
    producer_cfg.epoch = epoch;
    producer_cfg.layout_version = layout_version;
    producer_cfg.header_nslots = header_nslots;
    producer_cfg.header_uri = header_uri;
    producer_cfg.pools = &pool_cfg;
    producer_cfg.pool_count = 1;

    if (tp_producer_attach(&producer, &producer_cfg) < 0)
    {
        goto cleanup;
    }

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }
    consumer_ctx.stream_id = stream_id;
    consumer_ctx.consumer_id = 2;

    if (tp_consumer_init(&consumer, &client, &consumer_ctx) < 0)
    {
        goto cleanup;
    }

    memset(&consumer_pool_cfg, 0, sizeof(consumer_pool_cfg));
    consumer_pool_cfg.pool_id = pool_id;
    consumer_pool_cfg.nslots = header_nslots;
    consumer_pool_cfg.stride_bytes = pool_stride;
    consumer_pool_cfg.uri = pool_uri;

    memset(&consumer_cfg, 0, sizeof(consumer_cfg));
    consumer_cfg.stream_id = stream_id;
    consumer_cfg.epoch = epoch;
    consumer_cfg.layout_version = layout_version;
    consumer_cfg.header_nslots = header_nslots;
    consumer_cfg.header_uri = header_uri;
    consumer_cfg.pools = &consumer_pool_cfg;
    consumer_cfg.pool_count = 1;

    if (tp_consumer_attach(&consumer, &consumer_cfg) < 0)
    {
        goto cleanup;
    }

    if (tp_test_wait_for_publication(&client, producer.descriptor_publication) < 0)
    {
        goto cleanup;
    }

    if (tp_test_wait_for_subscription(&client, consumer.descriptor_subscription) < 0)
    {
        goto cleanup;
    }

    memset(&header, 0, sizeof(header));
    header.dtype = TP_DTYPE_FLOAT32;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.ndims = 2;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = 2;
    header.dims[1] = 2;
    if (tp_tensor_header_validate(&header, NULL) < 0)
    {
        goto cleanup;
    }

    memset(&frame, 0, sizeof(frame));
    frame.tensor = &header;
    frame.payload_len = sizeof(float) * 4;
    frame.payload = (float[4]){ 1.0f, 2.0f, 3.0f, 4.0f };
    frame.pool_id = pool_id;
    meta.timestamp_ns = 0;
    meta.meta_version = 0;

    state.consumer = &consumer;
    tp_consumer_set_descriptor_handler(&consumer, tp_on_descriptor, &state);

    for (i = 0; i < frame_count; i++)
    {
        if (tp_producer_offer_frame(&producer, &frame, &meta) < 0)
        {
            goto cleanup;
        }
    }

    {
        int64_t deadline = tp_clock_now_ns() + 5 * 1000 * 1000 * 1000LL;
        while (state.received < (int)frame_count && tp_clock_now_ns() < deadline)
        {
            tp_consumer_poll_descriptors(&consumer, 10);
        }
    }

    if (state.received == 0 || state.errors != 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    if (consumer.client)
    {
        tp_consumer_close(&consumer);
    }
    if (producer.client)
    {
        tp_producer_close(&producer);
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
