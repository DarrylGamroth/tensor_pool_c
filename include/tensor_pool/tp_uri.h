#ifndef TENSOR_POOL_TP_URI_H
#define TENSOR_POOL_TP_URI_H

#include <stdbool.h>
#include <stddef.h>

#include "tensor_pool/tp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_shm_uri_stct
{
    char path[4096];
    bool require_hugepages;
}
tp_shm_uri_t;

int tp_shm_uri_parse(tp_shm_uri_t *out, const char *uri, tp_log_t *log);

#ifdef __cplusplus
}
#endif

#endif
