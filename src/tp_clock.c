#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_clock.h"

#include <time.h>

int64_t tp_clock_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
        return 0;
    }

    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
