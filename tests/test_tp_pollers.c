#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_consumer_manager.h"
#include "tensor_pool/tp_control_poller.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_metadata_poller.h"
#include "tensor_pool/tp_progress_poller.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_qos.h"
#include "tensor_pool/tp_slot.h"

#include "aeronc.h"

#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/frameDescriptor.h"
#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/metaBlobAnnounce.h"
#include "wire/tensor_pool/metaBlobChunk.h"
#include "wire/tensor_pool/metaBlobComplete.h"
#include "wire/tensor_pool/mode.h"
#include "wire/tensor_pool/qosConsumer.h"
#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/shmPoolAnnounce.h"
#include "wire/tensor_pool/shmRegionSuperblock.h"
#include "wire/tensor_pool/slotHeader.h"
#include "wire/tensor_pool/tensorHeader.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct tp_poll_state_stct
{
    int saw_hello;
    int saw_qos;
    int saw_announce;
    int saw_meta;
    int saw_meta_blob_announce;
    int saw_meta_blob_chunk;
    int saw_meta_blob_complete;
    int saw_progress;
    int progress_count;
    int saw_descriptor;
}
tp_poll_state_t;

static void tp_on_progress_capture(void *clientd, const tp_frame_progress_t *progress)
{
    int *count = (int *)clientd;
    (void)progress;
    if (count)
    {
        (*count)++;
    }
}

static int tp_test_start_client(
    tp_client_t *client,
    tp_client_context_t *ctx,
    const char *aeron_dir,
    uint64_t announce_period_ns)
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
    tp_client_context_set_control_channel(ctx, "aeron:ipc", 1000);
    tp_client_context_set_qos_channel(ctx, "aeron:ipc", 1200);
    tp_client_context_set_metadata_channel(ctx, "aeron:ipc", 1300);
    tp_client_context_set_descriptor_channel(ctx, "aeron:ipc", 1100);
    tp_client_context_set_use_agent_invoker(ctx, true);
    if (announce_period_ns > 0)
    {
        tp_client_context_set_announce_period_ns(ctx, announce_period_ns);
    }
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

static int tp_test_start_client_any(
    tp_client_t *client,
    tp_client_context_t *ctx,
    uint64_t announce_period_ns)
{
    const char *candidates[] = { getenv("AERON_DIR"), "/dev/shm/aeron-dgamroth", "/dev/shm/aeron" };
    size_t i;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        if (NULL == candidates[i])
        {
            continue;
        }
        if (tp_test_start_client(client, ctx, candidates[i], announce_period_ns) == 0)
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

static int tp_test_offer(tp_client_t *client, aeron_publication_t *pub, const uint8_t *buffer, size_t length)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        int64_t result = aeron_publication_offer(pub, buffer, length, NULL, NULL);
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

static int tp_test_wait_for_publication(tp_client_t *client, aeron_publication_t *pub)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    if (NULL == pub)
    {
        return -1;
    }

    while (tp_clock_now_ns() < deadline)
    {
        if (aeron_publication_is_connected(pub))
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

static int tp_test_offer_frame(tp_producer_t *producer, const tp_frame_t *frame, tp_frame_metadata_t *meta)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        int64_t result = tp_producer_offer_frame(producer, frame, meta);
        if (result >= 0)
        {
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
    tensor_pool_shmRegionSuperblock_set_activityTimestampNs(&block, 1);

    lseek(fd, 0, SEEK_SET);
    (void)write(fd, buffer, sizeof(buffer));
}

static tp_consumer_entry_t *tp_test_find_consumer_entry(tp_consumer_manager_t *manager, uint32_t consumer_id)
{
    size_t i;

    if (NULL == manager)
    {
        return NULL;
    }

    for (i = 0; i < manager->registry.capacity; i++)
    {
        tp_consumer_entry_t *entry = &manager->registry.entries[i];
        if (entry->in_use && entry->consumer_id == consumer_id)
        {
            return entry;
        }
    }

    return NULL;
}

typedef struct tp_cadence_state_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    uint32_t consumer_id;
    int producer_qos_count;
    int consumer_qos_count;
    int announce_count;
    int meta_count;
}
tp_cadence_state_t;

static void tp_on_cadence_qos(void *clientd, const tp_qos_event_t *event)
{
    tp_cadence_state_t *state = (tp_cadence_state_t *)clientd;
    if (NULL == state || NULL == event)
    {
        return;
    }

    if (event->type == TP_QOS_EVENT_PRODUCER &&
        event->stream_id == state->stream_id &&
        event->producer_id == state->producer_id)
    {
        state->producer_qos_count++;
    }
    else if (event->type == TP_QOS_EVENT_CONSUMER &&
        event->stream_id == state->stream_id &&
        event->consumer_id == state->consumer_id)
    {
        state->consumer_qos_count++;
    }
}

static void tp_on_cadence_announce(const tp_data_source_announce_view_t *view, void *clientd)
{
    tp_cadence_state_t *state = (tp_cadence_state_t *)clientd;
    if (state && view && view->stream_id == state->stream_id)
    {
        state->announce_count++;
    }
}

static void tp_on_cadence_meta_begin(const tp_data_source_meta_view_t *view, void *clientd)
{
    tp_cadence_state_t *state = (tp_cadence_state_t *)clientd;
    if (state && view && view->stream_id == state->stream_id)
    {
        state->meta_count++;
    }
}

static void tp_on_consumer_hello(const tp_consumer_hello_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_hello = 1;
    }
}

static void tp_on_qos_event(void *clientd, const tp_qos_event_t *event)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (event)
    {
        state->saw_qos = 1;
    }
}

static void tp_on_announce(const tp_data_source_announce_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_announce = 1;
    }
}

static void tp_on_meta_begin(const tp_data_source_meta_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_meta = 1;
    }
}

static void tp_on_meta_blob_announce(const tp_meta_blob_announce_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_meta_blob_announce = 1;
    }
}

static void tp_on_meta_blob_chunk(const tp_meta_blob_chunk_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_meta_blob_chunk = 1;
    }
}

static void tp_on_meta_blob_complete(const tp_meta_blob_complete_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_meta_blob_complete = 1;
    }
}

static void tp_on_progress(void *clientd, const tp_frame_progress_t *progress)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (progress)
    {
        state->saw_progress = 1;
        state->progress_count++;
    }
}

static void tp_on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (desc)
    {
        state->saw_descriptor = 1;
    }
}

static void tp_on_descriptor_fragment(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    int *count = (int *)clientd;
    (void)buffer;
    (void)length;
    (void)header;
    if (count)
    {
        (*count)++;
    }
}

void tp_test_cadence(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    tp_consumer_context_t consumer_ctx;
    tp_consumer_t consumer;
    tp_qos_poller_t qos_poller;
    tp_qos_handlers_t qos_handlers;
    tp_metadata_poller_t meta_poller;
    tp_metadata_handlers_t meta_handlers;
    tp_cadence_state_t state;
    tp_data_source_announce_t announce;
    tp_meta_attribute_t attr;
    tp_data_source_meta_t meta;
    uint32_t fmt = 1;
    uint64_t last_producer_qos_ns = 0;
    uint64_t last_consumer_qos_ns = 0;
    uint64_t last_announce_ns = 0;
    uint64_t last_meta_ns = 0;
    int producer_qos_updates = 0;
    int consumer_qos_updates = 0;
    int announce_updates = 0;
    int meta_updates = 0;
    int result = -1;
    int64_t deadline;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));
    memset(&consumer, 0, sizeof(consumer));
    memset(&state, 0, sizeof(state));

    if (tp_test_start_client_any(&client, &ctx, 5 * 1000 * 1000ULL) < 0)
    {
        return;
    }

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }
    producer_ctx.stream_id = 50001;
    producer_ctx.producer_id = 7;

    if (tp_producer_init(&producer, &client, &producer_ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }
    consumer_ctx.stream_id = 50001;
    consumer_ctx.consumer_id = 9;

    if (tp_consumer_init(&consumer, &client, &consumer_ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_test_wait_for_publication(&client, producer.qos_publication) < 0 ||
        tp_test_wait_for_publication(&client, producer.metadata_publication) < 0 ||
        tp_test_wait_for_publication(&client, consumer.qos_publication) < 0)
    {
        goto cleanup;
    }

    state.stream_id = 50001;
    state.producer_id = 7;
    state.consumer_id = 9;

    memset(&qos_handlers, 0, sizeof(qos_handlers));
    qos_handlers.on_qos_event = tp_on_cadence_qos;
    qos_handlers.clientd = &state;
    if (tp_qos_poller_init(&qos_poller, &client, &qos_handlers) < 0)
    {
        goto cleanup;
    }

    memset(&meta_handlers, 0, sizeof(meta_handlers));
    meta_handlers.on_data_source_announce = tp_on_cadence_announce;
    meta_handlers.on_data_source_meta_begin = tp_on_cadence_meta_begin;
    meta_handlers.clientd = &state;
    if (tp_metadata_poller_init(&meta_poller, &client, &meta_handlers) < 0)
    {
        goto cleanup;
    }

    memset(&announce, 0, sizeof(announce));
    announce.stream_id = 50001;
    announce.producer_id = 7;
    announce.epoch = 1;
    announce.meta_version = 1;
    announce.name = "cadence";
    announce.summary = "cadence";
    if (tp_producer_set_data_source_announce(&producer, &announce) < 0)
    {
        goto cleanup;
    }

    attr.key = "pixel_format";
    attr.format = "u32";
    attr.value = (const uint8_t *)&fmt;
    attr.value_length = sizeof(fmt);
    meta.stream_id = 50001;
    meta.meta_version = 1;
    meta.timestamp_ns = tp_clock_now_ns();
    meta.attributes = &attr;
    meta.attribute_count = 1;
    if (tp_producer_set_data_source_meta(&producer, &meta) < 0)
    {
        goto cleanup;
    }

    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        tp_producer_poll_control(&producer, 10);
        tp_consumer_poll_control(&consumer, 10);
        tp_qos_poll(&qos_poller, 10);
        tp_metadata_poll(&meta_poller, 10);
        tp_client_do_work(&client);

        if (producer.last_qos_ns != 0 && producer.last_qos_ns != last_producer_qos_ns)
        {
            last_producer_qos_ns = producer.last_qos_ns;
            producer_qos_updates++;
        }
        if (consumer.last_qos_ns != 0 && consumer.last_qos_ns != last_consumer_qos_ns)
        {
            last_consumer_qos_ns = consumer.last_qos_ns;
            consumer_qos_updates++;
        }
        if (producer.last_announce_ns != 0 && producer.last_announce_ns != last_announce_ns)
        {
            last_announce_ns = producer.last_announce_ns;
            announce_updates++;
        }
        if (producer.last_meta_ns != 0 && producer.last_meta_ns != last_meta_ns)
        {
            last_meta_ns = producer.last_meta_ns;
            meta_updates++;
        }

        if ((state.producer_qos_count >= 2 &&
            state.consumer_qos_count >= 2 &&
            state.announce_count >= 2 &&
            state.meta_count >= 2) ||
            (producer_qos_updates >= 2 &&
                consumer_qos_updates >= 2 &&
                announce_updates >= 2 &&
                meta_updates >= 2))
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
    if (producer.client)
    {
        tp_producer_close(&producer);
    }
    if (consumer.client)
    {
        tp_consumer_close(&consumer);
    }
    if (qos_poller.assembler)
    {
        aeron_fragment_assembler_delete(qos_poller.assembler);
    }
    if (meta_poller.assembler)
    {
        aeron_fragment_assembler_delete(meta_poller.assembler);
    }
    if (client.context.base.aeron_dir[0] != '\0')
    {
        tp_client_close(&client);
    }

    assert(result == 0);
}

void tp_test_shm_announce_freshness(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_consumer_context_t consumer_ctx;
    tp_consumer_t consumer;
    aeron_publication_t *control_pub = NULL;
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmPoolAnnounce announce;
    struct tensor_pool_shmPoolAnnounce_payloadPools pools;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_announce_headerXXXXXX";
    char pool_path[] = "/tmp/tp_announce_poolXXXXXX";
    char header_uri[256];
    char pool_uri[256];
    uint64_t now_ns;
    uint64_t freshness_ns;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&consumer, 0, sizeof(consumer));

    if (tp_test_start_client_any(&client, &ctx, 0) < 0)
    {
        return;
    }

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }
    consumer_ctx.stream_id = 60001;
    consumer_ctx.consumer_id = 3;

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

    tp_test_write_superblock(header_fd, 60001, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 60001, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 64);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    now_ns = (uint64_t)tp_clock_now_ns();
    freshness_ns = client.context.base.announce_period_ns * TP_ANNOUNCE_FRESHNESS_MULTIPLIER;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmPoolAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmPoolAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmPoolAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmPoolAnnounce_sbe_schema_version());

    tensor_pool_shmPoolAnnounce_wrap_for_encode(&announce, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmPoolAnnounce_set_streamId(&announce, 60001);
    tensor_pool_shmPoolAnnounce_set_producerId(&announce, 2);
    tensor_pool_shmPoolAnnounce_set_epoch(&announce, 1);
    tensor_pool_shmPoolAnnounce_set_layoutVersion(&announce, 1);
    tensor_pool_shmPoolAnnounce_set_headerNslots(&announce, 4);
    tensor_pool_shmPoolAnnounce_set_headerSlotBytes(&announce, TP_HEADER_SLOT_BYTES);
    tensor_pool_shmPoolAnnounce_set_announceClockDomain(&announce, tensor_pool_clockDomain_MONOTONIC);
    tensor_pool_shmPoolAnnounce_set_announceTimestampNs(&announce, now_ns - (freshness_ns / 2));

    tensor_pool_shmPoolAnnounce_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_shmPoolAnnounce_sbe_position_ptr(&announce),
        tensor_pool_shmPoolAnnounce_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_shmPoolAnnounce_payloadPools_next(&pools);
    tensor_pool_shmPoolAnnounce_payloadPools_set_poolId(&pools, 1);
    tensor_pool_shmPoolAnnounce_payloadPools_set_poolNslots(&pools, 4);
    tensor_pool_shmPoolAnnounce_payloadPools_set_strideBytes(&pools, 64);
    tensor_pool_shmPoolAnnounce_payloadPools_put_regionUri(&pools, pool_uri, strlen(pool_uri));
    tensor_pool_shmPoolAnnounce_put_headerRegionUri(&announce, header_uri, strlen(header_uri));

    if (tp_test_offer(&client, control_pub, buffer, (size_t)tensor_pool_shmPoolAnnounce_sbe_position(&announce)) < 0)
    {
        goto cleanup;
    }

    {
        int i;
        for (i = 0; i < 50; i++)
        {
            tp_consumer_poll_control(&consumer, 10);
            tp_client_do_work(&client);
            {
                struct timespec ts = { 0, 1000000 };
                nanosleep(&ts, NULL);
            }
        }
        assert(!consumer.shm_mapped);
    }

    now_ns = (uint64_t)tp_clock_now_ns();
    tensor_pool_shmPoolAnnounce_set_announceTimestampNs(&announce, now_ns);
    if (tp_test_offer(&client, control_pub, buffer, (size_t)tensor_pool_shmPoolAnnounce_sbe_position(&announce)) < 0)
    {
        goto cleanup;
    }

    {
        int64_t map_deadline = tp_clock_now_ns() + 200 * 1000 * 1000LL;
        while (tp_clock_now_ns() < map_deadline)
        {
            tp_consumer_poll_control(&consumer, 10);
            tp_client_do_work(&client);
            if (consumer.shm_mapped)
            {
                result = 0;
                break;
            }
            {
                struct timespec ts = { 0, 1000000 };
                nanosleep(&ts, NULL);
            }
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
}

void tp_test_rate_limit(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    tp_consumer_hello_view_t hello;
    tp_consumer_manager_t *manager;
    tp_consumer_entry_t *entry = NULL;
    aeron_subscription_t *descriptor_sub = NULL;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_rate_headerXXXXXX";
    char pool_path[] = "/tmp/tp_rate_poolXXXXXX";
    char header_uri[256];
    char pool_uri[256];
    tp_producer_config_t config;
    tp_payload_pool_config_t pool_cfg;
    uint8_t payload[16];
    tp_tensor_header_t tensor;
    tp_frame_t frame;
    tp_frame_metadata_t meta;
    int result = -1;
    int fragments = 0;
    int64_t deadline;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));

    if (tp_test_start_client_any(&client, &ctx, 0) < 0)
    {
        return;
    }

    client.context.base.descriptor_channel[0] = '\0';
    client.context.base.descriptor_stream_id = -1;

    if (tp_test_add_subscription(&client, "aeron:ipc", 2100, &descriptor_sub) < 0)
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

    tp_test_write_superblock(header_fd, 70001, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 70001, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 64);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }
    producer_ctx.stream_id = 70001;
    producer_ctx.producer_id = 5;

    if (tp_producer_init(&producer, &client, &producer_ctx) < 0)
    {
        goto cleanup;
    }

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = 1;
    pool_cfg.nslots = 4;
    pool_cfg.stride_bytes = 64;
    pool_cfg.uri = pool_uri;

    memset(&config, 0, sizeof(config));
    config.stream_id = 70001;
    config.producer_id = 5;
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

    if (tp_producer_enable_consumer_manager(&producer, 4) < 0)
    {
        goto cleanup;
    }
    manager = producer.consumer_manager;

    memset(&hello, 0, sizeof(hello));
    hello.stream_id = 70001;
    hello.consumer_id = 44;
    hello.supports_shm = 1;
    hello.supports_progress = 1;
    hello.mode = TP_MODE_RATE_LIMITED;
    hello.max_rate_hz = 1;
    hello.descriptor_stream_id = 2100;
    hello.descriptor_channel.data = "aeron:ipc";
    hello.descriptor_channel.length = 9;

    if (tp_consumer_manager_handle_hello(manager, &hello, (uint64_t)tp_clock_now_ns()) < 0)
    {
        goto cleanup;
    }

    entry = tp_test_find_consumer_entry(manager, 44);
    if (NULL == entry || NULL == entry->descriptor_publication)
    {
        goto cleanup;
    }
    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        if (aeron_subscription_is_connected(descriptor_sub) &&
            aeron_publication_is_connected(entry->descriptor_publication))
        {
            break;
        }
        tp_client_do_work(&client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }
    if (!aeron_subscription_is_connected(descriptor_sub) ||
        !aeron_publication_is_connected(entry->descriptor_publication))
    {
        goto cleanup;
    }

    memset(&tensor, 0, sizeof(tensor));
    tensor.dtype = tensor_pool_dtype_UINT8;
    tensor.major_order = tensor_pool_majorOrder_ROW;
    tensor.ndims = 1;
    tensor.progress_unit = tensor_pool_progressUnit_NONE;
    tensor.dims[0] = sizeof(payload);
    tensor.strides[0] = 1;
    memset(payload, 0xAB, sizeof(payload));

    memset(&frame, 0, sizeof(frame));
    frame.tensor = &tensor;
    frame.payload = payload;
    frame.payload_len = sizeof(payload);
    frame.pool_id = 1;
    frame.trace_id = 0;

    memset(&meta, 0, sizeof(meta));
    meta.timestamp_ns = 1;
    meta.meta_version = 1;

    if (tp_test_offer_frame(&producer, &frame, &meta) < 0)
    {
        goto cleanup;
    }
    if (tp_test_offer_frame(&producer, &frame, &meta) < 0)
    {
        goto cleanup;
    }

    deadline = tp_clock_now_ns() + 500 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        aeron_subscription_poll(descriptor_sub, tp_on_descriptor_fragment, &fragments, 10);
        if (fragments >= 1)
        {
            break;
        }
        tp_client_do_work(&client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

    if (fragments < 1)
    {
        goto cleanup;
    }

    {
        int initial = fragments;
        deadline = tp_clock_now_ns() + 300 * 1000 * 1000LL;
        while (tp_clock_now_ns() < deadline)
        {
            aeron_subscription_poll(descriptor_sub, tp_on_descriptor_fragment, &fragments, 10);
            tp_client_do_work(&client);
            {
                struct timespec ts = { 0, 1000000 };
                nanosleep(&ts, NULL);
            }
        }

        if (fragments == initial)
        {
            result = 0;
        }
    }

cleanup:
    if (descriptor_sub)
    {
        aeron_subscription_close(descriptor_sub, NULL, NULL);
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

void tp_test_qos_liveness(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_context_t producer_ctx;
    tp_producer_t producer;
    tp_consumer_hello_view_t hello;
    tp_consumer_manager_t *manager;
    tp_consumer_entry_t *entry = NULL;
    aeron_publication_t *qos_pub = NULL;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_qos_headerXXXXXX";
    char pool_path[] = "/tmp/tp_qos_poolXXXXXX";
    char header_uri[256];
    char pool_uri[256];
    tp_producer_config_t config;
    tp_payload_pool_config_t pool_cfg;
    uint8_t buffer[128];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_qosConsumer qos;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_qosConsumer_sbe_block_length();
    int result = -1;
    int64_t deadline;
    uint64_t stale_ns = 5ULL * 1000 * 1000 * 1000ULL;
    uint64_t old_ns;

    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));

    if (tp_test_start_client_any(&client, &ctx, 0) < 0)
    {
        return;
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

    tp_test_write_superblock(header_fd, 72001, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 72001, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 64);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }
    producer_ctx.stream_id = 72001;
    producer_ctx.producer_id = 6;

    if (tp_producer_init(&producer, &client, &producer_ctx) < 0)
    {
        goto cleanup;
    }

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = 1;
    pool_cfg.nslots = 4;
    pool_cfg.stride_bytes = 64;
    pool_cfg.uri = pool_uri;

    memset(&config, 0, sizeof(config));
    config.stream_id = 72001;
    config.producer_id = 6;
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

    if (tp_producer_enable_consumer_manager(&producer, 4) < 0)
    {
        goto cleanup;
    }
    manager = producer.consumer_manager;

    memset(&hello, 0, sizeof(hello));
    hello.stream_id = 72001;
    hello.consumer_id = 55;
    hello.supports_shm = 1;
    hello.supports_progress = 1;
    hello.mode = TP_MODE_STREAM;
    old_ns = (uint64_t)tp_clock_now_ns() - stale_ns - 1000000ULL;

    if (tp_consumer_manager_handle_hello(manager, &hello, old_ns) < 0)
    {
        goto cleanup;
    }

    entry = tp_test_find_consumer_entry(manager, 55);
    if (NULL == entry)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1200, &qos_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_wait_for_publication(&client, qos_pub) < 0)
    {
        goto cleanup;
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
    tensor_pool_qosConsumer_set_streamId(&qos, 72001);
    tensor_pool_qosConsumer_set_consumerId(&qos, 55);
    tensor_pool_qosConsumer_set_epoch(&qos, 1);
    tensor_pool_qosConsumer_set_lastSeqSeen(&qos, 0);
    tensor_pool_qosConsumer_set_dropsGap(&qos, 0);
    tensor_pool_qosConsumer_set_dropsLate(&qos, 0);
    tensor_pool_qosConsumer_set_mode(&qos, tensor_pool_mode_STREAM);

    if (tp_test_offer(&client, qos_pub, buffer, header_len + body_len) < 0)
    {
        goto cleanup;
    }

    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        if (tp_producer_poll_control(&producer, 10) < 0)
        {
            goto cleanup;
        }

        entry = tp_test_find_consumer_entry(manager, 55);
        if (entry && entry->in_use && entry->last_seen_ns > old_ns)
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
    if (qos_pub)
    {
        aeron_publication_close(qos_pub, NULL, NULL);
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

void tp_test_progress_per_consumer_control(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_consumer_context_t consumer_ctx;
    tp_consumer_t consumer;
    tp_consumer_config_t consumer_config;
    tp_consumer_pool_config_t pool_cfg;
    aeron_publication_t *control_pub = NULL;
    aeron_publication_t *progress_pub = NULL;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_progress_headerXXXXXX";
    char pool_path[] = "/tmp/tp_progress_poolXXXXXX";
    char header_uri[256];
    char pool_uri[256];
    uint8_t buffer[256];
    uint8_t header_bytes[128];
    uint8_t slot_bytes[TP_HEADER_SLOT_BYTES];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_tensorHeader tensor_header;
    struct tensor_pool_slotHeader slot_header;
    int progress_count = 0;
    int result = -1;
    int step = 0;
    uint64_t seq = 0;
    uint32_t header_index = 0;
    int64_t deadline;

    memset(&client, 0, sizeof(client));
    memset(&consumer, 0, sizeof(consumer));

    if (tp_test_start_client_any(&client, &ctx, 0) < 0)
    {
        return;
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

    tp_test_write_superblock(header_fd, 91001, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 91001, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 64);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }
    step = 2;
    consumer_ctx.stream_id = 91001;
    consumer_ctx.consumer_id = 77;
    consumer_ctx.use_driver = false;

    if (tp_consumer_init(&consumer, &client, &consumer_ctx) < 0)
    {
        goto cleanup;
    }
    step = 3;

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = 1;
    pool_cfg.nslots = 4;
    pool_cfg.stride_bytes = 64;
    pool_cfg.uri = pool_uri;

    memset(&consumer_config, 0, sizeof(consumer_config));
    consumer_config.stream_id = 91001;
    consumer_config.epoch = 1;
    consumer_config.layout_version = 1;
    consumer_config.header_nslots = 4;
    consumer_config.header_uri = header_uri;
    consumer_config.pools = &pool_cfg;
    consumer_config.pool_count = 1;

    if (tp_consumer_attach(&consumer, &consumer_config) < 0)
    {
        goto cleanup;
    }
    step = 4;

    header_index = (uint32_t)(seq & (consumer_config.header_nslots - 1));
    memset(header_bytes, 0, sizeof(header_bytes));
    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)header_bytes,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(header_bytes));
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_tensorHeader_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_tensorHeader_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_tensorHeader_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version());

    tensor_pool_tensorHeader_wrap_for_encode(
        &tensor_header,
        (char *)header_bytes,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(header_bytes));
    tensor_pool_tensorHeader_set_dtype(&tensor_header, tensor_pool_dtype_UINT8);
    tensor_pool_tensorHeader_set_majorOrder(&tensor_header, tensor_pool_majorOrder_ROW);
    tensor_pool_tensorHeader_set_ndims(&tensor_header, 1);
    tensor_pool_tensorHeader_set_padAlign(&tensor_header, 0);
    tensor_pool_tensorHeader_set_progressUnit(&tensor_header, tensor_pool_progressUnit_NONE);
    tensor_pool_tensorHeader_set_progressStrideBytes(&tensor_header, 0);
    tensor_pool_tensorHeader_set_dims(&tensor_header, 0, 32);
    tensor_pool_tensorHeader_set_strides(&tensor_header, 0, 1);

    memset(slot_bytes, 0, sizeof(slot_bytes));
    tensor_pool_slotHeader_wrap_for_encode(&slot_header, (char *)slot_bytes, 0, sizeof(slot_bytes));
    tensor_pool_slotHeader_set_seqCommit(&slot_header, tp_seq_committed(seq));
    tensor_pool_slotHeader_set_valuesLenBytes(&slot_header, 32);
    tensor_pool_slotHeader_set_payloadSlot(&slot_header, header_index);
    tensor_pool_slotHeader_set_poolId(&slot_header, 1);
    tensor_pool_slotHeader_set_payloadOffset(&slot_header, 0);
    tensor_pool_slotHeader_set_timestampNs(&slot_header, 1);
    tensor_pool_slotHeader_set_metaVersion(&slot_header, 1);
    tensor_pool_slotHeader_put_headerBytes(&slot_header, (const char *)header_bytes,
        tensor_pool_messageHeader_encoded_length() + tensor_pool_tensorHeader_sbe_block_length());

    if (pwrite(
        header_fd,
        slot_bytes,
        sizeof(slot_bytes),
        TP_SUPERBLOCK_SIZE_BYTES + (header_index * TP_HEADER_SLOT_BYTES)) != (ssize_t)sizeof(slot_bytes))
    {
        goto cleanup;
    }
    step = 5;

    if (tp_test_add_publication(&client, "aeron:ipc", 1000, &control_pub) < 0)
    {
        goto cleanup;
    }
    step = 6;

    if (tp_test_add_publication(&client, "aeron:ipc", 1003, &progress_pub) < 0)
    {
        goto cleanup;
    }
    step = 7;

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_consumerConfig cfg;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_consumerConfig_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerConfig_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerConfig_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerConfig_sbe_schema_version());

        tensor_pool_consumerConfig_wrap_for_encode(&cfg, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_consumerConfig_set_streamId(&cfg, 91001);
        tensor_pool_consumerConfig_set_consumerId(&cfg, 77);
        tensor_pool_consumerConfig_set_useShm(&cfg, 1);
        tensor_pool_consumerConfig_set_mode(&cfg, tensor_pool_mode_STREAM);
        tensor_pool_consumerConfig_set_descriptorStreamId(&cfg, 0);
        tensor_pool_consumerConfig_set_controlStreamId(&cfg, 1003);
        tensor_pool_consumerConfig_put_payloadFallbackUri(&cfg, "", 0);
        tensor_pool_consumerConfig_put_descriptorChannel(&cfg, "", 0);
        tensor_pool_consumerConfig_put_controlChannel(&cfg, "aeron:ipc", 9);

        if (tp_test_offer(&client, control_pub, buffer, (size_t)tensor_pool_consumerConfig_sbe_position(&cfg)) < 0)
        {
            goto cleanup;
        }
    }
    step = 8;

    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        tp_consumer_poll_control(&consumer, 10);
        if (consumer.control_subscription)
        {
            if (aeron_subscription_is_connected(consumer.control_subscription) &&
                aeron_publication_is_connected(progress_pub))
            {
                break;
            }
        }
        tp_client_do_work(&client);
    }
    if (NULL == consumer.control_subscription ||
        !aeron_subscription_is_connected(consumer.control_subscription) ||
        !aeron_publication_is_connected(progress_pub))
    {
        goto cleanup;
    }
    step = 9;

    if (tp_consumer_set_progress_handler(&consumer, tp_on_progress_capture, &progress_count) < 0)
    {
        goto cleanup;
    }
    step = 10;

    {
        tp_frame_progress_t progress_view;
        memset(&progress_view, 0, sizeof(progress_view));
        progress_view.seq = seq;
        progress_view.payload_bytes_filled = 32;
        progress_view.state = TP_PROGRESS_COMPLETE;
        if (tp_consumer_validate_progress(&consumer, &progress_view) != 0)
        {
            goto cleanup;
        }
    }
    step = 11;

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_frameProgress progress;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_frameProgress_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_frameProgress_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_frameProgress_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_frameProgress_sbe_schema_version());

        tensor_pool_frameProgress_wrap_for_encode(&progress, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_frameProgress_set_streamId(&progress, 91001);
        tensor_pool_frameProgress_set_epoch(&progress, 1);
        tensor_pool_frameProgress_set_seq(&progress, seq);
        tensor_pool_frameProgress_set_payloadBytesFilled(&progress, 32);
        tensor_pool_frameProgress_set_state(&progress, tensor_pool_frameProgressState_COMPLETE);

        if (tp_test_offer(&client, progress_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }
    step = 12;

    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        if (tp_consumer_poll_progress(&consumer, 10) < 0)
        {
            goto cleanup;
        }
        if (progress_count > 0)
        {
            break;
        }
        tp_client_do_work(&client);
    }

    if (progress_count == 0)
    {
        goto cleanup;
    }
    step = 13;

    result = 0;

cleanup:
    if (result != 0)
    {
        fprintf(stderr, "tp_test_progress_per_consumer_control failed at step %d: %s\n", step, tp_errmsg());
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
}

void tp_test_progress_layout_validation(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_consumer_context_t consumer_ctx;
    tp_consumer_t consumer;
    tp_consumer_config_t config;
    tp_consumer_pool_config_t pool_cfg;
    tp_frame_progress_t progress;
    uint8_t header_bytes[128];
    uint8_t slot_bytes[TP_HEADER_SLOT_BYTES];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_tensorHeader tensor_header;
    struct tensor_pool_slotHeader slot_header;
    int header_fd = -1;
    int pool_fd = -1;
    char header_path[] = "/tmp/tp_progress_headerXXXXXX";
    char pool_path[] = "/tmp/tp_progress_poolXXXXXX";
    char header_uri[256];
    char pool_uri[256];
    uint64_t seq = 5;
    uint32_t header_index = 0;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&consumer, 0, sizeof(consumer));

    if (tp_test_start_client_any(&client, &ctx, 0) < 0)
    {
        return;
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

    tp_test_write_superblock(header_fd, 80001, 1, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);
    tp_test_write_superblock(pool_fd, 80001, 1, tensor_pool_regionType_PAYLOAD_POOL, 1, 4, TP_NULL_U32, 64);

    snprintf(header_uri, sizeof(header_uri), "shm:file?path=%s", header_path);
    snprintf(pool_uri, sizeof(pool_uri), "shm:file?path=%s", pool_path);

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }
    consumer_ctx.stream_id = 80001;
    consumer_ctx.consumer_id = 2;

    if (tp_consumer_init(&consumer, &client, &consumer_ctx) < 0)
    {
        goto cleanup;
    }

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = 1;
    pool_cfg.nslots = 4;
    pool_cfg.stride_bytes = 64;
    pool_cfg.uri = pool_uri;

    memset(&config, 0, sizeof(config));
    config.stream_id = 80001;
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

    header_index = (uint32_t)(seq & (config.header_nslots - 1));
    memset(header_bytes, 0, sizeof(header_bytes));
    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)header_bytes,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(header_bytes));
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_tensorHeader_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_tensorHeader_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_tensorHeader_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version());

    tensor_pool_tensorHeader_wrap_for_encode(
        &tensor_header,
        (char *)header_bytes,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(header_bytes));
    tensor_pool_tensorHeader_set_dtype(&tensor_header, tensor_pool_dtype_UINT8);
    tensor_pool_tensorHeader_set_majorOrder(&tensor_header, tensor_pool_majorOrder_ROW);
    tensor_pool_tensorHeader_set_ndims(&tensor_header, 2);
    tensor_pool_tensorHeader_set_padAlign(&tensor_header, 0);
    tensor_pool_tensorHeader_set_progressUnit(&tensor_header, tensor_pool_progressUnit_ROWS);
    tensor_pool_tensorHeader_set_progressStrideBytes(&tensor_header, 4);
    tensor_pool_tensorHeader_set_dims(&tensor_header, 0, 4);
    tensor_pool_tensorHeader_set_dims(&tensor_header, 1, 4);
    tensor_pool_tensorHeader_set_strides(&tensor_header, 0, 4);
    tensor_pool_tensorHeader_set_strides(&tensor_header, 1, 1);

    memset(slot_bytes, 0, sizeof(slot_bytes));
    tensor_pool_slotHeader_wrap_for_encode(&slot_header, (char *)slot_bytes, 0, sizeof(slot_bytes));
    tensor_pool_slotHeader_set_seqCommit(&slot_header, tp_seq_committed(seq));
    tensor_pool_slotHeader_set_valuesLenBytes(&slot_header, 16);
    tensor_pool_slotHeader_set_payloadSlot(&slot_header, header_index);
    tensor_pool_slotHeader_set_poolId(&slot_header, 1);
    tensor_pool_slotHeader_set_payloadOffset(&slot_header, 0);
    tensor_pool_slotHeader_set_timestampNs(&slot_header, 1);
    tensor_pool_slotHeader_set_metaVersion(&slot_header, 1);
    tensor_pool_slotHeader_put_headerBytes(&slot_header, (const char *)header_bytes,
        tensor_pool_messageHeader_encoded_length() + tensor_pool_tensorHeader_sbe_block_length());

    if (pwrite(
        header_fd,
        slot_bytes,
        sizeof(slot_bytes),
        TP_SUPERBLOCK_SIZE_BYTES + (header_index * TP_HEADER_SLOT_BYTES)) != (ssize_t)sizeof(slot_bytes))
    {
        goto cleanup;
    }

    memset(&progress, 0, sizeof(progress));
    progress.seq = seq;
    progress.payload_bytes_filled = 32;
    progress.state = TP_PROGRESS_STARTED;
    assert(tp_consumer_validate_progress(&consumer, &progress) != 0);

    progress.payload_bytes_filled = 8;
    assert(tp_consumer_validate_progress(&consumer, &progress) == 0);

    result = 0;

cleanup:
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
}

void tp_test_pollers(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_consumer_context_t consumer_ctx;
    tp_consumer_t consumer;
    aeron_publication_t *control_pub = NULL;
    aeron_publication_t *qos_pub = NULL;
    aeron_publication_t *meta_pub = NULL;
    aeron_publication_t *progress_pub = NULL;
    aeron_publication_t *descriptor_pub = NULL;
    aeron_subscription_t *progress_sub = NULL;
    tp_control_poller_t control_poller;
    tp_qos_poller_t qos_poller;
    tp_metadata_poller_t meta_poller;
    tp_progress_poller_t progress_poller;
    tp_control_handlers_t control_handlers;
    tp_qos_handlers_t qos_handlers;
    tp_metadata_handlers_t meta_handlers;
    tp_progress_handlers_t progress_handlers;
    tp_poll_state_t state;
    int result = -1;
    uint8_t buffer[512];
    int64_t deadline;

    memset(&state, 0, sizeof(state));
    memset(&consumer, 0, sizeof(consumer));
    memset(&control_handlers, 0, sizeof(control_handlers));
    memset(&qos_handlers, 0, sizeof(qos_handlers));
    memset(&meta_handlers, 0, sizeof(meta_handlers));
    memset(&progress_handlers, 0, sizeof(progress_handlers));

    if (tp_test_start_client_any(&client, &ctx, 0) < 0)
    {
        return;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1000, &control_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1200, &qos_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1300, &meta_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1003, &progress_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1100, &descriptor_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_subscription(&client, "aeron:ipc", 1003, &progress_sub) < 0)
    {
        goto cleanup;
    }

    control_handlers.on_consumer_hello = tp_on_consumer_hello;
    control_handlers.clientd = &state;
    if (tp_control_poller_init(&control_poller, &client, &control_handlers) < 0)
    {
        goto cleanup;
    }

    qos_handlers.on_qos_event = tp_on_qos_event;
    qos_handlers.clientd = &state;
    if (tp_qos_poller_init(&qos_poller, &client, &qos_handlers) < 0)
    {
        goto cleanup;
    }

    meta_handlers.on_data_source_announce = tp_on_announce;
    meta_handlers.on_data_source_meta_begin = tp_on_meta_begin;
    meta_handlers.on_meta_blob_announce = tp_on_meta_blob_announce;
    meta_handlers.on_meta_blob_chunk = tp_on_meta_blob_chunk;
    meta_handlers.on_meta_blob_complete = tp_on_meta_blob_complete;
    meta_handlers.clientd = &state;
    if (tp_metadata_poller_init(&meta_poller, &client, &meta_handlers) < 0)
    {
        goto cleanup;
    }

    progress_handlers.on_progress = tp_on_progress;
    progress_handlers.clientd = &state;
    if (tp_progress_poller_init_with_subscription(&progress_poller, progress_sub, &progress_handlers) < 0)
    {
        goto cleanup;
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_consumerHello hello;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_consumerHello_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerHello_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerHello_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerHello_sbe_schema_version());

        tensor_pool_consumerHello_wrap_for_encode(&hello, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_consumerHello_set_streamId(&hello, 1);
        tensor_pool_consumerHello_set_consumerId(&hello, 2);
        tensor_pool_consumerHello_set_supportsShm(&hello, 1);
        tensor_pool_consumerHello_set_supportsProgress(&hello, 1);
        tensor_pool_consumerHello_set_mode(&hello, tensor_pool_mode_STREAM);
        tensor_pool_consumerHello_set_maxRateHz(&hello, 0);
        tensor_pool_consumerHello_set_expectedLayoutVersion(&hello, 0);
        tensor_pool_consumerHello_set_progressIntervalUs(&hello, tensor_pool_consumerHello_progressIntervalUs_null_value());
        tensor_pool_consumerHello_set_progressBytesDelta(&hello, tensor_pool_consumerHello_progressBytesDelta_null_value());
        tensor_pool_consumerHello_set_progressMajorDeltaUnits(&hello, tensor_pool_consumerHello_progressMajorDeltaUnits_null_value());
        tensor_pool_consumerHello_set_descriptorStreamId(&hello, 0);
        tensor_pool_consumerHello_set_controlStreamId(&hello, 0);
        tensor_pool_consumerHello_put_descriptorChannel(&hello, "", 0);
        tensor_pool_consumerHello_put_controlChannel(&hello, "", 0);

        if (tp_test_offer(&client, control_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_qosConsumer qos;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_qosConsumer_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_qosConsumer_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_qosConsumer_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_qosConsumer_sbe_schema_version());

        tensor_pool_qosConsumer_wrap_for_encode(&qos, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_qosConsumer_set_streamId(&qos, 1);
        tensor_pool_qosConsumer_set_consumerId(&qos, 2);
        tensor_pool_qosConsumer_set_epoch(&qos, 3);
        tensor_pool_qosConsumer_set_lastSeqSeen(&qos, 4);
        tensor_pool_qosConsumer_set_dropsGap(&qos, 0);
        tensor_pool_qosConsumer_set_dropsLate(&qos, 0);
        tensor_pool_qosConsumer_set_mode(&qos, tensor_pool_mode_STREAM);

        if (tp_test_offer(&client, qos_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_dataSourceAnnounce announce;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_dataSourceAnnounce_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_dataSourceAnnounce_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_dataSourceAnnounce_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_dataSourceAnnounce_sbe_schema_version());

        tensor_pool_dataSourceAnnounce_wrap_for_encode(&announce, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_dataSourceAnnounce_set_streamId(&announce, 9);
        tensor_pool_dataSourceAnnounce_set_producerId(&announce, 3);
        tensor_pool_dataSourceAnnounce_set_epoch(&announce, 7);
        tensor_pool_dataSourceAnnounce_set_metaVersion(&announce, 1);
        tensor_pool_dataSourceAnnounce_put_name(&announce, "name", 4);
        tensor_pool_dataSourceAnnounce_put_summary(&announce, "summary", 7);

        if (tp_test_offer(&client, meta_pub, buffer, (size_t)tensor_pool_dataSourceAnnounce_sbe_position(&announce)) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_dataSourceMeta meta;
        struct tensor_pool_dataSourceMeta_attributes attrs;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_dataSourceMeta_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_dataSourceMeta_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_dataSourceMeta_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_dataSourceMeta_sbe_schema_version());

        tensor_pool_dataSourceMeta_wrap_for_encode(&meta, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_dataSourceMeta_set_streamId(&meta, 9);
        tensor_pool_dataSourceMeta_set_metaVersion(&meta, 1);
        tensor_pool_dataSourceMeta_set_timestampNs(&meta, 10);

        tensor_pool_dataSourceMeta_attributes_wrap_for_encode(
            &attrs,
            (char *)buffer,
            1,
            tensor_pool_dataSourceMeta_sbe_position_ptr(&meta),
            tensor_pool_dataSourceMeta_sbe_schema_version(),
            sizeof(buffer));
        tensor_pool_dataSourceMeta_attributes_next(&attrs);
        tensor_pool_dataSourceMeta_attributes_put_key(&attrs, "k", 1);
        tensor_pool_dataSourceMeta_attributes_put_format(&attrs, "s", 1);
        tensor_pool_dataSourceMeta_attributes_put_value(&attrs, "v", 1);

        if (tp_test_offer(&client, meta_pub, buffer, (size_t)tensor_pool_dataSourceMeta_sbe_position(&meta)) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_metaBlobAnnounce announce;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_metaBlobAnnounce_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobAnnounce_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobAnnounce_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobAnnounce_sbe_schema_version());

        tensor_pool_metaBlobAnnounce_wrap_for_encode(&announce, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_metaBlobAnnounce_set_streamId(&announce, 9);
        tensor_pool_metaBlobAnnounce_set_metaVersion(&announce, 1);
        tensor_pool_metaBlobAnnounce_set_blobType(&announce, 7);
        tensor_pool_metaBlobAnnounce_set_totalLen(&announce, 3);
        tensor_pool_metaBlobAnnounce_set_checksum(&announce, 123);

        if (tp_test_offer(&client, meta_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_metaBlobChunk chunk;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_metaBlobChunk_sbe_block_length();
        const char bytes[] = "abc";

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobChunk_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobChunk_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobChunk_sbe_schema_version());

        tensor_pool_metaBlobChunk_wrap_for_encode(&chunk, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_metaBlobChunk_set_streamId(&chunk, 9);
        tensor_pool_metaBlobChunk_set_metaVersion(&chunk, 1);
        tensor_pool_metaBlobChunk_set_chunkOffset(&chunk, 0);
        tensor_pool_metaBlobChunk_put_bytes(&chunk, bytes, sizeof(bytes) - 1);

        if (tp_test_offer(&client, meta_pub, buffer, (size_t)tensor_pool_metaBlobChunk_sbe_position(&chunk)) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_metaBlobComplete complete;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_metaBlobComplete_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobComplete_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobComplete_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobComplete_sbe_schema_version());

        tensor_pool_metaBlobComplete_wrap_for_encode(&complete, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_metaBlobComplete_set_streamId(&complete, 9);
        tensor_pool_metaBlobComplete_set_metaVersion(&complete, 1);
        tensor_pool_metaBlobComplete_set_checksum(&complete, 123);

        if (tp_test_offer(&client, meta_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_frameProgress progress;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_frameProgress_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_frameProgress_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_frameProgress_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_frameProgress_sbe_schema_version());

        tensor_pool_frameProgress_wrap_for_encode(&progress, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_frameProgress_set_streamId(&progress, 1);
        tensor_pool_frameProgress_set_epoch(&progress, 1);
        tensor_pool_frameProgress_set_seq(&progress, 5);
        tensor_pool_frameProgress_set_payloadBytesFilled(&progress, 128);
        tensor_pool_frameProgress_set_state(&progress, tensor_pool_frameProgressState_STARTED);

        if (tp_test_offer(&client, progress_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }

        tensor_pool_frameProgress_set_payloadBytesFilled(&progress, 64);
        tensor_pool_frameProgress_set_state(&progress, tensor_pool_frameProgressState_PROGRESS);
        if (tp_test_offer(&client, progress_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_consumer_init(&consumer, &client, &consumer_ctx) < 0)
    {
        goto cleanup;
    }

    consumer.state = TP_CONSUMER_STATE_MAPPED;
    consumer.shm_mapped = true;
    consumer.mapped_epoch = 1;

    tp_consumer_set_descriptor_handler(&consumer, tp_on_descriptor, &state);

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_frameDescriptor descriptor;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_frameDescriptor_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_frameDescriptor_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_frameDescriptor_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_frameDescriptor_sbe_schema_version());

        tensor_pool_frameDescriptor_wrap_for_encode(&descriptor, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_frameDescriptor_set_streamId(&descriptor, 9);
        tensor_pool_frameDescriptor_set_epoch(&descriptor, 1);
        tensor_pool_frameDescriptor_set_seq(&descriptor, 42);
        tensor_pool_frameDescriptor_set_timestampNs(&descriptor, 123);
        tensor_pool_frameDescriptor_set_metaVersion(&descriptor, 1);
        tensor_pool_frameDescriptor_set_traceId(&descriptor, 9);

        if (tp_test_offer(&client, descriptor_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    deadline = tp_clock_now_ns() + 5 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        tp_control_poll(&control_poller, 10);
        tp_qos_poll(&qos_poller, 10);
        tp_metadata_poll(&meta_poller, 10);
        tp_progress_poll(&progress_poller, 10);
        tp_consumer_poll_descriptors(&consumer, 10);
        tp_client_do_work(&client);

        if (state.saw_hello && state.saw_qos && state.saw_announce && state.saw_meta &&
            state.saw_meta_blob_announce && state.saw_meta_blob_chunk && state.saw_meta_blob_complete &&
            state.saw_progress && state.saw_descriptor)
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
    if (consumer.client)
    {
        tp_consumer_close(&consumer);
    }
    if (control_poller.assembler)
    {
        aeron_fragment_assembler_delete(control_poller.assembler);
    }
    if (qos_poller.assembler)
    {
        aeron_fragment_assembler_delete(qos_poller.assembler);
    }
    if (meta_poller.assembler)
    {
        aeron_fragment_assembler_delete(meta_poller.assembler);
    }
    if (progress_poller.assembler)
    {
        aeron_fragment_assembler_delete(progress_poller.assembler);
    }
    if (control_pub)
    {
        aeron_publication_close(control_pub, NULL, NULL);
    }
    if (qos_pub)
    {
        aeron_publication_close(qos_pub, NULL, NULL);
    }
    if (meta_pub)
    {
        aeron_publication_close(meta_pub, NULL, NULL);
    }
    if (progress_pub)
    {
        aeron_publication_close(progress_pub, NULL, NULL);
    }
    if (descriptor_pub)
    {
        aeron_publication_close(descriptor_pub, NULL, NULL);
    }
    if (progress_sub)
    {
        aeron_subscription_close(progress_sub, NULL, NULL);
    }
    tp_client_close(&client);

    if (result != 0)
    {
        fprintf(stderr,
            "pollers: hello=%d qos=%d announce=%d meta=%d blob_announce=%d blob_chunk=%d blob_complete=%d progress=%d desc=%d\n",
            state.saw_hello,
            state.saw_qos,
            state.saw_announce,
            state.saw_meta,
            state.saw_meta_blob_announce,
            state.saw_meta_blob_chunk,
            state.saw_meta_blob_complete,
            state.saw_progress,
            state.saw_descriptor);
    }

    assert(result == 0);
    assert(state.progress_count == 1);
}
