#include "tensor_pool/tp_consumer_manager.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_control.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"

static tp_consumer_entry_t *tp_consumer_manager_find_entry(tp_consumer_manager_t *manager, uint32_t consumer_id)
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

int tp_consumer_manager_init(tp_consumer_manager_t *manager, tp_producer_t *producer, size_t capacity)
{
    if (NULL == manager || NULL == producer || capacity == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_init: invalid input");
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    manager->producer = producer;
    manager->progress_policy.interval_us = TP_PROGRESS_INTERVAL_DEFAULT_US;
    manager->progress_policy.bytes_delta = TP_PROGRESS_BYTES_DELTA_DEFAULT;
    manager->progress_policy.major_delta_units = 0;

    if (tp_consumer_registry_init(&manager->registry, capacity) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_consumer_manager_close(tp_consumer_manager_t *manager)
{
    if (NULL == manager)
    {
        return -1;
    }

    tp_consumer_registry_close(&manager->registry);
    manager->producer = NULL;
    return 0;
}

int tp_consumer_manager_handle_hello(
    tp_consumer_manager_t *manager,
    const tp_consumer_hello_view_t *hello,
    uint64_t now_ns)
{
    tp_consumer_request_t request;
    tp_consumer_entry_t *entry = NULL;
    tp_consumer_config_msg_t config;
    const char *descriptor_channel = "";
    const char *control_channel = "";
    uint32_t descriptor_stream_id = 0;
    uint32_t control_stream_id = 0;

    if (NULL == manager || NULL == manager->producer || NULL == hello)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_handle_hello: null input");
        return -1;
    }

    if (tp_consumer_request_validate(hello, &request) < 0)
    {
        return -1;
    }

    if (tp_consumer_registry_update(&manager->registry, hello, now_ns, &entry) < 0)
    {
        return -1;
    }

    if (request.descriptor_requested)
    {
        if (NULL == entry->descriptor_publication)
        {
            if (tp_aeron_add_publication(
                &entry->descriptor_publication,
                &manager->producer->aeron,
                entry->descriptor_channel,
                (int32_t)entry->descriptor_stream_id) < 0)
            {
                entry->descriptor_publication = NULL;
            }
        }

        if (NULL != entry->descriptor_publication)
        {
            descriptor_channel = entry->descriptor_channel;
            descriptor_stream_id = entry->descriptor_stream_id;
        }
    }
    else if (entry->descriptor_publication)
    {
        aeron_publication_close(entry->descriptor_publication, NULL, NULL);
        entry->descriptor_publication = NULL;
    }

    if (request.control_requested)
    {
        if (NULL == entry->control_publication)
        {
            if (tp_aeron_add_publication(
                &entry->control_publication,
                &manager->producer->aeron,
                entry->control_channel,
                (int32_t)entry->control_stream_id) < 0)
            {
                entry->control_publication = NULL;
            }
        }

        if (NULL != entry->control_publication)
        {
            control_channel = entry->control_channel;
            control_stream_id = entry->control_stream_id;
        }
    }
    else if (entry->control_publication)
    {
        aeron_publication_close(entry->control_publication, NULL, NULL);
        entry->control_publication = NULL;
    }

    memset(&config, 0, sizeof(config));
    config.stream_id = hello->stream_id;
    config.consumer_id = hello->consumer_id;
    config.use_shm = hello->supports_shm;
    config.mode = hello->mode;
    config.descriptor_stream_id = descriptor_stream_id;
    config.control_stream_id = control_stream_id;
    config.payload_fallback_uri = "";
    config.descriptor_channel = descriptor_channel;
    config.control_channel = control_channel;

    if (tp_consumer_manager_refresh_progress_policy(manager) < 0)
    {
        return -1;
    }

    return tp_producer_send_consumer_config(manager->producer, &config);
}

int tp_consumer_manager_touch(tp_consumer_manager_t *manager, uint32_t consumer_id, uint64_t now_ns)
{
    tp_consumer_entry_t *entry = tp_consumer_manager_find_entry(manager, consumer_id);

    if (NULL == entry)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_touch: unknown consumer");
        return -1;
    }

    entry->last_seen_ns = now_ns;
    return 0;
}

int tp_consumer_manager_sweep(tp_consumer_manager_t *manager, uint64_t now_ns, uint64_t stale_ns)
{
    int cleaned;

    if (NULL == manager)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_sweep: null input");
        return -1;
    }

    cleaned = tp_consumer_registry_sweep(&manager->registry, now_ns, stale_ns);
    if (cleaned < 0)
    {
        return -1;
    }

    if (tp_consumer_manager_refresh_progress_policy(manager) < 0)
    {
        return -1;
    }

    return cleaned;
}

int tp_consumer_manager_refresh_progress_policy(tp_consumer_manager_t *manager)
{
    if (NULL == manager)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_refresh_progress_policy: null input");
        return -1;
    }

    return tp_consumer_registry_aggregate_progress(&manager->registry, &manager->progress_policy);
}

int tp_consumer_manager_get_progress_policy(const tp_consumer_manager_t *manager, tp_progress_policy_t *out)
{
    if (NULL == manager || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_get_progress_policy: null input");
        return -1;
    }

    *out = manager->progress_policy;
    return 0;
}

int tp_consumer_manager_should_publish_progress(
    const tp_consumer_manager_t *manager,
    tp_progress_state_t *state,
    uint64_t now_ns,
    uint64_t payload_bytes_filled,
    int *out_should_publish)
{
    uint64_t interval_ns;
    int should_publish = 0;

    if (NULL == manager || NULL == state || NULL == out_should_publish)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_should_publish_progress: null input");
        return -1;
    }

    interval_ns = (uint64_t)manager->progress_policy.interval_us * 1000ULL;

    if (state->last_timestamp_ns == 0)
    {
        should_publish = 1;
    }
    else if (interval_ns > 0 && now_ns - state->last_timestamp_ns >= interval_ns)
    {
        should_publish = 1;
    }
    else if (manager->progress_policy.bytes_delta > 0 &&
        payload_bytes_filled >= state->last_bytes + manager->progress_policy.bytes_delta)
    {
        should_publish = 1;
    }
    else if (manager->progress_policy.major_delta_units > 0 &&
        payload_bytes_filled >= state->last_bytes + manager->progress_policy.major_delta_units)
    {
        should_publish = 1;
    }

    if (should_publish)
    {
        state->last_timestamp_ns = now_ns;
        state->last_bytes = payload_bytes_filled;
    }

    *out_should_publish = should_publish;
    return 0;
}

int tp_consumer_manager_publish_descriptor(
    tp_consumer_manager_t *manager,
    uint32_t consumer_id,
    uint64_t seq,
    uint32_t header_index,
    uint64_t timestamp_ns,
    uint32_t meta_version)
{
    tp_consumer_entry_t *entry;
    aeron_publication_t *publication = NULL;

    if (NULL == manager || NULL == manager->producer)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_publish_descriptor: null input");
        return -1;
    }

    entry = tp_consumer_manager_find_entry(manager, consumer_id);
    if (entry && entry->descriptor_publication)
    {
        publication = entry->descriptor_publication;
    }
    else
    {
        publication = manager->producer->descriptor_publication;
    }

    return tp_producer_publish_descriptor_to(
        manager->producer,
        publication,
        seq,
        header_index,
        timestamp_ns,
        meta_version);
}

int tp_consumer_manager_publish_progress(
    tp_consumer_manager_t *manager,
    uint32_t consumer_id,
    uint64_t seq,
    uint32_t header_index,
    uint64_t payload_bytes_filled,
    uint8_t state)
{
    tp_consumer_entry_t *entry;
    aeron_publication_t *publication = NULL;

    if (NULL == manager || NULL == manager->producer)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_manager_publish_progress: null input");
        return -1;
    }

    entry = tp_consumer_manager_find_entry(manager, consumer_id);
    if (entry && entry->control_publication)
    {
        publication = entry->control_publication;
    }
    else
    {
        publication = manager->producer->control_publication;
    }

    return tp_producer_publish_progress_to(
        manager->producer,
        publication,
        seq,
        header_index,
        payload_bytes_filled,
        state);
}
