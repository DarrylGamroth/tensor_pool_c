#include "tensor_pool/tp_supervisor.h"

#include <assert.h>
#include <string.h>

static tp_consumer_hello_view_t tp_make_hello(
    uint32_t stream_id,
    uint32_t consumer_id,
    const char *descriptor_channel,
    uint32_t descriptor_stream_id,
    const char *control_channel,
    uint32_t control_stream_id)
{
    tp_consumer_hello_view_t hello;

    memset(&hello, 0, sizeof(hello));
    hello.stream_id = stream_id;
    hello.consumer_id = consumer_id;
    hello.supports_shm = 1;
    hello.supports_progress = 1;
    hello.mode = TP_MODE_STREAM;
    hello.descriptor_stream_id = descriptor_stream_id;
    hello.control_stream_id = control_stream_id;
    hello.descriptor_channel.data = descriptor_channel;
    hello.descriptor_channel.length = descriptor_channel ? (uint32_t)strlen(descriptor_channel) : 0;
    hello.control_channel.data = control_channel;
    hello.control_channel.length = control_channel ? (uint32_t)strlen(control_channel) : 0;
    return hello;
}

static void tp_test_supervisor_per_consumer_assign(void)
{
    tp_supervisor_config_t config;
    tp_supervisor_t supervisor;
    tp_consumer_hello_view_t hello;
    tp_consumer_config_msg_t out;

    assert(tp_supervisor_config_init(&config) == 0);
    config.per_consumer_enabled = true;
    strncpy(config.per_consumer_descriptor_channel, "aeron:ipc", sizeof(config.per_consumer_descriptor_channel) - 1);
    strncpy(config.per_consumer_control_channel, "aeron:ipc", sizeof(config.per_consumer_control_channel) - 1);
    config.per_consumer_descriptor_base = 31000;
    config.per_consumer_descriptor_range = 1000;
    config.per_consumer_control_base = 32000;
    config.per_consumer_control_range = 1000;
    config.force_mode = TP_MODE_RATE_LIMITED;
    config.force_no_shm = true;
    strncpy(config.payload_fallback_uri, "aeron:udp?endpoint=localhost:40123", sizeof(config.payload_fallback_uri) - 1);

    assert(tp_supervisor_init(&supervisor, &config) == 0);

    hello = tp_make_hello(10000, 42, "aeron:ipc", 1100, "aeron:ipc", 1000);
    assert(tp_supervisor_handle_hello(&supervisor, &hello, &out) == 0);
    assert(out.stream_id == 10000);
    assert(out.consumer_id == 42);
    assert(out.use_shm == 0);
    assert(out.mode == TP_MODE_RATE_LIMITED);
    assert(out.descriptor_stream_id == 31042);
    assert(out.control_stream_id == 32042);
    assert(strcmp(out.descriptor_channel, "aeron:ipc") == 0);
    assert(strcmp(out.control_channel, "aeron:ipc") == 0);
    assert(strcmp(out.payload_fallback_uri, "aeron:udp?endpoint=localhost:40123") == 0);

    tp_supervisor_close(&supervisor);
}

static void tp_test_supervisor_disabled_request(void)
{
    tp_supervisor_config_t config;
    tp_supervisor_t supervisor;
    tp_consumer_hello_view_t hello;
    tp_consumer_config_msg_t out;

    assert(tp_supervisor_config_init(&config) == 0);
    config.per_consumer_enabled = false;
    assert(tp_supervisor_init(&supervisor, &config) == 0);

    hello = tp_make_hello(10000, 7, "aeron:ipc", 1100, "aeron:ipc", 1000);
    assert(tp_supervisor_handle_hello(&supervisor, &hello, &out) == 0);
    assert(out.descriptor_stream_id == 0);
    assert(out.control_stream_id == 0);
    assert(strcmp(out.descriptor_channel, "") == 0);
    assert(strcmp(out.control_channel, "") == 0);

    tp_supervisor_close(&supervisor);
}

void tp_test_supervisor(void)
{
    tp_test_supervisor_per_consumer_assign();
    tp_test_supervisor_disabled_request();
}
