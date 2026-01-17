#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_tensor.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t slot[TP_HEADER_SLOT_BYTES];
    tp_slot_view_t view;
    tp_tensor_header_t tensor;
    size_t copy_len;

    if (data == NULL)
    {
        return 0;
    }

    memset(slot, 0, sizeof(slot));
    copy_len = size < sizeof(slot) ? size : sizeof(slot);
    if (copy_len > 0)
    {
        memcpy(slot, data, copy_len);
    }

    if (tp_slot_decode(&view, slot, sizeof(slot), NULL) == 0)
    {
        if (view.header_bytes != NULL &&
            view.header_bytes_length > 0 &&
            view.header_bytes_length <= sizeof(slot))
        {
            if (tp_tensor_header_decode(&tensor, view.header_bytes, view.header_bytes_length, NULL) == 0)
            {
                (void)tp_tensor_header_validate(&tensor, NULL);
            }
        }
    }

    return 0;
}
