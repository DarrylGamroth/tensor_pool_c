#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/tp_qos.h"

static void tp_fuzz_on_qos_event(void *clientd, const tp_qos_event_t *event)
{
    (void)clientd;
    (void)event;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_qos_poller_t poller;

    if (data == NULL)
    {
        return 0;
    }

    memset(&poller, 0, sizeof(poller));
    poller.handlers.on_qos_event = tp_fuzz_on_qos_event;
    poller.handlers.clientd = NULL;

    tp_qos_poller_handle_fragment(&poller, data, size);

    return 0;
}
