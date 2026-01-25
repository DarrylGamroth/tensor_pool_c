#include "tensor_pool/common/tp_agent.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "aeron_agent.h"
#include "tensor_pool/common/tp_error.h"

struct tp_agent_runner_stct
{
    aeron_agent_runner_t runner;
    uint64_t idle_sleep_ns;
};

int tp_agent_runner_init(
    tp_agent_runner_t **out,
    const char *role_name,
    void *state,
    tp_agent_do_work_func_t do_work,
    tp_agent_on_close_func_t on_close,
    uint64_t idle_sleep_ns)
{
    tp_agent_runner_t *runner = NULL;
    const char *role = role_name ? role_name : "tp-agent";

    if (NULL == out || NULL == do_work)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_agent_runner_init: invalid input");
        return -1;
    }

    runner = (tp_agent_runner_t *)calloc(1, sizeof(*runner));
    if (NULL == runner)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_agent_runner_init: allocation failed");
        return -1;
    }

    runner->idle_sleep_ns = idle_sleep_ns == 0 ? 1000000ULL : idle_sleep_ns;

    if (aeron_agent_init(
            &runner->runner,
            role,
            state,
            NULL,
            NULL,
            (aeron_agent_do_work_func_t)do_work,
            (aeron_agent_on_close_func_t)on_close,
            aeron_idle_strategy_sleeping_idle,
            &runner->idle_sleep_ns) < 0)
    {
        free(runner);
        return -1;
    }

    *out = runner;
    return 0;
}

int tp_agent_runner_start(tp_agent_runner_t *runner)
{
    if (NULL == runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_agent_runner_start: null runner");
        return -1;
    }

    return aeron_agent_start(&runner->runner);
}

int tp_agent_runner_stop(tp_agent_runner_t *runner)
{
    if (NULL == runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_agent_runner_stop: null runner");
        return -1;
    }

    return aeron_agent_stop(&runner->runner);
}

int tp_agent_runner_close(tp_agent_runner_t *runner)
{
    if (NULL == runner)
    {
        return 0;
    }

    aeron_agent_close(&runner->runner);
    free(runner);
    return 0;
}

int tp_agent_runner_do_work(tp_agent_runner_t *runner)
{
    if (NULL == runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_agent_runner_do_work: null runner");
        return -1;
    }

    return aeron_agent_do_work(&runner->runner);
}

void tp_agent_runner_idle(tp_agent_runner_t *runner, int work_count)
{
    if (NULL == runner)
    {
        return;
    }

    aeron_agent_idle(&runner->runner, work_count);
}
