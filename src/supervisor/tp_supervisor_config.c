#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_supervisor.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"
#include "tensor_pool/internal/tp_context.h"

#include "tomlc17.h"

static int tp_supervisor_copy_string(
    char *dst,
    size_t dst_len,
    toml_datum_t value,
    const char *name,
    bool required)
{
    size_t len;

    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_supervisor_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_STRING || NULL == value.u.str.ptr)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s must be a string", name);
        return -1;
    }

    len = (size_t)value.u.str.len;
    if (len >= dst_len)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s too long", name);
        return -1;
    }

    memcpy(dst, value.u.str.ptr, len);
    dst[len] = '\0';
    return 0;
}

static int tp_supervisor_copy_uint32(uint32_t *out, toml_datum_t value, const char *name, bool required)
{
    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_supervisor_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_INT64)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s must be an integer", name);
        return -1;
    }

    if (value.u.int64 < 0 || value.u.int64 > UINT32_MAX)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s out of range", name);
        return -1;
    }

    *out = (uint32_t)value.u.int64;
    return 0;
}

static int tp_supervisor_copy_int32(int32_t *out, toml_datum_t value, const char *name, bool required)
{
    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_supervisor_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_INT64)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s must be an integer", name);
        return -1;
    }

    if (value.u.int64 < INT32_MIN || value.u.int64 > INT32_MAX)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s out of range", name);
        return -1;
    }

    *out = (int32_t)value.u.int64;
    return 0;
}

static int tp_supervisor_copy_bool(bool *out, toml_datum_t value, const char *name, bool required)
{
    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_supervisor_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_BOOLEAN)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s must be a boolean", name);
        return -1;
    }

    *out = value.u.boolean;
    return 0;
}

static int tp_supervisor_copy_uint8(uint8_t *out, toml_datum_t value, const char *name)
{
    uint32_t temp = 0;

    if (value.type == TOML_UNKNOWN)
    {
        return 0;
    }

    if (tp_supervisor_copy_uint32(&temp, value, name, false) < 0)
    {
        return -1;
    }

    if (temp > UINT8_MAX)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s out of range", name);
        return -1;
    }

    *out = (uint8_t)temp;
    return 0;
}

int tp_supervisor_config_init(tp_supervisor_config_t *config)
{
    if (NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_config_init: null config");
        return -1;
    }

    memset(config, 0, sizeof(*config));
    if (tp_context_init(&config->base) < 0)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_supervisor_config_init: context alloc failed");
        return -1;
    }
    strncpy(config->control_channel, "aeron:ipc", sizeof(config->control_channel) - 1);
    strncpy(config->announce_channel, "aeron:ipc", sizeof(config->announce_channel) - 1);
    strncpy(config->metadata_channel, "aeron:ipc", sizeof(config->metadata_channel) - 1);
    strncpy(config->qos_channel, "aeron:ipc", sizeof(config->qos_channel) - 1);
    config->control_stream_id = 1000;
    config->announce_stream_id = 1001;
    config->metadata_stream_id = 1300;
    config->qos_stream_id = 1200;
    config->consumer_capacity = 256;
    config->consumer_stale_ms = 5000;
    config->per_consumer_enabled = false;
    config->per_consumer_descriptor_base = 0;
    config->per_consumer_descriptor_range = 0;
    config->per_consumer_control_base = 0;
    config->per_consumer_control_range = 0;
    config->force_no_shm = false;
    config->force_mode = 0;
    config->payload_fallback_uri[0] = '\0';
    return 0;
}

int tp_supervisor_config_load(tp_supervisor_config_t *config, const char *path)
{
    toml_result_t parsed;
    toml_datum_t supervisor;

    if (NULL == config || NULL == path)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_config_load: null input");
        return -1;
    }

    tp_supervisor_config_close(config);
    if (tp_supervisor_config_init(config) < 0)
    {
        return -1;
    }

    parsed = toml_parse_file_ex(path);
    if (!parsed.ok)
    {
        TP_SET_ERR(EINVAL, "tp_supervisor_config_load: %s", parsed.errmsg);
        return -1;
    }

    supervisor = toml_get(parsed.toptab, "supervisor");
    if (supervisor.type != TOML_TABLE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_config_load: missing [supervisor] table");
        toml_free(parsed);
        return -1;
    }

    if (tp_supervisor_copy_string(
            config->control_channel,
            sizeof(config->control_channel),
            toml_get(supervisor, "control_channel"),
            "supervisor.control_channel",
            false) < 0 ||
        tp_supervisor_copy_string(
            config->announce_channel,
            sizeof(config->announce_channel),
            toml_get(supervisor, "announce_channel"),
            "supervisor.announce_channel",
            false) < 0 ||
        tp_supervisor_copy_string(
            config->metadata_channel,
            sizeof(config->metadata_channel),
            toml_get(supervisor, "metadata_channel"),
            "supervisor.metadata_channel",
            false) < 0 ||
        tp_supervisor_copy_string(
            config->qos_channel,
            sizeof(config->qos_channel),
            toml_get(supervisor, "qos_channel"),
            "supervisor.qos_channel",
            false) < 0 ||
        tp_supervisor_copy_int32(
            &config->control_stream_id,
            toml_get(supervisor, "control_stream_id"),
            "supervisor.control_stream_id",
            false) < 0 ||
        tp_supervisor_copy_int32(
            &config->announce_stream_id,
            toml_get(supervisor, "announce_stream_id"),
            "supervisor.announce_stream_id",
            false) < 0 ||
        tp_supervisor_copy_int32(
            &config->metadata_stream_id,
            toml_get(supervisor, "metadata_stream_id"),
            "supervisor.metadata_stream_id",
            false) < 0 ||
        tp_supervisor_copy_int32(
            &config->qos_stream_id,
            toml_get(supervisor, "qos_stream_id"),
            "supervisor.qos_stream_id",
            false) < 0 ||
        tp_supervisor_copy_uint32(
            &config->consumer_capacity,
            toml_get(supervisor, "consumer_capacity"),
            "supervisor.consumer_capacity",
            false) < 0 ||
        tp_supervisor_copy_uint32(
            &config->consumer_stale_ms,
            toml_get(supervisor, "consumer_stale_ms"),
            "supervisor.consumer_stale_ms",
            false) < 0 ||
        tp_supervisor_copy_bool(
            &config->per_consumer_enabled,
            toml_get(supervisor, "per_consumer_enabled"),
            "supervisor.per_consumer_enabled",
            false) < 0 ||
        tp_supervisor_copy_string(
            config->per_consumer_descriptor_channel,
            sizeof(config->per_consumer_descriptor_channel),
            toml_get(supervisor, "per_consumer_descriptor_channel"),
            "supervisor.per_consumer_descriptor_channel",
            false) < 0 ||
        tp_supervisor_copy_string(
            config->per_consumer_control_channel,
            sizeof(config->per_consumer_control_channel),
            toml_get(supervisor, "per_consumer_control_channel"),
            "supervisor.per_consumer_control_channel",
            false) < 0 ||
        tp_supervisor_copy_int32(
            &config->per_consumer_descriptor_base,
            toml_get(supervisor, "per_consumer_descriptor_base"),
            "supervisor.per_consumer_descriptor_base",
            false) < 0 ||
        tp_supervisor_copy_int32(
            &config->per_consumer_control_base,
            toml_get(supervisor, "per_consumer_control_base"),
            "supervisor.per_consumer_control_base",
            false) < 0 ||
        tp_supervisor_copy_uint32(
            &config->per_consumer_descriptor_range,
            toml_get(supervisor, "per_consumer_descriptor_range"),
            "supervisor.per_consumer_descriptor_range",
            false) < 0 ||
        tp_supervisor_copy_uint32(
            &config->per_consumer_control_range,
            toml_get(supervisor, "per_consumer_control_range"),
            "supervisor.per_consumer_control_range",
            false) < 0 ||
        tp_supervisor_copy_bool(
            &config->force_no_shm,
            toml_get(supervisor, "force_no_shm"),
            "supervisor.force_no_shm",
            false) < 0 ||
        tp_supervisor_copy_uint8(
            &config->force_mode,
            toml_get(supervisor, "force_mode"),
            "supervisor.force_mode") < 0 ||
        tp_supervisor_copy_string(
            config->payload_fallback_uri,
            sizeof(config->payload_fallback_uri),
            toml_get(supervisor, "payload_fallback_uri"),
            "supervisor.payload_fallback_uri",
            false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (config->per_consumer_descriptor_base != 0 && config->per_consumer_descriptor_range == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_config_load: per_consumer_descriptor_range required");
        toml_free(parsed);
        return -1;
    }
    if (config->per_consumer_control_base != 0 && config->per_consumer_control_range == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_config_load: per_consumer_control_range required");
        toml_free(parsed);
        return -1;
    }
    if (config->force_mode != 0 && config->force_mode != TP_MODE_STREAM && config->force_mode != TP_MODE_RATE_LIMITED)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_supervisor_config_load: force_mode invalid");
        toml_free(parsed);
        return -1;
    }

    toml_free(parsed);
    return 0;
}

void tp_supervisor_config_close(tp_supervisor_config_t *config)
{
    if (NULL == config)
    {
        return;
    }

    if (NULL != config->base)
    {
        tp_context_close(config->base);
        config->base = NULL;
    }
    memset(config, 0, sizeof(*config));
}
