#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_supervisor.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include "tensor_pool/tp_clock.h"
#include "tensor_pool/internal/tp_consumer_registry.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"
#include "tensor_pool/internal/tp_context.h"
#include "tensor_pool/internal/tp_aeron.h"

#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/qosConsumer.h"
#include "wire/tensor_pool/qosProducer.h"
#include "wire/tensor_pool/shmPoolAnnounce.h"

static tp_consumer_registry_t *tp_supervisor_registry(tp_supervisor_t *supervisor)
{
    return (tp_consumer_registry_t *)supervisor->registry;
}

static int tp_supervisor_offer_message(tp_publication_t *pub, const uint8_t *buffer, size_t length)
{
    int result;

    if (NULL == pub || NULL == buffer || length == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_offer_message: invalid input");
        return -1;
    }

    result = (int)aeron_publication_offer(tp_publication_handle(pub), buffer, length, NULL, NULL);
    if (result < 0)
    {
        TP_SET_ERR(-result, "%s", "tp_supervisor_offer_message: offer failed");
        return -1;
    }

    return 0;
}

static int tp_supervisor_send_consumer_config(tp_supervisor_t *supervisor, const tp_consumer_config_msg_t *config)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_consumerConfig cfg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_consumerConfig_sbe_block_length();

    if (NULL == supervisor || NULL == supervisor->control_publication || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_send_consumer_config: control publication unavailable");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_consumerConfig_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_consumerConfig_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_consumerConfig_sbe_schema_version());

    tensor_pool_consumerConfig_wrap_for_encode(&cfg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_consumerConfig_set_streamId(&cfg, config->stream_id);
    tensor_pool_consumerConfig_set_consumerId(&cfg, config->consumer_id);
    tensor_pool_consumerConfig_set_useShm(&cfg, config->use_shm);
    tensor_pool_consumerConfig_set_mode(&cfg, (enum tensor_pool_mode)config->mode);
    tensor_pool_consumerConfig_set_descriptorStreamId(&cfg, config->descriptor_stream_id);
    tensor_pool_consumerConfig_set_controlStreamId(&cfg, config->control_stream_id);

    if (NULL != config->payload_fallback_uri)
    {
        if (tensor_pool_consumerConfig_put_payloadFallbackUri(&cfg, config->payload_fallback_uri,
                strlen(config->payload_fallback_uri)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_consumerConfig_put_payloadFallbackUri(&cfg, "", 0);
    }

    if (NULL != config->descriptor_channel)
    {
        if (tensor_pool_consumerConfig_put_descriptorChannel(&cfg, config->descriptor_channel,
                strlen(config->descriptor_channel)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_consumerConfig_put_descriptorChannel(&cfg, "", 0);
    }

    if (NULL != config->control_channel)
    {
        if (tensor_pool_consumerConfig_put_controlChannel(&cfg, config->control_channel,
                strlen(config->control_channel)) < 0)
        {
            return -1;
        }
    }
    else
    {
        tensor_pool_consumerConfig_put_controlChannel(&cfg, "", 0);
    }

    if (tp_supervisor_offer_message(supervisor->control_publication, buffer,
            tensor_pool_consumerConfig_sbe_position(&cfg)) < 0)
    {
        return -1;
    }

    supervisor->config_count++;
    return 0;
}

static uint32_t tp_supervisor_assign_stream_id(uint32_t base, uint32_t range, uint32_t consumer_id)
{
    if (base == 0)
    {
        return 0;
    }
    if (range == 0)
    {
        return 0;
    }
    return base + (consumer_id % range);
}

int tp_supervisor_handle_hello(
    tp_supervisor_t *supervisor,
    const tp_consumer_hello_view_t *hello,
    tp_consumer_config_msg_t *out_config)
{
    tp_consumer_request_t request;
    tp_consumer_entry_t *entry = NULL;
    tp_consumer_registry_t *registry = NULL;
    tp_consumer_config_msg_t config;
    const char *descriptor_channel = "";
    const char *control_channel = "";
    uint32_t descriptor_stream_id = 0;
    uint32_t control_stream_id = 0;
    uint64_t now_ns = tp_clock_now_ns();

    if (NULL == supervisor || NULL == hello)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_handle_hello: null input");
        return -1;
    }

    if (tp_consumer_request_validate(hello, &request) < 0)
    {
        return -1;
    }

    registry = tp_supervisor_registry(supervisor);
    if (NULL == registry || tp_consumer_registry_update(registry, hello, now_ns, &entry) < 0)
    {
        return -1;
    }

    if (supervisor->config.per_consumer_enabled && request.descriptor_requested)
    {
        descriptor_channel = (supervisor->config.per_consumer_descriptor_channel[0] != '\0')
            ? supervisor->config.per_consumer_descriptor_channel
            : entry->descriptor_channel;
        descriptor_stream_id = (supervisor->config.per_consumer_descriptor_base != 0)
            ? tp_supervisor_assign_stream_id(
                (uint32_t)supervisor->config.per_consumer_descriptor_base,
                supervisor->config.per_consumer_descriptor_range,
                hello->consumer_id)
            : entry->descriptor_stream_id;
        if (descriptor_channel[0] == '\0' || descriptor_stream_id == 0)
        {
            descriptor_channel = "";
            descriptor_stream_id = 0;
        }
        else
        {
            strncpy(entry->descriptor_channel, descriptor_channel, sizeof(entry->descriptor_channel) - 1);
            entry->descriptor_channel[sizeof(entry->descriptor_channel) - 1] = '\0';
            entry->descriptor_stream_id = descriptor_stream_id;
        }
    }

    if (supervisor->config.per_consumer_enabled && request.control_requested)
    {
        control_channel = (supervisor->config.per_consumer_control_channel[0] != '\0')
            ? supervisor->config.per_consumer_control_channel
            : entry->control_channel;
        control_stream_id = (supervisor->config.per_consumer_control_base != 0)
            ? tp_supervisor_assign_stream_id(
                (uint32_t)supervisor->config.per_consumer_control_base,
                supervisor->config.per_consumer_control_range,
                hello->consumer_id)
            : entry->control_stream_id;
        if (control_channel[0] == '\0' || control_stream_id == 0)
        {
            control_channel = "";
            control_stream_id = 0;
        }
        else
        {
            strncpy(entry->control_channel, control_channel, sizeof(entry->control_channel) - 1);
            entry->control_channel[sizeof(entry->control_channel) - 1] = '\0';
            entry->control_stream_id = control_stream_id;
        }
    }

    memset(&config, 0, sizeof(config));
    config.stream_id = hello->stream_id;
    config.consumer_id = hello->consumer_id;
    config.use_shm = supervisor->config.force_no_shm ? 0 : hello->supports_shm;
    config.mode = supervisor->config.force_mode != 0 ? supervisor->config.force_mode : hello->mode;
    config.descriptor_stream_id = descriptor_stream_id;
    config.control_stream_id = control_stream_id;
    config.payload_fallback_uri = supervisor->config.payload_fallback_uri[0] != '\0'
        ? supervisor->config.payload_fallback_uri
        : "";
    config.descriptor_channel = descriptor_channel;
    config.control_channel = control_channel;

    if (out_config)
    {
        *out_config = config;
    }

    if (supervisor->control_publication)
    {
        supervisor->hello_count++;
        return tp_supervisor_send_consumer_config(supervisor, &config);
    }

    supervisor->hello_count++;
    return 0;
}

static void tp_supervisor_on_control_fragment(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_supervisor_t *supervisor = (tp_supervisor_t *)clientd;
    tp_consumer_hello_view_t hello;

    (void)header;

    if (NULL == supervisor || NULL == buffer)
    {
        return;
    }

    if (tp_control_decode_consumer_hello(buffer, length, &hello) == 0)
    {
        if (tp_supervisor_handle_hello(supervisor, &hello, NULL) < 0)
        {
            tp_log_emit(&supervisor->config.base->log, TP_LOG_WARN,
                "supervisor: consumer hello handling failed: %s", tp_errmsg());
        }
    }
}

static void tp_supervisor_on_qos_fragment(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_supervisor_t *supervisor = (tp_supervisor_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_qosConsumer qos;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    uint32_t consumer_id;
    uint64_t now_ns;
    tp_consumer_registry_t *registry;
    size_t i;

    (void)header;

    if (NULL == supervisor || NULL == buffer || length < tensor_pool_messageHeader_encoded_length())
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
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);

    if (schema_id == tensor_pool_qosProducer_sbe_schema_id() &&
        template_id == tensor_pool_qosProducer_sbe_template_id())
    {
        supervisor->qos_producer_count++;
        return;
    }

    if (schema_id != tensor_pool_qosConsumer_sbe_schema_id() ||
        template_id != tensor_pool_qosConsumer_sbe_template_id())
    {
        return;
    }

    tensor_pool_qosConsumer_wrap_for_decode(
        &qos,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    consumer_id = tensor_pool_qosConsumer_consumerId(&qos);
    now_ns = tp_clock_now_ns();
    supervisor->qos_consumer_count++;

    registry = tp_supervisor_registry(supervisor);
    if (registry == NULL || registry->entries == NULL)
    {
        return;
    }

    for (i = 0; i < registry->capacity; i++)
    {
        tp_consumer_entry_t *entry = &registry->entries[i];
        if (entry->in_use && entry->consumer_id == consumer_id)
        {
            entry->last_seen_ns = now_ns;
            return;
        }
    }
}

static void tp_supervisor_on_announce_fragment(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_supervisor_t *supervisor = (tp_supervisor_t *)clientd;
    (void)buffer;
    (void)length;
    (void)header;

    if (supervisor)
    {
        supervisor->announce_count++;
    }
}

static void tp_supervisor_on_metadata_fragment(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_supervisor_t *supervisor = (tp_supervisor_t *)clientd;
    (void)buffer;
    (void)length;
    (void)header;

    if (supervisor)
    {
        supervisor->metadata_count++;
    }
}

int tp_supervisor_init(tp_supervisor_t *supervisor, tp_supervisor_config_t *config)
{
    tp_consumer_registry_t *registry;

    if (NULL == supervisor || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_init: null input");
        return -1;
    }

    memset(supervisor, 0, sizeof(*supervisor));
    supervisor->config = *config;
    memset(config, 0, sizeof(*config));

    registry = (tp_consumer_registry_t *)calloc(1, sizeof(*registry));
    if (NULL == registry)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_supervisor_init: registry allocation failed");
        return -1;
    }

    if (tp_consumer_registry_init(registry, supervisor->config.consumer_capacity) < 0)
    {
        free(registry);
        return -1;
    }

    supervisor->registry = registry;
    supervisor->last_sweep_ns = tp_clock_now_ns();
    return 0;
}

int tp_supervisor_start(tp_supervisor_t *supervisor)
{
    if (NULL == supervisor)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_start: null supervisor");
        return -1;
    }

    if (tp_aeron_client_init(&supervisor->aeron, supervisor->config.base) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_subscription(&supervisor->control_subscription, &supervisor->aeron,
            supervisor->config.control_channel, supervisor->config.control_stream_id) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_subscription(&supervisor->announce_subscription, &supervisor->aeron,
            supervisor->config.announce_channel, supervisor->config.announce_stream_id) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_subscription(&supervisor->metadata_subscription, &supervisor->aeron,
            supervisor->config.metadata_channel, supervisor->config.metadata_stream_id) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_subscription(&supervisor->qos_subscription, &supervisor->aeron,
            supervisor->config.qos_channel, supervisor->config.qos_stream_id) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_publication(&supervisor->control_publication, &supervisor->aeron,
            supervisor->config.control_channel, supervisor->config.control_stream_id) < 0)
    {
        return -1;
    }

    if (tp_fragment_assembler_create(&supervisor->control_assembler, tp_supervisor_on_control_fragment, supervisor) < 0 ||
        tp_fragment_assembler_create(&supervisor->announce_assembler, tp_supervisor_on_announce_fragment, supervisor) < 0 ||
        tp_fragment_assembler_create(&supervisor->metadata_assembler, tp_supervisor_on_metadata_fragment, supervisor) < 0 ||
        tp_fragment_assembler_create(&supervisor->qos_assembler, tp_supervisor_on_qos_fragment, supervisor) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_supervisor_do_work(tp_supervisor_t *supervisor)
{
    uint64_t now_ns;
    uint64_t sweep_interval_ns;
    int work = 0;

    if (NULL == supervisor)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_do_work: null supervisor");
        return -1;
    }

    if (supervisor->control_subscription && supervisor->control_assembler)
    {
        work += aeron_subscription_poll(
            tp_subscription_handle(supervisor->control_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(supervisor->control_assembler),
            10);
    }

    if (supervisor->announce_subscription && supervisor->announce_assembler)
    {
        work += aeron_subscription_poll(
            tp_subscription_handle(supervisor->announce_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(supervisor->announce_assembler),
            10);
    }

    if (supervisor->metadata_subscription && supervisor->metadata_assembler)
    {
        work += aeron_subscription_poll(
            tp_subscription_handle(supervisor->metadata_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(supervisor->metadata_assembler),
            10);
    }

    if (supervisor->qos_subscription && supervisor->qos_assembler)
    {
        work += aeron_subscription_poll(
            tp_subscription_handle(supervisor->qos_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(supervisor->qos_assembler),
            10);
    }

    now_ns = tp_clock_now_ns();
    sweep_interval_ns = (uint64_t)supervisor->config.consumer_stale_ms * 1000000ULL;
    if (sweep_interval_ns > 0 && now_ns - supervisor->last_sweep_ns >= sweep_interval_ns)
    {
        tp_consumer_registry_t *registry = tp_supervisor_registry(supervisor);
        if (registry)
        {
            (void)tp_consumer_registry_sweep(registry, now_ns, sweep_interval_ns);
        }
        supervisor->last_sweep_ns = now_ns;
    }

    return work;
}

int tp_supervisor_close(tp_supervisor_t *supervisor)
{
    tp_consumer_registry_t *registry;

    if (NULL == supervisor)
    {
        return 0;
    }

    tp_fragment_assembler_close(&supervisor->control_assembler);
    tp_fragment_assembler_close(&supervisor->announce_assembler);
    tp_fragment_assembler_close(&supervisor->metadata_assembler);
    tp_fragment_assembler_close(&supervisor->qos_assembler);

    tp_subscription_close(&supervisor->control_subscription);
    tp_subscription_close(&supervisor->announce_subscription);
    tp_subscription_close(&supervisor->metadata_subscription);
    tp_subscription_close(&supervisor->qos_subscription);
    tp_publication_close(&supervisor->control_publication);

    tp_aeron_client_close(&supervisor->aeron);

    registry = tp_supervisor_registry(supervisor);
    if (registry)
    {
        tp_consumer_registry_close(registry);
        free(registry);
    }

    memset(supervisor, 0, sizeof(*supervisor));
    return 0;
}

int tp_supervisor_get_stats(const tp_supervisor_t *supervisor, tp_supervisor_stats_t *out)
{
    if (NULL == supervisor || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_get_stats: null input");
        return -1;
    }

    out->hello_count = supervisor->hello_count;
    out->config_count = supervisor->config_count;
    out->qos_consumer_count = supervisor->qos_consumer_count;
    out->qos_producer_count = supervisor->qos_producer_count;
    out->announce_count = supervisor->announce_count;
    out->metadata_count = supervisor->metadata_count;
    return 0;
}
