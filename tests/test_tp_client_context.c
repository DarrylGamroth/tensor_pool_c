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
    tp_client_context_t ctx;
    int error_calls = 0;
    int invoker_calls = 0;
    int log_calls = 0;
    aeron_t *dummy_aeron = (aeron_t *)0x1;

    tp_client_context_set_aeron_dir(NULL, "aeron:ipc");
    tp_client_context_set_aeron(NULL, dummy_aeron);
    tp_client_context_set_owns_aeron_client(NULL, false);
    tp_client_context_set_client_name(NULL, "client");
    tp_client_context_set_message_timeout_ns(NULL, 1);
    tp_client_context_set_message_retry_attempts(NULL, 2);
    tp_client_context_set_error_handler(NULL, tp_test_error_handler, &error_calls);
    tp_client_context_set_delegating_invoker(NULL, tp_test_invoker, &invoker_calls);
    tp_client_context_set_log_handler(NULL, tp_test_log_handler, &log_calls);
    tp_client_context_set_control_channel(NULL, "aeron:ipc", 1);
    tp_client_context_set_announce_channel(NULL, "aeron:ipc", 2);
    tp_client_context_set_descriptor_channel(NULL, "aeron:ipc", 3);
    tp_client_context_set_qos_channel(NULL, "aeron:ipc", 4);
    tp_client_context_set_metadata_channel(NULL, "aeron:ipc", 5);
    tp_client_context_set_driver_timeout_ns(NULL, 6);
    tp_client_context_set_keepalive_interval_ns(NULL, 7);
    tp_client_context_set_lease_expiry_grace_intervals(NULL, 8);
    tp_client_context_set_idle_sleep_duration_ns(NULL, 9);
    tp_client_context_set_announce_period_ns(NULL, 10);
    tp_client_context_set_use_agent_invoker(NULL, true);

    assert(tp_client_context_init(&ctx) == 0);

    tp_client_context_set_aeron_dir(&ctx, "/tmp/aeron");
    assert(strcmp(ctx.base.aeron_dir, "/tmp/aeron") == 0);

    tp_client_context_set_aeron(&ctx, dummy_aeron);
    assert(ctx.aeron == dummy_aeron);

    tp_client_context_set_owns_aeron_client(&ctx, false);
    assert(!ctx.owns_aeron_client);

    tp_client_context_set_client_name(&ctx, "client");
    assert(strcmp(ctx.client_name, "client") == 0);
    tp_client_context_set_client_name(&ctx, NULL);

    tp_client_context_set_message_timeout_ns(&ctx, 123);
    assert(ctx.message_timeout_ns == 123);

    tp_client_context_set_message_retry_attempts(&ctx, 7);
    assert(ctx.message_retry_attempts == 7);

    tp_client_context_set_error_handler(&ctx, tp_test_error_handler, &error_calls);
    assert(ctx.error_handler == tp_test_error_handler);
    assert(ctx.error_handler_clientd == &error_calls);

    tp_client_context_set_delegating_invoker(&ctx, tp_test_invoker, &invoker_calls);
    assert(ctx.delegating_invoker == tp_test_invoker);
    assert(ctx.delegating_invoker_clientd == &invoker_calls);

    tp_client_context_set_log_handler(&ctx, tp_test_log_handler, &log_calls);
    tp_log_emit(&ctx.base.log, TP_LOG_INFO, "log");
    assert(log_calls == 1);

    tp_client_context_set_control_channel(&ctx, "aeron:ipc", 1000);
    tp_client_context_set_announce_channel(&ctx, "aeron:ipc", 1001);
    tp_client_context_set_descriptor_channel(&ctx, "aeron:ipc", 1100);
    tp_client_context_set_qos_channel(&ctx, "aeron:ipc", 1200);
    tp_client_context_set_metadata_channel(&ctx, "aeron:ipc", 1300);

    assert(ctx.base.control_stream_id == 1000);
    assert(ctx.base.announce_stream_id == 1001);
    assert(ctx.base.descriptor_stream_id == 1100);
    assert(ctx.base.qos_stream_id == 1200);
    assert(ctx.base.metadata_stream_id == 1300);

    tp_client_context_set_driver_timeout_ns(&ctx, 1000);
    tp_client_context_set_keepalive_interval_ns(&ctx, 2000);
    tp_client_context_set_lease_expiry_grace_intervals(&ctx, 4);
    tp_client_context_set_idle_sleep_duration_ns(&ctx, 3000);
    tp_client_context_set_announce_period_ns(&ctx, 5000);
    tp_client_context_set_use_agent_invoker(&ctx, true);

    assert(ctx.driver_timeout_ns == 1000);
    assert(ctx.keepalive_interval_ns == 2000);
    assert(ctx.lease_expiry_grace_intervals == 4);
    assert(ctx.idle_sleep_duration_ns == 3000);
    assert(ctx.base.announce_period_ns == 5000);
    assert(ctx.use_agent_invoker);
}
