#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_log.h"

#include "aeronc.h"

#include <assert.h>
#include <string.h>

static void tp_test_error_handler(void *clientd, int errcode, const char *message)
{
    int *count = (int *)clientd;

    (void)errcode;
    (void)message;

    if (count)
    {
        (*count)++;
    }
}

static void tp_test_invoker(void *clientd)
{
    int *count = (int *)clientd;

    if (count)
    {
        (*count)++;
    }
}

static void tp_test_log_handler(tp_log_level_t level, const char *message, void *clientd)
{
    int *count = (int *)clientd;

    (void)level;
    (void)message;

    if (count)
    {
        (*count)++;
    }
}

void tp_test_client_context_setters(void)
{
    tp_context_t *ctx = NULL;
    int error_calls = 0;
    int invoker_calls = 0;
    int log_calls = 0;
    aeron_t *dummy_aeron = (aeron_t *)0x1;

    tp_context_set_aeron_dir(NULL, "aeron:ipc");
    tp_context_set_aeron(NULL, dummy_aeron);
    tp_context_set_owns_aeron_client(NULL, false);
    tp_context_set_client_name(NULL, "client");
    tp_context_set_message_timeout_ns(NULL, 1);
    tp_context_set_message_retry_attempts(NULL, 2);
    tp_context_set_error_handler(NULL, tp_test_error_handler, &error_calls);
    tp_context_set_delegating_invoker(NULL, tp_test_invoker, &invoker_calls);
    tp_context_set_log_handler(NULL, tp_test_log_handler, &log_calls);
    tp_context_set_control_channel(NULL, "aeron:ipc", 1);
    tp_context_set_announce_channel(NULL, "aeron:ipc", 2);
    tp_context_set_descriptor_channel(NULL, "aeron:ipc", 3);
    tp_context_set_qos_channel(NULL, "aeron:ipc", 4);
    tp_context_set_metadata_channel(NULL, "aeron:ipc", 5);
    tp_context_set_driver_timeout_ns(NULL, 6);
    tp_context_set_keepalive_interval_ns(NULL, 7);
    tp_context_set_lease_expiry_grace_intervals(NULL, 8);
    tp_context_set_idle_sleep_duration_ns(NULL, 9);
    tp_context_set_announce_period_ns(NULL, 10);
    tp_context_set_use_agent_invoker(NULL, true);

    assert(tp_context_init(&ctx) == 0);

    tp_context_set_aeron_dir(ctx, "/tmp/aeron");
    assert(strcmp(tp_context_get_aeron_dir(ctx), "/tmp/aeron") == 0);

    tp_context_set_aeron(ctx, dummy_aeron);
    assert(tp_context_get_aeron(ctx) == dummy_aeron);

    tp_context_set_owns_aeron_client(ctx, false);
    assert(!tp_context_get_owns_aeron_client(ctx));

    tp_context_set_client_name(ctx, "client");
    assert(strcmp(tp_context_get_client_name(ctx), "client") == 0);
    tp_context_set_client_name(ctx, NULL);

    tp_context_set_message_timeout_ns(ctx, 123);
    assert(tp_context_get_message_timeout_ns(ctx) == 123);

    tp_context_set_message_retry_attempts(ctx, 7);
    assert(tp_context_get_message_retry_attempts(ctx) == 7);

    tp_context_set_error_handler(ctx, tp_test_error_handler, &error_calls);
    assert(tp_context_get_error_handler(ctx) == tp_test_error_handler);
    assert(tp_context_get_error_handler_clientd(ctx) == &error_calls);

    tp_context_set_delegating_invoker(ctx, tp_test_invoker, &invoker_calls);
    assert(tp_context_get_delegating_invoker(ctx) == tp_test_invoker);
    assert(tp_context_get_delegating_invoker_clientd(ctx) == &invoker_calls);

    tp_context_set_log_handler(ctx, tp_test_log_handler, &log_calls);
    tp_log_emit(tp_context_log(ctx), TP_LOG_INFO, "log");
    assert(log_calls == 1);
    assert(tp_context_get_log_handler(ctx) == tp_test_log_handler);
    assert(tp_context_get_log_handler_clientd(ctx) == &log_calls);

    tp_context_set_control_channel(ctx, "aeron:ipc", 1000);
    tp_context_set_announce_channel(ctx, "aeron:ipc", 1001);
    tp_context_set_descriptor_channel(ctx, "aeron:ipc", 1100);
    tp_context_set_qos_channel(ctx, "aeron:ipc", 1200);
    tp_context_set_metadata_channel(ctx, "aeron:ipc", 1300);

    assert(tp_context_get_control_stream_id(ctx) == 1000);
    assert(tp_context_get_announce_stream_id(ctx) == 1001);
    assert(tp_context_get_descriptor_stream_id(ctx) == 1100);
    assert(tp_context_get_qos_stream_id(ctx) == 1200);
    assert(tp_context_get_metadata_stream_id(ctx) == 1300);

    tp_context_set_driver_timeout_ns(ctx, 1000);
    tp_context_set_keepalive_interval_ns(ctx, 2000);
    tp_context_set_lease_expiry_grace_intervals(ctx, 4);
    tp_context_set_idle_sleep_duration_ns(ctx, 3000);
    tp_context_set_announce_period_ns(ctx, 5000);
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_idle_strategy(ctx, 7000);

    assert(tp_context_get_driver_timeout_ns(ctx) == 1000);
    assert(tp_context_get_keepalive_interval_ns(ctx) == 2000);
    assert(tp_context_get_lease_expiry_grace_intervals(ctx) == 4);
    assert(tp_context_get_idle_sleep_duration_ns(ctx) == 7000);
    assert(tp_context_get_announce_period_ns(ctx) == 5000);
    assert(tp_context_get_use_agent_invoker(ctx));

    assert(tp_context_close(ctx) == 0);
}
