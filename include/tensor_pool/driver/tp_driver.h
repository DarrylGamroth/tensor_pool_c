#ifndef TENSOR_POOL_TP_DRIVER_H
#define TENSOR_POOL_TP_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/common/tp_aeron_client.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_handles.h"
#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_driver_pool_def_stct
{
    uint16_t pool_id;
    uint32_t stride_bytes;
}
tp_driver_pool_def_t;

typedef struct tp_driver_profile_stct
{
    char name[128];
    uint32_t header_nslots;
    tp_driver_pool_def_t *pools;
    size_t pool_count;
}
tp_driver_profile_t;

typedef struct tp_driver_stream_def_stct
{
    char name[128];
    uint32_t stream_id;
    tp_driver_profile_t *profile;
}
tp_driver_stream_def_t;

typedef struct tp_driver_id_range_stct
{
    uint32_t start;
    uint32_t end;
    uint32_t next;
}
tp_driver_id_range_t;

typedef struct tp_driver_id_ranges_stct
{
    tp_driver_id_range_t *ranges;
    size_t count;
}
tp_driver_id_ranges_t;

typedef struct tp_driver_config_stct
{
    tp_context_t *base;
    char instance_id[256];
    char shm_base_dir[4096];
    char shm_namespace[256];
    bool require_hugepages;
    uint32_t page_size_bytes;
    uint32_t permissions_mode;
    char **allowed_base_dirs;
    size_t allowed_base_dirs_len;
    bool allow_dynamic_streams;
    char default_profile[128];
    uint32_t announce_period_ms;
    uint32_t lease_keepalive_interval_ms;
    uint32_t lease_expiry_grace_intervals;
    bool prefault_shm;
    bool mlock_shm;
    bool epoch_gc_enabled;
    uint32_t epoch_gc_keep;
    uint64_t epoch_gc_min_age_ns;
    bool epoch_gc_on_startup;
    uint32_t node_id_reuse_cooldown_ms;
    tp_driver_id_ranges_t stream_id_ranges;
    tp_driver_id_ranges_t descriptor_stream_id_ranges;
    tp_driver_id_ranges_t control_stream_id_ranges;
    tp_driver_profile_t *profiles;
    size_t profile_count;
    tp_driver_stream_def_t *streams;
    size_t stream_count;
    bool supervisor_enabled;
    tp_supervisor_config_t supervisor_config;
}
tp_driver_config_t;

typedef struct tp_driver_stct
{
    tp_driver_config_t config;
    tp_aeron_client_t aeron;
    tp_publication_t *control_publication;
    tp_publication_t *announce_publication;
    tp_subscription_t *control_subscription;
    tp_fragment_assembler_t *control_assembler;
    void *streams;
    size_t stream_count;
    void *leases;
    size_t lease_count;
    size_t lease_capacity;
    uint64_t lease_counter;
    uint64_t next_announce_ns;
    void *node_id_cooldowns;
    size_t node_id_cooldown_count;
    size_t node_id_cooldown_capacity;
    bool supervisor_enabled;
    tp_supervisor_t supervisor;
}
tp_driver_t;

int tp_driver_config_init(tp_driver_config_t *config);
int tp_driver_config_load(tp_driver_config_t *config, const char *path);
void tp_driver_config_close(tp_driver_config_t *config);

int tp_driver_init(tp_driver_t *driver, tp_driver_config_t *config);
int tp_driver_start(tp_driver_t *driver);
int tp_driver_do_work(tp_driver_t *driver);
int tp_driver_close(tp_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif
