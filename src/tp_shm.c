#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
extern char *realpath(const char *path, char *resolved_path);
#endif
#include <sys/mman.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/statfs.h>
#endif

#include "tensor_pool/tp_error.h"

#include "wire/tensor_pool/shmRegionSuperblock.h"
#include "wire/tensor_pool/regionType.h"

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

static int tp_shm_check_hugepages(int fd, const char *path)
{
#if defined(__linux__)
    struct statfs statfs_buf;

    if (fstatfs(fd, &statfs_buf) < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: fstatfs failed for %s", path);
        return -1;
    }

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

static int tp_shm_validate_path(const char *resolved_path, const tp_allowed_paths_t *allowed)
{
    struct stat st_path;

    if (NULL != allowed && allowed->canonical_length > 0 && NULL != allowed->canonical_paths)
    {
        size_t i;
        bool allowed_match = false;

        for (i = 0; i < allowed->canonical_length; i++)
        {
            const char *base = allowed->canonical_paths[i];
            size_t base_len;

            if (NULL == base)
            {
                continue;
            }

            base_len = strlen(base);
            if (0 == base_len)
            {
                continue;
            }

            if (0 == strncmp(resolved_path, base, base_len))
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
    else if (NULL != allowed && allowed->length > 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_map: allowed paths not canonicalized");
        return -1;
    }

    if (stat(resolved_path, &st_path) < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: stat failed for %s", resolved_path);
        return -1;
    }

    if (!S_ISREG(st_path.st_mode))
    {
        TP_SET_ERR(EINVAL, "tp_shm_map: not a regular file: %s", resolved_path);
        return -1;
    }

    return 0;
}

static int tp_shm_open_no_symlink(const char *path, int flags, int *out_fd)
{
    int dirfd = -1;
    int fd = -1;
    const char *cursor;

    if (NULL == path || NULL == out_fd)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_map: null path");
        return -1;
    }

    if (path[0] != '/')
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_map: path must be absolute");
        return -1;
    }

#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#else
    TP_SET_ERR(EINVAL, "tp_shm_map: platform lacks O_NOFOLLOW for %s", path);
    return -1;
#endif

#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif

    dirfd = open("/", O_RDONLY | O_DIRECTORY
#ifdef O_CLOEXEC
        | O_CLOEXEC
#endif
    );
    if (dirfd < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: open root failed for %s", path);
        return -1;
    }

    cursor = path;
    while (*cursor != '\0')
    {
        const char *next;
        size_t len;
        char component[NAME_MAX + 1];
        int next_fd;

        while (*cursor == '/')
        {
            cursor++;
        }
        if (*cursor == '\0')
        {
            break;
        }

        next = strchr(cursor, '/');
        if (NULL == next)
        {
            len = strlen(cursor);
        }
        else
        {
            len = (size_t)(next - cursor);
        }

        if (len == 0 || len > NAME_MAX)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_map: invalid path component");
            close(dirfd);
            return -1;
        }

        memcpy(component, cursor, len);
        component[len] = '\0';

        if (NULL == next)
        {
            fd = openat(dirfd, component, flags);
            if (fd < 0)
            {
                TP_SET_ERR(errno, "tp_shm_map: open failed for %s", path);
            }
            close(dirfd);
            dirfd = -1;
            break;
        }

        next_fd = openat(dirfd, component, O_RDONLY | O_DIRECTORY
#ifdef O_NOFOLLOW
            | O_NOFOLLOW
#endif
#ifdef O_CLOEXEC
            | O_CLOEXEC
#endif
        );
        close(dirfd);
        dirfd = -1;
        if (next_fd < 0)
        {
            TP_SET_ERR(errno, "tp_shm_map: open directory failed for %s", path);
            return -1;
        }

        dirfd = next_fd;
        cursor = next;
    }

    if (dirfd >= 0)
    {
        close(dirfd);
    }

    if (fd < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_map: path missing file component");
        return -1;
    }

    *out_fd = fd;
    return 0;
}

static int tp_shm_open_canonical(const char *path, const tp_allowed_paths_t *allowed, int flags, int *out_fd)
{
    char resolved_path[4096];
    struct stat st_fd;
    struct stat st_path;
    int fd;

    if (NULL == path || NULL == out_fd)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_map: null path");
        return -1;
    }

    if (NULL == realpath(path, resolved_path))
    {
        TP_SET_ERR(errno, "tp_shm_map: realpath failed for %s", path);
        return -1;
    }

    if (tp_shm_validate_path(resolved_path, allowed) < 0)
    {
        return -1;
    }

    if (tp_shm_open_no_symlink(path, flags, &fd) < 0)
    {
        return -1;
    }

    if (fstat(fd, &st_fd) < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: fstat failed for %s", path);
        close(fd);
        return -1;
    }

    if (!S_ISREG(st_fd.st_mode))
    {
        TP_SET_ERR(EINVAL, "tp_shm_map: not a regular file: %s", resolved_path);
        close(fd);
        return -1;
    }

    if (stat(resolved_path, &st_path) < 0)
    {
        TP_SET_ERR(errno, "tp_shm_map: stat failed for %s", resolved_path);
        close(fd);
        return -1;
    }

    if (st_fd.st_dev != st_path.st_dev || st_fd.st_ino != st_path.st_ino)
    {
        TP_SET_ERR(EINVAL, "tp_shm_map: path identity mismatch for %s", resolved_path);
        close(fd);
        return -1;
    }

    *out_fd = fd;
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

    if (tp_shm_open_canonical(region->uri.path, allowed, flags, &region->fd) < 0)
    {
        return -1;
    }

#if defined(__linux__)
    {
        struct statfs statfs_buf;
        if (fstatfs(region->fd, &statfs_buf) == 0)
        {
            if (statfs_buf.f_type != TMPFS_MAGIC && statfs_buf.f_type != HUGETLBFS_MAGIC)
            {
                tp_log_emit(log, TP_LOG_WARN, "tp_shm_map: file-backed mapping for %s", region->uri.path);
            }
        }
    }
#endif

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
    {
        enum tensor_pool_regionType decoded_region_type;
        if (!tensor_pool_shmRegionSuperblock_regionType(&superblock, &decoded_region_type))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_superblock: invalid region type");
            return -1;
        }
        region_type = (int16_t)decoded_region_type;
    }
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

int tp_shm_validate_stride_alignment(const char *uri, uint32_t stride_bytes, tp_log_t *log)
{
    tp_shm_uri_t parsed;
    size_t page_size = 0;

    if (NULL == uri)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_stride_alignment: null uri");
        return -1;
    }

    if (stride_bytes == 0 || (stride_bytes & (stride_bytes - 1)) != 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_stride_alignment: stride not power of two");
        return -1;
    }

    if (tp_shm_uri_parse(&parsed, uri, log) < 0)
    {
        return -1;
    }

#if defined(__linux__)
    {
        struct statfs st;
        if (statfs(parsed.path, &st) < 0)
        {
            TP_SET_ERR(errno, "tp_shm_validate_stride_alignment: statfs failed for %s", parsed.path);
            return -1;
        }
        page_size = (size_t)st.f_bsize;
        if (parsed.require_hugepages)
        {
            if ((uint64_t)st.f_type != (uint64_t)HUGETLBFS_MAGIC)
            {
                TP_SET_ERR(EINVAL, "tp_shm_validate_stride_alignment: not hugetlbfs: %s", parsed.path);
                return -1;
            }
        }
    }
#else
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    if (parsed.require_hugepages)
    {
        TP_SET_ERR(EINVAL, "tp_shm_validate_stride_alignment: hugepages unsupported: %s", parsed.path);
        return -1;
    }
#endif

    if (page_size == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_stride_alignment: invalid page size");
        return -1;
    }

    if (stride_bytes % page_size != 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_shm_validate_stride_alignment: stride not aligned to page size");
        return -1;
    }

    return 0;
}
