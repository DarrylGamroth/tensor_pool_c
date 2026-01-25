#include "tensor_pool/internal/tp_client_internal.h"
#include "tensor_pool/internal/tp_control_poller.h"

#include <assert.h>
#include <string.h>

void tp_test_control_poller_errors(void)
{
    tp_control_poller_t poller;
    tp_client_t client;

    memset(&poller, 0, sizeof(poller));
    memset(&client, 0, sizeof(client));

    assert(tp_control_poller_init(NULL, NULL, NULL) < 0);
    assert(tp_control_poller_init(&poller, NULL, NULL) < 0);
    assert(tp_control_poller_init(&poller, &client, NULL) < 0);

    assert(tp_control_poll(NULL, 1) < 0);
    assert(tp_control_poll(&poller, 1) < 0);
}
