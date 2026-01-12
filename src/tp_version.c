#include "tensor_pool/tp_version.h"

uint32_t tp_version_compose(uint16_t major, uint16_t minor, uint16_t patch)
{
    return ((uint32_t)major << 16) | ((uint32_t)minor << 8) | (uint32_t)patch;
}
