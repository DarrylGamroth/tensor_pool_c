#ifndef TENSOR_POOL_TP_AGENT_H
#define TENSOR_POOL_TP_AGENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_agent_runner_stct tp_agent_runner_t;

typedef int (*tp_agent_do_work_func_t)(void *state);
typedef void (*tp_agent_on_close_func_t)(void *state);

int tp_agent_runner_init(
    tp_agent_runner_t **out,
    const char *role_name,
    void *state,
    tp_agent_do_work_func_t do_work,
    tp_agent_on_close_func_t on_close,
    uint64_t idle_sleep_ns);

int tp_agent_runner_start(tp_agent_runner_t *runner);
int tp_agent_runner_stop(tp_agent_runner_t *runner);
int tp_agent_runner_close(tp_agent_runner_t *runner);
int tp_agent_runner_do_work(tp_agent_runner_t *runner);
void tp_agent_runner_idle(tp_agent_runner_t *runner, int work_count);

#ifdef __cplusplus
}
#endif

#endif
