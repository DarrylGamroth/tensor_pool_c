#ifndef TENSOR_POOL_TP_CLIENT_CONDUCTOR_AGENT_H
#define TENSOR_POOL_TP_CLIENT_CONDUCTOR_AGENT_H

#include <stdint.h>

#include "tensor_pool/internal/tp_client_conductor.h"
#include "tensor_pool/common/tp_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_client_conductor_agent_stct
{
    tp_client_conductor_t *conductor;
    tp_agent_runner_t *runner;
}
tp_client_conductor_agent_t;

int tp_client_conductor_agent_init(
    tp_client_conductor_agent_t *agent,
    tp_client_conductor_t *conductor,
    uint64_t idle_sleep_ns);
int tp_client_conductor_agent_start(tp_client_conductor_agent_t *agent);
int tp_client_conductor_agent_stop(tp_client_conductor_agent_t *agent);
int tp_client_conductor_agent_close(tp_client_conductor_agent_t *agent);
int tp_client_conductor_agent_do_work(tp_client_conductor_agent_t *agent);
void tp_client_conductor_agent_idle(tp_client_conductor_agent_t *agent, int work_count);

#ifdef __cplusplus
}
#endif

#endif
