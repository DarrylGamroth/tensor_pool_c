#ifndef TENSOR_POOL_TP_DISCOVERY_SERVICE_H
#define TENSOR_POOL_TP_DISCOVERY_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/tp_aeron.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_handles.h"
#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_control.h"
#include "tensor_pool/tp_discovery_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_discovery_service_config_stct
{
    tp_context_t base;
    char request_channel[1024];
    int32_t request_stream_id;
    char announce_channel[1024];
    int32_t announce_stream_id;
    char metadata_channel[1024];
    int32_t metadata_stream_id;
    char driver_instance_id[256];
    char driver_control_channel[1024];
    int32_t driver_control_stream_id;
    uint32_t announce_period_ms;
    uint32_t max_results;
}
tp_discovery_service_config_t;

typedef struct tp_discovery_service_stct
{
    tp_discovery_service_config_t config;
    tp_aeron_client_t aeron;
    tp_subscription_t *request_subscription;
    tp_subscription_t *announce_subscription;
    tp_subscription_t *metadata_subscription;
    tp_fragment_assembler_t *request_assembler;
    tp_fragment_assembler_t *announce_assembler;
    tp_fragment_assembler_t *metadata_assembler;
    void *entries;
    size_t entry_count;
    void *publications;
    size_t publication_count;
}
tp_discovery_service_t;

int tp_discovery_service_config_init(tp_discovery_service_config_t *config);
int tp_discovery_service_config_load(tp_discovery_service_config_t *config, const char *path);
void tp_discovery_service_config_close(tp_discovery_service_config_t *config);

int tp_discovery_service_init(tp_discovery_service_t *service, tp_discovery_service_config_t *config);
int tp_discovery_service_start(tp_discovery_service_t *service);
int tp_discovery_service_do_work(tp_discovery_service_t *service);
int tp_discovery_service_close(tp_discovery_service_t *service);

int tp_discovery_service_apply_announce(
    tp_discovery_service_t *service,
    const tp_shm_pool_announce_t *announce);
int tp_discovery_service_apply_data_source(
    tp_discovery_service_t *service,
    const tp_data_source_announce_t *announce);
int tp_discovery_service_set_tags(
    tp_discovery_service_t *service,
    uint32_t stream_id,
    const char **tags,
    size_t tag_count);

int tp_discovery_service_query(
    tp_discovery_service_t *service,
    const tp_discovery_request_t *request,
    tp_discovery_response_t *response);
void tp_discovery_service_response_close(tp_discovery_response_t *response);

#ifdef __cplusplus
}
#endif

#endif
