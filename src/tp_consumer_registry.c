#include "tensor_pool/tp_consumer_registry.h"

#include <errno.h>
#include <string.h>

#include "aeron_alloc.h"

#include "tensor_pool/tp_error.h"

static int tp_copy_string_view(char *dst, size_t dst_len, const tp_string_view_t *view)
{
    if (NULL == dst || 0 == dst_len || NULL == view)
    {
        return -1;
    }

    if (view->length == 0 || NULL == view->data)
    {
        dst[0] = '\0';
        return 0;
    }

    if (view->length >= dst_len)
    {
        return -1;
    }

    memcpy(dst, view->data, view->length);
    dst[view->length] = '\0';
    return 0;
}

int tp_consumer_request_validate(const tp_consumer_hello_view_t *hello, tp_consumer_request_t *out)
{
    if (NULL == hello || NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_request_validate: null input");
        return -1;
    }

    memset(out, 0, sizeof(*out));

    if ((hello->descriptor_channel.length > 0 && hello->descriptor_stream_id == 0) ||
        (hello->descriptor_channel.length == 0 && hello->descriptor_stream_id != 0))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_request_validate: invalid descriptor request");
        return -1;
    }

    if ((hello->control_channel.length > 0 && hello->control_stream_id == 0) ||
        (hello->control_channel.length == 0 && hello->control_stream_id != 0))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_request_validate: invalid control request");
        return -1;
    }

    out->descriptor_requested = (hello->descriptor_channel.length > 0 && hello->descriptor_stream_id != 0);
    out->control_requested = (hello->control_channel.length > 0 && hello->control_stream_id != 0);

    return 0;
}

int tp_consumer_registry_init(tp_consumer_registry_t *registry, size_t capacity)
{
    if (NULL == registry || capacity == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_registry_init: invalid input");
        return -1;
    }

    memset(registry, 0, sizeof(*registry));

    if (aeron_alloc((void **)&registry->entries, sizeof(tp_consumer_entry_t) * capacity) < 0)
    {
        return -1;
    }

    registry->capacity = capacity;
    return 0;
}

void tp_consumer_registry_close(tp_consumer_registry_t *registry)
{
    size_t i;

    if (NULL == registry || NULL == registry->entries)
    {
        return;
    }

    for (i = 0; i < registry->capacity; i++)
    {
        tp_consumer_entry_t *entry = &registry->entries[i];
        if (entry->descriptor_publication)
        {
            aeron_publication_close(entry->descriptor_publication, NULL, NULL);
            entry->descriptor_publication = NULL;
        }
        if (entry->control_publication)
        {
            aeron_publication_close(entry->control_publication, NULL, NULL);
            entry->control_publication = NULL;
        }
    }

    aeron_free(registry->entries);
    registry->entries = NULL;
    registry->capacity = 0;
}

static tp_consumer_entry_t *tp_consumer_registry_find(tp_consumer_registry_t *registry, uint32_t consumer_id)
{
    size_t i;

    for (i = 0; i < registry->capacity; i++)
    {
        tp_consumer_entry_t *entry = &registry->entries[i];
        if (entry->in_use && entry->consumer_id == consumer_id)
        {
            return entry;
        }
    }

    return NULL;
}

static tp_consumer_entry_t *tp_consumer_registry_alloc(tp_consumer_registry_t *registry, uint32_t consumer_id)
{
    size_t i;

    for (i = 0; i < registry->capacity; i++)
    {
        tp_consumer_entry_t *entry = &registry->entries[i];
        if (!entry->in_use)
        {
            memset(entry, 0, sizeof(*entry));
            entry->in_use = true;
            entry->consumer_id = consumer_id;
            return entry;
        }
    }

    return NULL;
}

int tp_consumer_registry_update(
    tp_consumer_registry_t *registry,
    const tp_consumer_hello_view_t *hello,
    uint64_t now_ns,
    tp_consumer_entry_t **out_entry)
{
    tp_consumer_request_t request;
    tp_consumer_entry_t *entry;

    if (NULL == registry || NULL == hello)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_registry_update: null input");
        return -1;
    }

    if (tp_consumer_request_validate(hello, &request) < 0)
    {
        return -1;
    }

    entry = tp_consumer_registry_find(registry, hello->consumer_id);
    if (NULL == entry)
    {
        entry = tp_consumer_registry_alloc(registry, hello->consumer_id);
        if (NULL == entry)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_consumer_registry_update: registry full");
            return -1;
        }
    }

    entry->last_seen_ns = now_ns;
    entry->mode = hello->mode;
    entry->max_rate_hz = hello->max_rate_hz;
    entry->supports_progress = hello->supports_progress;
    entry->progress_interval_us = hello->progress_interval_us;
    entry->progress_bytes_delta = hello->progress_bytes_delta;
    entry->progress_major_delta_units = hello->progress_major_delta_units;

    if (tp_copy_string_view(entry->descriptor_channel, sizeof(entry->descriptor_channel), &hello->descriptor_channel) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_registry_update: descriptor channel too long");
        return -1;
    }
    entry->descriptor_stream_id = hello->descriptor_stream_id;

    if (tp_copy_string_view(entry->control_channel, sizeof(entry->control_channel), &hello->control_channel) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_registry_update: control channel too long");
        return -1;
    }
    entry->control_stream_id = hello->control_stream_id;

    if (NULL != out_entry)
    {
        *out_entry = entry;
    }

    return 0;
}

int tp_consumer_registry_sweep(tp_consumer_registry_t *registry, uint64_t now_ns, uint64_t stale_ns)
{
    size_t i;
    int cleaned = 0;

    if (NULL == registry || NULL == registry->entries)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_registry_sweep: null input");
        return -1;
    }

    for (i = 0; i < registry->capacity; i++)
    {
        tp_consumer_entry_t *entry = &registry->entries[i];
        if (!entry->in_use)
        {
            continue;
        }

        if (now_ns - entry->last_seen_ns > stale_ns)
        {
            if (entry->descriptor_publication)
            {
                aeron_publication_close(entry->descriptor_publication, NULL, NULL);
                entry->descriptor_publication = NULL;
            }
            if (entry->control_publication)
            {
                aeron_publication_close(entry->control_publication, NULL, NULL);
                entry->control_publication = NULL;
            }
            entry->in_use = false;
            cleaned++;
        }
    }

    return cleaned;
}

int tp_consumer_registry_aggregate_progress(
    const tp_consumer_registry_t *registry,
    tp_progress_policy_t *out_policy)
{
    size_t i;
    uint32_t interval = TP_PROGRESS_INTERVAL_DEFAULT_US;
    uint32_t bytes_delta = TP_PROGRESS_BYTES_DELTA_DEFAULT;
    uint32_t major_delta = 0;
    bool have_interval = false;
    bool have_bytes = false;
    bool have_major = false;

    if (NULL == registry || NULL == out_policy)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_consumer_registry_aggregate_progress: null input");
        return -1;
    }

    for (i = 0; i < registry->capacity; i++)
    {
        const tp_consumer_entry_t *entry = &registry->entries[i];
        if (!entry->in_use || !entry->supports_progress)
        {
            continue;
        }

        if (entry->progress_interval_us != TP_NULL_U32)
        {
            interval = have_interval ? (entry->progress_interval_us < interval ? entry->progress_interval_us : interval)
                                      : entry->progress_interval_us;
            have_interval = true;
        }

        if (entry->progress_bytes_delta != TP_NULL_U32)
        {
            bytes_delta = have_bytes ? (entry->progress_bytes_delta < bytes_delta ? entry->progress_bytes_delta : bytes_delta)
                                      : entry->progress_bytes_delta;
            have_bytes = true;
        }

        if (entry->progress_major_delta_units != TP_NULL_U32)
        {
            major_delta = have_major ? (entry->progress_major_delta_units < major_delta ? entry->progress_major_delta_units : major_delta)
                                      : entry->progress_major_delta_units;
            have_major = true;
        }
    }

    out_policy->interval_us = interval;
    out_policy->bytes_delta = bytes_delta;
    out_policy->major_delta_units = major_delta;

    return 0;
}
