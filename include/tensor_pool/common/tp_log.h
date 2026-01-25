#ifndef TENSOR_POOL_TP_LOG_H
#define TENSOR_POOL_TP_LOG_H

#include <stdarg.h>

#include "tensor_pool/tp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tp_log_func_t)(tp_log_level_t level, const char *message, void *clientd);

typedef struct tp_log_stct
{
    tp_log_func_t handler;
    void *clientd;
    tp_log_level_t min_level;
}
tp_log_t;

void tp_log_init(tp_log_t *log);
void tp_log_set_handler(tp_log_t *log, tp_log_func_t handler, void *clientd);
void tp_log_set_level(tp_log_t *log, tp_log_level_t level);
void tp_log_emit(tp_log_t *log, tp_log_level_t level, const char *format, ...);
void tp_log_emit_v(tp_log_t *log, tp_log_level_t level, const char *format, va_list args);

#ifdef __cplusplus
}
#endif

#endif
