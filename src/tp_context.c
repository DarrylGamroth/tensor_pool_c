#include "tensor_pool/tp_context.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
extern char *realpath(const char *path, char *resolved_path);
#endif

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"

static char *tp_strdup(const char *value)
{
    size_t len;
    char *copy;

    if (NULL == value)
    {
        return NULL;
    }

    len = strlen(value) + 1;
    copy = malloc(len);
    if (NULL == copy)
    {
        return NULL;
    }

    memcpy(copy, value, len);
    return copy;
}

int tp_context_init(tp_context_t *context)
{
    if (NULL == context)
    {
        return -1;
    }

    memset(context, 0, sizeof(*context));
    tp_log_init(&context->log);
    context->descriptor_stream_id = -1;
    context->control_stream_id = -1;
    context->qos_stream_id = -1;
    context->metadata_stream_id = -1;
    context->announce_period_ns = TP_ANNOUNCE_PERIOD_DEFAULT_NS;

    return 0;
}

void tp_context_set_aeron_dir(tp_context_t *context, const char *dir)
{
    if (NULL == context || NULL == dir)
    {
        return;
    }

    strncpy(context->aeron_dir, dir, sizeof(context->aeron_dir) - 1);
}

static void tp_context_set_channel(char *dst, size_t dst_len, const char *channel, int32_t *stream_id, int32_t value)
{
    if (NULL == channel || NULL == dst || NULL == stream_id)
    {
        return;
    }

    strncpy(dst, channel, dst_len - 1);
    *stream_id = value;
}

void tp_context_set_descriptor_channel(tp_context_t *context, const char *channel, int32_t stream_id)
{
    tp_context_set_channel(context->descriptor_channel, sizeof(context->descriptor_channel), channel,
        &context->descriptor_stream_id, stream_id);
}

void tp_context_set_control_channel(tp_context_t *context, const char *channel, int32_t stream_id)
{
    tp_context_set_channel(context->control_channel, sizeof(context->control_channel), channel,
        &context->control_stream_id, stream_id);
}

void tp_context_set_qos_channel(tp_context_t *context, const char *channel, int32_t stream_id)
{
    tp_context_set_channel(context->qos_channel, sizeof(context->qos_channel), channel,
        &context->qos_stream_id, stream_id);
}

void tp_context_set_metadata_channel(tp_context_t *context, const char *channel, int32_t stream_id)
{
    tp_context_set_channel(context->metadata_channel, sizeof(context->metadata_channel), channel,
        &context->metadata_stream_id, stream_id);
}

void tp_context_set_allowed_paths(tp_context_t *context, const char **paths, size_t length)
{
    if (NULL == context)
    {
        return;
    }

    tp_context_clear_allowed_paths(context);
    context->allowed_paths.paths = paths;
    context->allowed_paths.length = length;
}

void tp_context_set_announce_period_ns(tp_context_t *context, uint64_t period_ns)
{
    if (NULL == context)
    {
        return;
    }

    context->announce_period_ns = period_ns;
}

int tp_context_finalize_allowed_paths(tp_context_t *context)
{
    size_t i;

    if (NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_context_finalize_allowed_paths: null context");
        return -1;
    }

    tp_context_clear_allowed_paths(context);

    if (NULL == context->allowed_paths.paths || context->allowed_paths.length == 0)
    {
        return 0;
    }

    context->allowed_paths.canonical_paths = calloc(context->allowed_paths.length, sizeof(char *));
    if (NULL == context->allowed_paths.canonical_paths)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_context_finalize_allowed_paths: allocation failed");
        return -1;
    }

    for (i = 0; i < context->allowed_paths.length; i++)
    {
        const char *path = context->allowed_paths.paths[i];
        char resolved_path[4096];

        if (NULL == path || NULL == realpath(path, resolved_path))
        {
            TP_SET_ERR(errno, "tp_context_finalize_allowed_paths: realpath failed for %s", path ? path : "(null)");
            tp_context_clear_allowed_paths(context);
            return -1;
        }

        context->allowed_paths.canonical_paths[i] = tp_strdup(resolved_path);
        if (NULL == context->allowed_paths.canonical_paths[i])
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_context_finalize_allowed_paths: allocation failed");
            tp_context_clear_allowed_paths(context);
            return -1;
        }
    }

    context->allowed_paths.canonical_length = context->allowed_paths.length;
    return 0;
}

void tp_context_clear_allowed_paths(tp_context_t *context)
{
    size_t i;

    if (NULL == context)
    {
        return;
    }

    if (context->allowed_paths.canonical_paths)
    {
        for (i = 0; i < context->allowed_paths.canonical_length; i++)
        {
            free(context->allowed_paths.canonical_paths[i]);
        }
        free(context->allowed_paths.canonical_paths);
    }

    context->allowed_paths.canonical_paths = NULL;
    context->allowed_paths.canonical_length = 0;
}
