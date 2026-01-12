#include "tensor_pool/tp_context.h"

#include <string.h>

#include "tensor_pool/tp_types.h"

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

    context->allowed_paths.paths = paths;
    context->allowed_paths.length = length;
}
