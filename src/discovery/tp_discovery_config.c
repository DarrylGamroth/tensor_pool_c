#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_discovery_service.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"
#include "tensor_pool/internal/tp_context.h"

#include "tomlc17.h"

static int tp_discovery_copy_string(char *dst, size_t dst_len, toml_datum_t value, const char *name, bool required)
{
    size_t len;

    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_discovery_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_STRING || NULL == value.u.str.ptr)
    {
        TP_SET_ERR(EINVAL, "tp_discovery_config_load: %s must be a string", name);
        return -1;
    }

    len = (size_t)value.u.str.len;
    if (len >= dst_len)
    {
        TP_SET_ERR(EINVAL, "tp_discovery_config_load: %s too long", name);
        return -1;
    }

    memcpy(dst, value.u.str.ptr, len);
    dst[len] = '\0';
    return 0;
}

static int tp_discovery_copy_uint32(uint32_t *out, toml_datum_t value, const char *name, bool required)
{
    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_discovery_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_INT64)
    {
        TP_SET_ERR(EINVAL, "tp_discovery_config_load: %s must be an integer", name);
        return -1;
    }

    if (value.u.int64 < 0 || value.u.int64 > UINT32_MAX)
    {
        TP_SET_ERR(EINVAL, "tp_discovery_config_load: %s out of range", name);
        return -1;
    }

    *out = (uint32_t)value.u.int64;
    return 0;
}

int tp_discovery_service_config_init(tp_discovery_service_config_t *config)
{
    if (NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_config_init: null config");
        return -1;
    }

    memset(config, 0, sizeof(*config));
    if (tp_context_init(&config->base) < 0)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_discovery_service_config_init: context alloc failed");
        return -1;
    }
    config->request_stream_id = -1;
    config->announce_stream_id = -1;
    config->metadata_stream_id = -1;
    config->driver_control_stream_id = -1;
    config->announce_period_ms = 1000;
    config->max_results = 1000;
    return 0;
}

void tp_discovery_service_config_close(tp_discovery_service_config_t *config)
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
}

int tp_discovery_service_config_load(tp_discovery_service_config_t *config, const char *path)
{
    toml_result_t parsed;
    toml_datum_t discovery;
    toml_datum_t driver;

    if (NULL == config || NULL == path)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_config_load: null input");
        return -1;
    }

    tp_discovery_service_config_close(config);
    if (tp_discovery_service_config_init(config) < 0)
    {
        return -1;
    }

    parsed = toml_parse_file_ex(path);
    if (!parsed.ok)
    {
        TP_SET_ERR(EINVAL, "tp_discovery_service_config_load: %s", parsed.errmsg);
        return -1;
    }

    discovery = toml_get(parsed.toptab, "discovery");
    driver = toml_get(parsed.toptab, "driver");

    if (discovery.type != TOML_TABLE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_config_load: missing [discovery] table");
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_string(config->request_channel, sizeof(config->request_channel),
            toml_get(discovery, "request_channel"), "discovery.request_channel", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_uint32((uint32_t *)&config->request_stream_id,
            toml_get(discovery, "request_stream_id"), "discovery.request_stream_id", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_string(config->announce_channel, sizeof(config->announce_channel),
            toml_get(discovery, "announce_channel"), "discovery.announce_channel", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_uint32((uint32_t *)&config->announce_stream_id,
            toml_get(discovery, "announce_stream_id"), "discovery.announce_stream_id", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_string(config->metadata_channel, sizeof(config->metadata_channel),
            toml_get(discovery, "metadata_channel"), "discovery.metadata_channel", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_uint32((uint32_t *)&config->metadata_stream_id,
            toml_get(discovery, "metadata_stream_id"), "discovery.metadata_stream_id", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_uint32(&config->announce_period_ms,
            toml_get(discovery, "announce_period_ms"), "discovery.announce_period_ms", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_uint32(&config->max_results,
            toml_get(discovery, "max_results"), "discovery.max_results", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (driver.type == TOML_TABLE)
    {
        char aeron_dir[4096] = {0};

        if (tp_discovery_copy_string(aeron_dir, sizeof(aeron_dir),
                toml_get(driver, "aeron_dir"), "driver.aeron_dir", false) < 0)
        {
            toml_free(parsed);
            return -1;
        }
        if (aeron_dir[0] != '\0')
        {
            tp_context_set_aeron_dir(config->base, aeron_dir);
        }
    }

    if (tp_discovery_copy_string(config->driver_instance_id, sizeof(config->driver_instance_id),
            toml_get(discovery, "driver_instance_id"), "discovery.driver_instance_id", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_string(config->driver_control_channel, sizeof(config->driver_control_channel),
            toml_get(discovery, "driver_control_channel"), "discovery.driver_control_channel", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_discovery_copy_uint32((uint32_t *)&config->driver_control_stream_id,
            toml_get(discovery, "driver_control_stream_id"), "discovery.driver_control_stream_id", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    toml_free(parsed);
    return 0;
}
