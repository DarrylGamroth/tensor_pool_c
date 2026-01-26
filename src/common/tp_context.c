#include "tensor_pool/tp_context.h"
#include "tensor_pool/internal/tp_context.h"

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

int tp_context_init(tp_context_t **context)
{
    tp_context_t *ctx = NULL;

    if (NULL == context)
    {
        return -1;
    }

    ctx = calloc(1, sizeof(*ctx));
    if (NULL == ctx)
    {
        return -1;
    }

    tp_log_init(&ctx->log);
    ctx->descriptor_stream_id = -1;
    ctx->control_stream_id = -1;
    ctx->announce_stream_id = -1;
    ctx->qos_stream_id = -1;
    ctx->metadata_stream_id = -1;
    ctx->announce_period_ns = TP_ANNOUNCE_PERIOD_DEFAULT_NS;
    ctx->owns_aeron_client = true;
    ctx->message_timeout_ns = 0;
    ctx->message_retry_attempts = 0;
    ctx->driver_timeout_ns = 5 * 1000 * 1000 * 1000ULL;
    ctx->keepalive_interval_ns = 1000 * 1000 * 1000ULL;
    ctx->lease_expiry_grace_intervals = 3;
    ctx->allowed_paths.enforce_permissions = 1;
    ctx->allowed_paths.expected_uid = TP_NULL_U32;
    ctx->allowed_paths.expected_gid = TP_NULL_U32;
    ctx->allowed_paths.forbidden_mode = 0007;

    *context = ctx;

    return 0;
}

int tp_context_close(tp_context_t *context)
{
    if (NULL == context)
    {
        return -1;
    }

    tp_context_clear_allowed_paths(context);
    free(context);
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

const char *tp_context_get_aeron_dir(const tp_context_t *context)
{
    if (NULL == context)
    {
        return NULL;
    }

    return context->aeron_dir;
}

void tp_context_set_aeron(tp_context_t *context, void *aeron)
{
    if (NULL == context)
    {
        return;
    }

    context->aeron = aeron;
}

void *tp_context_get_aeron(const tp_context_t *context)
{
    return NULL == context ? NULL : context->aeron;
}

void tp_context_set_owns_aeron_client(tp_context_t *context, bool owns)
{
    if (NULL == context)
    {
        return;
    }

    context->owns_aeron_client = owns;
}

bool tp_context_get_owns_aeron_client(const tp_context_t *context)
{
    return NULL == context ? false : context->owns_aeron_client;
}

void tp_context_set_client_name(tp_context_t *context, const char *name)
{
    if (NULL == context || NULL == name)
    {
        return;
    }

    strncpy(context->client_name, name, sizeof(context->client_name) - 1);
}

const char *tp_context_get_client_name(const tp_context_t *context)
{
    return NULL == context ? NULL : context->client_name;
}

void tp_context_set_message_timeout_ns(tp_context_t *context, int64_t timeout_ns)
{
    if (NULL == context)
    {
        return;
    }

    context->message_timeout_ns = timeout_ns;
}

int64_t tp_context_get_message_timeout_ns(const tp_context_t *context)
{
    return NULL == context ? 0 : context->message_timeout_ns;
}

void tp_context_set_message_retry_attempts(tp_context_t *context, int32_t attempts)
{
    if (NULL == context)
    {
        return;
    }

    context->message_retry_attempts = attempts;
}

int32_t tp_context_get_message_retry_attempts(const tp_context_t *context)
{
    return NULL == context ? 0 : context->message_retry_attempts;
}

void tp_context_set_error_handler(tp_context_t *context, tp_error_handler_t handler, void *clientd)
{
    if (NULL == context)
    {
        return;
    }

    context->error_handler = handler;
    context->error_handler_clientd = clientd;
}

tp_error_handler_t tp_context_get_error_handler(const tp_context_t *context)
{
    return NULL == context ? NULL : context->error_handler;
}

void *tp_context_get_error_handler_clientd(const tp_context_t *context)
{
    return NULL == context ? NULL : context->error_handler_clientd;
}

void tp_context_set_delegating_invoker(tp_context_t *context, tp_delegating_invoker_t invoker, void *clientd)
{
    if (NULL == context)
    {
        return;
    }

    context->delegating_invoker = invoker;
    context->delegating_invoker_clientd = clientd;
}

tp_delegating_invoker_t tp_context_get_delegating_invoker(const tp_context_t *context)
{
    return NULL == context ? NULL : context->delegating_invoker;
}

void *tp_context_get_delegating_invoker_clientd(const tp_context_t *context)
{
    return NULL == context ? NULL : context->delegating_invoker_clientd;
}

void tp_context_set_log_handler(tp_context_t *context, tp_log_func_t handler, void *clientd)
{
    if (NULL == context)
    {
        return;
    }

    tp_log_set_handler(&context->log, handler, clientd);
}

tp_log_func_t tp_context_get_log_handler(const tp_context_t *context)
{
    return NULL == context ? NULL : context->log.handler;
}

void *tp_context_get_log_handler_clientd(const tp_context_t *context)
{
    return NULL == context ? NULL : context->log.clientd;
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
    if (NULL == context)
    {
        return;
    }

    tp_context_set_channel(context->descriptor_channel, sizeof(context->descriptor_channel), channel,
        &context->descriptor_stream_id, stream_id);
}

void tp_context_set_control_channel(tp_context_t *context, const char *channel, int32_t stream_id)
{
    if (NULL == context)
    {
        return;
    }

    tp_context_set_channel(context->control_channel, sizeof(context->control_channel), channel,
        &context->control_stream_id, stream_id);
}

void tp_context_set_announce_channel(tp_context_t *context, const char *channel, int32_t stream_id)
{
    if (NULL == context)
    {
        return;
    }

    tp_context_set_channel(context->announce_channel, sizeof(context->announce_channel), channel,
        &context->announce_stream_id, stream_id);
}

void tp_context_set_qos_channel(tp_context_t *context, const char *channel, int32_t stream_id)
{
    if (NULL == context)
    {
        return;
    }

    tp_context_set_channel(context->qos_channel, sizeof(context->qos_channel), channel,
        &context->qos_stream_id, stream_id);
}

void tp_context_set_metadata_channel(tp_context_t *context, const char *channel, int32_t stream_id)
{
    if (NULL == context)
    {
        return;
    }

    tp_context_set_channel(context->metadata_channel, sizeof(context->metadata_channel), channel,
        &context->metadata_stream_id, stream_id);
}

const char *tp_context_get_descriptor_channel(const tp_context_t *context)
{
    return NULL == context ? NULL : context->descriptor_channel;
}

const char *tp_context_get_control_channel(const tp_context_t *context)
{
    return NULL == context ? NULL : context->control_channel;
}

const char *tp_context_get_announce_channel(const tp_context_t *context)
{
    return NULL == context ? NULL : context->announce_channel;
}

const char *tp_context_get_qos_channel(const tp_context_t *context)
{
    return NULL == context ? NULL : context->qos_channel;
}

const char *tp_context_get_metadata_channel(const tp_context_t *context)
{
    return NULL == context ? NULL : context->metadata_channel;
}

int32_t tp_context_get_descriptor_stream_id(const tp_context_t *context)
{
    return NULL == context ? -1 : context->descriptor_stream_id;
}

int32_t tp_context_get_control_stream_id(const tp_context_t *context)
{
    return NULL == context ? -1 : context->control_stream_id;
}

int32_t tp_context_get_announce_stream_id(const tp_context_t *context)
{
    return NULL == context ? -1 : context->announce_stream_id;
}

int32_t tp_context_get_qos_stream_id(const tp_context_t *context)
{
    return NULL == context ? -1 : context->qos_stream_id;
}

int32_t tp_context_get_metadata_stream_id(const tp_context_t *context)
{
    return NULL == context ? -1 : context->metadata_stream_id;
}

int tp_context_set_default_channels(tp_context_t *context, const char *channel, int32_t announce_stream_id)
{
    if (NULL == context || NULL == channel)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_context_set_default_channels: invalid input");
        return -1;
    }

    tp_context_set_control_channel(context, channel, 1000);
    tp_context_set_descriptor_channel(context, channel, 1100);
    tp_context_set_qos_channel(context, channel, 1200);
    tp_context_set_metadata_channel(context, channel, 1300);
    tp_context_set_announce_channel(context, channel, announce_stream_id > 0 ? announce_stream_id : 1001);

    return 0;
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

void tp_context_set_driver_timeout_ns(tp_context_t *context, uint64_t value)
{
    if (NULL == context)
    {
        return;
    }

    context->driver_timeout_ns = value;
}

uint64_t tp_context_get_driver_timeout_ns(const tp_context_t *context)
{
    return NULL == context ? 0 : context->driver_timeout_ns;
}

void tp_context_set_keepalive_interval_ns(tp_context_t *context, uint64_t value)
{
    if (NULL == context)
    {
        return;
    }

    context->keepalive_interval_ns = value;
}

uint64_t tp_context_get_keepalive_interval_ns(const tp_context_t *context)
{
    return NULL == context ? 0 : context->keepalive_interval_ns;
}

void tp_context_set_lease_expiry_grace_intervals(tp_context_t *context, uint32_t value)
{
    if (NULL == context)
    {
        return;
    }

    context->lease_expiry_grace_intervals = value;
}

uint32_t tp_context_get_lease_expiry_grace_intervals(const tp_context_t *context)
{
    return NULL == context ? 0 : context->lease_expiry_grace_intervals;
}

void tp_context_set_idle_sleep_duration_ns(tp_context_t *context, uint64_t value)
{
    if (NULL == context)
    {
        return;
    }

    context->idle_sleep_duration_ns = value;
}

uint64_t tp_context_get_idle_sleep_duration_ns(const tp_context_t *context)
{
    return NULL == context ? 0 : context->idle_sleep_duration_ns;
}

void tp_context_set_idle_strategy(tp_context_t *context, uint64_t sleep_ns)
{
    tp_context_set_idle_sleep_duration_ns(context, sleep_ns);
}

void tp_context_set_announce_period_ns(tp_context_t *context, uint64_t period_ns)
{
    if (NULL == context)
    {
        return;
    }

    context->announce_period_ns = period_ns;
}

uint64_t tp_context_get_announce_period_ns(const tp_context_t *context)
{
    return NULL == context ? 0 : context->announce_period_ns;
}

void tp_context_set_use_agent_invoker(tp_context_t *context, bool value)
{
    if (NULL == context)
    {
        return;
    }

    context->use_agent_invoker = value;
}

void tp_context_set_use_conductor_agent_invoker(tp_context_t *context, bool value)
{
    tp_context_set_use_agent_invoker(context, value);
}

bool tp_context_get_use_agent_invoker(const tp_context_t *context)
{
    return NULL == context ? false : context->use_agent_invoker;
}

void tp_context_set_shm_permissions(
    tp_context_t *context,
    bool enforce,
    uint32_t expected_uid,
    uint32_t expected_gid,
    uint32_t forbidden_mode)
{
    if (NULL == context)
    {
        return;
    }

    context->allowed_paths.enforce_permissions = enforce ? 1 : 0;
    context->allowed_paths.expected_uid = expected_uid;
    context->allowed_paths.expected_gid = expected_gid;
    context->allowed_paths.forbidden_mode = forbidden_mode;
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

tp_log_t *tp_context_log(tp_context_t *context)
{
    return NULL == context ? NULL : &context->log;
}

const tp_allowed_paths_t *tp_context_allowed_paths(const tp_context_t *context)
{
    return NULL == context ? NULL : &context->allowed_paths;
}
