#include "tensor_pool/tp_log.h"

#include <stdio.h>
#include <string.h>

static const char *tp_log_level_name(tp_log_level_t level)
{
    switch (level)
    {
        case TP_LOG_ERROR:
            return "ERROR";
        case TP_LOG_WARN:
            return "WARN";
        case TP_LOG_INFO:
            return "INFO";
        case TP_LOG_DEBUG:
            return "DEBUG";
        case TP_LOG_TRACE:
            return "TRACE";
        default:
            return "UNKNOWN";
    }
}

static void tp_log_default_handler(tp_log_level_t level, const char *message, void *clientd)
{
    (void)clientd;
    fprintf(stderr, "[tp][%s] %s\n", tp_log_level_name(level), message);
}

void tp_log_init(tp_log_t *log)
{
    if (NULL == log)
    {
        return;
    }

    log->handler = tp_log_default_handler;
    log->clientd = NULL;
    log->min_level = TP_LOG_INFO;
}

void tp_log_set_handler(tp_log_t *log, tp_log_func_t handler, void *clientd)
{
    if (NULL == log)
    {
        return;
    }

    log->handler = handler;
    log->clientd = clientd;
}

void tp_log_set_level(tp_log_t *log, tp_log_level_t level)
{
    if (NULL == log)
    {
        return;
    }

    log->min_level = level;
}

void tp_log_emit(tp_log_t *log, tp_log_level_t level, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    tp_log_emit_v(log, level, format, args);
    va_end(args);
}

void tp_log_emit_v(tp_log_t *log, tp_log_level_t level, const char *format, va_list args)
{
    char buffer[1024];
    int written;

    if (NULL == log || NULL == log->handler)
    {
        return;
    }

    if (level > log->min_level)
    {
        return;
    }

    written = vsnprintf(buffer, sizeof(buffer), format, args);
    if (written < 0)
    {
        return;
    }

    if ((size_t)written >= sizeof(buffer))
    {
        buffer[sizeof(buffer) - 1] = '\0';
    }

    log->handler(level, buffer, log->clientd);
}
