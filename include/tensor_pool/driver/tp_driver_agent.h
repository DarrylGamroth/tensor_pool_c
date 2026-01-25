#ifndef TENSOR_POOL_TP_DRIVER_AGENT_H
#define TENSOR_POOL_TP_DRIVER_AGENT_H

#include <stdint.h>

#include "tensor_pool/driver/tp_driver.h"
#include "tensor_pool/common/tp_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_driver_agent_stct
{
    tp_driver_t *driver;
    tp_agent_runner_t *runner;
}
tp_driver_agent_t;

int tp_driver_agent_init(tp_driver_agent_t *agent, tp_driver_t *driver, uint64_t idle_sleep_ns);
int tp_driver_agent_start(tp_driver_agent_t *agent);
int tp_driver_agent_stop(tp_driver_agent_t *agent);
int tp_driver_agent_close(tp_driver_agent_t *agent);
int tp_driver_agent_do_work(tp_driver_agent_t *agent);
void tp_driver_agent_idle(tp_driver_agent_t *agent, int work_count);

#ifdef __cplusplus
}
#endif

#endif
