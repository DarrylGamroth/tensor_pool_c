#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/tp_discovery_client.h"

static uint64_t tp_fuzz_u64(const uint8_t *data, size_t size)
{
    uint64_t value = 0;
    size_t i;

    for (i = 0; i < sizeof(value) && i < size; i++)
    {
        value |= ((uint64_t)data[i]) << (8u * i);
    }

    return value;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_discovery_response_t response;
    uint64_t request_id;

    if (data == NULL)
    {
        return 0;
    }

    memset(&response, 0, sizeof(response));
    request_id = tp_fuzz_u64(data, size);

    (void)tp_discovery_decode_response(data, size, request_id, &response);
    tp_discovery_response_close(&response);

    return 0;
}
