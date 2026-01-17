#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/tp_slot.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_slot_view_t view;

    if (data == NULL || size == 0)
    {
        return 0;
    }

    (void)tp_slot_decode(&view, data, size, NULL);
    return 0;
}
