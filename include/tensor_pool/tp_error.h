#ifndef TENSOR_POOL_TP_ERROR_H
#define TENSOR_POOL_TP_ERROR_H

#include "util/aeron_error.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int tp_errcode(void)
{
    return aeron_errcode();
}

static inline const char *tp_errmsg(void)
{
    return aeron_errmsg();
}

#define TP_SET_ERR(errcode, fmt, ...) AERON_SET_ERR((errcode), fmt, __VA_ARGS__)
#define TP_APPEND_ERR(fmt, ...) AERON_APPEND_ERR(fmt, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
