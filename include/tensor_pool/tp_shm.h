#ifndef TENSOR_POOL_TP_SHM_H
#define TENSOR_POOL_TP_SHM_H

#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/tp_log.h"
#include "tensor_pool/tp_types.h"
#include "tensor_pool/tp_uri.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_allowed_paths_stct
{
    const char **paths;
    size_t length;
    char **canonical_paths;
    size_t canonical_length;
}
tp_allowed_paths_t;

typedef struct tp_shm_region_stct
{
    int fd;
    size_t length;
    void *addr;
    tp_shm_uri_t uri;
}
tp_shm_region_t;

typedef struct tp_shm_expected_stct
{
    uint32_t stream_id;
    uint32_t layout_version;
    uint64_t epoch;
    int16_t region_type;
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t slot_bytes;
    uint32_t stride_bytes;
}
tp_shm_expected_t;

int tp_shm_map(tp_shm_region_t *region, const char *uri, int writable, const tp_allowed_paths_t *allowed, tp_log_t *log);
int tp_shm_unmap(tp_shm_region_t *region, tp_log_t *log);
int tp_shm_validate_superblock(const tp_shm_region_t *region, const tp_shm_expected_t *expected, tp_log_t *log);

#ifdef __cplusplus
}
#endif

#endif
