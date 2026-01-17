#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/tp_uri.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char buffer[4096];
    tp_shm_uri_t uri;
    size_t copy_len = size < (sizeof(buffer) - 1) ? size : (sizeof(buffer) - 1);

    if (data == NULL)
    {
        return 0;
    }

    if (copy_len > 0)
    {
        memcpy(buffer, data, copy_len);
    }
    buffer[copy_len] = '\0';

    (void)tp_shm_uri_parse(&uri, buffer, NULL);
    return 0;
}
