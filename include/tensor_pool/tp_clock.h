#ifndef TENSOR_POOL_TP_CLOCK_H
#define TENSOR_POOL_TP_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t tp_clock_now_ns(void);
int64_t tp_clock_now_realtime_ns(void);

#ifdef __cplusplus
}
#endif

#endif
