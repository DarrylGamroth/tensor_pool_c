#include "tensor_pool/tp_producer.h"

#include <inttypes.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_consumer_manager.h"
#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_qos.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_types.h"

#include "aeron_alloc.h"
#include "aeronc.h"

#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/role.h"

#include "wire/tensor_pool/frameDescriptor.h"
#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/slotHeader.h"
#include "wire/tensor_pool/tensorHeader.h"
#include "wire/tensor_pool/regionType.h"

typedef struct tp_tracelink_entry_stct
{
    uint64_t seq;
    uint64_t trace_id;
}
tp_tracelink_entry_t;

static tp_payload_pool_t *tp_find_pool_for_length(tp_producer_t *producer, size_t length);

static tp_payload_pool_t *tp_find_pool(tp_producer_t *producer, uint16_t pool_id)
{
    size_t i;

    for (i = 0; i < producer->pool_count; i++)
    {
        if (producer->pools[i].pool_id == pool_id)
        {
            return &producer->pools[i];
        }
    }

    return NULL;
}

static void tp_zero_slot_padding(uint8_t *slot)
{
    const size_t offset = tensor_pool_slotHeader_pad_encoding_offset();
    const size_t length = 26;

    if (NULL == slot)
    {
        return;
    }

    memset(slot + offset, 0, length);
}

static void tp_producer_clear_cached_meta(tp_producer_t *producer)
{
    size_t i;

    if (NULL == producer)
    {
        return;
    }

    for (i = 0; i < producer->cached_attr_count; i++)
    {
        tp_meta_attribute_owned_t *attr = &producer->cached_attrs[i];
        free(attr->key);
        free(attr->format);
        free(attr->value);
        attr->key = NULL;
        attr->format = NULL;
        attr->value = NULL;
        attr->value_length = 0;
    }

    free(producer->cached_attrs);
    producer->cached_attrs = NULL;
    producer->cached_attr_count = 0;
    producer->cached_meta.attributes = NULL;
    producer->cached_meta.attribute_count = 0;
    producer->has_meta = false;
}

static int tp_producer_init_tracelink_entries(tp_producer_t *producer)
{
    size_t i;

    if (NULL == producer || producer->header_nslots == 0)
    {
        return 0;
    }

    if (producer->tracelink_entries)
    {
        aeron_free(producer->tracelink_entries);
        producer->tracelink_entries = NULL;
        producer->tracelink_entry_count = 0;
    }

    if (aeron_alloc(
        (void **)&producer->tracelink_entries,
        sizeof(tp_tracelink_entry_t) * producer->header_nslots) < 0)
    {
        return -1;
    }

    producer->tracelink_entry_count = producer->header_nslots;
    for (i = 0; i < producer->tracelink_entry_count; i++)
    {
        producer->tracelink_entries[i].seq = TP_NULL_U64;
        producer->tracelink_entries[i].trace_id = 0;
    }

    tp_producer_clear_reattach(producer);
    return 0;
}

static void tp_producer_record_tracelink_descriptor(tp_producer_t *producer, uint64_t seq, uint64_t trace_id)
{
    size_t index;

    if (NULL == producer || NULL == producer->tracelink_entries || producer->header_nslots == 0)
    {
        return;
    }

    index = (size_t)(seq & (producer->header_nslots - 1));
    producer->tracelink_entries[index].seq = seq;
    producer->tracelink_entries[index].trace_id = trace_id;
}

static int tp_producer_default_tracelink_validator(const tp_tracelink_set_t *set, void *clientd)
{
    tp_producer_t *producer = (tp_producer_t *)clientd;
    size_t index;
    tp_tracelink_entry_t *entry;

    if (NULL == producer || NULL == set)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_tracelink_validate: invalid input");
        return -1;
    }

    if (NULL == producer->tracelink_entries || producer->header_nslots == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_tracelink_validate: trace history unavailable");
        return -1;
    }

    index = (size_t)(set->seq & (producer->header_nslots - 1));
    entry = &producer->tracelink_entries[index];
    if (entry->seq != set->seq || entry->trace_id != set->trace_id)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_tracelink_validate: descriptor mismatch");
        return -1;
    }

    return 0;
}

static int tp_producer_resolve_trace_id(
    tp_producer_t *producer,
    uint64_t requested_trace_id,
    uint64_t *out_trace_id)
{
    uint64_t trace_id = requested_trace_id;

    if (NULL == producer || NULL == out_trace_id)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_resolve_trace_id: null input");
        return -1;
    }

    if (trace_id == 0 && producer->trace_id_generator)
    {
        trace_id = tp_trace_id_generator_next(producer->trace_id_generator);
        if (trace_id == 0)
        {
            return -1;
        }
    }

    *out_trace_id = trace_id;
    return 0;
}

static char *tp_dup_string(const char *value)
{
    size_t len;
    char *copy;

    if (NULL == value)
    {
        return NULL;
    }

    len = strlen(value);
    copy = calloc(len + 1, 1);
    if (NULL == copy)
    {
        return NULL;
    }
    memcpy(copy, value, len);
    copy[len] = '\0';
    return copy;
}

static void tp_producer_clear_shm_uris(tp_producer_t *producer)
{
    size_t i;

    if (NULL == producer)
    {
        return;
    }

    if (producer->header_uri)
    {
        free(producer->header_uri);
        producer->header_uri = NULL;
    }

    if (producer->pool_uris)
    {
        for (i = 0; i < producer->pool_uri_count; i++)
        {
            free(producer->pool_uris[i]);
        }
        free(producer->pool_uris);
        producer->pool_uris = NULL;
    }

    producer->pool_uri_count = 0;
}

static int tp_producer_store_shm_uris(tp_producer_t *producer, const tp_producer_config_t *config)
{
    size_t i;
    int result = -1;

    if (NULL == producer || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_store_shm_uris: null input");
        return -1;
    }

    tp_producer_clear_shm_uris(producer);

    producer->header_uri = tp_dup_string(config->header_uri);
    if (NULL == producer->header_uri)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_producer_store_shm_uris: header uri allocation failed");
        goto cleanup;
    }

    if (config->pool_count > 0)
    {
        producer->pool_uris = calloc(config->pool_count, sizeof(*producer->pool_uris));
        if (NULL == producer->pool_uris)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_producer_store_shm_uris: pool uri allocation failed");
            goto cleanup;
        }
    }

    producer->pool_uri_count = config->pool_count;
    for (i = 0; i < config->pool_count; i++)
    {
        producer->pool_uris[i] = tp_dup_string(config->pools[i].uri);
        if (NULL == producer->pool_uris[i])
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_producer_store_shm_uris: pool uri allocation failed");
            goto cleanup;
        }
    }

    producer->last_shm_announce_ns = 0;
    result = 0;

cleanup:
    if (result != 0)
    {
        tp_producer_clear_shm_uris(producer);
    }
    return result;
}

static int tp_is_power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static void tp_producer_fill_driver_request(tp_producer_t *producer, tp_driver_attach_request_t *out)
{
    if (NULL == producer || NULL == out)
    {
        return;
    }

    *out = producer->context.driver_request;

    if (out->correlation_id == 0)
    {
        out->correlation_id = tp_driver_next_correlation_id();
    }

    if (out->stream_id == 0)
    {
        out->stream_id = producer->context.stream_id;
    }

    if (out->client_id == 0)
    {
        out->client_id = producer->context.producer_id;
        if (out->client_id == 0)
        {
            out->client_id = tp_driver_next_client_id();
            producer->context.producer_id = out->client_id;
        }
    }

    if (out->role == 0)
    {
        out->role = tensor_pool_role_PRODUCER;
    }

    if (out->publish_mode == 0)
    {
        out->publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    }

    if (out->require_hugepages == 0)
    {
        out->require_hugepages = tensor_pool_hugepagesPolicy_UNSPECIFIED;
    }
}

int tp_producer_publish_descriptor_to(
    tp_producer_t *producer,
    aeron_publication_t *publication,
    uint64_t seq,
    uint64_t timestamp_ns,
    uint32_t meta_version,
    uint64_t trace_id)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameDescriptor descriptor;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_frameDescriptor_sbe_block_length();
    int64_t result;
    uint64_t encoded_timestamp =
        (timestamp_ns == TP_NULL_U64) ? tensor_pool_frameDescriptor_timestampNs_null_value() : timestamp_ns;
    uint32_t encoded_meta_version =
        (meta_version == TP_NULL_U32) ? tensor_pool_frameDescriptor_metaVersion_null_value() : meta_version;
    tp_log_t *log = NULL;

    if (NULL == producer || NULL == publication)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_descriptor_to: publication unavailable");
        return -1;
    }

    if (producer->client)
    {
        log = &producer->client->context.base.log;
    }

    memset(buffer, 0, header_len + tensor_pool_tensorHeader_sbe_block_length());

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_frameDescriptor_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_frameDescriptor_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_frameDescriptor_sbe_schema_version());

    tensor_pool_frameDescriptor_wrap_for_encode(
        &descriptor,
        (char *)buffer,
        header_len,
        sizeof(buffer));

    tensor_pool_frameDescriptor_set_streamId(&descriptor, producer->stream_id);
    tensor_pool_frameDescriptor_set_epoch(&descriptor, producer->epoch);
    tensor_pool_frameDescriptor_set_seq(&descriptor, seq);
    tensor_pool_frameDescriptor_set_timestampNs(&descriptor, encoded_timestamp);
    tensor_pool_frameDescriptor_set_metaVersion(&descriptor, encoded_meta_version);
    tensor_pool_frameDescriptor_set_traceId(&descriptor, trace_id);

    if (log)
    {
        tp_log_emit(
            log,
            TP_LOG_TRACE,
            "descriptor publish stream=%u epoch=%" PRIu64 " seq=%" PRIu64 " ts=%" PRIu64 " meta=%u trace=%" PRIu64,
            producer->stream_id,
            producer->epoch,
            seq,
            encoded_timestamp,
            encoded_meta_version,
            trace_id);
    }

    result = aeron_publication_offer(
        publication,
        buffer,
        header_len + body_len,
        NULL,
        NULL);

    if (result < 0)
    {
        if (result != AERON_PUBLICATION_ERROR)
        {
            const char *reason = "unknown";
            switch (result)
            {
                case AERON_PUBLICATION_NOT_CONNECTED:
                    reason = "NOT_CONNECTED";
                    break;
                case AERON_PUBLICATION_BACK_PRESSURED:
                    reason = "BACK_PRESSURED";
                    break;
                case AERON_PUBLICATION_ADMIN_ACTION:
                    reason = "ADMIN_ACTION";
                    break;
                case AERON_PUBLICATION_CLOSED:
                    reason = "CLOSED";
                    break;
                case AERON_PUBLICATION_MAX_POSITION_EXCEEDED:
                    reason = "MAX_POSITION_EXCEEDED";
                    break;
                default:
                    break;
            }
            TP_SET_ERR(EAGAIN, "tp_producer_publish_descriptor_to: offer failed (%s)", reason);
        }
        return (int)result;
    }

    return 0;
}

static int tp_producer_publish_descriptor(
    tp_producer_t *producer,
    uint64_t seq,
    uint64_t timestamp_ns,
    uint32_t meta_version,
    uint64_t trace_id)
{
    int result = 0;
    int published = 0;
    int published_ok = 0;
    uint64_t now_ns = (uint64_t)tp_clock_now_ns();

    if (producer->consumer_manager)
    {
        tp_consumer_registry_t *registry = &producer->consumer_manager->registry;
        size_t i;
        for (i = 0; i < registry->capacity; i++)
        {
            tp_consumer_entry_t *entry = &registry->entries[i];
            if (!entry->in_use || NULL == entry->descriptor_publication)
            {
                continue;
            }
            if (entry->mode == TP_MODE_RATE_LIMITED && entry->max_rate_hz > 0)
            {
                uint64_t interval_ns = 1000000000ULL / entry->max_rate_hz;
                if (interval_ns > 0 && entry->last_descriptor_ns != 0 &&
                    now_ns - entry->last_descriptor_ns < interval_ns)
                {
                    continue;
                }
                entry->last_descriptor_ns = now_ns;
            }
            {
                int offer_result = tp_producer_publish_descriptor_to(
                    producer,
                    entry->descriptor_publication,
                    seq,
                    timestamp_ns,
                    meta_version,
                    trace_id);
                if (offer_result == AERON_PUBLICATION_NOT_CONNECTED &&
                    producer->context.drop_unconnected_descriptors)
                {
                    published = 1;
                    continue;
                }
                if (offer_result < 0)
                {
                    result = -1;
                }
                else
                {
                    published_ok = 1;
                }
                published = 1;
            }
        }
    }

    if (producer->descriptor_publication)
    {
        int offer_result = tp_producer_publish_descriptor_to(
            producer,
            producer->descriptor_publication,
            seq,
            timestamp_ns,
            meta_version,
            trace_id);
        if (offer_result == AERON_PUBLICATION_NOT_CONNECTED &&
            producer->context.drop_unconnected_descriptors)
        {
            published = 1;
        }
        else if (offer_result < 0)
        {
            result = -1;
        }
        else
        {
            published_ok = 1;
        }
        published = 1;
    }

    if (!published)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_descriptor: descriptor publication unavailable");
        return -1;
    }

    if (published_ok)
    {
        tp_producer_record_tracelink_descriptor(producer, seq, trace_id);
    }

    return result;
}

static int tp_producer_add_publication(
    tp_producer_t *producer,
    const char *channel,
    int32_t stream_id,
    aeron_publication_t **out_pub)
{
    aeron_async_add_publication_t *async_add = NULL;

    if (NULL == producer || NULL == producer->client || NULL == out_pub || NULL == channel || stream_id < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_add_publication: invalid input");
        return -1;
    }

    if (tp_client_async_add_publication(producer->client, channel, stream_id, &async_add) < 0)
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
        tp_client_do_work(producer->client);
    }

    return 0;
}

int tp_producer_context_init(tp_producer_context_t *ctx)
{
    if (NULL == ctx)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_context_init: null input");
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->driver_request.desired_node_id = TP_NULL_U32;
    return 0;
}

void tp_producer_context_set_fixed_pool_mode(tp_producer_context_t *ctx, bool enabled)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->fixed_pool_mode = enabled;
}

void tp_producer_context_set_drop_unconnected_descriptors(tp_producer_context_t *ctx, bool enabled)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->drop_unconnected_descriptors = enabled;
}

void tp_producer_context_set_publish_descriptor_timestamp(tp_producer_context_t *ctx, bool enabled)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->publish_descriptor_timestamp = enabled;
}

static void tp_producer_unmap_regions(tp_producer_t *producer)
{
    size_t i;

    if (NULL == producer || NULL == producer->client)
    {
        return;
    }

    tp_shm_unmap(&producer->header_region, &producer->client->context.base.log);

    if (producer->pools)
    {
        for (i = 0; i < producer->pool_count; i++)
        {
            tp_shm_unmap(&producer->pools[i].region, &producer->client->context.base.log);
        }
        aeron_free(producer->pools);
        producer->pools = NULL;
    }

    if (producer->tracelink_entries)
    {
        aeron_free(producer->tracelink_entries);
        producer->tracelink_entries = NULL;
        producer->tracelink_entry_count = 0;
    }

    producer->pool_count = 0;
    producer->header_nslots = 0;
    producer->epoch = 0;
    producer->layout_version = 0;
    producer->next_seq = 0;
}

static uint64_t tp_producer_next_backoff_ns(uint32_t failures)
{
    const uint64_t base_ns = 100ULL * 1000ULL * 1000ULL;
    const uint32_t max_shift = 5;
    uint32_t shift = failures > max_shift ? max_shift : failures;

    return base_ns << shift;
}

void tp_producer_schedule_reattach(tp_producer_t *producer, uint64_t now_ns)
{
    if (NULL == producer || !producer->context.use_driver)
    {
        return;
    }

    producer->reattach_requested = true;
    producer->attach_failures++;
    producer->next_attach_ns = now_ns + tp_producer_next_backoff_ns(producer->attach_failures - 1);
}

int tp_producer_reattach_due(const tp_producer_t *producer, uint64_t now_ns)
{
    if (NULL == producer || !producer->reattach_requested)
    {
        return 0;
    }

    return now_ns >= producer->next_attach_ns ? 1 : 0;
}

void tp_producer_clear_reattach(tp_producer_t *producer)
{
    if (NULL == producer)
    {
        return;
    }

    producer->reattach_requested = false;
    producer->attach_failures = 0;
    producer->next_attach_ns = 0;
}

void tp_producer_context_set_payload_flush(
    tp_producer_context_t *ctx,
    void (*payload_flush)(void *clientd, void *payload, size_t length),
    void *clientd)
{
    if (NULL == ctx)
    {
        return;
    }

    ctx->payload_flush = payload_flush;
    ctx->payload_flush_clientd = clientd;
}

void tp_producer_set_trace_id_generator(tp_producer_t *producer, tp_trace_id_generator_t *generator)
{
    if (NULL == producer)
    {
        return;
    }

    producer->trace_id_generator = generator;
}

void tp_producer_set_tracelink_validator(tp_producer_t *producer, tp_tracelink_validate_t validator, void *clientd)
{
    if (NULL == producer)
    {
        return;
    }

    producer->tracelink_validator = validator;
    producer->tracelink_validator_clientd = clientd;
}

static void tp_producer_control_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_producer_t *producer = (tp_producer_t *)clientd;
    tp_consumer_hello_view_t hello;
    tp_driver_lease_revoked_t revoked;
    tp_driver_shutdown_t shutdown_event;

    (void)header;

    if (NULL == producer || NULL == buffer)
    {
        return;
    }

    if (producer->consumer_manager && tp_control_decode_consumer_hello(buffer, length, &hello) == 0)
    {
        tp_consumer_manager_handle_hello(producer->consumer_manager, &hello, (uint64_t)tp_clock_now_ns());
        return;
    }

    if (tp_driver_decode_lease_revoked(buffer, length, &revoked) == 0)
    {
        if (revoked.client_id == producer->context.producer_id &&
            revoked.role == tensor_pool_role_PRODUCER)
        {
            uint64_t now = (uint64_t)tp_clock_now_ns();

            TP_SET_ERR(
                ECANCELED,
                "producer lease revoked stream=%u reason=%u: %s",
                revoked.stream_id,
                revoked.reason,
                revoked.error_message);
            if (producer->client && producer->client->context.error_handler)
            {
                producer->client->context.error_handler(
                    producer->client->context.error_handler_clientd,
                    tp_errcode(),
                    tp_errmsg());
            }
            tp_producer_unmap_regions(producer);
            if (producer->driver_attached)
            {
                tp_driver_attach_info_close(&producer->driver_attach);
            }
            producer->driver.active_lease_id = 0;
            producer->driver_attached = false;
            tp_producer_schedule_reattach(producer, now);
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
        if (producer->client && producer->client->context.error_handler)
        {
            producer->client->context.error_handler(
                producer->client->context.error_handler_clientd,
                tp_errcode(),
                tp_errmsg());
        }
        return;
    }
}

static void tp_producer_qos_event_handler(void *clientd, const tp_qos_event_t *event)
{
    tp_producer_t *producer = (tp_producer_t *)clientd;

    if (NULL == producer || NULL == event || NULL == producer->consumer_manager)
    {
        return;
    }

    if (event->type != TP_QOS_EVENT_CONSUMER)
    {
        return;
    }

    if (event->stream_id != producer->stream_id || event->epoch != producer->epoch)
    {
        return;
    }

    tp_consumer_manager_touch(producer->consumer_manager, event->consumer_id, (uint64_t)tp_clock_now_ns());
}

static void tp_producer_update_activity(tp_producer_t *producer, uint64_t now_ns)
{
    size_t i;

    if (NULL == producer || NULL == producer->client)
    {
        return;
    }

    if (producer->header_region.addr)
    {
        if (tp_shm_update_activity_timestamp(&producer->header_region, now_ns, &producer->client->context.base.log) < 0)
        {
            tp_log_emit(&producer->client->context.base.log, TP_LOG_WARN, "%s", tp_errmsg());
        }
    }

    for (i = 0; i < producer->pool_count; i++)
    {
        if (producer->pools[i].region.addr)
        {
            if (tp_shm_update_activity_timestamp(&producer->pools[i].region, now_ns, &producer->client->context.base.log) < 0)
            {
                tp_log_emit(&producer->client->context.base.log, TP_LOG_WARN, "%s", tp_errmsg());
            }
        }
    }
}

static int tp_producer_publish_shm_pool_announce(tp_producer_t *producer, uint64_t now_ns)
{
    tp_shm_pool_announce_pool_t *pools = NULL;
    tp_shm_pool_announce_t announce;
    size_t i;
    int result = -1;

    if (NULL == producer || NULL == producer->control_publication)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_shm_pool_announce: control publication unavailable");
        return -1;
    }

    if (producer->pool_count == 0 || NULL == producer->pools || NULL == producer->header_uri)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_shm_pool_announce: missing pool configuration");
        return -1;
    }

    pools = calloc(producer->pool_count, sizeof(*pools));
    if (NULL == pools)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_producer_publish_shm_pool_announce: pool allocation failed");
        return -1;
    }

    for (i = 0; i < producer->pool_count; i++)
    {
        pools[i].pool_id = producer->pools[i].pool_id;
        pools[i].pool_nslots = producer->pools[i].nslots;
        pools[i].stride_bytes = producer->pools[i].stride_bytes;
        pools[i].region_uri = (producer->pool_uris && i < producer->pool_uri_count)
            ? producer->pool_uris[i]
            : NULL;
    }

    memset(&announce, 0, sizeof(announce));
    announce.stream_id = producer->stream_id;
    announce.producer_id = producer->producer_id;
    announce.epoch = producer->epoch;
    announce.announce_timestamp_ns = now_ns;
    announce.announce_clock_domain = TP_CLOCK_DOMAIN_MONOTONIC;
    announce.layout_version = producer->layout_version;
    announce.header_nslots = producer->header_nslots;
    announce.header_slot_bytes = TP_HEADER_SLOT_BYTES;
    announce.header_region_uri = producer->header_uri;
    announce.pools = pools;
    announce.pool_count = producer->pool_count;

    result = tp_producer_send_shm_pool_announce(producer, &announce);

    free(pools);
    return result;
}

static int tp_producer_poll_qos(tp_producer_t *producer, int fragment_limit)
{
    tp_qos_handlers_t handlers;

    if (NULL == producer || NULL == producer->client || NULL == producer->client->qos_subscription)
    {
        return 0;
    }

    if (!producer->consumer_manager)
    {
        return 0;
    }

    if (NULL == producer->qos_poller)
    {
        if (aeron_alloc((void **)&producer->qos_poller, sizeof(*producer->qos_poller)) < 0)
        {
            return -1;
        }
        memset(producer->qos_poller, 0, sizeof(*producer->qos_poller));

        memset(&handlers, 0, sizeof(handlers));
        handlers.on_qos_event = tp_producer_qos_event_handler;
        handlers.clientd = producer;

        if (tp_qos_poller_init(producer->qos_poller, producer->client, &handlers) < 0)
        {
            aeron_free(producer->qos_poller);
            producer->qos_poller = NULL;
            return -1;
        }
    }

    return tp_qos_poll(producer->qos_poller, fragment_limit);
}

int tp_producer_init(tp_producer_t *producer, tp_client_t *client, const tp_producer_context_t *context)
{
    if (NULL == producer || NULL == client || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_init: null input");
        return -1;
    }

    memset(producer, 0, sizeof(*producer));
    producer->client = client;
    producer->context = *context;
    producer->tracelink_validator = tp_producer_default_tracelink_validator;
    producer->tracelink_validator_clientd = producer;

    if (client->context.base.descriptor_channel[0] != '\0' && client->context.base.descriptor_stream_id >= 0)
    {
        if (tp_producer_add_publication(
            producer,
            client->context.base.descriptor_channel,
            client->context.base.descriptor_stream_id,
            &producer->descriptor_publication) < 0)
        {
            return -1;
        }
    }

    if (client->context.base.control_channel[0] != '\0' && client->context.base.control_stream_id >= 0)
    {
        if (tp_producer_add_publication(
            producer,
            client->context.base.control_channel,
            client->context.base.control_stream_id,
            &producer->control_publication) < 0)
        {
            return -1;
        }
    }

    if (client->context.base.qos_channel[0] != '\0' && client->context.base.qos_stream_id >= 0)
    {
        if (tp_producer_add_publication(
            producer,
            client->context.base.qos_channel,
            client->context.base.qos_stream_id,
            &producer->qos_publication) < 0)
        {
            return -1;
        }
    }

    if (client->context.base.metadata_channel[0] != '\0' && client->context.base.metadata_stream_id >= 0)
    {
        if (tp_producer_add_publication(
            producer,
            client->context.base.metadata_channel,
            client->context.base.metadata_stream_id,
            &producer->metadata_publication) < 0)
        {
            return -1;
        }
    }

    if (producer->context.use_driver)
    {
        if (tp_producer_attach(producer, NULL) < 0)
        {
            return -1;
        }
    }

    return 0;
}

static int tp_producer_attach_config(tp_producer_t *producer, const tp_producer_config_t *config)
{
    tp_shm_expected_t expected;
    size_t i;
    int result = -1;

    if (NULL == producer || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_attach_config: null input");
        return -1;
    }

    producer->stream_id = config->stream_id;
    producer->producer_id = config->producer_id;
    producer->epoch = config->epoch;
    producer->layout_version = config->layout_version;
    producer->header_nslots = config->header_nslots;
    producer->pool_count = config->pool_count;

    if (config->layout_version != TP_LAYOUT_VERSION)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_attach: unsupported layout version");
        goto cleanup;
    }

    if (!tp_is_power_of_two(config->header_nslots))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_attach_config: header_nslots must be power of two");
        return -1;
    }

    if (tp_producer_init_tracelink_entries(producer) < 0)
    {
        return -1;
    }

    if (producer->pool_count > 0)
    {
        if (aeron_alloc((void **)&producer->pools, sizeof(tp_payload_pool_t) * producer->pool_count) < 0)
        {
            return -1;
        }
    }

    if (tp_shm_map(
        &producer->header_region,
        config->header_uri,
        1,
        &producer->client->context.base.allowed_paths,
        &producer->client->context.base.log) < 0)
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

    if (tp_shm_validate_superblock(&producer->header_region, &expected, &producer->client->context.base.log) < 0)
    {
        goto cleanup;
    }

    for (i = 0; i < config->pool_count; i++)
    {
        const tp_payload_pool_config_t *pool_cfg = &config->pools[i];
        tp_payload_pool_t *pool = &producer->pools[i];

        memset(pool, 0, sizeof(*pool));
        if (pool_cfg->nslots != config->header_nslots)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_producer_attach_config: pool nslots mismatch");
            goto cleanup;
        }
        pool->pool_id = pool_cfg->pool_id;
        pool->nslots = pool_cfg->nslots;
        pool->stride_bytes = pool_cfg->stride_bytes;

        if (tp_shm_validate_stride_alignment(
            pool_cfg->uri,
            pool->stride_bytes,
            &producer->client->context.base.log) < 0)
        {
            goto cleanup;
        }

        if (tp_shm_map(
            &pool->region,
            pool_cfg->uri,
            1,
            &producer->client->context.base.allowed_paths,
            &producer->client->context.base.log) < 0)
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

        if (tp_shm_validate_superblock(&pool->region, &expected, &producer->client->context.base.log) < 0)
        {
            goto cleanup;
        }
    }

    if (tp_producer_store_shm_uris(producer, config) < 0)
    {
        goto cleanup;
    }

    return 0;

cleanup:
    for (i = 0; i < producer->pool_count; i++)
    {
        tp_shm_unmap(&producer->pools[i].region, &producer->client->context.base.log);
    }

    tp_shm_unmap(&producer->header_region, &producer->client->context.base.log);

    if (producer->pools)
    {
        aeron_free(producer->pools);
        producer->pools = NULL;
    }

    tp_producer_clear_shm_uris(producer);

    if (producer->tracelink_entries)
    {
        aeron_free(producer->tracelink_entries);
        producer->tracelink_entries = NULL;
        producer->tracelink_entry_count = 0;
    }

    return result;
}

static int tp_producer_attach_driver(tp_producer_t *producer)
{
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_producer_config_t config;
    tp_payload_pool_config_t *pool_cfg = NULL;
    size_t i;
    int result = -1;

    if (NULL == producer)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_attach_driver: null input");
        return -1;
    }

    if (!producer->driver_initialized)
    {
        if (tp_driver_client_init(&producer->driver, producer->client) < 0)
        {
            return -1;
        }
        producer->driver_initialized = true;
    }

    tp_producer_fill_driver_request(producer, &request);

    if (tp_driver_attach(&producer->driver, &request, &info, (int64_t)producer->client->context.driver_timeout_ns) < 0)
    {
        return -1;
    }

    if (info.code != tensor_pool_responseCode_OK)
    {
        TP_SET_ERR(EINVAL, "tp_producer_attach_driver: driver attach failed (%d): %s", info.code, info.error_message);
        tp_driver_attach_info_close(&info);
        return -1;
    }

    if (producer->driver_attached)
    {
        tp_driver_attach_info_close(&producer->driver_attach);
    }

    producer->driver_attach = info;
    producer->driver_attached = true;

    pool_cfg = calloc(info.pool_count, sizeof(*pool_cfg));
    if (NULL == pool_cfg)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_producer_attach_driver: pool allocation failed");
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
    config.producer_id = producer->context.producer_id;
    config.epoch = info.epoch;
    config.layout_version = info.layout_version;
    config.header_nslots = info.header_nslots;
    config.header_uri = info.header_region_uri;
    config.pools = pool_cfg;
    config.pool_count = info.pool_count;

    result = tp_producer_attach_config(producer, &config);

cleanup:
    free(pool_cfg);
    if (result < 0)
    {
        tp_driver_attach_info_close(&producer->driver_attach);
        producer->driver_attached = false;
    }
    return result;
}

int tp_producer_attach(tp_producer_t *producer, const tp_producer_config_t *config)
{
    if (NULL == producer)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_attach: null input");
        return -1;
    }

    if (producer->context.use_driver)
    {
        if (NULL != config)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_producer_attach: driver mode ignores manual config");
            return -1;
        }
        return tp_producer_attach_driver(producer);
    }

    if (NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_attach: null config");
        return -1;
    }

    return tp_producer_attach_config(producer, config);
}

static int tp_encode_tensor_header(uint8_t *buffer, size_t buffer_len, const tp_tensor_header_t *tensor)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_tensorHeader header;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t i;

    if (buffer_len < header_len + tensor_pool_tensorHeader_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_encode_tensor_header: buffer too small");
        return -1;
    }

    memset(buffer, 0, buffer_len);

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
        &header,
        (char *)buffer,
        header_len,
        buffer_len);

    tensor_pool_tensorHeader_set_dtype(&header, tensor->dtype);
    tensor_pool_tensorHeader_set_majorOrder(&header, tensor->major_order);
    tensor_pool_tensorHeader_set_ndims(&header, tensor->ndims);
    tensor_pool_tensorHeader_set_padAlign(&header, 0);
    tensor_pool_tensorHeader_set_progressUnit(&header, tensor->progress_unit);
    tensor_pool_tensorHeader_set_progressStrideBytes(&header, tensor->progress_stride_bytes);

    for (i = 0; i < TP_MAX_DIMS; i++)
    {
        tensor_pool_tensorHeader_set_dims(&header, i, tensor->dims[i]);
        tensor_pool_tensorHeader_set_strides(&header, i, tensor->strides[i]);
    }

    return 0;
}

static int tp_prepare_tensor_header(tp_producer_t *producer, const tp_tensor_header_t *tensor, tp_tensor_header_t *out)
{
    size_t i;
    tp_log_t *log = NULL;

    if (NULL == tensor || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_prepare_tensor_header: null input");
        return -1;
    }

    if (producer && producer->client)
    {
        log = &producer->client->context.base.log;
    }

    memcpy(out, tensor, sizeof(*out));
    if (out->ndims < TP_MAX_DIMS)
    {
        for (i = out->ndims; i < TP_MAX_DIMS; i++)
        {
            out->dims[i] = 0;
            out->strides[i] = 0;
        }
    }

    if (tp_tensor_header_validate(out, log) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_producer_publish_frame(
    tp_producer_t *producer,
    uint64_t seq,
    const tp_tensor_header_t *tensor,
    const void *payload,
    uint32_t payload_len,
    uint16_t pool_id,
    uint64_t timestamp_ns,
    uint32_t meta_version,
    uint64_t trace_id)
{
    tp_payload_pool_t *pool;
    uint8_t *slot;
    uint8_t *payload_dst;
    struct tensor_pool_slotHeader slot_header;
    uint8_t header_bytes[TP_HEADER_SLOT_BYTES];
    uint32_t header_index;
    uint64_t in_progress;
    uint64_t committed;
    uint64_t slot_timestamp_ns;
    tp_tensor_header_t prepared_tensor;

    if (NULL == producer || NULL == tensor || (NULL == payload && payload_len > 0))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_frame: null input");
        return -1;
    }

    if (producer->header_nslots == 0 || producer->pool_count == 0 || NULL == producer->header_region.addr)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_frame: producer not attached");
        return -1;
    }

    header_index = (uint32_t)(seq & (producer->header_nslots - 1));

    pool = tp_find_pool(producer, pool_id);
    if (NULL == pool)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_frame: unknown pool id");
        return -1;
    }

    if (payload_len > pool->stride_bytes)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_frame: payload too large for pool");
        return -1;
    }

    if (tp_prepare_tensor_header(producer, tensor, &prepared_tensor) < 0)
    {
        return -1;
    }

    slot = tp_slot_at(producer->header_region.addr, header_index);
    payload_dst = (uint8_t *)pool->region.addr + TP_SUPERBLOCK_SIZE_BYTES + (header_index * pool->stride_bytes);

    in_progress = tp_seq_in_progress(seq);
    committed = tp_seq_committed(seq);
    if (timestamp_ns == 0 || timestamp_ns == TP_NULL_U64)
    {
        slot_timestamp_ns = (uint64_t)tp_clock_now_ns();
    }
    else
    {
        slot_timestamp_ns = timestamp_ns;
    }

    tp_atomic_store_u64((uint64_t *)slot, in_progress);
    if (payload_len > 0)
    {
        memcpy(payload_dst, payload, payload_len);
    }

    tensor_pool_slotHeader_wrap_for_encode(
        &slot_header,
        (char *)slot,
        0,
        TP_HEADER_SLOT_BYTES);

    tensor_pool_slotHeader_set_valuesLenBytes(&slot_header, payload_len);
    tensor_pool_slotHeader_set_payloadSlot(&slot_header, header_index);
    tensor_pool_slotHeader_set_poolId(&slot_header, pool_id);
    tensor_pool_slotHeader_set_payloadOffset(&slot_header, 0);
    tensor_pool_slotHeader_set_timestampNs(&slot_header, slot_timestamp_ns);
    tensor_pool_slotHeader_set_metaVersion(&slot_header, meta_version);
    tp_zero_slot_padding(slot);

    if (tp_encode_tensor_header(header_bytes, sizeof(header_bytes), &prepared_tensor) < 0)
    {
        return -1;
    }

    {
        uint32_t header_len = (uint32_t)(tensor_pool_messageHeader_encoded_length() + tensor_pool_tensorHeader_sbe_block_length());
        uint32_t header_len_le = SBE_LITTLE_ENDIAN_ENCODE_32(header_len);
        uint8_t *header_dst = slot + tensor_pool_slotHeader_sbe_block_length();
        memcpy(header_dst, &header_len_le, sizeof(header_len_le));
        memcpy(header_dst + sizeof(header_len_le), header_bytes, header_len);
    }

    if (producer->context.payload_flush && payload_len > 0)
    {
        producer->context.payload_flush(
            producer->context.payload_flush_clientd,
            payload_dst,
            payload_len);
    }

    atomic_thread_fence(memory_order_release);
    tp_atomic_store_u64((uint64_t *)slot, committed);

    {
        uint64_t descriptor_timestamp_ns = TP_NULL_U64;
        if (producer->context.publish_descriptor_timestamp)
        {
            descriptor_timestamp_ns = (uint64_t)tp_clock_now_ns();
        }
        return tp_producer_publish_descriptor(producer, seq, descriptor_timestamp_ns, meta_version, trace_id);
    }
}

int tp_producer_publish_progress_to(
    tp_producer_t *producer,
    aeron_publication_t *publication,
    uint64_t seq,
    uint64_t payload_bytes_filled,
    tp_progress_state_t state)
{
    uint8_t buffer[128];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameProgress progress;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_frameProgress_sbe_block_length();
    int64_t result;

    if (NULL == producer || NULL == publication)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_progress_to: control publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_frameProgress_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_frameProgress_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_frameProgress_sbe_schema_version());

    tensor_pool_frameProgress_wrap_for_encode(&progress, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_frameProgress_set_streamId(&progress, producer->stream_id);
    tensor_pool_frameProgress_set_epoch(&progress, producer->epoch);
    tensor_pool_frameProgress_set_seq(&progress, seq);
    tensor_pool_frameProgress_set_payloadBytesFilled(&progress, payload_bytes_filled);
    tensor_pool_frameProgress_set_state(&progress, (enum tensor_pool_frameProgressState)state);

    result = aeron_publication_offer(publication, buffer, header_len + body_len, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }

    return 0;
}

int tp_producer_publish_progress(
    tp_producer_t *producer,
    uint64_t seq,
    uint64_t payload_bytes_filled,
    tp_progress_state_t state)
{
    int result = 0;
    int published = 0;

    if (producer && producer->consumer_manager)
    {
        tp_consumer_registry_t *registry = &producer->consumer_manager->registry;
        size_t i;
        for (i = 0; i < registry->capacity; i++)
        {
            tp_consumer_entry_t *entry = &registry->entries[i];
            if (!entry->in_use || NULL == entry->control_publication)
            {
                continue;
            }
            if (tp_producer_publish_progress_to(
                producer,
                entry->control_publication,
                seq,
                payload_bytes_filled,
                state) < 0)
            {
                result = -1;
            }
            published = 1;
        }
    }

    if (producer && producer->control_publication)
    {
        if (tp_producer_publish_progress_to(
            producer,
            producer->control_publication,
            seq,
            payload_bytes_filled,
            state) < 0)
        {
            result = -1;
        }
        published = 1;
    }

    if (!published)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_publish_progress: control publication unavailable");
        return -1;
    }

    return result;
}

int64_t tp_producer_offer_frame(tp_producer_t *producer, const tp_frame_t *frame, tp_frame_metadata_t *meta)
{
    uint64_t seq;
    uint64_t timestamp_ns = TP_NULL_U64;
    uint32_t meta_version = TP_NULL_U32;
    uint64_t trace_id = 0;
    tp_payload_pool_t *pool;
    int result;

    if (NULL == producer || NULL == frame || NULL == frame->tensor)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_offer_frame: null input");
        return -1;
    }

    if (producer->header_nslots == 0 || producer->pool_count == 0 || NULL == producer->header_region.addr)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_offer_frame: producer not attached");
        return -1;
    }

    if (NULL != meta)
    {
        timestamp_ns = meta->timestamp_ns;
        meta_version = meta->meta_version;
    }

    if (tp_producer_resolve_trace_id(producer, frame->trace_id, &trace_id) < 0)
    {
        return -1;
    }

    pool = tp_find_pool_for_length(producer, frame->payload_len);
    if (NULL == pool)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_offer_frame: no pool for payload length");
        return -1;
    }

    seq = producer->next_seq++;

    result = tp_producer_publish_frame(
        producer,
        seq,
        frame->tensor,
        frame->payload,
        frame->payload_len,
        pool->pool_id,
        timestamp_ns,
        meta_version,
        trace_id);
    if (result < 0)
    {
        return -1;
    }

    return (int64_t)seq;
}

static tp_payload_pool_t *tp_find_pool_for_length(tp_producer_t *producer, size_t length)
{
    size_t i;
    tp_payload_pool_t *best = NULL;

    for (i = 0; i < producer->pool_count; i++)
    {
        tp_payload_pool_t *pool = &producer->pools[i];
        if (pool->stride_bytes >= length)
        {
            if (NULL == best || pool->stride_bytes < best->stride_bytes)
            {
                best = pool;
            }
        }
    }

    return best;
}

int64_t tp_producer_try_claim(tp_producer_t *producer, size_t length, tp_buffer_claim_t *claim)
{
    tp_payload_pool_t *pool;
    uint32_t header_index;
    uint8_t *slot;
    uint64_t seq;

    if (NULL == producer || NULL == claim)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_try_claim: null input");
        return -1;
    }

    if (producer->header_nslots == 0 || producer->pool_count == 0 || NULL == producer->header_region.addr)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_try_claim: producer not attached");
        return -1;
    }

    pool = tp_find_pool_for_length(producer, length);
    if (NULL == pool)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_try_claim: no pool for payload length");
        return -1;
    }

    seq = producer->next_seq++;
    header_index = (uint32_t)(seq & (producer->header_nslots - 1));
    slot = tp_slot_at(producer->header_region.addr, header_index);

    claim->seq = seq;
    claim->header_index = header_index;
    claim->pool_id = pool->pool_id;
    claim->payload_len = (uint32_t)length;
    claim->payload = (uint8_t *)pool->region.addr + TP_SUPERBLOCK_SIZE_BYTES + (header_index * pool->stride_bytes);
    claim->trace_id = 0;

    tp_atomic_store_u64((uint64_t *)slot, tp_seq_in_progress(seq));

    return (int64_t)seq;
}

int tp_producer_commit_claim(tp_producer_t *producer, tp_buffer_claim_t *claim, const tp_frame_metadata_t *meta)
{
    tp_frame_metadata_t local_meta;
    uint64_t trace_id = 0;

    if (NULL == producer || NULL == claim)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_commit_claim: null input");
        return -1;
    }

    if (NULL == meta)
    {
        memset(&local_meta, 0, sizeof(local_meta));
        local_meta.timestamp_ns = TP_NULL_U64;
        local_meta.meta_version = TP_NULL_U32;
        meta = &local_meta;
    }

    if (tp_producer_resolve_trace_id(producer, claim->trace_id, &trace_id) < 0)
    {
        return -1;
    }
    claim->trace_id = trace_id;

    return tp_producer_publish_frame(
        producer,
        claim->seq,
        &claim->tensor,
        claim->payload,
        claim->payload_len,
        claim->pool_id,
        meta->timestamp_ns,
        meta->meta_version,
        trace_id);
}

int tp_producer_abort_claim(tp_producer_t *producer, tp_buffer_claim_t *claim)
{
    if (NULL == producer || NULL == claim)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_abort_claim: null input");
        return -1;
    }

    return 0;
}

int64_t tp_producer_queue_claim(tp_producer_t *producer, tp_buffer_claim_t *claim)
{
    uint8_t *slot;
    uint64_t seq;

    if (NULL == producer || NULL == claim)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_queue_claim: null input");
        return -1;
    }

    if (!producer->context.fixed_pool_mode)
    {
        return TP_ADMIN_ACTION;
    }

    seq = producer->next_seq++;
    claim->seq = seq;
    claim->trace_id = 0;
    slot = tp_slot_at(producer->header_region.addr, claim->header_index);
    tp_atomic_store_u64((uint64_t *)slot, tp_seq_in_progress(seq));

    return (int64_t)seq;
}

int tp_producer_offer_progress(tp_producer_t *producer, const tp_frame_progress_t *progress)
{
    if (NULL == producer || NULL == progress)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_offer_progress: null input");
        return -1;
    }

    return tp_producer_publish_progress(
        producer,
        progress->seq,
        progress->payload_bytes_filled,
        progress->state);
}

int tp_producer_enable_consumer_manager(tp_producer_t *producer, size_t capacity)
{
    tp_consumer_manager_t *manager = NULL;

    if (NULL == producer || capacity == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_enable_consumer_manager: invalid input");
        return -1;
    }

    if (producer->consumer_manager)
    {
        return 0;
    }

    if (aeron_alloc((void **)&manager, sizeof(*manager)) < 0)
    {
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    if (tp_consumer_manager_init(manager, producer, capacity) < 0)
    {
        aeron_free(manager);
        return -1;
    }

    producer->consumer_manager = manager;
    producer->last_consumer_sweep_ns = (uint64_t)tp_clock_now_ns();
    return 0;
}

int tp_producer_poll_control(tp_producer_t *producer, int fragment_limit)
{
    uint64_t now_ns;
    uint64_t stale_ns = 5ULL * 1000 * 1000 * 1000ULL;
    int fragments;
    int qos_fragments;

    if (NULL == producer || NULL == producer->client || NULL == producer->client->control_subscription)
    {
        return 0;
    }

    if (NULL == producer->control_assembler)
    {
        if (aeron_fragment_assembler_create(
            &producer->control_assembler,
            tp_producer_control_handler,
            producer) < 0)
        {
            return -1;
        }
    }

    fragments = aeron_subscription_poll(
        producer->client->control_subscription,
        aeron_fragment_assembler_handler,
        producer->control_assembler,
        fragment_limit);
    if (fragments < 0)
    {
        return -1;
    }

    if (producer->consumer_manager)
    {
        qos_fragments = tp_producer_poll_qos(producer, fragment_limit);
        if (qos_fragments < 0)
        {
            return -1;
        }
        if (producer->client && producer->client->context.base.announce_period_ns > 0)
        {
            stale_ns = producer->client->context.base.announce_period_ns * 5ULL;
        }

        now_ns = (uint64_t)tp_clock_now_ns();
        tp_consumer_manager_sweep(producer->consumer_manager, now_ns, stale_ns);
    }

    now_ns = (uint64_t)tp_clock_now_ns();
    if (producer->client->context.base.announce_period_ns > 0)
    {
        uint64_t period = producer->client->context.base.announce_period_ns;
        if (producer->qos_publication &&
            (producer->last_qos_ns == 0 || now_ns - producer->last_qos_ns >= period))
        {
            tp_qos_publish_producer(producer, producer->next_seq, TP_NULL_U32);
            producer->last_qos_ns = now_ns;
        }

        if (!producer->context.use_driver &&
            producer->control_publication &&
            (producer->last_shm_announce_ns == 0 || now_ns - producer->last_shm_announce_ns >= period))
        {
            if (tp_producer_publish_shm_pool_announce(producer, now_ns) == 0)
            {
                producer->last_shm_announce_ns = now_ns;
            }
        }

        if (producer->metadata_publication && producer->has_announce &&
            (producer->last_announce_ns == 0 || now_ns - producer->last_announce_ns >= period))
        {
            tp_producer_send_data_source_announce(producer, &producer->cached_announce);
            producer->last_announce_ns = now_ns;
        }

        if (producer->metadata_publication && producer->has_meta &&
            (producer->last_meta_ns == 0 || now_ns - producer->last_meta_ns >= period))
        {
            tp_producer_send_data_source_meta(producer, &producer->cached_meta);
            producer->last_meta_ns = now_ns;
        }
    }

    {
        uint64_t period = producer->client->context.base.announce_period_ns;
        if (period == 0 || producer->last_activity_ns == 0 || now_ns - producer->last_activity_ns >= period)
        {
            tp_producer_update_activity(producer, now_ns);
            producer->last_activity_ns = now_ns;
        }
    }

    tp_client_do_work(producer->client);
    return fragments;
}

int tp_producer_set_data_source_announce(tp_producer_t *producer, const tp_data_source_announce_t *announce)
{
    if (NULL == producer || NULL == announce)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_set_data_source_announce: null input");
        return -1;
    }

    tp_producer_clear_data_source_announce(producer);

    producer->cached_announce.stream_id = announce->stream_id;
    producer->cached_announce.producer_id = announce->producer_id;
    producer->cached_announce.epoch = announce->epoch;
    producer->cached_announce.meta_version = announce->meta_version;
    producer->cached_announce.name = tp_dup_string(announce->name);
    producer->cached_announce.summary = tp_dup_string(announce->summary);
    producer->has_announce = true;

    if ((announce->name && NULL == producer->cached_announce.name) ||
        (announce->summary && NULL == producer->cached_announce.summary))
    {
        tp_producer_clear_data_source_announce(producer);
        TP_SET_ERR(ENOMEM, "%s", "tp_producer_set_data_source_announce: allocation failed");
        return -1;
    }

    return 0;
}

int tp_producer_set_data_source_meta(tp_producer_t *producer, const tp_data_source_meta_t *meta)
{
    size_t i;

    if (NULL == producer || NULL == meta)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_set_data_source_meta: null input");
        return -1;
    }

    tp_producer_clear_data_source_meta(producer);

    producer->cached_meta.stream_id = meta->stream_id;
    producer->cached_meta.meta_version = meta->meta_version;
    producer->cached_meta.timestamp_ns = meta->timestamp_ns;
    producer->cached_attr_count = meta->attribute_count;

    if (meta->attribute_count > 0)
    {
        producer->cached_attrs = calloc(meta->attribute_count, sizeof(*producer->cached_attrs));
        if (NULL == producer->cached_attrs)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_producer_set_data_source_meta: allocation failed");
            return -1;
        }
    }

    for (i = 0; i < meta->attribute_count; i++)
    {
        const tp_meta_attribute_t *src = &meta->attributes[i];
        tp_meta_attribute_owned_t *dst = &producer->cached_attrs[i];

        if (NULL == src->key || NULL == src->format || (NULL == src->value && src->value_length > 0))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_producer_set_data_source_meta: invalid attribute");
            tp_producer_clear_data_source_meta(producer);
            return -1;
        }

        dst->key = tp_dup_string(src->key);
        dst->format = tp_dup_string(src->format);
        if (src->value_length > 0)
        {
            dst->value = calloc(src->value_length, 1);
            if (NULL != dst->value)
            {
                memcpy(dst->value, src->value, src->value_length);
                dst->value_length = src->value_length;
            }
        }

        if ((src->key && NULL == dst->key) ||
            (src->format && NULL == dst->format) ||
            (src->value_length > 0 && NULL == dst->value))
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_producer_set_data_source_meta: allocation failed");
            tp_producer_clear_data_source_meta(producer);
            return -1;
        }
    }

    producer->cached_meta.attributes = (const tp_meta_attribute_t *)producer->cached_attrs;
    producer->cached_meta.attribute_count = producer->cached_attr_count;
    producer->has_meta = true;
    return 0;
}

void tp_producer_clear_data_source_announce(tp_producer_t *producer)
{
    if (NULL == producer)
    {
        return;
    }

    free((char *)producer->cached_announce.name);
    free((char *)producer->cached_announce.summary);
    memset(&producer->cached_announce, 0, sizeof(producer->cached_announce));
    producer->has_announce = false;
}

void tp_producer_clear_data_source_meta(tp_producer_t *producer)
{
    tp_producer_clear_cached_meta(producer);
    memset(&producer->cached_meta, 0, sizeof(producer->cached_meta));
}

int tp_producer_close(tp_producer_t *producer)
{
    size_t i;

    if (NULL == producer)
    {
        return -1;
    }

    if (producer->descriptor_publication)
    {
        aeron_publication_close(producer->descriptor_publication, NULL, NULL);
        producer->descriptor_publication = NULL;
    }

    if (producer->control_publication)
    {
        aeron_publication_close(producer->control_publication, NULL, NULL);
        producer->control_publication = NULL;
    }

    if (producer->qos_publication)
    {
        aeron_publication_close(producer->qos_publication, NULL, NULL);
        producer->qos_publication = NULL;
    }

    if (producer->metadata_publication)
    {
        aeron_publication_close(producer->metadata_publication, NULL, NULL);
        producer->metadata_publication = NULL;
    }

    if (producer->control_assembler)
    {
        aeron_fragment_assembler_delete(producer->control_assembler);
        producer->control_assembler = NULL;
    }

    if (producer->qos_poller)
    {
        if (producer->qos_poller->assembler)
        {
            aeron_fragment_assembler_delete(producer->qos_poller->assembler);
            producer->qos_poller->assembler = NULL;
        }
        aeron_free(producer->qos_poller);
        producer->qos_poller = NULL;
    }

    tp_shm_unmap(&producer->header_region, &producer->client->context.base.log);

    if (producer->pools)
    {
        for (i = 0; i < producer->pool_count; i++)
        {
            tp_shm_unmap(&producer->pools[i].region, &producer->client->context.base.log);
        }
        aeron_free(producer->pools);
        producer->pools = NULL;
    }
    producer->pool_count = 0;

    tp_producer_clear_shm_uris(producer);

    if (producer->consumer_manager)
    {
        tp_consumer_manager_close(producer->consumer_manager);
        aeron_free(producer->consumer_manager);
        producer->consumer_manager = NULL;
    }

    tp_producer_clear_data_source_meta(producer);
    tp_producer_clear_data_source_announce(producer);

    if (producer->tracelink_entries)
    {
        aeron_free(producer->tracelink_entries);
        producer->tracelink_entries = NULL;
        producer->tracelink_entry_count = 0;
    }

    if (producer->driver_attached)
    {
        tp_driver_detach(
            &producer->driver,
            tp_clock_now_ns(),
            producer->driver.active_lease_id,
            producer->driver.active_stream_id,
            producer->driver.client_id,
            producer->driver.role);
        tp_driver_attach_info_close(&producer->driver_attach);
        producer->driver_attached = false;
    }

    if (producer->driver_initialized)
    {
        tp_driver_client_close(&producer->driver);
        producer->driver_initialized = false;
    }

    return 0;
}
