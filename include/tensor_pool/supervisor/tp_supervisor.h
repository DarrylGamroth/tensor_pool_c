#ifndef TENSOR_POOL_TP_SUPERVISOR_H
#define TENSOR_POOL_TP_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/common/tp_aeron_client.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_control.h"
#include "tensor_pool/client/tp_control_view.h"
#include "tensor_pool/tp_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_supervisor_config_stct
{
    tp_context_t *base;
    char control_channel[1024];
    int32_t control_stream_id;
    char announce_channel[1024];
    int32_t announce_stream_id;
    char metadata_channel[1024];
    int32_t metadata_stream_id;
    char qos_channel[1024];
    int32_t qos_stream_id;
    uint32_t consumer_capacity;
    uint32_t consumer_stale_ms;
    bool per_consumer_enabled;
    char per_consumer_descriptor_channel[1024];
    int32_t per_consumer_descriptor_base;
    uint32_t per_consumer_descriptor_range;
    char per_consumer_control_channel[1024];
    int32_t per_consumer_control_base;
    uint32_t per_consumer_control_range;
    bool force_no_shm;
    uint8_t force_mode;
    char payload_fallback_uri[1024];
}
tp_supervisor_config_t;

typedef struct tp_supervisor_stct
{
    tp_supervisor_config_t config;
    tp_aeron_client_t aeron;
    tp_subscription_t *control_subscription;
    tp_subscription_t *announce_subscription;
    tp_subscription_t *metadata_subscription;
    tp_subscription_t *qos_subscription;
    tp_publication_t *control_publication;
    tp_fragment_assembler_t *control_assembler;
    tp_fragment_assembler_t *announce_assembler;
    tp_fragment_assembler_t *metadata_assembler;
    tp_fragment_assembler_t *qos_assembler;
    void *registry;
    uint64_t last_sweep_ns;
    uint64_t hello_count;
    uint64_t config_count;
    uint64_t qos_consumer_count;
    uint64_t qos_producer_count;
    uint64_t announce_count;
    uint64_t metadata_count;
}
tp_supervisor_t;

typedef struct tp_supervisor_stats_stct
{
    uint64_t hello_count;
    uint64_t config_count;
    uint64_t qos_consumer_count;
    uint64_t qos_producer_count;
    uint64_t announce_count;
    uint64_t metadata_count;
}
tp_supervisor_stats_t;

int tp_supervisor_config_init(tp_supervisor_config_t *config);
int tp_supervisor_config_load(tp_supervisor_config_t *config, const char *path);
void tp_supervisor_config_close(tp_supervisor_config_t *config);

int tp_supervisor_init(tp_supervisor_t *supervisor, tp_supervisor_config_t *config);
int tp_supervisor_start(tp_supervisor_t *supervisor);
int tp_supervisor_do_work(tp_supervisor_t *supervisor);
int tp_supervisor_close(tp_supervisor_t *supervisor);

int tp_supervisor_handle_hello(
    tp_supervisor_t *supervisor,
    const tp_consumer_hello_view_t *hello,
    tp_consumer_config_msg_t *out_config);
int tp_supervisor_get_stats(const tp_supervisor_t *supervisor, tp_supervisor_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
