#include "tensor_pool/internal/tp_consumer_registry.h"
#include "tensor_pool/internal/tp_consumer_manager.h"
#include "tensor_pool/internal/tp_control_adapter.h"
#include "tensor_pool/tp_types.h"

#include <assert.h>
#include <string.h>

static void test_consumer_request_validation(void)
{
    tp_consumer_hello_view_t hello;
    tp_consumer_request_t request;
    int result = -1;

    memset(&hello, 0, sizeof(hello));
    hello.descriptor_channel.data = "";
    hello.descriptor_channel.length = 0;
    hello.descriptor_stream_id = 0;

    if (tp_consumer_request_validate(&hello, &request) != 0)
    {
        goto cleanup;
    }
    assert(request.descriptor_requested == false);

    hello.descriptor_channel.data = "aeron:ipc";
    hello.descriptor_channel.length = 9;
    hello.descriptor_stream_id = 42;
    if (tp_consumer_request_validate(&hello, &request) != 0)
    {
        goto cleanup;
    }
    assert(request.descriptor_requested == true);
    assert(request.control_requested == false);

    hello.control_channel.data = "aeron:ipc";
    hello.control_channel.length = 9;
    hello.control_stream_id = 7;
    if (tp_consumer_request_validate(&hello, &request) != 0)
    {
        goto cleanup;
    }
    assert(request.control_requested == true);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_consumer_progress_aggregation(void)
{
    tp_consumer_registry_t registry;
    tp_progress_policy_t policy;
    tp_consumer_hello_view_t hello;
    int result = -1;

    if (tp_consumer_registry_init(&registry, 2) != 0)
    {
        goto cleanup;
    }

    memset(&hello, 0, sizeof(hello));
    hello.consumer_id = 1;
    hello.supports_progress = 1;
    hello.progress_interval_us = 500;
    hello.progress_bytes_delta = 4096;
    hello.progress_major_delta_units = 4;

    if (tp_consumer_registry_update(&registry, &hello, 100, NULL) != 0)
    {
        goto cleanup;
    }

    memset(&hello, 0, sizeof(hello));
    hello.consumer_id = 2;
    hello.supports_progress = 1;
    hello.progress_interval_us = 250;
    hello.progress_bytes_delta = 1024;
    hello.progress_major_delta_units = 2;

    if (tp_consumer_registry_update(&registry, &hello, 120, NULL) != 0)
    {
        goto cleanup;
    }

    if (tp_consumer_registry_aggregate_progress(&registry, &policy) != 0)
    {
        goto cleanup;
    }

    assert(policy.interval_us == 250);
    assert(policy.bytes_delta == 1024);
    assert(policy.major_delta_units == 2);

    result = 0;

cleanup:
    tp_consumer_registry_close(&registry);
    assert(result == 0);
}

static void test_consumer_registry_decline_invalid_channel(void)
{
    tp_consumer_registry_t registry;
    tp_consumer_hello_view_t hello;
    tp_consumer_entry_t *entry = NULL;
    char long_channel[TP_URI_MAX_LENGTH + 8];
    int result = -1;

    if (tp_consumer_registry_init(&registry, 1) != 0)
    {
        goto cleanup;
    }

    memset(long_channel, 'a', sizeof(long_channel));
    long_channel[sizeof(long_channel) - 1] = '\0';

    memset(&hello, 0, sizeof(hello));
    hello.consumer_id = 1;
    hello.descriptor_channel.data = long_channel;
    hello.descriptor_channel.length = (uint32_t)(sizeof(long_channel) - 1);
    hello.descriptor_stream_id = 10;
    hello.control_channel.data = long_channel;
    hello.control_channel.length = (uint32_t)(sizeof(long_channel) - 1);
    hello.control_stream_id = 11;

    if (tp_consumer_registry_update(&registry, &hello, 10, &entry) != 0)
    {
        goto cleanup;
    }

    assert(entry != NULL);
    assert(entry->descriptor_stream_id == 0);
    assert(entry->descriptor_channel[0] == '\0');
    assert(entry->control_stream_id == 0);
    assert(entry->control_channel[0] == '\0');

    result = 0;

cleanup:
    tp_consumer_registry_close(&registry);
    assert(result == 0);
}

static void test_consumer_registry_sweep(void)
{
    tp_consumer_registry_t registry;
    tp_consumer_hello_view_t hello;
    int result = -1;
    int cleaned = 0;

    if (tp_consumer_registry_init(&registry, 1) != 0)
    {
        goto cleanup;
    }

    memset(&hello, 0, sizeof(hello));
    hello.consumer_id = 9;
    if (tp_consumer_registry_update(&registry, &hello, 10, NULL) != 0)
    {
        goto cleanup;
    }

    cleaned = tp_consumer_registry_sweep(&registry, 50, 20);
    if (cleaned < 0)
    {
        goto cleanup;
    }
    assert(cleaned == 1);

    result = 0;

cleanup:
    tp_consumer_registry_close(&registry);
    assert(result == 0);
}

static void test_progress_throttle_should_publish(void)
{
    tp_consumer_manager_t manager;
    tp_producer_t producer;
    tp_progress_tracker_t state;
    int should_publish = 0;
    int result = -1;

    memset(&producer, 0, sizeof(producer));
    if (tp_consumer_manager_init(&manager, &producer, 1) != 0)
    {
        goto cleanup;
    }

    manager.progress_policy.interval_us = 100;
    manager.progress_policy.bytes_delta = 256;
    manager.progress_policy.major_delta_units = 0;

    memset(&state, 0, sizeof(state));
    if (tp_consumer_manager_should_publish_progress(&manager, &state, 1000, 0, &should_publish) != 0)
    {
        goto cleanup;
    }
    assert(should_publish == 1);

    should_publish = 0;
    if (tp_consumer_manager_should_publish_progress(&manager, &state, 1000, 128, &should_publish) != 0)
    {
        goto cleanup;
    }
    assert(should_publish == 0);

    should_publish = 0;
    if (tp_consumer_manager_should_publish_progress(&manager, &state, 1000 + 100000, 128, &should_publish) != 0)
    {
        goto cleanup;
    }
    assert(should_publish == 1);

    result = 0;

cleanup:
    tp_consumer_manager_close(&manager);
    assert(result == 0);
}

static void test_consumer_manager_force_no_shm(void)
{
    tp_consumer_manager_t manager;
    tp_producer_t producer;
    int result = -1;

    memset(&producer, 0, sizeof(producer));
    if (tp_consumer_manager_init(&manager, &producer, 1) != 0)
    {
        goto cleanup;
    }

    tp_consumer_manager_set_force_no_shm(&manager, true);
    assert(manager.force_no_shm == true);
    tp_consumer_manager_set_force_no_shm(&manager, false);
    assert(manager.force_no_shm == false);
    result = 0;

cleanup:
    tp_consumer_manager_close(&manager);
    assert(result == 0);
}

void tp_test_consumer_registry(void)
{
    test_consumer_request_validation();
    test_consumer_progress_aggregation();
    test_consumer_registry_decline_invalid_channel();
    test_consumer_registry_sweep();
    test_progress_throttle_should_publish();
    test_consumer_manager_force_no_shm();
}
