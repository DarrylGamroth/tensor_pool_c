#include "tensor_pool/tp_consumer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "aeron_alloc.h"
#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_qos.h"
#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_types.h"

#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/role.h"

#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/frameDescriptor.h"
#include "wire/tensor_pool/tensorHeader.h"

static tp_consumer_pool_t *tp_consumer_find_pool(tp_consumer_t *consumer, uint16_t pool_id)
{
    size_t i;

    for (i = 0; i < consumer->pool_count; i++)
    {
        if (consumer->pools[i].pool_id == pool_id)
        {
            return &consumer->pools[i];
        }
    }

    return NULL;
}

static int tp_consumer_attach_config(tp_consumer_t *consumer, const tp_consumer_config_t *config);

static int tp_is_power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static void tp_consumer_unmap_regions(tp_consumer_t *consumer)
{
    size_t i;

    if (NULL == consumer)
    {
        return;
    }

    for (i = 0; i < consumer->pool_count; i++)
    {
        tp_shm_unmap(&consumer->pools[i].region, &consumer->client->context.base.log);
    }

    tp_shm_unmap(&consumer->header_region, &consumer->client->context.base.log);

    if (consumer->pools)
    {
        aeron_free(consumer->pools);
        consumer->pools = NULL;
    }

    consumer->pool_count = 0;
    consumer->header_nslots = 0;
    consumer->state = TP_CONSUMER_STATE_UNMAPPED;
    consumer->shm_mapped = false;
    consumer->mapped_epoch = 0;
    consumer->last_seq_seen = 0;
    consumer->drops_gap = 0;
    consumer->drops_late = 0;
    consumer->last_qos_ns = 0;
}

static char *tp_string_view_dup(const tp_string_view_t *view)
{
    char *copy;

    if (NULL == view || view->length == 0 || NULL == view->data)
    {
        return NULL;
    }

    copy = calloc(view->length + 1, 1);
    if (NULL == copy)
    {
        return NULL;
    }

    memcpy(copy, view->data, view->length);
    copy[view->length] = '\0';
    return copy;
}

static int tp_consumer_apply_announce(tp_consumer_t *consumer, const tp_shm_pool_announce_view_t *announce)
{
    tp_consumer_config_t config;
    tp_consumer_pool_config_t *pool_cfg = NULL;
    char *header_uri = NULL;
    char **pool_uris = NULL;
    size_t i;
    int result = -1;

    if (NULL == consumer || NULL == announce)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_apply_announce: null input");
        return -1;
    }

    if (announce->header_region_uri.length == 0 || NULL == announce->header_region_uri.data)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_apply_announce: missing header region uri");
        return -1;
    }

    if (announce->header_slot_bytes != TP_HEADER_SLOT_BYTES)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_apply_announce: header slot bytes mismatch");
        return -1;
    }

    if (announce->pool_count == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_apply_announce: missing payload pools");
        return -1;
    }

    header_uri = tp_string_view_dup(&announce->header_region_uri);
    if (NULL == header_uri)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_consumer_apply_announce: header uri allocation failed");
        return -1;
    }

    pool_cfg = calloc(announce->pool_count, sizeof(*pool_cfg));
    pool_uris = calloc(announce->pool_count, sizeof(*pool_uris));
    if (NULL == pool_cfg || NULL == pool_uris)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_consumer_apply_announce: pool allocation failed");
        goto cleanup;
    }

    for (i = 0; i < announce->pool_count; i++)
    {
        const tp_shm_pool_desc_t *pool = &announce->pools[i];
        char *pool_uri = tp_string_view_dup(&pool->region_uri);

        if (NULL == pool_uri)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_consumer_apply_announce: pool uri allocation failed");
            goto cleanup;
        }

        if (pool->nslots != announce->header_nslots)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_consumer_apply_announce: pool nslots mismatch");
            goto cleanup;
        }

        pool_cfg[i].pool_id = pool->pool_id;
        pool_cfg[i].nslots = pool->nslots;
        pool_cfg[i].stride_bytes = pool->stride_bytes;
        pool_cfg[i].uri = pool_uri;
        pool_uris[i] = pool_uri;
    }

    memset(&config, 0, sizeof(config));
    config.stream_id = announce->stream_id;
    config.epoch = announce->epoch;
    config.layout_version = announce->layout_version;
    config.header_nslots = announce->header_nslots;
    config.header_uri = header_uri;
    config.pools = pool_cfg;
    config.pool_count = announce->pool_count;

    tp_consumer_unmap_regions(consumer);
    if (tp_consumer_attach_config(consumer, &config) < 0)
    {
        goto cleanup;
    }

    consumer->state = TP_CONSUMER_STATE_MAPPED;
    consumer->shm_mapped = true;
    consumer->mapped_epoch = announce->epoch;
    result = 0;

cleanup:
    if (pool_uris)
    {
        for (i = 0; i < announce->pool_count; i++)
        {
            free(pool_uris[i]);
        }
    }
    free(pool_uris);
    free(pool_cfg);
    free(header_uri);
    return result;
}

static void tp_consumer_fill_driver_request(const tp_consumer_t *consumer, tp_driver_attach_request_t *out)
{
    if (NULL == consumer || NULL == out)
    {
        return;
    }

    *out = consumer->context.driver_request;

    if (out->correlation_id == 0)
    {
        out->correlation_id = tp_clock_now_ns();
    }

    if (out->stream_id == 0)
    {
        out->stream_id = consumer->context.stream_id;
    }

    if (out->client_id == 0)
    {
        out->client_id = consumer->context.consumer_id;
    }

    if (out->role == 0)
    {
        out->role = tensor_pool_role_CONSUMER;
    }

    if (out->publish_mode == 0)
    {
        out->publish_mode = tensor_pool_publishMode_REQUIRE_EXISTING;
    }

    if (out->require_hugepages == 0)
    {
        out->require_hugepages = tensor_pool_hugepagesPolicy_UNSPECIFIED;
    }
}

static void tp_consumer_prepare_hello(const tp_consumer_t *consumer, tp_consumer_hello_t *hello)
{
    if (NULL == consumer || NULL == hello)
    {
        return;
    }

    *hello = consumer->context.hello;
    if (hello->stream_id == 0)
    {
        hello->stream_id = consumer->stream_id ? consumer->stream_id : consumer->context.stream_id;
    }
    if (hello->consumer_id == 0)
    {
        hello->consumer_id = consumer->context.consumer_id;
    }
}

static int tp_consumer_add_subscription(
    tp_consumer_t *consumer,
    const char *channel,
    int32_t stream_id,
    aeron_subscription_t **out_sub);

static void tp_consumer_descriptor_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_consumer_t *consumer = (tp_consumer_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameDescriptor descriptor;
    tp_frame_descriptor_t view;
    uint16_t template_id;
    uint16_t schema_id;

    (void)header;

    if (NULL == consumer || NULL == buffer)
    {
        return;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);

    if (schema_id != tensor_pool_frameDescriptor_sbe_schema_id() ||
        template_id != tensor_pool_frameDescriptor_sbe_template_id())
    {
        return;
    }

    tensor_pool_frameDescriptor_wrap_for_decode(
        &descriptor,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        tensor_pool_frameDescriptor_sbe_block_length(),
        tensor_pool_frameDescriptor_sbe_schema_version(),
        length);

    view.seq = tensor_pool_frameDescriptor_seq(&descriptor);
    view.timestamp_ns = tensor_pool_frameDescriptor_timestampNs(&descriptor);
    view.meta_version = tensor_pool_frameDescriptor_metaVersion(&descriptor);
    view.trace_id = tensor_pool_frameDescriptor_traceId(&descriptor);

    if (!consumer->shm_mapped || consumer->mapped_epoch != tensor_pool_frameDescriptor_epoch(&descriptor))
    {
        return;
    }

    if (consumer->last_seq_seen != 0 && view.seq > consumer->last_seq_seen + 1)
    {
        consumer->drops_gap += (view.seq - consumer->last_seq_seen - 1);
    }
    if (view.seq > consumer->last_seq_seen)
    {
        consumer->last_seq_seen = view.seq;
    }

    if (consumer->descriptor_handler)
    {
        consumer->descriptor_handler(consumer->descriptor_clientd, &view);
    }
}

static void tp_consumer_control_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_consumer_t *consumer = (tp_consumer_t *)clientd;
    tp_shm_pool_announce_view_t announce;
    tp_consumer_config_view_t view;
    tp_driver_lease_revoked_t revoked;
    tp_driver_shutdown_t shutdown_event;
    uint64_t now_ns;
    uint64_t freshness_ns;
    uint64_t announce_now_ns;

    (void)header;

    if (NULL == consumer || NULL == buffer)
    {
        return;
    }

    if (tp_control_decode_shm_pool_announce(buffer, length, &announce) == 0)
    {
        if (consumer->announce_join_time_ns == 0)
        {
            consumer->announce_join_time_ns = (uint64_t)tp_clock_now_ns();
        }

        if (announce.stream_id != consumer->context.stream_id)
        {
            tp_control_shm_pool_announce_close(&announce);
            return;
        }

        now_ns = (uint64_t)tp_clock_now_ns();
        freshness_ns = consumer->client->context.base.announce_period_ns * TP_ANNOUNCE_FRESHNESS_MULTIPLIER;
        announce_now_ns = now_ns;

        if (announce.announce_clock_domain == TP_CLOCK_DOMAIN_REALTIME_SYNCED)
        {
            announce_now_ns = (uint64_t)tp_clock_now_realtime_ns();
            if (announce_now_ns == 0)
            {
                tp_control_shm_pool_announce_close(&announce);
                return;
            }
        }

        if (announce_now_ns > announce.announce_timestamp_ns &&
            announce_now_ns - announce.announce_timestamp_ns > freshness_ns)
        {
            tp_control_shm_pool_announce_close(&announce);
            return;
        }

        if (announce.announce_clock_domain == TP_CLOCK_DOMAIN_MONOTONIC &&
            announce.announce_timestamp_ns < consumer->announce_join_time_ns)
        {
            tp_control_shm_pool_announce_close(&announce);
            return;
        }

        if (consumer->context.hello.expected_layout_version != 0 &&
            announce.layout_version != consumer->context.hello.expected_layout_version)
        {
            tp_control_shm_pool_announce_close(&announce);
            return;
        }

        if ((consumer->shm_mapped && announce.epoch < consumer->mapped_epoch) ||
            (consumer->last_announce_epoch != 0 && announce.epoch < consumer->last_announce_epoch))
        {
            tp_control_shm_pool_announce_close(&announce);
            return;
        }

        if (tp_consumer_apply_announce(consumer, &announce) == 0)
        {
            consumer->last_announce_rx_ns = now_ns;
            consumer->last_announce_timestamp_ns = announce.announce_timestamp_ns;
            consumer->last_announce_clock_domain = announce.announce_clock_domain;
            consumer->last_announce_epoch = announce.epoch;
        }

        tp_control_shm_pool_announce_close(&announce);
        return;
    }

    if (tp_control_decode_consumer_config(buffer, length, &view) == 0)
    {
        if (view.consumer_id != consumer->context.consumer_id)
        {
            return;
        }

        if (view.descriptor_channel.length > 0 && view.descriptor_stream_id > 0)
        {
            char channel[4096];
            size_t copy_len = view.descriptor_channel.length;
            if (copy_len >= sizeof(channel))
            {
                copy_len = sizeof(channel) - 1;
            }
            memcpy(channel, view.descriptor_channel.data, copy_len);
            channel[copy_len] = '\0';

            if (consumer->descriptor_subscription)
            {
                aeron_subscription_close(consumer->descriptor_subscription, NULL, NULL);
                consumer->descriptor_subscription = NULL;
            }

            tp_consumer_add_subscription(
                consumer,
                channel,
                (int32_t)view.descriptor_stream_id,
                &consumer->descriptor_subscription);
        }
        else if (consumer->descriptor_subscription)
        {
            aeron_subscription_close(consumer->descriptor_subscription, NULL, NULL);
            consumer->descriptor_subscription = NULL;
        }

        if (view.control_channel.length > 0 && view.control_stream_id > 0)
        {
            char channel[4096];
            size_t copy_len = view.control_channel.length;
            if (copy_len >= sizeof(channel))
            {
                copy_len = sizeof(channel) - 1;
            }
            memcpy(channel, view.control_channel.data, copy_len);
            channel[copy_len] = '\0';

            if (consumer->control_subscription)
            {
                aeron_subscription_close(consumer->control_subscription, NULL, NULL);
                consumer->control_subscription = NULL;
            }

            tp_consumer_add_subscription(
                consumer,
                channel,
                (int32_t)view.control_stream_id,
                &consumer->control_subscription);
        }
        else if (consumer->control_subscription)
        {
            aeron_subscription_close(consumer->control_subscription, NULL, NULL);
            consumer->control_subscription = NULL;
        }

        return;
    }

    if (tp_driver_decode_lease_revoked(buffer, length, &revoked) == 0)
    {
        if (revoked.client_id == consumer->context.consumer_id &&
            revoked.role == tensor_pool_role_CONSUMER)
        {
            TP_SET_ERR(
                ECANCELED,
                "consumer lease revoked stream=%u reason=%u: %s",
                revoked.stream_id,
                revoked.reason,
                revoked.error_message);
            if (consumer->client && consumer->client->context.error_handler)
            {
                consumer->client->context.error_handler(
                    consumer->client->context.error_handler_clientd,
                    tp_errcode(),
                    tp_errmsg());
            }
            consumer->driver.active_lease_id = 0;
            consumer->driver_attached = false;
        }
        return;
    }

    if (tp_driver_decode_shutdown(buffer, length, &shutdown_event) == 0)
    {
        TP_SET_ERR(
            ECANCELED,
            "driver shutdown reason=%u: %s",
            shutdown_event.reason,
            shutdown_event.error_message);
        if (consumer->client && consumer->client->context.error_handler)
        {
            consumer->client->context.error_handler(
                consumer->client->context.error_handler_clientd,
                tp_errcode(),
                tp_errmsg());
        }
        return;
    }
}

static int tp_consumer_add_publication(
    tp_consumer_t *consumer,
    const char *channel,
    int32_t stream_id,
    aeron_publication_t **out_pub)
{
    aeron_async_add_publication_t *async_add = NULL;

    if (NULL == consumer || NULL == consumer->client || NULL == out_pub || NULL == channel || stream_id < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_add_publication: invalid input");
        return -1;
    }

    if (tp_client_async_add_publication(consumer->client, channel, stream_id, &async_add) < 0)
    {
        return -1;
    }

    *out_pub = NULL;
    while (NULL == *out_pub)
    {
        if (tp_client_async_add_publication_poll(out_pub, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(consumer->client);
    }

    return 0;
}

static int tp_consumer_add_subscription(
    tp_consumer_t *consumer,
    const char *channel,
    int32_t stream_id,
    aeron_subscription_t **out_sub)
{
    aeron_async_add_subscription_t *async_add = NULL;

    if (NULL == consumer || NULL == consumer->client || NULL == out_sub || NULL == channel || stream_id < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_add_subscription: invalid input");
        return -1;
    }

    if (tp_client_async_add_subscription(
        consumer->client,
        channel,
        stream_id,
        NULL,
        NULL,
        NULL,
        NULL,
        &async_add) < 0)
    {
        return -1;
    }

    *out_sub = NULL;
    while (NULL == *out_sub)
    {
        if (tp_client_async_add_subscription_poll(out_sub, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(consumer->client);
    }

    return 0;
}

int tp_consumer_context_init(tp_consumer_context_t *ctx)
{
    if (NULL == ctx)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_context_init: null input");
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->hello.supports_shm = 1;
    ctx->hello.supports_progress = 1;
    ctx->hello.mode = TP_MODE_STREAM;
    ctx->hello.progress_interval_us = TP_NULL_U32;
    ctx->hello.progress_bytes_delta = TP_NULL_U32;
    ctx->hello.progress_major_delta_units = TP_NULL_U32;
    return 0;
}

int tp_consumer_init(tp_consumer_t *consumer, tp_client_t *client, const tp_consumer_context_t *context)
{
    if (NULL == consumer || NULL == client || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_init: null input");
        return -1;
    }

    memset(consumer, 0, sizeof(*consumer));
    consumer->client = client;
    consumer->context = *context;

    if (client->context.base.descriptor_channel[0] != '\0' && client->context.base.descriptor_stream_id >= 0)
    {
        if (tp_consumer_add_subscription(
            consumer,
            client->context.base.descriptor_channel,
            client->context.base.descriptor_stream_id,
            &consumer->descriptor_subscription) < 0)
        {
            return -1;
        }
    }

    if (client->context.base.control_channel[0] != '\0' && client->context.base.control_stream_id >= 0)
    {
        if (tp_consumer_add_publication(
            consumer,
            client->context.base.control_channel,
            client->context.base.control_stream_id,
            &consumer->control_publication) < 0)
        {
            return -1;
        }
    }

    if (client->context.base.qos_channel[0] != '\0' && client->context.base.qos_stream_id >= 0)
    {
        if (tp_consumer_add_publication(
            consumer,
            client->context.base.qos_channel,
            client->context.base.qos_stream_id,
            &consumer->qos_publication) < 0)
        {
            return -1;
        }
    }

    if (consumer->context.use_driver)
    {
        if (tp_consumer_attach(consumer, NULL) < 0)
        {
            return -1;
        }
    }

    return 0;
}

void tp_consumer_set_descriptor_handler(tp_consumer_t *consumer, tp_frame_descriptor_handler_t handler, void *clientd)
{
    if (NULL == consumer)
    {
        return;
    }

    consumer->descriptor_handler = handler;
    consumer->descriptor_clientd = clientd;
}

static int tp_consumer_attach_config(tp_consumer_t *consumer, const tp_consumer_config_t *config)
{
    tp_shm_expected_t expected;
    size_t i;
    int result = -1;

    if (NULL == consumer || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach: null input");
        return -1;
    }

    consumer->stream_id = config->stream_id;
    consumer->epoch = config->epoch;
    consumer->layout_version = config->layout_version;
    consumer->header_nslots = config->header_nslots;
    consumer->pool_count = config->pool_count;

    if (!tp_is_power_of_two(config->header_nslots))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach: header_nslots must be power of two");
        return -1;
    }

    if (consumer->pool_count > 0)
    {
        if (aeron_alloc((void **)&consumer->pools, sizeof(tp_consumer_pool_t) * consumer->pool_count) < 0)
        {
            return -1;
        }
    }

    if (tp_shm_map(
        &consumer->header_region,
        config->header_uri,
        0,
        &consumer->client->context.base.allowed_paths,
        &consumer->client->context.base.log) < 0)
    {
        goto cleanup;
    }

    memset(&expected, 0, sizeof(expected));
    expected.stream_id = config->stream_id;
    expected.layout_version = config->layout_version;
    expected.epoch = config->epoch;
    expected.region_type = tensor_pool_regionType_HEADER_RING;
    expected.pool_id = 0;
    expected.nslots = config->header_nslots;
    expected.slot_bytes = TP_HEADER_SLOT_BYTES;
    expected.stride_bytes = TP_NULL_U32;

    if (tp_shm_validate_superblock(&consumer->header_region, &expected, &consumer->client->context.base.log) < 0)
    {
        goto cleanup;
    }

    for (i = 0; i < config->pool_count; i++)
    {
        const tp_consumer_pool_config_t *pool_cfg = &config->pools[i];
        tp_consumer_pool_t *pool = &consumer->pools[i];

        memset(pool, 0, sizeof(*pool));
        if (pool_cfg->nslots != config->header_nslots)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach: pool nslots mismatch");
            goto cleanup;
        }
        pool->pool_id = pool_cfg->pool_id;
        pool->nslots = pool_cfg->nslots;
        pool->stride_bytes = pool_cfg->stride_bytes;

        if (tp_shm_validate_stride_alignment(
            pool_cfg->uri,
            pool->stride_bytes,
            &consumer->client->context.base.log) < 0)
        {
            goto cleanup;
        }

        if (tp_shm_map(
            &pool->region,
            pool_cfg->uri,
            0,
            &consumer->client->context.base.allowed_paths,
            &consumer->client->context.base.log) < 0)
        {
            goto cleanup;
        }

        memset(&expected, 0, sizeof(expected));
        expected.stream_id = config->stream_id;
        expected.layout_version = config->layout_version;
        expected.epoch = config->epoch;
        expected.region_type = tensor_pool_regionType_PAYLOAD_POOL;
        expected.pool_id = pool->pool_id;
        expected.nslots = pool->nslots;
        expected.slot_bytes = TP_NULL_U32;
        expected.stride_bytes = pool->stride_bytes;

        if (tp_shm_validate_superblock(&pool->region, &expected, &consumer->client->context.base.log) < 0)
        {
            goto cleanup;
        }
    }

    if (consumer->control_publication)
    {
        tp_consumer_hello_t hello;
        tp_consumer_prepare_hello(consumer, &hello);
        tp_consumer_send_hello(consumer, &hello);
    }

    consumer->state = TP_CONSUMER_STATE_MAPPED;
    consumer->shm_mapped = true;
    consumer->mapped_epoch = config->epoch;
    consumer->last_seq_seen = 0;
    consumer->drops_gap = 0;
    consumer->drops_late = 0;
    consumer->last_qos_ns = 0;
    return 0;

cleanup:
    for (i = 0; i < consumer->pool_count; i++)
    {
        tp_shm_unmap(&consumer->pools[i].region, &consumer->client->context.base.log);
    }

    tp_shm_unmap(&consumer->header_region, &consumer->client->context.base.log);

    if (consumer->pools)
    {
        aeron_free(consumer->pools);
        consumer->pools = NULL;
    }

    consumer->state = TP_CONSUMER_STATE_UNMAPPED;
    consumer->shm_mapped = false;
    return result;
}

static int tp_consumer_attach_driver(tp_consumer_t *consumer)
{
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_consumer_config_t config;
    tp_consumer_pool_config_t *pool_cfg = NULL;
    size_t i;
    int result = -1;

    if (NULL == consumer)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach_driver: null input");
        return -1;
    }

    if (!consumer->driver_initialized)
    {
        if (tp_driver_client_init(&consumer->driver, consumer->client) < 0)
        {
            return -1;
        }
        consumer->driver_initialized = true;
    }

    tp_consumer_fill_driver_request(consumer, &request);

    if (tp_driver_attach(&consumer->driver, &request, &info, (int64_t)consumer->client->context.driver_timeout_ns) < 0)
    {
        return -1;
    }

    if (info.code != tensor_pool_responseCode_OK)
    {
        TP_SET_ERR(EINVAL, "tp_consumer_attach_driver: driver attach failed (%d): %s", info.code, info.error_message);
        tp_driver_attach_info_close(&info);
        return -1;
    }

    if (consumer->driver_attached)
    {
        tp_driver_attach_info_close(&consumer->driver_attach);
    }

    consumer->driver_attach = info;
    consumer->driver_attached = true;

    pool_cfg = calloc(info.pool_count, sizeof(*pool_cfg));
    if (NULL == pool_cfg)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_consumer_attach_driver: pool allocation failed");
        goto cleanup;
    }

    for (i = 0; i < info.pool_count; i++)
    {
        pool_cfg[i].pool_id = info.pools[i].pool_id;
        pool_cfg[i].nslots = info.pools[i].nslots;
        pool_cfg[i].stride_bytes = info.pools[i].stride_bytes;
        pool_cfg[i].uri = info.pools[i].region_uri;
    }

    memset(&config, 0, sizeof(config));
    config.stream_id = info.stream_id;
    config.epoch = info.epoch;
    config.layout_version = info.layout_version;
    config.header_nslots = info.header_nslots;
    config.header_uri = info.header_region_uri;
    config.pools = pool_cfg;
    config.pool_count = info.pool_count;

    result = tp_consumer_attach_config(consumer, &config);

cleanup:
    free(pool_cfg);
    if (result < 0)
    {
        tp_driver_attach_info_close(&consumer->driver_attach);
        consumer->driver_attached = false;
    }
    return result;
}

int tp_consumer_attach(tp_consumer_t *consumer, const tp_consumer_config_t *config)
{
    if (NULL == consumer)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach: null input");
        return -1;
    }

    if (consumer->context.use_driver && NULL == config)
    {
        return tp_consumer_attach_driver(consumer);
    }

    if (NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_attach: null config");
        return -1;
    }

    return tp_consumer_attach_config(consumer, config);
}

int tp_consumer_read_frame(tp_consumer_t *consumer, uint64_t seq, tp_frame_view_t *out)
{
    uint8_t *slot;
    tp_slot_view_t slot_view;
    tp_consumer_pool_t *pool;
    uint32_t header_index;
    uint64_t seq_first;
    uint64_t seq_second;

    if (NULL == consumer || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_read_frame: null input");
        return -1;
    }

    if (!consumer->shm_mapped)
    {
        return 1;
    }

    if (consumer->header_nslots == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_read_frame: header ring unavailable");
        return -1;
    }

    header_index = (uint32_t)(seq & (consumer->header_nslots - 1));
    if (header_index >= consumer->header_nslots)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_read_frame: header index out of range");
        return -1;
    }

    slot = tp_slot_at(consumer->header_region.addr, header_index);

    seq_first = tp_atomic_load_u64((uint64_t *)slot);
    if (!tp_seq_is_committed(seq_first))
    {
        consumer->drops_late++;
        return 1;
    }

    if (tp_slot_decode(&slot_view, slot, TP_HEADER_SLOT_BYTES, &consumer->client->context.base.log) < 0)
    {
        return -1;
    }

    if (slot_view.payload_offset != 0)
    {
        return 1;
    }

    if (slot_view.payload_slot != header_index)
    {
        return 1;
    }

    pool = tp_consumer_find_pool(consumer, slot_view.pool_id);
    if (NULL == pool)
    {
        return 1;
    }

    if (slot_view.values_len_bytes > pool->stride_bytes)
    {
        return 1;
    }

    if (slot_view.header_bytes_length != (tensor_pool_messageHeader_encoded_length() + tensor_pool_tensorHeader_sbe_block_length()))
    {
        return 1;
    }

    if (tp_tensor_header_decode(&out->tensor, slot_view.header_bytes, slot_view.header_bytes_length, &consumer->client->context.base.log) < 0)
    {
        return 1;
    }

    if (tp_tensor_header_validate(&out->tensor, &consumer->client->context.base.log) < 0)
    {
        return 1;
    }

    out->payload_len = slot_view.values_len_bytes;
    out->payload = (const uint8_t *)pool->region.addr + TP_SUPERBLOCK_SIZE_BYTES + (slot_view.payload_slot * pool->stride_bytes);
    out->pool_id = slot_view.pool_id;
    out->payload_slot = slot_view.payload_slot;
    out->timestamp_ns = slot_view.timestamp_ns;
    out->meta_version = slot_view.meta_version;

    seq_second = tp_atomic_load_u64((uint64_t *)slot);
    if (seq_second != seq_first || !tp_seq_is_committed(seq_second))
    {
        consumer->drops_late++;
        return 1;
    }

    if (tp_seq_value(seq_second) != seq)
    {
        consumer->drops_late++;
        return 1;
    }

    return 0;
}

int tp_consumer_get_drop_counts(const tp_consumer_t *consumer, uint64_t *drops_gap, uint64_t *drops_late, uint64_t *last_seq_seen)
{
    if (NULL == consumer)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_get_drop_counts: null input");
        return -1;
    }

    if (drops_gap)
    {
        *drops_gap = consumer->drops_gap;
    }
    if (drops_late)
    {
        *drops_late = consumer->drops_late;
    }
    if (last_seq_seen)
    {
        *last_seq_seen = consumer->last_seq_seen;
    }

    return 0;
}

int tp_consumer_poll_descriptors(tp_consumer_t *consumer, int fragment_limit)
{
    if (NULL == consumer || NULL == consumer->descriptor_subscription)
    {
        return 0;
    }

    if (NULL == consumer->descriptor_assembler)
    {
        if (aeron_fragment_assembler_create(
            &consumer->descriptor_assembler,
            tp_consumer_descriptor_handler,
            consumer) < 0)
        {
            return -1;
        }
    }

    int fragments = aeron_subscription_poll(
        consumer->descriptor_subscription,
        aeron_fragment_assembler_handler,
        consumer->descriptor_assembler,
        fragment_limit);
    if (fragments < 0)
    {
        return -1;
    }

    tp_client_do_work(consumer->client);
    return fragments;
}

int tp_consumer_poll_control(tp_consumer_t *consumer, int fragment_limit)
{
    uint64_t now_ns;

    if (NULL == consumer || NULL == consumer->client || NULL == consumer->client->control_subscription)
    {
        return 0;
    }

    if (NULL == consumer->control_assembler)
    {
        if (aeron_fragment_assembler_create(
            &consumer->control_assembler,
            tp_consumer_control_handler,
            consumer) < 0)
        {
            return -1;
        }
    }

    int fragments = aeron_subscription_poll(
        consumer->client->control_subscription,
        aeron_fragment_assembler_handler,
        consumer->control_assembler,
        fragment_limit);
    if (fragments < 0)
    {
        return -1;
    }

    now_ns = (uint64_t)tp_clock_now_ns();
    if (consumer->qos_publication && consumer->client->context.base.announce_period_ns > 0)
    {
        uint64_t period = consumer->client->context.base.announce_period_ns;
        if (consumer->last_qos_ns == 0 || now_ns - consumer->last_qos_ns >= period)
        {
            tp_qos_publish_consumer(
                consumer,
                consumer->context.consumer_id,
                consumer->last_seq_seen,
                consumer->drops_gap,
                consumer->drops_late,
                consumer->context.hello.mode);
            consumer->last_qos_ns = now_ns;
        }
    }

    tp_client_do_work(consumer->client);
    return fragments;
}

int tp_consumer_close(tp_consumer_t *consumer)
{
    if (NULL == consumer)
    {
        return -1;
    }

    if (consumer->descriptor_subscription)
    {
        aeron_subscription_close(consumer->descriptor_subscription, NULL, NULL);
        consumer->descriptor_subscription = NULL;
    }

    if (consumer->control_subscription)
    {
        aeron_subscription_close(consumer->control_subscription, NULL, NULL);
        consumer->control_subscription = NULL;
    }

    if (consumer->control_publication)
    {
        aeron_publication_close(consumer->control_publication, NULL, NULL);
        consumer->control_publication = NULL;
    }

    if (consumer->qos_publication)
    {
        aeron_publication_close(consumer->qos_publication, NULL, NULL);
        consumer->qos_publication = NULL;
    }

    if (consumer->descriptor_assembler)
    {
        aeron_fragment_assembler_delete(consumer->descriptor_assembler);
        consumer->descriptor_assembler = NULL;
    }

    if (consumer->control_assembler)
    {
        aeron_fragment_assembler_delete(consumer->control_assembler);
        consumer->control_assembler = NULL;
    }

    tp_consumer_unmap_regions(consumer);

    if (consumer->driver_attached)
    {
        tp_driver_detach(
            &consumer->driver,
            tp_clock_now_ns(),
            consumer->driver.active_lease_id,
            consumer->driver.active_stream_id,
            consumer->driver.client_id,
            consumer->driver.role);
        tp_driver_attach_info_close(&consumer->driver_attach);
        consumer->driver_attached = false;
    }

    if (consumer->driver_initialized)
    {
        tp_driver_client_close(&consumer->driver);
        consumer->driver_initialized = false;
    }

    return 0;
}
