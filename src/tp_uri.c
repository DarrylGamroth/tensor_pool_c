#include "tensor_pool/tp_uri.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

static int tp_uri_parse_bool(const char *value, bool *out)
{
    if (0 == strcmp(value, "true"))
    {
        *out = true;
        return 0;
    }

    if (0 == strcmp(value, "false"))
    {
        *out = false;
        return 0;
    }

    return -1;
}

int tp_shm_uri_parse(tp_shm_uri_t *out, const char *uri, tp_log_t *log)
{
    const char *prefix = "shm:file?";
    const char *cursor;
    bool have_path = false;

    if (NULL == out || NULL == uri)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_uri_parse: null input");
        return -1;
    }

    if (0 != strncmp(uri, prefix, strlen(prefix)))
    {
        TP_SET_ERR(EINVAL, "tp_shm_uri_parse: unsupported scheme: %s", uri);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->require_hugepages = false;

    cursor = uri + strlen(prefix);
    while (*cursor != '\0')
    {
        const char *sep = strchr(cursor, '|');
        size_t len = (NULL == sep) ? strlen(cursor) : (size_t)(sep - cursor);
        const char *eq = memchr(cursor, '=', len);
        size_t key_len;
        size_t value_len;

        if (NULL == eq)
        {
            TP_SET_ERR(EINVAL, "tp_shm_uri_parse: invalid parameter: %s", cursor);
            return -1;
        }

        key_len = (size_t)(eq - cursor);
        value_len = len - key_len - 1;

        if (0 == key_len || 0 == value_len)
        {
            TP_SET_ERR(EINVAL, "tp_shm_uri_parse: empty key or value: %s", cursor);
            return -1;
        }

        if (0 == strncmp(cursor, "path", key_len))
        {
            if (have_path)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_shm_uri_parse: duplicate path parameter");
                return -1;
            }

            if (value_len >= sizeof(out->path))
            {
                TP_SET_ERR(EINVAL, "%s", "tp_shm_uri_parse: path too long");
                return -1;
            }

            memcpy(out->path, eq + 1, value_len);
            out->path[value_len] = '\0';
            have_path = true;
        }
        else if (0 == strncmp(cursor, "require_hugepages", key_len))
        {
            char value[16];

            if (value_len >= sizeof(value))
            {
                TP_SET_ERR(EINVAL, "%s", "tp_shm_uri_parse: require_hugepages too long");
                return -1;
            }

            memcpy(value, eq + 1, value_len);
            value[value_len] = '\0';

            if (tp_uri_parse_bool(value, &out->require_hugepages) < 0)
            {
                TP_SET_ERR(EINVAL, "tp_shm_uri_parse: invalid require_hugepages: %s", value);
                return -1;
            }
        }
        else
        {
            TP_SET_ERR(EINVAL, "tp_shm_uri_parse: unknown parameter: %.*s", (int)key_len, cursor);
            return -1;
        }

        if (NULL == sep)
        {
            break;
        }

        cursor = sep + 1;
    }

    if (!have_path)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_uri_parse: missing path parameter");
        return -1;
    }

    if (out->path[0] != '/')
    {
        TP_SET_ERR(EINVAL, "tp_shm_uri_parse: path must be absolute: %s", out->path);
        return -1;
    }

    if (NULL != log)
    {
        tp_log_emit(log, TP_LOG_DEBUG, "Parsed shm uri path=%s require_hugepages=%s", out->path,
            out->require_hugepages ? "true" : "false");
    }

    return 0;
}
