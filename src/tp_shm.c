#include "tensor_pool/tp_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/statfs.h>
#endif

#include "tensor_pool/tp_error.h"

#include "wire/tensor_pool/shmRegionSuperblock.h"
#include "wire/tensor_pool/regionType.h"

static int tp_shm_check_hugepages(int fd, const char *path)
{
#if defined(__linux__)
    struct statfs statfs_buf;

    if (fstatfs(fd, &statfs_buf) < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: fstatfs failed for %s", path);
        return -1;
    }

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif

    if ((uint64_t)statfs_buf.f_type != (uint64_t)HUGETLBFS_MAGIC)
    {
        TP_SET_ERR(EINVAL, "tp_shm_map: require_hugepages true but not hugetlbfs: %s", path);
        return -1;
    }

    return 0;
#else
    (void)fd;
    TP_SET_ERR(EINVAL, "tp_shm_map: require_hugepages unsupported on this platform: %s", path);
    return -1;
#endif
}

static int tp_shm_validate_path(const char *path, const tp_allowed_paths_t *allowed)
{
    struct stat st;
    char resolved_path[4096];

    if (NULL == realpath(path, resolved_path))
    {
        TP_SET_ERR(errno, "tp_shm_map: realpath failed for %s", path);
        return -1;
    }

    if (stat(resolved_path, &st) < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: stat failed for %s", resolved_path);
        return -1;
    }

    if (!S_ISREG(st.st_mode))
    {
        TP_SET_ERR(EINVAL, "tp_shm_map: not a regular file: %s", resolved_path);
        return -1;
    }

    if (NULL != allowed && allowed->length > 0 && NULL != allowed->paths)
    {
        size_t i;
        bool allowed_match = false;

        for (i = 0; i < allowed->length; i++)
        {
            const char *base = allowed->paths[i];
            char resolved_base[4096];
            size_t base_len;

            if (NULL == base)
            {
                continue;
            }

            if (NULL == realpath(base, resolved_base))
            {
                continue;
            }

            base_len = strlen(resolved_base);
            if (0 == base_len)
            {
                continue;
            }

            if (0 == strncmp(resolved_path, resolved_base, base_len))
            {
                if (resolved_path[base_len] == '/' || resolved_path[base_len] == '\0')
                {
                    allowed_match = true;
                    break;
                }
            }
        }

        if (!allowed_match)
        {
            TP_SET_ERR(EINVAL, "tp_shm_map: path not in allowlist: %s", resolved_path);
            return -1;
        }
    }

    return 0;
}

int tp_shm_map(tp_shm_region_t *region, const char *uri, int writable, const tp_allowed_paths_t *allowed, tp_log_t *log)
{
    struct stat st;
    int flags = writable ? O_RDWR : O_RDONLY;
    int prot = writable ? (PROT_READ | PROT_WRITE) : PROT_READ;

    if (NULL == region)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_map: region is null");
        return -1;
    }

    memset(region, 0, sizeof(*region));
    region->fd = -1;

    if (tp_shm_uri_parse(&region->uri, uri, log) < 0)
    {
        return -1;
    }

    if (tp_shm_validate_path(region->uri.path, allowed) < 0)
    {
        return -1;
    }

    region->fd = open(region->uri.path, flags);
    if (region->fd < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: open failed for %s", region->uri.path);
        return -1;
    }

    if (region->uri.require_hugepages)
    {
        if (tp_shm_check_hugepages(region->fd, region->uri.path) < 0)
        {
            close(region->fd);
            region->fd = -1;
            return -1;
        }
    }

    if (fstat(region->fd, &st) < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: fstat failed for %s", region->uri.path);
        close(region->fd);
        region->fd = -1;
        return -1;
    }

    if (st.st_size < (off_t)TP_SUPERBLOCK_SIZE_BYTES)
    {
        TP_SET_ERR(EINVAL, "tp_shm_map: region too small: %s", region->uri.path);
        close(region->fd);
        region->fd = -1;
        return -1;
    }

    region->length = (size_t)st.st_size;
    region->addr = mmap(NULL, region->length, prot, MAP_SHARED, region->fd, 0);
    if (MAP_FAILED == region->addr)
    {
        TP_SET_ERR(errno, "tp_shm_map: mmap failed for %s", region->uri.path);
        close(region->fd);
        region->fd = -1;
        region->addr = NULL;
        return -1;
    }

    if (NULL != log)
    {
        tp_log_emit(log, TP_LOG_DEBUG, "Mapped SHM %s length=%zu", region->uri.path, region->length);
    }

    return 0;
}

int tp_shm_unmap(tp_shm_region_t *region, tp_log_t *log)
{
    if (NULL == region)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_unmap: region is null");
        return -1;
    }

    if (NULL != region->addr && 0 != region->length)
    {
        if (munmap(region->addr, region->length) < 0)
        {
            TP_SET_ERR(errno, "%s", "tp_shm_unmap: munmap failed");
            return -1;
        }
    }

    if (region->fd >= 0)
    {
        close(region->fd);
    }

    if (NULL != log)
    {
        tp_log_emit(log, TP_LOG_DEBUG, "Unmapped SHM %s", region->uri.path);
    }

    memset(region, 0, sizeof(*region));
    region->fd = -1;

    return 0;
}

static int tp_shm_validate_slot_bytes(uint32_t slot_bytes, tp_log_t *log)
{
    if (slot_bytes != TP_HEADER_SLOT_BYTES)
    {
        if (NULL != log)
        {
            tp_log_emit(log, TP_LOG_ERROR, "Invalid header slot bytes: %u", slot_bytes);
        }
        TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: invalid slot bytes");
        return -1;
    }

    return 0;
}

int tp_shm_validate_superblock(const tp_shm_region_t *region, const tp_shm_expected_t *expected, tp_log_t *log)
{
    struct tensor_pool_shmRegionSuperblock superblock;
    uint64_t magic;
    uint32_t layout_version;
    uint64_t epoch;
    uint32_t stream_id;
    int16_t region_type;
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t slot_bytes;
    uint32_t stride_bytes;

    if (NULL == region || NULL == region->addr)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: region not mapped");
        return -1;
    }

    tensor_pool_shmRegionSuperblock_wrap_for_decode(
        &superblock,
        (char *)region->addr,
        0,
        tensor_pool_shmRegionSuperblock_sbe_block_length(),
        tensor_pool_shmRegionSuperblock_sbe_schema_version(),
        region->length);

    magic = tensor_pool_shmRegionSuperblock_magic(&superblock);
    layout_version = tensor_pool_shmRegionSuperblock_layoutVersion(&superblock);
    epoch = tensor_pool_shmRegionSuperblock_epoch(&superblock);
    stream_id = tensor_pool_shmRegionSuperblock_streamId(&superblock);
    region_type = tensor_pool_shmRegionSuperblock_regionType(&superblock);
    pool_id = tensor_pool_shmRegionSuperblock_poolId(&superblock);
    nslots = tensor_pool_shmRegionSuperblock_nslots(&superblock);
    slot_bytes = tensor_pool_shmRegionSuperblock_slotBytes(&superblock);
    stride_bytes = tensor_pool_shmRegionSuperblock_strideBytes(&superblock);

    if (magic != TP_MAGIC_U64)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: magic mismatch");
        return -1;
    }

    if (NULL != expected)
    {
        if (expected->layout_version != TP_NULL_U32 && layout_version != expected->layout_version)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: layout version mismatch");
            return -1;
        }

        if (expected->epoch != TP_NULL_U64 && epoch != expected->epoch)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: epoch mismatch");
            return -1;
        }

        if (expected->stream_id != TP_NULL_U32 && stream_id != expected->stream_id)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: stream id mismatch");
            return -1;
        }

        if (expected->region_type != 0 && region_type != expected->region_type)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: region type mismatch");
            return -1;
        }

        if (expected->pool_id != 0 && pool_id != expected->pool_id)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: pool id mismatch");
            return -1;
        }

        if (expected->nslots != TP_NULL_U32 && nslots != expected->nslots)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: nslots mismatch");
            return -1;
        }

        if (expected->slot_bytes != TP_NULL_U32 && slot_bytes != expected->slot_bytes)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: slot bytes mismatch");
            return -1;
        }

        if (expected->stride_bytes != TP_NULL_U32 && stride_bytes != expected->stride_bytes)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: stride bytes mismatch");
            return -1;
        }
    }

    if (region_type == tensor_pool_regionType_PAYLOAD_POOL)
    {
        if (stride_bytes == 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: stride bytes must be > 0");
            return -1;
        }
    }

    if (region_type == tensor_pool_regionType_HEADER_RING)
    {
        if (tp_shm_validate_slot_bytes(slot_bytes, log) < 0)
        {
            return -1;
        }
    }

    if (NULL != log)
    {
        tp_log_emit(log, TP_LOG_DEBUG, "Validated superblock stream=%u epoch=%" PRIu64, stream_id, epoch);
    }

    return 0;
}
