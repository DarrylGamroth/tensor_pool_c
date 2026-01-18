#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_shm.h"

#include "aeronc.h"

#include "driver/tensor_pool/messageHeader.h"
#include "driver/tensor_pool/leaseRevokeReason.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/shmLeaseRevoked.h"

#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/shmRegionSuperblock.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static int tp_test_start_client(tp_client_t *client, tp_client_context_t *ctx, const char *aeron_dir)
{
    const char *allowed_paths[] = { "/tmp" };

    if (tp_client_context_init(ctx) < 0)
    {
        return -1;
    }

    tp_client_context_set_aeron_dir(ctx, aeron_dir);
    tp_client_context_set_control_channel(ctx, "aeron:ipc", 1000);
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

static int tp_test_start_client_any(tp_client_t *client, tp_client_context_t *ctx)
{
    char default_dir[AERON_MAX_PATH];
    const char *env_dir = getenv("AERON_DIR");
    const char *candidates[2];
    size_t candidate_count = 0;
    size_t i;

    default_dir[0] = '\0';
    if (aeron_default_path(default_dir, sizeof(default_dir)) < 0)
    {
        default_dir[0] = '\0';
    }

    if (NULL != env_dir && env_dir[0] != '\0')
    {
        candidates[candidate_count++] = env_dir;
    }
    if (default_dir[0] != '\0')
    {
        candidates[candidate_count++] = default_dir;
    }

    for (i = 0; i < candidate_count; i++)
    {
        if (NULL == candidates[i])
        {
            continue;
        }
        if (tp_test_start_client(client, ctx, candidates[i]) == 0)
        {
            return 0;
        }
    }

    return -1;
}

static int tp_test_add_publication(tp_client_t *client, const char *channel, int32_t stream_id, aeron_publication_t **out)
{
    aeron_async_add_publication_t *async_add = NULL;

    if (tp_client_async_add_publication(client, channel, stream_id, &async_add) < 0)
    {
        return -1;
    }

    *out = NULL;
    while (NULL == *out)
    {
        if (tp_client_async_add_publication_poll(out, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(client);
    }

    return 0;
}

static int tp_test_offer(tp_client_t *client, aeron_publication_t *pub, const uint8_t *buffer, size_t length)
{
    int64_t result;
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        result = aeron_publication_offer(pub, buffer, length, NULL, NULL);
        if (result >= 0)
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

void tp_test_consumer_lease_revoked(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_consumer_context_t consumer_ctx;
    tp_consumer_t consumer;
    aeron_publication_t *control_pub = NULL;
    tp_consumer_config_t config;
    tp_consumer_pool_config_t pool_cfg;
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmLeaseRevoked revoked;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_lease_headerXXXXXX";
    char pool_path[] = "/tmp/tp_lease_poolXXXXXX";
    char header_uri[256];
    char pool_uri[256];
    int result = -1;
    int64_t deadline;

    memset(&client, 0, sizeof(client));
    memset(&consumer, 0, sizeof(consumer));

    if (tp_test_start_client_any(&client, &ctx) < 0)
    {
        return;
    }

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }
    consumer_ctx.stream_id = 76001;
    consumer_ctx.consumer_id = 42;
    consumer_ctx.use_driver = false;

    if (tp_consumer_init(&consumer, &client, &consumer_ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1000, &control_pub) < 0)
    {
        goto cleanup;
    }

    header_fd = mkstemp(header_path);
    pool_fd = mkstemp(pool_path);
    if (header_fd < 0 || pool_fd < 0)
    {
        goto cleanup;
    }

    if (ftruncate(header_fd, TP_SUPERBLOCK_SIZE_BYTES + TP_HEADER_SLOT_BYTES * 4) != 0 ||
        ftruncate(pool_fd, TP_SUPERBLOCK_SIZE_BYTES + 64 * 4) != 0)
    {
        goto cleanup;
    }

    tp_test_write_superblock(header_fd, 76001, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 76001, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 64);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = 1;
    pool_cfg.nslots = 4;
    pool_cfg.stride_bytes = 64;
    pool_cfg.uri = pool_uri;

    memset(&config, 0, sizeof(config));
    config.stream_id = 76001;
    config.epoch = 1;
    config.layout_version = 1;
    config.header_nslots = 4;
    config.header_uri = header_uri;
    config.pools = &pool_cfg;
    config.pool_count = 1;

    if (tp_consumer_attach(&consumer, &config) < 0)
    {
        goto cleanup;
    }

    consumer.context.use_driver = true;
    consumer.driver_attached = true;
    consumer.driver.active_lease_id = 55;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmLeaseRevoked_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmLeaseRevoked_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmLeaseRevoked_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmLeaseRevoked_sbe_schema_version());

    tensor_pool_shmLeaseRevoked_wrap_for_encode(
        &revoked,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(buffer));
    tensor_pool_shmLeaseRevoked_set_timestampNs(&revoked, (uint64_t)tp_clock_now_ns());
    tensor_pool_shmLeaseRevoked_set_leaseId(&revoked, 55);
    tensor_pool_shmLeaseRevoked_set_streamId(&revoked, 76001);
    tensor_pool_shmLeaseRevoked_set_clientId(&revoked, consumer.context.consumer_id);
    tensor_pool_shmLeaseRevoked_set_role(&revoked, tensor_pool_role_CONSUMER);
    tensor_pool_shmLeaseRevoked_set_reason(&revoked, tensor_pool_leaseRevokeReason_REVOKED);
    tensor_pool_shmLeaseRevoked_put_errorMessage(&revoked, "test", 4);

    if (tp_test_offer(&client, control_pub, buffer, (size_t)tensor_pool_shmLeaseRevoked_sbe_position(&revoked)) < 0)
    {
        goto cleanup;
    }

    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        tp_consumer_poll_control(&consumer, 10);
        tp_client_do_work(&client);
        if (!consumer.shm_mapped && consumer.reattach_requested)
        {
            result = 0;
            break;
        }
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

cleanup:
    if (control_pub)
    {
        aeron_publication_close(control_pub, NULL, NULL);
    }
    if (consumer.client)
    {
        tp_consumer_close(&consumer);
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
    assert(tp_consumer_reattach_due(&consumer, consumer.next_attach_ns) == 1);
}

void tp_test_producer_lease_revoked(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    aeron_publication_t *control_pub = NULL;
    tp_producer_config_t config;
    tp_payload_pool_config_t pool_cfg;
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmLeaseRevoked revoked;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_lease_prod_headerXXXXXX";
    char pool_path[] = "/tmp/tp_lease_prod_poolXXXXXX";
    char header_uri[256];
    char pool_uri[256];
    int result = -1;
    int64_t deadline;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));

    if (tp_test_start_client_any(&client, &ctx) < 0)
    {
        return;
    }

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }
    producer_ctx.stream_id = 76002;
    producer_ctx.producer_id = 24;
    producer_ctx.use_driver = false;

    if (tp_producer_init(&producer, &client, &producer_ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1000, &control_pub) < 0)
    {
        goto cleanup;
    }

    header_fd = mkstemp(header_path);
    pool_fd = mkstemp(pool_path);
    if (header_fd < 0 || pool_fd < 0)
    {
        goto cleanup;
    }

    if (ftruncate(header_fd, TP_SUPERBLOCK_SIZE_BYTES + TP_HEADER_SLOT_BYTES * 4) != 0 ||
        ftruncate(pool_fd, TP_SUPERBLOCK_SIZE_BYTES + 64 * 4) != 0)
    {
        goto cleanup;
    }

    tp_test_write_superblock(header_fd, 76002, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 76002, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 64);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = 1;
    pool_cfg.nslots = 4;
    pool_cfg.stride_bytes = 64;
    pool_cfg.uri = pool_uri;

    memset(&config, 0, sizeof(config));
    config.stream_id = 76002;
    config.producer_id = 24;
    config.epoch = 1;
    config.layout_version = 1;
    config.header_nslots = 4;
    config.header_uri = header_uri;
    config.pools = &pool_cfg;
    config.pool_count = 1;

    if (tp_producer_attach(&producer, &config) < 0)
    {
        goto cleanup;
    }

    producer.context.use_driver = true;
    producer.driver_attached = true;
    producer.driver.active_lease_id = 77;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmLeaseRevoked_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmLeaseRevoked_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmLeaseRevoked_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmLeaseRevoked_sbe_schema_version());

    tensor_pool_shmLeaseRevoked_wrap_for_encode(
        &revoked,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(buffer));
    tensor_pool_shmLeaseRevoked_set_timestampNs(&revoked, (uint64_t)tp_clock_now_ns());
    tensor_pool_shmLeaseRevoked_set_leaseId(&revoked, 77);
    tensor_pool_shmLeaseRevoked_set_streamId(&revoked, 76002);
    tensor_pool_shmLeaseRevoked_set_clientId(&revoked, producer.context.producer_id);
    tensor_pool_shmLeaseRevoked_set_role(&revoked, tensor_pool_role_PRODUCER);
    tensor_pool_shmLeaseRevoked_set_reason(&revoked, tensor_pool_leaseRevokeReason_REVOKED);
    tensor_pool_shmLeaseRevoked_put_errorMessage(&revoked, "test", 4);

    if (tp_test_offer(&client, control_pub, buffer, (size_t)tensor_pool_shmLeaseRevoked_sbe_position(&revoked)) < 0)
    {
        goto cleanup;
    }

    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        tp_producer_poll_control(&producer, 10);
        tp_client_do_work(&client);
        if (producer.pool_count == 0 && producer.reattach_requested)
        {
            result = 0;
            break;
        }
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

cleanup:
    if (control_pub)
    {
        aeron_publication_close(control_pub, NULL, NULL);
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
    assert(tp_producer_reattach_due(&producer, producer.next_attach_ns) == 1);
}
