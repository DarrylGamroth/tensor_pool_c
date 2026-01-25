#ifndef TENSOR_POOL_TP_VERSION_H
#define TENSOR_POOL_TP_VERSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TP_VERSION_MAJOR 0
#define TP_VERSION_MINOR 1
#define TP_VERSION_PATCH 0

uint32_t tp_version_compose(uint16_t major, uint16_t minor, uint16_t patch);

#ifdef __cplusplus
}
#endif

#endif
