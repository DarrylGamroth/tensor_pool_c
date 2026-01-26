#ifndef TENSOR_POOL_TP_AGENT_H
#define TENSOR_POOL_TP_AGENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_agent_runner_stct tp_agent_runner_t;

typedef int (*tp_agent_do_work_func_t)(void *state);
typedef void (*tp_agent_on_close_func_t)(void *state);

typedef enum tp_agent_idle_strategy_enum
{
    TP_AGENT_IDLE_SLEEPING = 0,
    TP_AGENT_IDLE_YIELDING = 1,
    TP_AGENT_IDLE_BUSY_SPIN = 2,
    TP_AGENT_IDLE_NOOP = 3,
    TP_AGENT_IDLE_BACKOFF = 4
}
tp_agent_idle_strategy_t;

typedef struct tp_agent_idle_strategy_config_stct
{
    uint64_t sleep_ns;
    uint64_t max_spins;
    uint64_t max_yields;
    uint64_t min_park_period_ns;
    uint64_t max_park_period_ns;
}
tp_agent_idle_strategy_config_t;

int tp_agent_runner_init(
    tp_agent_runner_t **out,
    const char *role_name,
    void *state,
    tp_agent_do_work_func_t do_work,
    tp_agent_on_close_func_t on_close,
    tp_agent_idle_strategy_t idle_strategy,
    const tp_agent_idle_strategy_config_t *config);

int tp_agent_runner_start(tp_agent_runner_t *runner);
int tp_agent_runner_stop(tp_agent_runner_t *runner);
int tp_agent_runner_close(tp_agent_runner_t *runner);
int tp_agent_runner_do_work(tp_agent_runner_t *runner);
void tp_agent_runner_idle(tp_agent_runner_t *runner, int work_count);

#ifdef __cplusplus
}
#endif

#endif
