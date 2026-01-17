#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/tp_tensor.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_tensor_header_t header;

    if (data == NULL || size == 0)
    {
        return 0;
    }

    if (tp_tensor_header_decode(&header, data, size, NULL) == 0)
    {
        (void)tp_tensor_header_validate(&header, NULL);
    }

    return 0;
}
