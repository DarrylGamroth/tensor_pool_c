#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "tensor_pool/tp_shm.h"
#include "tensor_pool/tp_uri.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_shm_uri_t parsed;
    char uri[512];
    uint32_t stride = 0;
    size_t i;
    size_t str_len;

    if (size < 5)
    {
        return 0;
    }

    stride = ((uint32_t)data[0]) |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) |
        ((uint32_t)data[3] << 24);

    str_len = size - 4;
    if (str_len > sizeof(uri) - 1)
    {
        str_len = sizeof(uri) - 1;
    }

    for (i = 0; i < str_len; i++)
    {
        uri[i] = (char)(32 + (data[4 + i] % 95));
    }
    uri[str_len] = '\0';

    if (tp_shm_uri_parse(&parsed, uri, NULL) == 0)
    {
        (void)tp_shm_validate_stride_alignment(uri, stride, NULL);
    }

    return 0;
}
