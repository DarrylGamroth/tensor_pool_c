#include "tensor_pool/tp.h"

#include <assert.h>

typedef struct tp_test_agent_state_stct
{
    int work_count;
}
tp_test_agent_state_t;

static int tp_test_agent_do_work(void *state)
{
    tp_test_agent_state_t *ctx = (tp_test_agent_state_t *)state;

    if (NULL == ctx)
    {
        return -1;
    }

    ctx->work_count++;
    return 1;
}

static void test_agent_runner_manual(void)
{
    tp_test_agent_state_t state = {0};
    tp_agent_runner_t *runner = NULL;
    int work = 0;
    tp_agent_idle_strategy_config_t config;

    memset(&config, 0, sizeof(config));
    config.sleep_ns = 1000;

    assert(tp_agent_runner_init(
        &runner,
        "tp-test-agent",
        &state,
        tp_test_agent_do_work,
        NULL,
        TP_AGENT_IDLE_SLEEPING,
        &config) == 0);
    work = tp_agent_runner_do_work(runner);
    assert(work == 1);
    tp_agent_runner_idle(runner, work);
    assert(state.work_count == 1);
    assert(tp_agent_runner_close(runner) == 0);
}

void tp_test_agent_runner(void)
{
    test_agent_runner_manual();
}
