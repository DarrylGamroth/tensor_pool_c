#include "tensor_pool/driver/tp_driver_agent.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/common/tp_error.h"

static int tp_driver_agent_do_work_adapter(void *state)
{
    tp_driver_t *driver = (tp_driver_t *)state;

    if (NULL == driver)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_agent_do_work: null driver");
        return -1;
    }

    return tp_driver_do_work(driver);
}

int tp_driver_agent_init(tp_driver_agent_t *agent, tp_driver_t *driver, uint64_t idle_sleep_ns)
{
    if (NULL == agent || NULL == driver)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_agent_init: invalid input");
        return -1;
    }

    memset(agent, 0, sizeof(*agent));
    agent->driver = driver;

    {
        tp_agent_idle_strategy_config_t config;
        memset(&config, 0, sizeof(config));
        config.sleep_ns = idle_sleep_ns;

        if (tp_agent_runner_init(
                &agent->runner,
                "tp-driver",
                driver,
                tp_driver_agent_do_work_adapter,
                NULL,
                TP_AGENT_IDLE_SLEEPING,
                &config) < 0)
        {
            return -1;
        }
    }

    return 0;
}

int tp_driver_agent_start(tp_driver_agent_t *agent)
{
    if (NULL == agent || NULL == agent->runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_agent_start: invalid agent");
        return -1;
    }

    return tp_agent_runner_start(agent->runner);
}

int tp_driver_agent_stop(tp_driver_agent_t *agent)
{
    if (NULL == agent || NULL == agent->runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_agent_stop: invalid agent");
        return -1;
    }

    return tp_agent_runner_stop(agent->runner);
}

int tp_driver_agent_close(tp_driver_agent_t *agent)
{
    if (NULL == agent)
    {
        return 0;
    }

    tp_agent_runner_close(agent->runner);
    agent->runner = NULL;
    agent->driver = NULL;
    return 0;
}

int tp_driver_agent_do_work(tp_driver_agent_t *agent)
{
    if (NULL == agent || NULL == agent->runner)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_agent_do_work: invalid agent");
        return -1;
    }

    return tp_agent_runner_do_work(agent->runner);
}

void tp_driver_agent_idle(tp_driver_agent_t *agent, int work_count)
{
    if (NULL == agent || NULL == agent->runner)
    {
        return;
    }

    tp_agent_runner_idle(agent->runner, work_count);
}
