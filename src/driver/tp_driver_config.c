#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_driver.h"

#include <errno.h>
#include <inttypes.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"

#include "tomlc17.h"

static char *tp_driver_strdup(const char *value)
{
    size_t len;
    char *copy;

    if (NULL == value)
    {
        return NULL;
    }

    len = strlen(value) + 1;
    copy = (char *)malloc(len);
    if (NULL == copy)
    {
        return NULL;
    }

    memcpy(copy, value, len);
    return copy;
}

static int tp_driver_copy_string(char *dst, size_t dst_len, toml_datum_t value, const char *name, bool required)
{
    size_t len;

    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_driver_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_STRING || NULL == value.u.str.ptr)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s must be a string", name);
        return -1;
    }

    len = (size_t)value.u.str.len;
    if (len >= dst_len)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s too long", name);
        return -1;
    }

    memcpy(dst, value.u.str.ptr, len);
    dst[len] = '\0';
    return 0;
}

static int tp_driver_copy_uint32(uint32_t *out, toml_datum_t value, const char *name, bool required)
{
    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_driver_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_INT64)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s must be an integer", name);
        return -1;
    }

    if (value.u.int64 < 0 || value.u.int64 > UINT32_MAX)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s out of range", name);
        return -1;
    }

    *out = (uint32_t)value.u.int64;
    return 0;
}

static int tp_driver_copy_uint64(uint64_t *out, toml_datum_t value, const char *name, bool required)
{
    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_driver_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_INT64)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s must be an integer", name);
        return -1;
    }

    if (value.u.int64 < 0)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s out of range", name);
        return -1;
    }

    *out = (uint64_t)value.u.int64;
    return 0;
}

static int tp_driver_copy_bool(bool *out, toml_datum_t value, const char *name, bool required)
{
    if (value.type == TOML_UNKNOWN)
    {
        if (required)
        {
            TP_SET_ERR(EINVAL, "tp_driver_config_load: missing %s", name);
            return -1;
        }
        return 0;
    }

    if (value.type != TOML_BOOLEAN)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s must be a boolean", name);
        return -1;
    }

    *out = value.u.boolean;
    return 0;
}

static int tp_driver_parse_permissions_mode(uint32_t *out, toml_datum_t value)
{
    char *end = NULL;
    unsigned long mode;

    if (value.type == TOML_UNKNOWN)
    {
        return 0;
    }

    if (value.type != TOML_STRING || NULL == value.u.str.ptr)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: permissions_mode must be a string");
        return -1;
    }

    mode = strtoul(value.u.str.ptr, &end, 8);
    if (end == value.u.str.ptr || *end != '\0')
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: permissions_mode invalid");
        return -1;
    }

    if (mode > 0777)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: permissions_mode out of range");
        return -1;
    }

    *out = (uint32_t)mode;
    return 0;
}

static int tp_driver_parse_range_string(const char *value, uint32_t *start, uint32_t *end)
{
    char *dash;
    char *endptr = NULL;
    unsigned long first;
    unsigned long last;

    if (NULL == value || value[0] == '\0')
    {
        return -1;
    }

    dash = strchr(value, '-');
    if (NULL == dash)
    {
        first = strtoul(value, &endptr, 10);
        if (endptr == value || *endptr != '\0')
        {
            return -1;
        }
        last = first;
    }
    else
    {
        char left[32];
        char right[32];
        size_t left_len = (size_t)(dash - value);
        size_t right_len = strlen(dash + 1);

        if (left_len == 0 || left_len >= sizeof(left) || right_len == 0 || right_len >= sizeof(right))
        {
            return -1;
        }

        memcpy(left, value, left_len);
        left[left_len] = '\0';
        memcpy(right, dash + 1, right_len + 1);

        first = strtoul(left, &endptr, 10);
        if (endptr == left || *endptr != '\0')
        {
            return -1;
        }
        endptr = NULL;
        last = strtoul(right, &endptr, 10);
        if (endptr == right || *endptr != '\0')
        {
            return -1;
        }
    }

    if (first == 0 || last == 0 || first > UINT32_MAX || last > UINT32_MAX || first > last)
    {
        return -1;
    }

    *start = (uint32_t)first;
    *end = (uint32_t)last;
    return 0;
}

static int tp_driver_id_ranges_add(tp_driver_id_ranges_t *ranges, uint32_t start, uint32_t end)
{
    tp_driver_id_range_t *next;
    size_t new_count;

    if (NULL == ranges)
    {
        return -1;
    }

    new_count = ranges->count + 1;
    next = (tp_driver_id_range_t *)realloc(ranges->ranges, new_count * sizeof(*next));
    if (NULL == next)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_driver_config_load: range allocation failed");
        return -1;
    }

    next[new_count - 1].start = start;
    next[new_count - 1].end = end;
    next[new_count - 1].next = start;
    ranges->ranges = next;
    ranges->count = new_count;
    return 0;
}

static int tp_driver_parse_ranges(tp_driver_id_ranges_t *ranges, toml_datum_t value, const char *name)
{
    if (NULL == ranges)
    {
        return -1;
    }

    if (value.type == TOML_UNKNOWN)
    {
        return 0;
    }

    if (value.type == TOML_STRING)
    {
        uint32_t start = 0;
        uint32_t end = 0;

        if (tp_driver_parse_range_string(value.u.str.ptr, &start, &end) < 0)
        {
            TP_SET_ERR(EINVAL, "tp_driver_config_load: invalid %s range", name);
            return -1;
        }

        return tp_driver_id_ranges_add(ranges, start, end);
    }

    if (value.type == TOML_ARRAY)
    {
        int32_t i;

        for (i = 0; i < value.u.arr.size; i++)
        {
            toml_datum_t elem = value.u.arr.elem[i];
            uint32_t start = 0;
            uint32_t end = 0;

            if (elem.type != TOML_STRING || NULL == elem.u.str.ptr)
            {
                TP_SET_ERR(EINVAL, "tp_driver_config_load: %s range must be string", name);
                return -1;
            }

            if (tp_driver_parse_range_string(elem.u.str.ptr, &start, &end) < 0)
            {
                TP_SET_ERR(EINVAL, "tp_driver_config_load: invalid %s range", name);
                return -1;
            }

            if (tp_driver_id_ranges_add(ranges, start, end) < 0)
            {
                return -1;
            }
        }

        return 0;
    }

    TP_SET_ERR(EINVAL, "tp_driver_config_load: %s range must be string or array", name);
    return -1;
}

static tp_driver_profile_t *tp_driver_find_profile(tp_driver_config_t *config, const char *name)
{
    size_t i;

    if (NULL == config || NULL == name)
    {
        return NULL;
    }

    for (i = 0; i < config->profile_count; i++)
    {
        if (0 == strcmp(config->profiles[i].name, name))
        {
            return &config->profiles[i];
        }
    }

    return NULL;
}

static int tp_driver_validate_pow2(uint32_t value, const char *name)
{
    if (value == 0 || (value & (value - 1)) != 0)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s must be power of two", name);
        return -1;
    }
    return 0;
}

static int tp_driver_load_allowed_base_dirs(tp_driver_config_t *config, toml_datum_t value)
{
    int32_t i;

    if (value.type == TOML_UNKNOWN)
    {
        return 0;
    }

    if (value.type != TOML_ARRAY)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: allowed_base_dirs must be array");
        return -1;
    }

    config->allowed_base_dirs = (char **)calloc((size_t)value.u.arr.size, sizeof(char *));
    if (NULL == config->allowed_base_dirs)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_driver_config_load: allowed_base_dirs allocation failed");
        return -1;
    }

    config->allowed_base_dirs_len = (size_t)value.u.arr.size;
    for (i = 0; i < value.u.arr.size; i++)
    {
        toml_datum_t elem = value.u.arr.elem[i];
        if (elem.type != TOML_STRING || NULL == elem.u.str.ptr)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: allowed_base_dirs must be strings");
            return -1;
        }

        config->allowed_base_dirs[i] = tp_driver_strdup(elem.u.str.ptr);
        if (NULL == config->allowed_base_dirs[i])
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_driver_config_load: allowed_base_dirs allocation failed");
            return -1;
        }
    }

    return 0;
}

static int tp_driver_load_profiles(tp_driver_config_t *config, toml_datum_t profiles)
{
    int32_t i;
    size_t count = 0;

    if (profiles.type == TOML_UNKNOWN)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: profiles missing");
        return -1;
    }

    if (profiles.type != TOML_TABLE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: profiles must be table");
        return -1;
    }

    if (profiles.u.tab.size == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: profiles empty");
        return -1;
    }

    config->profiles = (tp_driver_profile_t *)calloc((size_t)profiles.u.tab.size, sizeof(*config->profiles));
    if (NULL == config->profiles)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_driver_config_load: profiles allocation failed");
        return -1;
    }

    for (i = 0; i < profiles.u.tab.size; i++)
    {
        const char *profile_name = profiles.u.tab.key[i];
        toml_datum_t profile = toml_get(profiles, profile_name);
        toml_datum_t pools;
        tp_driver_profile_t *dest;
        int32_t p;

        if (profile.type != TOML_TABLE)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: profile entry must be table");
            return -1;
        }

        dest = &config->profiles[count];
        strncpy(dest->name, profile_name, sizeof(dest->name) - 1);

        if (tp_driver_copy_uint32(&dest->header_nslots, toml_get(profile, "header_nslots"),
                "profiles.<name>.header_nslots", true) < 0)
        {
            return -1;
        }
        if (tp_driver_validate_pow2(dest->header_nslots, "profiles.<name>.header_nslots") < 0)
        {
            return -1;
        }

        pools = toml_get(profile, "payload_pools");
        if (pools.type != TOML_ARRAY || pools.u.arr.size <= 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: payload_pools must be array");
            return -1;
        }

        dest->pools = (tp_driver_pool_def_t *)calloc((size_t)pools.u.arr.size, sizeof(*dest->pools));
        if (NULL == dest->pools)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_driver_config_load: payload_pools allocation failed");
            return -1;
        }
        dest->pool_count = (size_t)pools.u.arr.size;

        for (p = 0; p < pools.u.arr.size; p++)
        {
            toml_datum_t pool = pools.u.arr.elem[p];
            tp_driver_pool_def_t *pool_def = &dest->pools[p];
            uint32_t stride_bytes = 0;
            uint32_t pool_id = 0;
            int32_t q;

            if (pool.type != TOML_TABLE)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: payload_pools entries must be tables");
                return -1;
            }

            if (tp_driver_copy_uint32(&pool_id, toml_get(pool, "pool_id"),
                    "payload_pools.pool_id", true) < 0)
            {
                return -1;
            }
            if (pool_id == 0 || pool_id > UINT16_MAX)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: pool_id must be 1..65535");
                return -1;
            }

            if (tp_driver_copy_uint32(&stride_bytes, toml_get(pool, "stride_bytes"),
                    "payload_pools.stride_bytes", true) < 0)
            {
                return -1;
            }
            if (stride_bytes == 0 || (stride_bytes % 64u) != 0)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: stride_bytes must be multiple of 64");
                return -1;
            }

            for (q = 0; q < p; q++)
            {
                if (dest->pools[q].pool_id == (uint16_t)pool_id)
                {
                    TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: duplicate pool_id");
                    return -1;
                }
            }

            pool_def->pool_id = (uint16_t)pool_id;
            pool_def->stride_bytes = stride_bytes;
        }

        count++;
    }

    config->profile_count = count;
    return 0;
}

static int tp_driver_load_streams(tp_driver_config_t *config, toml_datum_t streams)
{
    int32_t i;
    size_t count = 0;

    if (streams.type == TOML_UNKNOWN)
    {
        return 0;
    }

    if (streams.type != TOML_TABLE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: streams must be table");
        return -1;
    }

    if (streams.u.tab.size == 0)
    {
        return 0;
    }

    config->streams = (tp_driver_stream_def_t *)calloc((size_t)streams.u.tab.size, sizeof(*config->streams));
    if (NULL == config->streams)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_driver_config_load: streams allocation failed");
        return -1;
    }

    for (i = 0; i < streams.u.tab.size; i++)
    {
        const char *stream_name = streams.u.tab.key[i];
        toml_datum_t stream = toml_get(streams, stream_name);
        tp_driver_stream_def_t *dest = &config->streams[count];
        uint32_t stream_id = 0;
        char profile_name[128] = {0};
        size_t j;

        if (stream.type != TOML_TABLE)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: stream entry must be table");
            return -1;
        }

        strncpy(dest->name, stream_name, sizeof(dest->name) - 1);

        if (tp_driver_copy_uint32(&stream_id, toml_get(stream, "stream_id"),
                "streams.<name>.stream_id", true) < 0)
        {
            return -1;
        }
        if (stream_id == 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: stream_id must be non-zero");
            return -1;
        }

        if (tp_driver_copy_string(profile_name, sizeof(profile_name), toml_get(stream, "profile"),
                "streams.<name>.profile", true) < 0)
        {
            return -1;
        }

        dest->profile = tp_driver_find_profile(config, profile_name);
        if (NULL == dest->profile)
        {
            TP_SET_ERR(EINVAL, "tp_driver_config_load: unknown profile %s", profile_name);
            return -1;
        }

        for (j = 0; j < count; j++)
        {
            if (config->streams[j].stream_id == stream_id)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: duplicate stream_id");
                return -1;
            }
        }

        dest->stream_id = stream_id;
        count++;
    }

    config->stream_count = count;
    return 0;
}

int tp_driver_config_init(tp_driver_config_t *config)
{
    if (NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_init: null config");
        return -1;
    }

    memset(config, 0, sizeof(*config));
    tp_context_init(&config->base);

    strncpy(config->shm_base_dir, "/dev/shm", sizeof(config->shm_base_dir) - 1);
    strncpy(config->shm_namespace, "default", sizeof(config->shm_namespace) - 1);
    config->require_hugepages = false;
    config->page_size_bytes = 4096;
    config->permissions_mode = 0660;
    config->allow_dynamic_streams = false;
    config->announce_period_ms = 1000;
    config->lease_keepalive_interval_ms = 1000;
    config->lease_expiry_grace_intervals = 3;
    config->prefault_shm = true;
    config->mlock_shm = false;
    config->epoch_gc_enabled = true;
    config->epoch_gc_keep = 2;
    config->epoch_gc_on_startup = false;
    config->epoch_gc_min_age_ns = 3ULL * 1000ULL * 1000ULL * 1000ULL;

    return 0;
}

void tp_driver_config_close(tp_driver_config_t *config)
{
    size_t i;

    if (NULL == config)
    {
        return;
    }

    for (i = 0; i < config->profile_count; i++)
    {
        free(config->profiles[i].pools);
    }
    free(config->profiles);
    config->profiles = NULL;
    config->profile_count = 0;

    free(config->streams);
    config->streams = NULL;
    config->stream_count = 0;

    for (i = 0; i < config->allowed_base_dirs_len; i++)
    {
        free(config->allowed_base_dirs[i]);
    }
    free(config->allowed_base_dirs);
    config->allowed_base_dirs = NULL;
    config->allowed_base_dirs_len = 0;

    free(config->stream_id_ranges.ranges);
    config->stream_id_ranges.ranges = NULL;
    config->stream_id_ranges.count = 0;

    free(config->descriptor_stream_id_ranges.ranges);
    config->descriptor_stream_id_ranges.ranges = NULL;
    config->descriptor_stream_id_ranges.count = 0;

    free(config->control_stream_id_ranges.ranges);
    config->control_stream_id_ranges.ranges = NULL;
    config->control_stream_id_ranges.count = 0;

    tp_context_clear_allowed_paths(&config->base);
}

int tp_driver_config_load(tp_driver_config_t *config, const char *path)
{
    toml_result_t parsed;
    toml_datum_t driver;
    toml_datum_t shm;
    toml_datum_t policies;
    toml_datum_t profiles;
    toml_datum_t streams;
    uint32_t announce_period_ms = 0;
    bool allow_dynamic_streams = false;

    if (NULL == config || NULL == path)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: null input");
        return -1;
    }

    tp_driver_config_close(config);
    if (tp_driver_config_init(config) < 0)
    {
        return -1;
    }

    parsed = toml_parse_file_ex(path);
    if (!parsed.ok)
    {
        TP_SET_ERR(EINVAL, "tp_driver_config_load: %s", parsed.errmsg);
        return -1;
    }

    driver = toml_get(parsed.toptab, "driver");
    shm = toml_get(parsed.toptab, "shm");
    policies = toml_get(parsed.toptab, "policies");
    profiles = toml_get(parsed.toptab, "profiles");
    streams = toml_get(parsed.toptab, "streams");

    if (driver.type != TOML_TABLE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: missing [driver] table");
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_string(config->instance_id, sizeof(config->instance_id),
            toml_get(driver, "instance_id"), "driver.instance_id", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    {
        char aeron_dir[4096] = {0};
        if (tp_driver_copy_string(aeron_dir, sizeof(aeron_dir),
                toml_get(driver, "aeron_dir"), "driver.aeron_dir", false) < 0)
        {
            toml_free(parsed);
            return -1;
        }
        if (aeron_dir[0] != '\0')
        {
            tp_context_set_aeron_dir(&config->base, aeron_dir);
        }
    }

    {
        char channel[4096] = {0};
        uint32_t stream_id = 0;

        if (tp_driver_copy_string(channel, sizeof(channel),
                toml_get(driver, "control_channel"), "driver.control_channel", true) < 0)
        {
            toml_free(parsed);
            return -1;
        }
        if (tp_driver_copy_uint32(&stream_id, toml_get(driver, "control_stream_id"),
                "driver.control_stream_id", true) < 0)
        {
            toml_free(parsed);
            return -1;
        }
        tp_context_set_control_channel(&config->base, channel, (int32_t)stream_id);
    }

    {
        char channel[4096] = {0};
        uint32_t stream_id = 0;

        if (tp_driver_copy_string(channel, sizeof(channel),
                toml_get(driver, "announce_channel"), "driver.announce_channel", true) < 0)
        {
            toml_free(parsed);
            return -1;
        }
        if (tp_driver_copy_uint32(&stream_id, toml_get(driver, "announce_stream_id"),
                "driver.announce_stream_id", true) < 0)
        {
            toml_free(parsed);
            return -1;
        }
        tp_context_set_announce_channel(&config->base, channel, (int32_t)stream_id);
    }

    {
        char channel[4096] = {0};
        uint32_t stream_id = 0;

        if (tp_driver_copy_string(channel, sizeof(channel),
                toml_get(driver, "qos_channel"), "driver.qos_channel", true) < 0)
        {
            toml_free(parsed);
            return -1;
        }
        if (tp_driver_copy_uint32(&stream_id, toml_get(driver, "qos_stream_id"),
                "driver.qos_stream_id", true) < 0)
        {
            toml_free(parsed);
            return -1;
        }
        tp_context_set_qos_channel(&config->base, channel, (int32_t)stream_id);
    }

    if (tp_driver_parse_ranges(&config->stream_id_ranges,
            toml_get(driver, "stream_id_range"), "driver.stream_id_range") < 0)
    {
        toml_free(parsed);
        return -1;
    }
    if (tp_driver_parse_ranges(&config->descriptor_stream_id_ranges,
            toml_get(driver, "descriptor_stream_id_range"), "driver.descriptor_stream_id_range") < 0)
    {
        toml_free(parsed);
        return -1;
    }
    if (tp_driver_parse_ranges(&config->control_stream_id_ranges,
            toml_get(driver, "control_stream_id_range"), "driver.control_stream_id_range") < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (shm.type != TOML_TABLE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: missing [shm] table");
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_string(config->shm_base_dir, sizeof(config->shm_base_dir),
            toml_get(shm, "base_dir"), "shm.base_dir", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_string(config->shm_namespace, sizeof(config->shm_namespace),
            toml_get(shm, "namespace"), "shm.namespace", true) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_bool(&config->require_hugepages, toml_get(shm, "require_hugepages"),
            "shm.require_hugepages", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_uint32(&config->page_size_bytes, toml_get(shm, "page_size_bytes"),
            "shm.page_size_bytes", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_parse_permissions_mode(&config->permissions_mode,
            toml_get(shm, "permissions_mode")) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_load_allowed_base_dirs(config, toml_get(shm, "allowed_base_dirs")) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (policies.type != TOML_TABLE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: missing [policies] table");
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_bool(&config->allow_dynamic_streams, toml_get(policies, "allow_dynamic_streams"),
            "policies.allow_dynamic_streams", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_string(config->default_profile, sizeof(config->default_profile),
            toml_get(policies, "default_profile"), "policies.default_profile", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_uint32(&config->announce_period_ms, toml_get(policies, "announce_period_ms"),
            "policies.announce_period_ms", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_uint32(&config->lease_keepalive_interval_ms,
            toml_get(policies, "lease_keepalive_interval_ms"),
            "policies.lease_keepalive_interval_ms", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_uint32(&config->lease_expiry_grace_intervals,
            toml_get(policies, "lease_expiry_grace_intervals"),
            "policies.lease_expiry_grace_intervals", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_bool(&config->prefault_shm, toml_get(policies, "prefault_shm"),
            "policies.prefault_shm", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_bool(&config->mlock_shm, toml_get(policies, "mlock_shm"),
            "policies.mlock_shm", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_bool(&config->epoch_gc_enabled, toml_get(policies, "epoch_gc_enabled"),
            "policies.epoch_gc_enabled", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_uint32(&config->epoch_gc_keep, toml_get(policies, "epoch_gc_keep"),
            "policies.epoch_gc_keep", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_uint64(&config->epoch_gc_min_age_ns, toml_get(policies, "epoch_gc_min_age_ns"),
            "policies.epoch_gc_min_age_ns", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_copy_bool(&config->epoch_gc_on_startup, toml_get(policies, "epoch_gc_on_startup"),
            "policies.epoch_gc_on_startup", false) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_load_profiles(config, profiles) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (tp_driver_load_streams(config, streams) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    if (config->allow_dynamic_streams)
    {
        if (config->default_profile[0] == '\0')
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: default_profile required for dynamic streams");
            toml_free(parsed);
            return -1;
        }
        if (NULL == tp_driver_find_profile(config, config->default_profile))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: default_profile not found");
            toml_free(parsed);
            return -1;
        }
        if (config->stream_id_ranges.count == 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_config_load: stream_id_range required for dynamic streams");
            toml_free(parsed);
            return -1;
        }
    }

    announce_period_ms = config->announce_period_ms;
    allow_dynamic_streams = config->allow_dynamic_streams;
    (void)allow_dynamic_streams;

    if (announce_period_ms > 0)
    {
        tp_context_set_announce_period_ns(&config->base, (uint64_t)announce_period_ms * 1000000ULL);
        if (config->epoch_gc_min_age_ns == 0)
        {
            config->epoch_gc_min_age_ns = 3ULL * (uint64_t)announce_period_ms * 1000000ULL;
        }
    }

    if (config->allowed_base_dirs_len == 0)
    {
        config->allowed_base_dirs = (char **)calloc(1, sizeof(char *));
        if (NULL == config->allowed_base_dirs)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_driver_config_load: allowed_base_dirs allocation failed");
            toml_free(parsed);
            return -1;
        }
        config->allowed_base_dirs[0] = tp_driver_strdup(config->shm_base_dir);
        if (NULL == config->allowed_base_dirs[0])
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_driver_config_load: allowed_base_dirs allocation failed");
            toml_free(parsed);
            return -1;
        }
        config->allowed_base_dirs_len = 1;
    }

    tp_context_set_allowed_paths(&config->base, (const char **)config->allowed_base_dirs,
        config->allowed_base_dirs_len);
    if (tp_context_finalize_allowed_paths(&config->base) < 0)
    {
        toml_free(parsed);
        return -1;
    }

    toml_free(parsed);
    return 0;
}
