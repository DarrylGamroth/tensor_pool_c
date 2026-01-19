#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_progress_poller.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void tp_test_progress_poller_misc(void)
{
    tp_progress_poller_t poller;
    tp_consumer_t consumer;
    int result = -1;

    memset(&poller, 0, sizeof(poller));
    memset(&consumer, 0, sizeof(consumer));

    tp_progress_poller_set_max_payload_bytes(NULL, 1);
    tp_progress_poller_set_validator(NULL, NULL, NULL);
    tp_progress_poller_set_consumer(NULL, NULL);

    poller.tracker = calloc(1, sizeof(*poller.tracker));
    if (NULL == poller.tracker)
    {
        goto cleanup;
    }
    poller.tracker_capacity = 1;
    poller.tracker_cursor = 5;

    consumer.header_nslots = 4;
    tp_progress_poller_set_consumer(&poller, &consumer);

    assert(poller.tracker_capacity == 4);
    assert(poller.tracker_cursor == 0);
    assert(poller.validator_clientd == &consumer);

    if (tp_progress_poll(&poller, 1) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    free(poller.tracker);
    assert(result == 0);
}
