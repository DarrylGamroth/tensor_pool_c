#include "tensor_pool/internal/tp_client_conductor_agent.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/common/tp_error.h"

static int tp_client_conductor_agent_do_work_adapter(void *state)
{
    tp_client_conductor_t *conductor = (tp_client_conductor_t *)state;

    if (NULL == conductor)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_agent_do_work: null conductor");
        return -1;
    }

    return tp_client_conductor_do_work(conductor);
}

int tp_client_conductor_agent_init(
    tp_client_conductor_agent_t *agent,
    tp_client_conductor_t *conductor,
    uint64_t idle_sleep_ns)
{
    if (NULL == agent || NULL == conductor)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_agent_init: invalid input");
        return -1;
    }

    memset(agent, 0, sizeof(*agent));
    agent->conductor = conductor;

    {
        tp_agent_idle_strategy_config_t config;
        memset(&config, 0, sizeof(config));
        config.sleep_ns = idle_sleep_ns;

        if (tp_agent_runner_init(
                &agent->runner,
                "tp-client-conductor",
                conductor,
                tp_client_conductor_agent_do_work_adapter,
                NULL,
                TP_AGENT_IDLE_SLEEPING,
                &config) < 0)
        {
            return -1;
        }
    }

    return 0;
}

int tp_client_conductor_agent_start(tp_client_conductor_agent_t *agent)
{
    if (NULL == agent || NULL == agent->runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_agent_start: invalid agent");
        return -1;
    }

    return tp_agent_runner_start(agent->runner);
}

int tp_client_conductor_agent_stop(tp_client_conductor_agent_t *agent)
{
    if (NULL == agent || NULL == agent->runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_agent_stop: invalid agent");
        return -1;
    }

    return tp_agent_runner_stop(agent->runner);
}

int tp_client_conductor_agent_close(tp_client_conductor_agent_t *agent)
{
    if (NULL == agent)
    {
        return 0;
    }

    tp_agent_runner_close(agent->runner);
    agent->runner = NULL;
    agent->conductor = NULL;
    return 0;
}

int tp_client_conductor_agent_do_work(tp_client_conductor_agent_t *agent)
{
    if (NULL == agent || NULL == agent->runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_agent_do_work: invalid agent");
        return -1;
    }

    return tp_agent_runner_do_work(agent->runner);
}

void tp_client_conductor_agent_idle(tp_client_conductor_agent_t *agent, int work_count)
{
    if (NULL == agent || NULL == agent->runner)
    {
        return;
    }

    tp_agent_runner_idle(agent->runner, work_count);
}
