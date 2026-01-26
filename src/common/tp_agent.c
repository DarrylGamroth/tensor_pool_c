#include "tensor_pool/common/tp_agent.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "aeron_agent.h"
#include "aeron_alloc.h"
#include "tensor_pool/common/tp_error.h"

struct tp_agent_runner_stct
{
    aeron_agent_runner_t runner;
    uint64_t idle_sleep_ns;
    void *idle_state;
    bool owns_idle_state;
    aeron_idle_strategy_func_t idle_func;
};

int tp_agent_runner_init(
    tp_agent_runner_t **out,
    const char *role_name,
    void *state,
    tp_agent_do_work_func_t do_work,
    tp_agent_on_close_func_t on_close,
    tp_agent_idle_strategy_t idle_strategy,
    const tp_agent_idle_strategy_config_t *config)
{
    tp_agent_runner_t *runner = NULL;
    const char *role = role_name ? role_name : "tp-agent";
    uint64_t sleep_ns = 1000000ULL;
    aeron_idle_strategy_func_t idle_func = aeron_idle_strategy_sleeping_idle;
    void *idle_state = NULL;
    bool owns_idle_state = false;

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

    if (config && config->sleep_ns > 0)
    {
        sleep_ns = config->sleep_ns;
    }

    switch (idle_strategy)
    {
        case TP_AGENT_IDLE_SLEEPING:
            idle_func = aeron_idle_strategy_sleeping_idle;
            runner->idle_sleep_ns = sleep_ns;
            idle_state = &runner->idle_sleep_ns;
            break;
        case TP_AGENT_IDLE_YIELDING:
            idle_func = aeron_idle_strategy_yielding_idle;
            break;
        case TP_AGENT_IDLE_BUSY_SPIN:
            idle_func = aeron_idle_strategy_busy_spinning_idle;
            break;
        case TP_AGENT_IDLE_NOOP:
            idle_func = aeron_idle_strategy_noop_idle;
            break;
        case TP_AGENT_IDLE_BACKOFF:
        default:
            {
                uint64_t max_spins = AERON_IDLE_STRATEGY_BACKOFF_MAX_SPINS;
                uint64_t max_yields = AERON_IDLE_STRATEGY_BACKOFF_MAX_YIELDS;
                uint64_t min_park = AERON_IDLE_STRATEGY_BACKOFF_MIN_PARK_PERIOD_NS;
                uint64_t max_park = AERON_IDLE_STRATEGY_BACKOFF_MAX_PARK_PERIOD_NS;

                if (config)
                {
                    if (config->max_spins > 0)
                    {
                        max_spins = config->max_spins;
                    }
                    if (config->max_yields > 0)
                    {
                        max_yields = config->max_yields;
                    }
                    if (config->min_park_period_ns > 0)
                    {
                        min_park = config->min_park_period_ns;
                    }
                    if (config->max_park_period_ns > 0)
                    {
                        max_park = config->max_park_period_ns;
                    }
                }

                if (aeron_idle_strategy_backoff_state_init(&idle_state, max_spins, max_yields, min_park, max_park) < 0)
                {
                    free(runner);
                    return -1;
                }
                owns_idle_state = true;
                idle_func = aeron_idle_strategy_backoff_idle;
            }
            break;
    }

    runner->idle_state = idle_state;
    runner->owns_idle_state = owns_idle_state;
    runner->idle_func = idle_func;

    if (aeron_agent_init(
            &runner->runner,
            role,
            state,
            NULL,
            NULL,
            (aeron_agent_do_work_func_t)do_work,
            (aeron_agent_on_close_func_t)on_close,
            idle_func,
            idle_state) < 0)
    {
        if (runner->owns_idle_state)
        {
            aeron_free(runner->idle_state);
        }
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
    if (runner->owns_idle_state && runner->idle_state)
    {
        aeron_free(runner->idle_state);
        runner->idle_state = NULL;
    }
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
