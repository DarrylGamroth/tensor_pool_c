#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_driver.h"
#include "tensor_pool/internal/tp_aeron.h"

#include <errno.h>
#include <inttypes.h>
#include <ctype.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>

#if defined(__linux__)
#include <sys/statfs.h>
#include <sys/mman.h>
#include <sys/time.h>
#endif

#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"
#include "tensor_pool/internal/tp_context.h"
#include "tp_aeron_wrap.h"

#include "driver/tensor_pool/messageHeader.h"
#include "driver/tensor_pool/shmAttachRequest.h"
#include "driver/tensor_pool/shmAttachResponse.h"
#include "driver/tensor_pool/shmDetachRequest.h"
#include "driver/tensor_pool/shmDetachResponse.h"
#include "driver/tensor_pool/shmLeaseKeepalive.h"
#include "driver/tensor_pool/shmLeaseRevoked.h"
#include "driver/tensor_pool/shmDriverShutdown.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/leaseRevokeReason.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/shutdownReason.h"

#include "wire/tensor_pool/shmPoolAnnounce.h"
#include "wire/tensor_pool/shmRegionSuperblock.h"
#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/clockDomain.h"

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif

typedef struct tp_driver_lease_stct
{
    uint64_t lease_id;
    uint64_t expiry_ns;
    uint64_t issued_ns;
    uint32_t stream_id;
    uint32_t client_id;
    uint32_t node_id;
    uint8_t role;
}
tp_driver_lease_t;

typedef struct tp_driver_node_id_cooldown_stct
{
    uint32_t node_id;
    uint64_t expires_ns;
}
tp_driver_node_id_cooldown_t;

typedef struct tp_driver_stream_state_stct
{
    uint32_t stream_id;
    tp_driver_profile_t *profile;
    uint64_t epoch;
    uint64_t epoch_created_ns;
    uint64_t last_announce_ns;
    uint64_t producer_lease_id;
    uint32_t producer_client_id;
    bool require_hugepages;
}
tp_driver_stream_state_t;

static uint64_t tp_driver_seed_u64(void);
static bool tp_driver_node_id_in_use(tp_driver_t *driver, uint32_t node_id);
static int tp_driver_gc_stream(tp_driver_t *driver, tp_driver_stream_state_t *stream);
static void tp_driver_prune_node_id_cooldowns(tp_driver_t *driver, uint64_t now_ns);
static int tp_driver_node_id_in_cooldown(tp_driver_t *driver, uint32_t node_id, uint64_t now_ns);
static int tp_driver_record_node_id_cooldown(tp_driver_t *driver, uint32_t node_id, uint64_t now_ns);

static uint32_t tp_driver_next_node_id(tp_driver_t *driver)
{
    uint32_t candidate = 0;
    size_t attempts = 0;
    uint64_t now_ns = tp_clock_now_ns();

    if (driver->lease_counter == 0)
    {
        driver->lease_counter = tp_driver_seed_u64();
    }

    tp_driver_prune_node_id_cooldowns(driver, now_ns);

    while (attempts < 1024)
    {
        uint64_t seed = tp_driver_seed_u64() ^ driver->lease_counter ^ attempts;
        candidate = (uint32_t)(seed ^ (seed >> 32));
        if (candidate != 0 &&
            candidate != tensor_pool_shmAttachResponse_nodeId_null_value() &&
            !tp_driver_node_id_in_use(driver, candidate) &&
            !tp_driver_node_id_in_cooldown(driver, candidate, now_ns))
        {
            return candidate;
        }
        attempts++;
    }

    return tensor_pool_shmAttachResponse_nodeId_null_value();
}

static void tp_driver_sanitize_component(const char *input, char *output, size_t output_len)
{
    size_t i = 0;
    size_t j = 0;

    if (output_len == 0)
    {
        return;
    }

    output[0] = '\0';
    if (NULL == input)
    {
        return;
    }

    while (input[i] != '\0' && j + 1 < output_len)
    {
        unsigned char ch = (unsigned char)input[i++];
        if (isalnum(ch) || ch == '-' || ch == '_')
        {
            output[j++] = (char)ch;
        }
        else
        {
            output[j++] = '_';
        }
    }

    output[j] = '\0';
}

static int tp_driver_validate_namespace(const char *ns)
{
    size_t i;

    if (NULL == ns || ns[0] == '\0')
    {
        return -1;
    }

    for (i = 0; ns[i] != '\0'; i++)
    {
        unsigned char ch = (unsigned char)ns[i];
        if (!(isalnum(ch) || ch == '-' || ch == '_'))
        {
            return -1;
        }
    }

    return 0;
}

static int tp_driver_mkdir_p(const char *path, mode_t mode)
{
    char tmp[4096];
    size_t len;
    size_t i;

    if (NULL == path || path[0] == '\0')
    {
        return -1;
    }

    len = strlen(path);
    if (len >= sizeof(tmp))
    {
        return -1;
    }

    memcpy(tmp, path, len + 1);

    for (i = 1; i < len; i++)
    {
        if (tmp[i] == '/')
        {
            tmp[i] = '\0';
            if (mkdir(tmp, mode) < 0 && errno != EEXIST)
            {
                return -1;
            }
            tmp[i] = '/';
        }
    }

    if (mkdir(tmp, mode) < 0 && errno != EEXIST)
    {
        return -1;
    }

    return 0;
}

static tp_driver_stream_state_t *tp_driver_streams(tp_driver_t *driver)
{
    return (tp_driver_stream_state_t *)driver->streams;
}

static tp_driver_lease_t *tp_driver_leases(tp_driver_t *driver)
{
    return (tp_driver_lease_t *)driver->leases;
}

static tp_driver_node_id_cooldown_t *tp_driver_node_id_cooldowns(tp_driver_t *driver)
{
    return (tp_driver_node_id_cooldown_t *)driver->node_id_cooldowns;
}

static uint64_t tp_driver_seed_u64(void)
{
    uint64_t seed = 0;
    int fd = open("/dev/urandom", O_RDONLY);

    if (fd >= 0)
    {
        ssize_t read_len = read(fd, &seed, sizeof(seed));
        close(fd);
        if (read_len != (ssize_t)sizeof(seed))
        {
            seed = 0;
        }
    }

    if (seed == 0)
    {
        seed = tp_clock_now_ns();
        seed ^= ((uint64_t)getpid() << 32);
        seed ^= (uint64_t)(uintptr_t)&seed;
    }

    seed &= 0x7fffffffffffffffULL;
    if (seed == 0)
    {
        seed = 1;
    }

    return seed;
}

static uint64_t tp_driver_next_lease_id(tp_driver_t *driver)
{
    if (driver->lease_counter == 0)
    {
        driver->lease_counter = tp_driver_seed_u64();
    }

    driver->lease_counter++;
    if (driver->lease_counter == 0)
    {
        driver->lease_counter++;
    }

    return driver->lease_counter;
}

static tp_driver_stream_state_t *tp_driver_find_stream(tp_driver_t *driver, uint32_t stream_id)
{
    size_t i;

    if (NULL == driver || stream_id == 0)
    {
        return NULL;
    }

    for (i = 0; i < driver->stream_count; i++)
    {
        if (tp_driver_streams(driver)[i].stream_id == stream_id)
        {
            return &tp_driver_streams(driver)[i];
        }
    }

    return NULL;
}

static bool tp_driver_client_id_in_use(tp_driver_t *driver, uint32_t client_id)
{
    size_t i;

    if (client_id == 0)
    {
        return true;
    }

    for (i = 0; i < driver->lease_count; i++)
    {
        if (tp_driver_leases(driver)[i].client_id == client_id)
        {
            return true;
        }
    }

    return false;
}

static bool tp_driver_node_id_in_use(tp_driver_t *driver, uint32_t node_id)
{
    size_t i;

    if (node_id == TP_NULL_U32)
    {
        return false;
    }

    for (i = 0; i < driver->lease_count; i++)
    {
        if (tp_driver_leases(driver)[i].node_id == node_id)
        {
            return true;
        }
    }

    return false;
}

static void tp_driver_prune_node_id_cooldowns(tp_driver_t *driver, uint64_t now_ns)
{
    size_t i = 0;

    if (NULL == driver || driver->node_id_cooldown_count == 0)
    {
        return;
    }

    while (i < driver->node_id_cooldown_count)
    {
        if (tp_driver_node_id_cooldowns(driver)[i].expires_ns <= now_ns)
        {
            if (i + 1 < driver->node_id_cooldown_count)
            {
                memmove(&tp_driver_node_id_cooldowns(driver)[i],
                    &tp_driver_node_id_cooldowns(driver)[i + 1],
                    (driver->node_id_cooldown_count - i - 1) * sizeof(tp_driver_node_id_cooldown_t));
            }
            driver->node_id_cooldown_count--;
            continue;
        }
        i++;
    }
}

static int tp_driver_node_id_in_cooldown(tp_driver_t *driver, uint32_t node_id, uint64_t now_ns)
{
    size_t i;

    if (NULL == driver || node_id == 0 || driver->node_id_cooldown_count == 0)
    {
        return 0;
    }

    tp_driver_prune_node_id_cooldowns(driver, now_ns);
    for (i = 0; i < driver->node_id_cooldown_count; i++)
    {
        if (tp_driver_node_id_cooldowns(driver)[i].node_id == node_id)
        {
            return 1;
        }
    }

    return 0;
}

static int tp_driver_record_node_id_cooldown(tp_driver_t *driver, uint32_t node_id, uint64_t now_ns)
{
    size_t i;
    uint64_t expires_ns;
    tp_driver_node_id_cooldown_t *cooldowns;

    if (NULL == driver || node_id == 0 ||
        node_id == tensor_pool_shmAttachResponse_nodeId_null_value() ||
        driver->config.node_id_reuse_cooldown_ms == 0)
    {
        return 0;
    }

    tp_driver_prune_node_id_cooldowns(driver, now_ns);
    expires_ns = now_ns + (uint64_t)driver->config.node_id_reuse_cooldown_ms * 1000000ULL;

    for (i = 0; i < driver->node_id_cooldown_count; i++)
    {
        if (tp_driver_node_id_cooldowns(driver)[i].node_id == node_id)
        {
            tp_driver_node_id_cooldowns(driver)[i].expires_ns = expires_ns;
            return 0;
        }
    }

    if (driver->node_id_cooldown_count + 1 > driver->node_id_cooldown_capacity)
    {
        size_t new_capacity = driver->node_id_cooldown_capacity == 0 ? 8 : driver->node_id_cooldown_capacity * 2;
        cooldowns = (tp_driver_node_id_cooldown_t *)realloc(driver->node_id_cooldowns,
            new_capacity * sizeof(*cooldowns));
        if (NULL == cooldowns)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_driver_record_node_id_cooldown: allocation failed");
            return -1;
        }
        driver->node_id_cooldowns = cooldowns;
        driver->node_id_cooldown_capacity = new_capacity;
    }

    tp_driver_node_id_cooldowns(driver)[driver->node_id_cooldown_count].node_id = node_id;
    tp_driver_node_id_cooldowns(driver)[driver->node_id_cooldown_count].expires_ns = expires_ns;
    driver->node_id_cooldown_count++;
    return 0;
}

static int tp_driver_allocate_stream_id(tp_driver_t *driver, uint32_t *out_stream_id)
{
    size_t i;

    if (NULL == driver || NULL == out_stream_id)
    {
        return -1;
    }

    for (i = 0; i < driver->config.stream_id_ranges.count; i++)
    {
        tp_driver_id_range_t *range = &driver->config.stream_id_ranges.ranges[i];
        uint32_t attempts = 0;
        uint32_t cursor = range->next;

        while (attempts <= (range->end - range->start))
        {
            if (tp_driver_find_stream(driver, cursor) == NULL)
            {
                *out_stream_id = cursor;
                range->next = (cursor == range->end) ? range->start : (cursor + 1);
                return 0;
            }

            cursor = (cursor == range->end) ? range->start : (cursor + 1);
            attempts++;
        }
    }

    return -1;
}

static int tp_driver_add_stream(tp_driver_t *driver, uint32_t stream_id, tp_driver_profile_t *profile)
{
    tp_driver_stream_state_t *streams;
    size_t new_count;

    if (NULL == driver || NULL == profile)
    {
        return -1;
    }

    new_count = driver->stream_count + 1;
    streams = (tp_driver_stream_state_t *)realloc(driver->streams, new_count * sizeof(*streams));
    if (NULL == streams)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_driver_add_stream: allocation failed");
        return -1;
    }

    memset(&streams[new_count - 1], 0, sizeof(streams[new_count - 1]));
    streams[new_count - 1].stream_id = stream_id;
    streams[new_count - 1].profile = profile;
    streams[new_count - 1].require_hugepages = driver->config.require_hugepages;
    driver->streams = streams;
    driver->stream_count = new_count;
    return 0;
}

static int tp_driver_add_lease(tp_driver_t *driver, const tp_driver_lease_t *lease)
{
    tp_driver_lease_t *leases;

    if (driver->lease_count + 1 > driver->lease_capacity)
    {
        size_t new_capacity = driver->lease_capacity == 0 ? 8 : driver->lease_capacity * 2;
        leases = (tp_driver_lease_t *)realloc(driver->leases, new_capacity * sizeof(*leases));
        if (NULL == leases)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_driver_add_lease: allocation failed");
            return -1;
        }
        driver->leases = leases;
        driver->lease_capacity = new_capacity;
    }

    tp_driver_leases(driver)[driver->lease_count++] = *lease;
    return 0;
}

static tp_driver_lease_t *tp_driver_find_lease(tp_driver_t *driver, uint64_t lease_id)
{
    size_t i;

    if (NULL == driver || lease_id == 0)
    {
        return NULL;
    }

    for (i = 0; i < driver->lease_count; i++)
    {
        if (tp_driver_leases(driver)[i].lease_id == lease_id)
        {
            return &tp_driver_leases(driver)[i];
        }
    }

    return NULL;
}

static void tp_driver_remove_lease(tp_driver_t *driver, size_t index)
{
    if (NULL == driver || index >= driver->lease_count)
    {
        return;
    }

    if (index + 1 < driver->lease_count)
    {
        memmove(&tp_driver_leases(driver)[index],
            &tp_driver_leases(driver)[index + 1],
            (driver->lease_count - index - 1) * sizeof(tp_driver_lease_t));
    }

    driver->lease_count--;
}

static int tp_driver_is_hugepages_dir(const char *path)
{
#if defined(__linux__)
    struct statfs statfs_buf;

    if (statfs(path, &statfs_buf) < 0)
    {
        return -1;
    }

    return ((uint64_t)statfs_buf.f_type == (uint64_t)HUGETLBFS_MAGIC) ? 1 : 0;
#else
    (void)path;
    return -1;
#endif
}

static int tp_driver_build_region_path(
    tp_driver_t *driver,
    uint32_t stream_id,
    uint64_t epoch,
    uint16_t pool_id,
    int16_t region_type,
    char *path,
    size_t path_len)
{
    char user_buf[128];
    struct passwd *pw = NULL;
    const char *file_name = NULL;
    char pool_name[64];

    if (NULL == driver || NULL == path || path_len == 0)
    {
        return -1;
    }

    if (driver->config.shm_base_dir[0] != '/')
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_build_region_path: shm_base_dir must be absolute");
        return -1;
    }

    if (tp_driver_validate_namespace(driver->config.shm_namespace) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_build_region_path: invalid shm namespace");
        return -1;
    }

    pw = getpwuid(geteuid());
    tp_driver_sanitize_component(pw ? pw->pw_name : "unknown", user_buf, sizeof(user_buf));
    if (user_buf[0] == '\0')
    {
        strncpy(user_buf, "unknown", sizeof(user_buf) - 1);
        user_buf[sizeof(user_buf) - 1] = '\0';
    }

    if (region_type == tensor_pool_regionType_HEADER_RING)
    {
        file_name = "header.ring";
    }
    else
    {
        snprintf(pool_name, sizeof(pool_name), "%u.pool", (unsigned)pool_id);
        file_name = pool_name;
    }

    if (snprintf(
        path,
        path_len,
        "%s/tensorpool-%s/%s/%u/%" PRIu64 "/%s",
        driver->config.shm_base_dir,
        user_buf,
        driver->config.shm_namespace,
        stream_id,
        epoch,
        file_name) >= (int)path_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_build_region_path: path too long");
        return -1;
    }

    return 0;
}

static int tp_driver_build_stream_dir(
    tp_driver_t *driver,
    uint32_t stream_id,
    char *path,
    size_t path_len)
{
    char user_buf[128];
    struct passwd *pw = NULL;

    if (NULL == driver || NULL == path || path_len == 0)
    {
        return -1;
    }

    if (driver->config.shm_base_dir[0] != '/')
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_build_stream_dir: shm_base_dir must be absolute");
        return -1;
    }

    if (tp_driver_validate_namespace(driver->config.shm_namespace) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_build_stream_dir: invalid shm namespace");
        return -1;
    }

    pw = getpwuid(geteuid());
    tp_driver_sanitize_component(pw ? pw->pw_name : "unknown", user_buf, sizeof(user_buf));
    if (user_buf[0] == '\0')
    {
        strncpy(user_buf, "unknown", sizeof(user_buf) - 1);
        user_buf[sizeof(user_buf) - 1] = '\0';
    }

    if (snprintf(
        path,
        path_len,
        "%s/tensorpool-%s/%s/%u",
        driver->config.shm_base_dir,
        user_buf,
        driver->config.shm_namespace,
        stream_id) >= (int)path_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_build_stream_dir: path too long");
        return -1;
    }

    return 0;
}

static int tp_driver_build_epoch_dir(
    tp_driver_t *driver,
    uint32_t stream_id,
    uint64_t epoch,
    char *path,
    size_t path_len)
{
    char stream_dir[4096];

    if (tp_driver_build_stream_dir(driver, stream_id, stream_dir, sizeof(stream_dir)) < 0)
    {
        return -1;
    }

    if (snprintf(path, path_len, "%s/%" PRIu64, stream_dir, epoch) >= (int)path_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_build_epoch_dir: path too long");
        return -1;
    }

    return 0;
}

static int tp_driver_build_region_uri(
    tp_driver_t *driver,
    uint32_t stream_id,
    uint64_t epoch,
    uint16_t pool_id,
    int16_t region_type,
    bool require_hugepages,
    char *uri,
    size_t uri_len)
{
    char path[4096];

    if (tp_driver_build_region_path(driver, stream_id, epoch, pool_id, region_type, path, sizeof(path)) < 0)
    {
        return -1;
    }

    if (snprintf(uri, uri_len, "shm:file?path=%s|require_hugepages=%s",
            path,
            require_hugepages ? "true" : "false") >= (int)uri_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_build_region_uri: uri too long");
        return -1;
    }

    return 0;
}

static int tp_driver_prefault_file(int fd, size_t size, bool mlock_enabled)
{
#if defined(__linux__)
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED)
    {
        return -1;
    }

    memset(addr, 0, size);

    if (mlock_enabled)
    {
        if (mlock(addr, size) != 0)
        {
            munmap(addr, size);
            return -1;
        }
    }

    munmap(addr, size);
    return 0;
#else
    (void)fd;
    (void)size;
    (void)mlock_enabled;
    return 0;
#endif
}

static int tp_driver_create_region_file(
    tp_driver_t *driver,
    uint32_t stream_id,
    uint64_t epoch,
    uint16_t pool_id,
    int16_t region_type,
    uint32_t nslots,
    uint32_t stride_bytes)
{
    char path[4096];
    char dir_path[4096];
    char *slash;
    uint8_t buffer[TP_SUPERBLOCK_SIZE_BYTES];
    struct tensor_pool_shmRegionSuperblock superblock;
    uint32_t slot_bytes;
    size_t file_size;
    int fd;
    mode_t file_mode = (mode_t)driver->config.permissions_mode;
    mode_t dir_mode = file_mode | 0110;
    uint64_t now_ns;

    if (tp_driver_build_region_path(driver, stream_id, epoch, pool_id, region_type, path, sizeof(path)) < 0)
    {
        return -1;
    }

    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    slash = strrchr(dir_path, '/');
    if (NULL == slash)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_create_region_file: invalid path");
        return -1;
    }
    *slash = '\0';
    if (tp_driver_mkdir_p(dir_path, dir_mode) < 0)
    {
        TP_SET_ERR(errno, "tp_driver_create_region_file: mkdir failed for %s", dir_path);
        return -1;
    }

    if (region_type == tensor_pool_regionType_HEADER_RING)
    {
        slot_bytes = TP_HEADER_SLOT_BYTES;
        file_size = TP_SUPERBLOCK_SIZE_BYTES + ((size_t)nslots * TP_HEADER_SLOT_BYTES);
    }
    else
    {
        slot_bytes = TP_NULL_U32;
        file_size = TP_SUPERBLOCK_SIZE_BYTES + ((size_t)nslots * stride_bytes);
    }

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, file_mode);
    if (fd < 0)
    {
        TP_SET_ERR(errno, "tp_driver_create_region_file: open failed for %s", path);
        return -1;
    }

    if (ftruncate(fd, (off_t)file_size) != 0)
    {
        TP_SET_ERR(errno, "tp_driver_create_region_file: ftruncate failed for %s", path);
        close(fd);
        return -1;
    }

    if (driver->config.prefault_shm)
    {
        if (tp_driver_prefault_file(fd, file_size, driver->config.mlock_shm) < 0)
        {
            if (driver->config.mlock_shm)
            {
                TP_SET_ERR(errno, "tp_driver_create_region_file: mlock failed for %s", path);
                close(fd);
                return -1;
            }
            tp_log_emit(&driver->config.base->log, TP_LOG_WARN,
                "tp_driver_create_region_file: prefault failed for %s", path);
        }
    }

    now_ns = tp_clock_now_ns();
    memset(buffer, 0, sizeof(buffer));
    tensor_pool_shmRegionSuperblock_wrap_for_encode(&superblock, (char *)buffer, 0, sizeof(buffer));
    tensor_pool_shmRegionSuperblock_set_magic(&superblock, TP_MAGIC_U64);
    tensor_pool_shmRegionSuperblock_set_layoutVersion(&superblock, TP_LAYOUT_VERSION);
    tensor_pool_shmRegionSuperblock_set_epoch(&superblock, epoch);
    tensor_pool_shmRegionSuperblock_set_streamId(&superblock, stream_id);
    tensor_pool_shmRegionSuperblock_set_regionType(&superblock, region_type);
    tensor_pool_shmRegionSuperblock_set_poolId(&superblock, pool_id);
    tensor_pool_shmRegionSuperblock_set_nslots(&superblock, nslots);
    tensor_pool_shmRegionSuperblock_set_slotBytes(&superblock, slot_bytes);
    tensor_pool_shmRegionSuperblock_set_strideBytes(&superblock, stride_bytes);
    tensor_pool_shmRegionSuperblock_set_pid(&superblock, (uint64_t)getpid());
    tensor_pool_shmRegionSuperblock_set_startTimestampNs(&superblock, now_ns);
    tensor_pool_shmRegionSuperblock_set_activityTimestampNs(&superblock, now_ns);

    if (pwrite(fd, buffer, sizeof(buffer), 0) != (ssize_t)sizeof(buffer))
    {
        TP_SET_ERR(errno, "tp_driver_create_region_file: superblock write failed for %s", path);
        close(fd);
        return -1;
    }

    if (fsync(fd) != 0)
    {
        TP_SET_ERR(errno, "tp_driver_create_region_file: fsync failed for %s", path);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int tp_driver_create_shm_epoch(tp_driver_t *driver, tp_driver_stream_state_t *stream)
{
    size_t i;

    if (NULL == driver || NULL == stream || NULL == stream->profile)
    {
        return -1;
    }

    if (tp_driver_create_region_file(
        driver,
        stream->stream_id,
        stream->epoch,
        0,
        tensor_pool_regionType_HEADER_RING,
        stream->profile->header_nslots,
        0) < 0)
    {
        return -1;
    }

    for (i = 0; i < stream->profile->pool_count; i++)
    {
        const tp_driver_pool_def_t *pool = &stream->profile->pools[i];
        if (tp_driver_create_region_file(
            driver,
            stream->stream_id,
            stream->epoch,
            pool->pool_id,
            tensor_pool_regionType_PAYLOAD_POOL,
            stream->profile->header_nslots,
            pool->stride_bytes) < 0)
        {
            return -1;
        }
    }

    stream->epoch_created_ns = tp_clock_now_ns();
    if (driver->config.epoch_gc_enabled)
    {
        tp_driver_gc_stream(driver, stream);
    }
    return 0;
}

static int tp_driver_remove_epoch_dir(tp_driver_t *driver, tp_driver_stream_state_t *stream, uint64_t epoch)
{
    size_t i;
    char dir_path[4096];

    if (tp_driver_build_epoch_dir(driver, stream->stream_id, epoch, dir_path, sizeof(dir_path)) < 0)
    {
        return -1;
    }

    {
        char header_path[4096];
        if (tp_driver_build_region_path(driver, stream->stream_id, epoch, 0,
                tensor_pool_regionType_HEADER_RING, header_path, sizeof(header_path)) == 0)
        {
            unlink(header_path);
        }
    }

    for (i = 0; i < stream->profile->pool_count; i++)
    {
        char pool_path[4096];
        const tp_driver_pool_def_t *pool = &stream->profile->pools[i];
        if (tp_driver_build_region_path(driver, stream->stream_id, epoch, pool->pool_id,
                tensor_pool_regionType_PAYLOAD_POOL, pool_path, sizeof(pool_path)) == 0)
        {
            unlink(pool_path);
        }
    }

    if (rmdir(dir_path) != 0)
    {
        tp_log_emit(&driver->config.base->log, TP_LOG_WARN,
            "tp_driver_gc: failed to remove %s", dir_path);
        return -1;
    }

    return 0;
}

static int tp_driver_compare_epoch(const void *a, const void *b)
{
    uint64_t left = *(const uint64_t *)a;
    uint64_t right = *(const uint64_t *)b;

    if (left < right)
    {
        return -1;
    }
    if (left > right)
    {
        return 1;
    }
    return 0;
}

static int tp_driver_gc_stream(tp_driver_t *driver, tp_driver_stream_state_t *stream)
{
    char dir_path[4096];
    DIR *dir;
    struct dirent *entry;
    uint64_t *epochs = NULL;
    size_t epoch_count = 0;
    size_t epoch_cap = 0;
    size_t keep_old = 0;
    size_t i;
    uint64_t now_ns = tp_clock_now_ns();

    if (!driver->config.epoch_gc_enabled || driver->config.epoch_gc_keep == 0)
    {
        return 0;
    }

    if (tp_driver_build_stream_dir(driver, stream->stream_id, dir_path, sizeof(dir_path)) < 0)
    {
        return -1;
    }

    dir = opendir(dir_path);
    if (NULL == dir)
    {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        uint64_t epoch;
        char *endptr = NULL;
        char path[4096];
        struct stat st;

        if (entry->d_name[0] == '.')
        {
            continue;
        }

        epoch = strtoull(entry->d_name, &endptr, 10);
        if (endptr == entry->d_name || *endptr != '\0')
        {
            continue;
        }

        if (epoch == stream->epoch)
        {
            continue;
        }

        if (snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name) >= (int)sizeof(path))
        {
            continue;
        }

        if (stat(path, &st) != 0)
        {
            continue;
        }

        if (!S_ISDIR(st.st_mode))
        {
            continue;
        }

        if (epoch_count == epoch_cap)
        {
            size_t new_cap = epoch_cap == 0 ? 8 : epoch_cap * 2;
            uint64_t *next = (uint64_t *)realloc(epochs, new_cap * sizeof(uint64_t));
            if (NULL == next)
            {
                break;
            }
            epochs = next;
            epoch_cap = new_cap;
        }

        epochs[epoch_count++] = epoch;
    }

    closedir(dir);

    if (epoch_count == 0)
    {
        free(epochs);
        return 0;
    }

    qsort(epochs, epoch_count, sizeof(uint64_t), tp_driver_compare_epoch);

    if (driver->config.epoch_gc_keep > 0)
    {
        keep_old = driver->config.epoch_gc_keep - 1;
    }

    if (keep_old >= epoch_count)
    {
        free(epochs);
        return 0;
    }

    for (i = 0; i < epoch_count - keep_old; i++)
    {
        uint64_t epoch = epochs[i];
        char path[4096];
        struct stat st;
        uint64_t age_ns;

        if (snprintf(path, sizeof(path), "%s/%" PRIu64, dir_path, epoch) >= (int)sizeof(path))
        {
            continue;
        }

        if (stat(path, &st) != 0)
        {
            continue;
        }

        {
#if defined(__APPLE__)
            uint64_t mtime_ns = (uint64_t)st.st_mtimespec.tv_sec * 1000000000ULL +
                (uint64_t)st.st_mtimespec.tv_nsec;
#else
            uint64_t mtime_ns = (uint64_t)st.st_mtim.tv_sec * 1000000000ULL +
                (uint64_t)st.st_mtim.tv_nsec;
#endif
            age_ns = now_ns - mtime_ns;
        }

        if (driver->config.epoch_gc_min_age_ns > 0 && age_ns < driver->config.epoch_gc_min_age_ns)
        {
            continue;
        }

        tp_driver_remove_epoch_dir(driver, stream, epoch);
    }

    free(epochs);
    return 0;
}

static int tp_driver_send_control(
    tp_publication_t *publication,
    const uint8_t *buffer,
    size_t length)
{
    int64_t result = aeron_publication_offer(tp_publication_handle(publication), buffer, length, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }
    return 0;
}

static int tp_driver_send_attach_response(
    tp_driver_t *driver,
    int64_t correlation_id,
    int32_t code,
    const tp_driver_stream_state_t *stream,
    const tp_driver_lease_t *lease,
    bool require_hugepages,
    const char *error_message)
{
    uint8_t buffer[2048];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t buffer_len = sizeof(buffer);
    size_t i;

    tensor_pool_messageHeader_wrap(&msg_header, (char *)buffer, 0,
        tensor_pool_messageHeader_sbe_schema_version(), buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, header_len, buffer_len);
    tensor_pool_shmAttachResponse_set_correlationId(&response, correlation_id);
    tensor_pool_shmAttachResponse_set_code(&response, (enum tensor_pool_responseCode)code);

    if (code == tensor_pool_responseCode_OK && stream && lease)
    {
        char header_uri[4096];

        tensor_pool_shmAttachResponse_set_leaseId(&response, lease->lease_id);
        tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(&response, lease->expiry_ns);
        tensor_pool_shmAttachResponse_set_streamId(&response, stream->stream_id);
        tensor_pool_shmAttachResponse_set_epoch(&response, stream->epoch);
        tensor_pool_shmAttachResponse_set_layoutVersion(&response, TP_LAYOUT_VERSION);
        tensor_pool_shmAttachResponse_set_headerNslots(&response, stream->profile->header_nslots);
        tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, TP_HEADER_SLOT_BYTES);
        tensor_pool_shmAttachResponse_set_nodeId(&response, lease->node_id);

        if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
            &pools,
            (char *)buffer,
            (uint16_t)stream->profile->pool_count,
            tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
            tensor_pool_shmAttachResponse_sbe_schema_version(),
            buffer_len))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_send_attach_response: pool encode failed");
            return -1;
        }

        for (i = 0; i < stream->profile->pool_count; i++)
        {
            const tp_driver_pool_def_t *pool = &stream->profile->pools[i];
            char pool_uri[4096];

            if (!tensor_pool_shmAttachResponse_payloadPools_next(&pools))
            {
                TP_SET_ERR(EINVAL, "%s", "tp_driver_send_attach_response: pool next failed");
                return -1;
            }

            tensor_pool_shmAttachResponse_payloadPools_set_poolId(&pools, pool->pool_id);
            tensor_pool_shmAttachResponse_payloadPools_set_poolNslots(&pools, stream->profile->header_nslots);
            tensor_pool_shmAttachResponse_payloadPools_set_strideBytes(&pools, pool->stride_bytes);

            if (tp_driver_build_region_uri(driver, stream->stream_id, stream->epoch, pool->pool_id,
                    tensor_pool_regionType_PAYLOAD_POOL, require_hugepages, pool_uri, sizeof(pool_uri)) < 0)
            {
                return -1;
            }

            if (tensor_pool_shmAttachResponse_payloadPools_put_regionUri(
                &pools,
                pool_uri,
                strlen(pool_uri)) < 0)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_driver_send_attach_response: pool uri encode failed");
                return -1;
            }
        }

        if (tp_driver_build_region_uri(driver, stream->stream_id, stream->epoch, 0,
                tensor_pool_regionType_HEADER_RING, require_hugepages, header_uri, sizeof(header_uri)) < 0)
        {
            return -1;
        }

        if (tensor_pool_shmAttachResponse_put_headerRegionUri(&response, header_uri, strlen(header_uri)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_send_attach_response: header uri encode failed");
            return -1;
        }
    }
    else
    {
        tensor_pool_shmAttachResponse_set_leaseId(
            &response, tensor_pool_shmAttachResponse_leaseId_null_value());
        tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(
            &response, tensor_pool_shmAttachResponse_leaseExpiryTimestampNs_null_value());
        tensor_pool_shmAttachResponse_set_streamId(
            &response, tensor_pool_shmAttachResponse_streamId_null_value());
        tensor_pool_shmAttachResponse_set_epoch(
            &response, tensor_pool_shmAttachResponse_epoch_null_value());
        tensor_pool_shmAttachResponse_set_layoutVersion(
            &response, tensor_pool_shmAttachResponse_layoutVersion_null_value());
        tensor_pool_shmAttachResponse_set_headerNslots(
            &response, tensor_pool_shmAttachResponse_headerNslots_null_value());
        tensor_pool_shmAttachResponse_set_headerSlotBytes(
            &response, tensor_pool_shmAttachResponse_headerSlotBytes_null_value());
        tensor_pool_shmAttachResponse_set_nodeId(
            &response, tensor_pool_shmAttachResponse_nodeId_null_value());

        if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
            &pools,
            (char *)buffer,
            0,
            tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
            tensor_pool_shmAttachResponse_sbe_schema_version(),
            buffer_len))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_send_attach_response: pool encode failed");
            return -1;
        }

        tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "", 0);
    }

    if (NULL != error_message)
    {
        if (tensor_pool_shmAttachResponse_put_errorMessage(&response, error_message, strlen(error_message)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_send_attach_response: error message encode failed");
            return -1;
        }
    }
    else
    {
        tensor_pool_shmAttachResponse_put_errorMessage(&response, "", 0);
    }

    return tp_driver_send_control(driver->control_publication,
        buffer, tensor_pool_shmAttachResponse_sbe_position(&response));
}

static int tp_driver_send_detach_response(
    tp_driver_t *driver,
    int64_t correlation_id,
    int32_t code,
    const char *error_message)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmDetachResponse response;
    size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t buffer_len = sizeof(buffer);

    tensor_pool_messageHeader_wrap(&msg_header, (char *)buffer, 0,
        tensor_pool_messageHeader_sbe_schema_version(), buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_shmDetachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmDetachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmDetachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmDetachResponse_sbe_schema_version());

    tensor_pool_shmDetachResponse_wrap_for_encode(&response, (char *)buffer, header_len, buffer_len);
    tensor_pool_shmDetachResponse_set_correlationId(&response, correlation_id);
    tensor_pool_shmDetachResponse_set_code(&response, (enum tensor_pool_responseCode)code);

    if (NULL != error_message)
    {
        if (tensor_pool_shmDetachResponse_put_errorMessage(&response, error_message, strlen(error_message)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_send_detach_response: error message encode failed");
            return -1;
        }
    }
    else
    {
        tensor_pool_shmDetachResponse_put_errorMessage(&response, "", 0);
    }

    return tp_driver_send_control(driver->control_publication,
        buffer, tensor_pool_shmDetachResponse_sbe_position(&response));
}

static int tp_driver_send_lease_revoked(
    tp_driver_t *driver,
    const tp_driver_lease_t *lease,
    uint8_t reason,
    const char *error_message)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmLeaseRevoked revoked;
    size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t buffer_len = sizeof(buffer);

    if (NULL == driver || NULL == lease)
    {
        return -1;
    }

    tensor_pool_messageHeader_wrap(&msg_header, (char *)buffer, 0,
        tensor_pool_messageHeader_sbe_schema_version(), buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_shmLeaseRevoked_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmLeaseRevoked_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmLeaseRevoked_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmLeaseRevoked_sbe_schema_version());

    tensor_pool_shmLeaseRevoked_wrap_for_encode(&revoked, (char *)buffer, header_len, buffer_len);
    tensor_pool_shmLeaseRevoked_set_timestampNs(&revoked, tp_clock_now_ns());
    tensor_pool_shmLeaseRevoked_set_leaseId(&revoked, lease->lease_id);
    tensor_pool_shmLeaseRevoked_set_streamId(&revoked, lease->stream_id);
    tensor_pool_shmLeaseRevoked_set_clientId(&revoked, lease->client_id);
    tensor_pool_shmLeaseRevoked_set_role(&revoked, (enum tensor_pool_role)lease->role);
    tensor_pool_shmLeaseRevoked_set_reason(&revoked, (enum tensor_pool_leaseRevokeReason)reason);

    if (NULL != error_message)
    {
        if (tensor_pool_shmLeaseRevoked_put_errorMessage(&revoked, error_message, strlen(error_message)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_send_lease_revoked: error message encode failed");
            return -1;
        }
    }
    else
    {
        tensor_pool_shmLeaseRevoked_put_errorMessage(&revoked, "", 0);
    }

    return tp_driver_send_control(driver->control_publication,
        buffer, tensor_pool_shmLeaseRevoked_sbe_position(&revoked));
}

static int tp_driver_send_driver_shutdown(tp_driver_t *driver, uint8_t reason, const char *error_message)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmDriverShutdown shutdown;
    size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t buffer_len = sizeof(buffer);

    if (NULL == driver)
    {
        return -1;
    }

    tensor_pool_messageHeader_wrap(&msg_header, (char *)buffer, 0,
        tensor_pool_messageHeader_sbe_schema_version(), buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_shmDriverShutdown_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmDriverShutdown_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmDriverShutdown_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmDriverShutdown_sbe_schema_version());

    tensor_pool_shmDriverShutdown_wrap_for_encode(&shutdown, (char *)buffer, header_len, buffer_len);
    tensor_pool_shmDriverShutdown_set_timestampNs(&shutdown, tp_clock_now_ns());
    tensor_pool_shmDriverShutdown_set_reason(&shutdown, (enum tensor_pool_shutdownReason)reason);

    if (NULL != error_message)
    {
        tensor_pool_shmDriverShutdown_put_errorMessage(&shutdown, error_message, strlen(error_message));
    }
    else
    {
        tensor_pool_shmDriverShutdown_put_errorMessage(&shutdown, "", 0);
    }

    return tp_driver_send_control(driver->control_publication,
        buffer, tensor_pool_shmDriverShutdown_sbe_position(&shutdown));
}

static int tp_driver_send_announce(tp_driver_t *driver, tp_driver_stream_state_t *stream, bool require_hugepages)
{
    uint8_t buffer[2048];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmPoolAnnounce announce;
    struct tensor_pool_shmPoolAnnounce_payloadPools pools;
    size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t buffer_len = sizeof(buffer);
    size_t i;
    char header_uri[4096];
    uint64_t now_ns = tp_clock_now_ns();
    uint32_t producer_id = stream->producer_client_id;

    tensor_pool_messageHeader_wrap(&msg_header, (char *)buffer, 0,
        tensor_pool_messageHeader_sbe_schema_version(), buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_shmPoolAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmPoolAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmPoolAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmPoolAnnounce_sbe_schema_version());

    tensor_pool_shmPoolAnnounce_wrap_for_encode(&announce, (char *)buffer, header_len, buffer_len);
    tensor_pool_shmPoolAnnounce_set_streamId(&announce, stream->stream_id);
    tensor_pool_shmPoolAnnounce_set_producerId(&announce, producer_id);
    tensor_pool_shmPoolAnnounce_set_epoch(&announce, stream->epoch);
    tensor_pool_shmPoolAnnounce_set_announceTimestampNs(&announce, now_ns);
    tensor_pool_shmPoolAnnounce_set_announceClockDomain(&announce, tensor_pool_clockDomain_MONOTONIC);
    tensor_pool_shmPoolAnnounce_set_layoutVersion(&announce, TP_LAYOUT_VERSION);
    tensor_pool_shmPoolAnnounce_set_headerNslots(&announce, stream->profile->header_nslots);
    tensor_pool_shmPoolAnnounce_set_headerSlotBytes(&announce, TP_HEADER_SLOT_BYTES);

    if (NULL == tensor_pool_shmPoolAnnounce_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        (uint16_t)stream->profile->pool_count,
        tensor_pool_shmPoolAnnounce_sbe_position_ptr(&announce),
        tensor_pool_shmPoolAnnounce_sbe_schema_version(),
        buffer_len))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_send_announce: pool wrap failed");
        return -1;
    }

    for (i = 0; i < stream->profile->pool_count; i++)
    {
        const tp_driver_pool_def_t *pool = &stream->profile->pools[i];
        char pool_uri[4096];

        if (!tensor_pool_shmPoolAnnounce_payloadPools_next(&pools))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_send_announce: pool next failed");
            return -1;
        }

        tensor_pool_shmPoolAnnounce_payloadPools_set_poolId(&pools, pool->pool_id);
        tensor_pool_shmPoolAnnounce_payloadPools_set_poolNslots(&pools, stream->profile->header_nslots);
        tensor_pool_shmPoolAnnounce_payloadPools_set_strideBytes(&pools, pool->stride_bytes);

        if (tp_driver_build_region_uri(driver, stream->stream_id, stream->epoch, pool->pool_id,
                tensor_pool_regionType_PAYLOAD_POOL, require_hugepages, pool_uri, sizeof(pool_uri)) < 0)
        {
            return -1;
        }

        if (tensor_pool_shmPoolAnnounce_payloadPools_put_regionUri(&pools, pool_uri, strlen(pool_uri)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_driver_send_announce: pool uri encode failed");
            return -1;
        }
    }

    if (tp_driver_build_region_uri(driver, stream->stream_id, stream->epoch, 0,
            tensor_pool_regionType_HEADER_RING, require_hugepages, header_uri, sizeof(header_uri)) < 0)
    {
        return -1;
    }

    if (tensor_pool_shmPoolAnnounce_put_headerRegionUri(&announce, header_uri, strlen(header_uri)) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_send_announce: header uri encode failed");
        return -1;
    }

    stream->last_announce_ns = now_ns;
    return tp_driver_send_control(driver->announce_publication,
        buffer, tensor_pool_shmPoolAnnounce_sbe_position(&announce));
}

static int tp_driver_bump_epoch(tp_driver_stream_state_t *stream)
{
    uint64_t now = tp_clock_now_ns();

    if (now <= stream->epoch)
    {
        stream->epoch++;
    }
    else
    {
        stream->epoch = now;
    }

    return 0;
}

static int tp_driver_release_producer(tp_driver_stream_state_t *stream, const tp_driver_lease_t *lease)
{
    if (NULL == stream || NULL == lease)
    {
        return -1;
    }

    if (stream->producer_lease_id == lease->lease_id)
    {
        stream->producer_lease_id = 0;
        stream->producer_client_id = 0;
    }

    return 0;
}

static void tp_driver_handle_expired_leases(tp_driver_t *driver)
{
    size_t i = 0;
    uint64_t now = tp_clock_now_ns();

    while (i < driver->lease_count)
    {
        tp_driver_lease_t *lease = &tp_driver_leases(driver)[i];
        tp_driver_stream_state_t *stream = tp_driver_find_stream(driver, lease->stream_id);
        bool is_producer = (lease->role == tensor_pool_role_PRODUCER);
        bool bump_epoch = false;

        if (lease->expiry_ns != 0 && now > lease->expiry_ns)
        {
            if (NULL != stream && is_producer)
            {
                bump_epoch = true;
            }

            tp_driver_send_lease_revoked(driver, lease, tensor_pool_leaseRevokeReason_EXPIRED, "lease expired");
            (void)tp_driver_record_node_id_cooldown(driver, lease->node_id, now);
            tp_driver_release_producer(stream, lease);
            tp_driver_remove_lease(driver, i);

            if (bump_epoch && NULL != stream)
            {
                tp_driver_bump_epoch(stream);
                if (tp_driver_create_shm_epoch(driver, stream) == 0)
                {
                    tp_driver_send_announce(driver, stream, stream->require_hugepages);
                }
            }
            continue;
        }

        i++;
    }
}

static int tp_driver_handle_detach(
    tp_driver_t *driver,
    const struct tensor_pool_shmDetachRequest *request,
    size_t length)
{
    uint64_t lease_id = tensor_pool_shmDetachRequest_leaseId(request);
    uint32_t stream_id = tensor_pool_shmDetachRequest_streamId(request);
    uint32_t client_id = tensor_pool_shmDetachRequest_clientId(request);
    enum tensor_pool_role role_enum;
    uint8_t role = 0;
    tp_driver_lease_t *lease = tp_driver_find_lease(driver, lease_id);
    tp_driver_stream_state_t *stream = tp_driver_find_stream(driver, stream_id);
    bool is_producer = (role == tensor_pool_role_PRODUCER);
    bool bump_epoch = false;
    size_t index = 0;
    size_t i;

    (void)length;

    if (!tensor_pool_shmDetachRequest_role(request, &role_enum))
    {
        return tp_driver_send_detach_response(driver,
            tensor_pool_shmDetachRequest_correlationId(request),
            tensor_pool_responseCode_INVALID_PARAMS,
            "invalid role");
    }

    role = (uint8_t)role_enum;

    if (NULL == lease || lease->stream_id != stream_id || lease->client_id != client_id || lease->role != role)
    {
        return tp_driver_send_detach_response(driver,
            tensor_pool_shmDetachRequest_correlationId(request),
            tensor_pool_responseCode_REJECTED,
            "lease not found");
    }

    for (i = 0; i < driver->lease_count; i++)
    {
        if (&tp_driver_leases(driver)[i] == lease)
        {
            index = i;
            break;
        }
    }

    if (is_producer && NULL != stream)
    {
        bump_epoch = true;
    }

    tp_driver_send_lease_revoked(driver, lease, tensor_pool_leaseRevokeReason_DETACHED, "lease detached");
    (void)tp_driver_record_node_id_cooldown(driver, lease->node_id, tp_clock_now_ns());
    tp_driver_release_producer(stream, lease);
    tp_driver_remove_lease(driver, index);

    if (bump_epoch && NULL != stream)
    {
        tp_driver_bump_epoch(stream);
        if (tp_driver_create_shm_epoch(driver, stream) == 0)
        {
            tp_driver_send_announce(driver, stream, stream->require_hugepages);
        }
    }

    return tp_driver_send_detach_response(driver,
        tensor_pool_shmDetachRequest_correlationId(request),
        tensor_pool_responseCode_OK,
        NULL);
}

static int tp_driver_handle_keepalive(
    tp_driver_t *driver,
    const struct tensor_pool_shmLeaseKeepalive *request,
    size_t length)
{
    uint64_t lease_id = tensor_pool_shmLeaseKeepalive_leaseId(request);
    uint32_t stream_id = tensor_pool_shmLeaseKeepalive_streamId(request);
    uint32_t client_id = tensor_pool_shmLeaseKeepalive_clientId(request);
    enum tensor_pool_role role_enum;
    uint8_t role = 0;
    tp_driver_lease_t *lease = tp_driver_find_lease(driver, lease_id);
    uint64_t now;
    uint64_t interval_ns;

    (void)length;

    if (!tensor_pool_shmLeaseKeepalive_role(request, &role_enum))
    {
        return 0;
    }

    role = (uint8_t)role_enum;

    if (NULL == lease)
    {
        return 0;
    }

    if (lease->stream_id != stream_id || lease->client_id != client_id || lease->role != role)
    {
        return 0;
    }

    now = tp_clock_now_ns();
    interval_ns = (uint64_t)driver->config.lease_keepalive_interval_ms * 1000000ULL;
    lease->expiry_ns = now + interval_ns * driver->config.lease_expiry_grace_intervals;
    return 0;
}

static int tp_driver_should_require_hugepages(const tp_driver_config_t *config, uint8_t policy)
{
    if (policy == tensor_pool_hugepagesPolicy_HUGEPAGES)
    {
        return 1;
    }
    if (policy == tensor_pool_hugepagesPolicy_STANDARD)
    {
        return 0;
    }

    return config->require_hugepages ? 1 : 0;
}

static int tp_driver_handle_attach(
    tp_driver_t *driver,
    const struct tensor_pool_shmAttachRequest *request,
    size_t length)
{
    int64_t correlation_id = tensor_pool_shmAttachRequest_correlationId(request);
    uint32_t stream_id = tensor_pool_shmAttachRequest_streamId(request);
    uint32_t client_id = tensor_pool_shmAttachRequest_clientId(request);
    enum tensor_pool_role role_enum;
    enum tensor_pool_publishMode publish_mode_enum;
    enum tensor_pool_hugepagesPolicy hugepages_enum;
    uint8_t role = 0;
    uint32_t expected_layout = tensor_pool_shmAttachRequest_expectedLayoutVersion(request);
    uint8_t publish_mode = 0;
    uint8_t hugepages_policy = tensor_pool_hugepagesPolicy_UNSPECIFIED;
    uint32_t desired_node_id = tensor_pool_shmAttachRequest_desiredNodeId(request);
    tp_driver_stream_state_t *stream = NULL;
    tp_driver_profile_t *profile = NULL;
    tp_driver_lease_t lease;
    uint64_t now;
    uint64_t interval_ns;
    bool create_allowed;
    bool require_hugepages;

    (void)length;

    if (!tensor_pool_shmAttachRequest_role(request, &role_enum))
    {
        return tp_driver_send_attach_response(driver, correlation_id,
            tensor_pool_responseCode_INVALID_PARAMS, NULL, NULL,
            driver->config.require_hugepages, "invalid role");
    }

    role = (uint8_t)role_enum;

    if (client_id == 0)
    {
        return tp_driver_send_attach_response(driver, correlation_id,
            tensor_pool_responseCode_INVALID_PARAMS, NULL, NULL,
            driver->config.require_hugepages, "client_id must be non-zero");
    }

    if (tp_driver_client_id_in_use(driver, client_id))
    {
        return tp_driver_send_attach_response(driver, correlation_id,
            tensor_pool_responseCode_REJECTED, NULL, NULL,
            driver->config.require_hugepages, "client_id already attached");
    }

    if (expected_layout != 0 && expected_layout != TP_LAYOUT_VERSION)
    {
        return tp_driver_send_attach_response(driver, correlation_id,
            tensor_pool_responseCode_REJECTED, NULL, NULL,
            driver->config.require_hugepages, "layout version mismatch");
    }

    if (tensor_pool_shmAttachRequest_publishMode(request, &publish_mode_enum))
    {
        publish_mode = (uint8_t)publish_mode_enum;
    }
    else
    {
        publish_mode = 0;
    }

    if (publish_mode == 0)
    {
        publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    }

    create_allowed = (publish_mode == tensor_pool_publishMode_EXISTING_OR_CREATE);

    stream = tp_driver_find_stream(driver, stream_id);
    if (NULL == stream)
    {
        if (!create_allowed || !driver->config.allow_dynamic_streams)
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_REJECTED, NULL, NULL,
                driver->config.require_hugepages, "stream not provisioned");
        }

        if (stream_id == 0 && tp_driver_allocate_stream_id(driver, &stream_id) < 0)
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_INVALID_PARAMS, NULL, NULL,
                driver->config.require_hugepages, "no stream ids available");
        }

        profile = NULL;
        if (driver->config.default_profile[0] != '\0')
        {
            size_t i;
            for (i = 0; i < driver->config.profile_count; i++)
            {
                if (0 == strcmp(driver->config.profiles[i].name, driver->config.default_profile))
                {
                    profile = &driver->config.profiles[i];
                    break;
                }
            }
        }

        if (NULL == profile)
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_INVALID_PARAMS, NULL, NULL,
                driver->config.require_hugepages, "default profile missing");
        }

        if (tp_driver_add_stream(driver, stream_id, profile) < 0)
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_INTERNAL_ERROR, NULL, NULL,
                driver->config.require_hugepages, "stream allocation failed");
        }

        stream = tp_driver_find_stream(driver, stream_id);
    }

    if (NULL == stream || NULL == stream->profile || stream->profile->pool_count == 0)
    {
        return tp_driver_send_attach_response(driver, correlation_id,
            tensor_pool_responseCode_INVALID_PARAMS, NULL, NULL,
            driver->config.require_hugepages, "invalid profile");
    }

    if (role == tensor_pool_role_PRODUCER && stream->producer_lease_id != 0)
    {
        return tp_driver_send_attach_response(driver, correlation_id,
            tensor_pool_responseCode_REJECTED, NULL, NULL,
            driver->config.require_hugepages, "producer already attached");
    }

    if (tensor_pool_shmAttachRequest_requireHugepages(request, &hugepages_enum))
    {
        hugepages_policy = (uint8_t)hugepages_enum;
    }
    else
    {
        hugepages_policy = tensor_pool_hugepagesPolicy_UNSPECIFIED;
    }

    if (hugepages_policy != tensor_pool_hugepagesPolicy_UNSPECIFIED &&
        hugepages_policy != tensor_pool_hugepagesPolicy_STANDARD &&
        hugepages_policy != tensor_pool_hugepagesPolicy_HUGEPAGES)
    {
        return tp_driver_send_attach_response(driver, correlation_id,
            tensor_pool_responseCode_INVALID_PARAMS, NULL, NULL,
            driver->config.require_hugepages, "invalid hugepages policy");
    }

    require_hugepages = tp_driver_should_require_hugepages(&driver->config, hugepages_policy);
    if (require_hugepages)
    {
        int huge_ok = tp_driver_is_hugepages_dir(driver->config.shm_base_dir);
        if (huge_ok != 1)
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_REJECTED, NULL, NULL,
                require_hugepages, "hugepages not available");
        }
    }
    else if (hugepages_policy == tensor_pool_hugepagesPolicy_STANDARD)
    {
        int huge_ok = tp_driver_is_hugepages_dir(driver->config.shm_base_dir);
        if (huge_ok == 1)
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_REJECTED, NULL, NULL,
                require_hugepages, "standard pages requested");
        }
    }

    if (desired_node_id != tensor_pool_shmAttachRequest_desiredNodeId_null_value())
    {
        uint64_t now_ns = tp_clock_now_ns();
        if (tp_driver_node_id_in_use(driver, desired_node_id) ||
            tp_driver_node_id_in_cooldown(driver, desired_node_id, now_ns))
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_REJECTED, NULL, NULL,
                require_hugepages, "desired node_id unavailable");
        }
    }

    now = tp_clock_now_ns();
    interval_ns = (uint64_t)driver->config.lease_keepalive_interval_ms * 1000000ULL;

    if (stream->epoch != 0)
    {
        require_hugepages = stream->require_hugepages;
    }

    if (role == tensor_pool_role_PRODUCER)
    {
        tp_driver_bump_epoch(stream);
        stream->require_hugepages = require_hugepages;
        if (tp_driver_create_shm_epoch(driver, stream) < 0)
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_INTERNAL_ERROR, NULL, NULL,
                require_hugepages, "shm creation failed");
        }
    }
    else if (stream->epoch == 0)
    {
        tp_driver_bump_epoch(stream);
        stream->require_hugepages = require_hugepages;
        if (tp_driver_create_shm_epoch(driver, stream) < 0)
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_INTERNAL_ERROR, NULL, NULL,
                require_hugepages, "shm creation failed");
        }
    }

    memset(&lease, 0, sizeof(lease));
    lease.lease_id = tp_driver_next_lease_id(driver);
    lease.stream_id = stream->stream_id;
    lease.client_id = client_id;
    lease.role = role;
    lease.issued_ns = now;
    lease.expiry_ns = now + interval_ns * driver->config.lease_expiry_grace_intervals;
    if (desired_node_id != tensor_pool_shmAttachRequest_desiredNodeId_null_value())
    {
        lease.node_id = desired_node_id;
    }
    else
    {
        lease.node_id = tp_driver_next_node_id(driver);
        if (lease.node_id == tensor_pool_shmAttachResponse_nodeId_null_value())
        {
            return tp_driver_send_attach_response(driver, correlation_id,
                tensor_pool_responseCode_INTERNAL_ERROR, NULL, NULL,
                require_hugepages, "node_id allocation failed");
        }
    }

    if (tp_driver_add_lease(driver, &lease) < 0)
    {
        return tp_driver_send_attach_response(driver, correlation_id,
            tensor_pool_responseCode_INTERNAL_ERROR, NULL, NULL,
            require_hugepages, "lease allocation failed");
    }

    if (role == tensor_pool_role_PRODUCER)
    {
        stream->producer_lease_id = lease.lease_id;
        stream->producer_client_id = client_id;
    }

    tp_driver_send_announce(driver, stream, require_hugepages);
    return tp_driver_send_attach_response(driver, correlation_id,
        tensor_pool_responseCode_OK, stream, &lease, require_hugepages, NULL);
}

static void tp_driver_on_control_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_driver_t *driver = (tp_driver_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    (void)header;

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);

    if (schema_id != tensor_pool_shmAttachRequest_sbe_schema_id() ||
        version > tensor_pool_shmAttachRequest_sbe_schema_version())
    {
        return;
    }

    if (template_id == tensor_pool_shmAttachRequest_sbe_template_id())
    {
        struct tensor_pool_shmAttachRequest request;
        if (block_length != tensor_pool_shmAttachRequest_sbe_block_length())
        {
            tp_log_emit(&driver->config.base->log, TP_LOG_WARN,
                "tp_driver: attach block length mismatch");
            return;
        }

        tensor_pool_shmAttachRequest_wrap_for_decode(
            &request,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            block_length,
            version,
            length);

        tp_driver_handle_attach(driver, &request, length);
        return;
    }

    if (template_id == tensor_pool_shmDetachRequest_sbe_template_id())
    {
        struct tensor_pool_shmDetachRequest request;
        if (block_length != tensor_pool_shmDetachRequest_sbe_block_length())
        {
            tp_log_emit(&driver->config.base->log, TP_LOG_WARN,
                "tp_driver: detach block length mismatch");
            return;
        }

        tensor_pool_shmDetachRequest_wrap_for_decode(
            &request,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            block_length,
            version,
            length);

        tp_driver_handle_detach(driver, &request, length);
        return;
    }

    if (template_id == tensor_pool_shmLeaseKeepalive_sbe_template_id())
    {
        struct tensor_pool_shmLeaseKeepalive request;
        if (block_length != tensor_pool_shmLeaseKeepalive_sbe_block_length())
        {
            tp_log_emit(&driver->config.base->log, TP_LOG_WARN,
                "tp_driver: keepalive block length mismatch");
            return;
        }

        tensor_pool_shmLeaseKeepalive_wrap_for_decode(
            &request,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            block_length,
            version,
            length);

        tp_driver_handle_keepalive(driver, &request, length);
        return;
    }

    return;
}

int tp_driver_init(tp_driver_t *driver, tp_driver_config_t *config)
{
    size_t i;

    if (NULL == driver || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_init: null input");
        return -1;
    }

    memset(driver, 0, sizeof(*driver));
    driver->config = *config;
    memset(config, 0, sizeof(*config));

    if (driver->config.announce_period_ms == 0)
    {
        driver->config.announce_period_ms = 1000;
    }

    driver->stream_count = driver->config.stream_count;
    if (driver->stream_count > 0)
    {
        driver->streams = calloc(driver->stream_count, sizeof(tp_driver_stream_state_t));
        if (NULL == driver->streams)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_driver_init: stream allocation failed");
            return -1;
        }

        for (i = 0; i < driver->stream_count; i++)
        {
            tp_driver_streams(driver)[i].stream_id = driver->config.streams[i].stream_id;
            tp_driver_streams(driver)[i].profile = driver->config.streams[i].profile;
            tp_driver_streams(driver)[i].require_hugepages = driver->config.require_hugepages;
        }
    }

    if (driver->config.supervisor_enabled)
    {
        if (tp_supervisor_init(&driver->supervisor, &driver->config.supervisor_config) < 0)
        {
            free(driver->streams);
            driver->streams = NULL;
            driver->stream_count = 0;
            tp_driver_config_close(&driver->config);
            return -1;
        }
        driver->supervisor_enabled = true;
    }

    return 0;
}

int tp_driver_start(tp_driver_t *driver)
{
    size_t i;

    if (NULL == driver)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_start: null driver");
        return -1;
    }

    if (tp_aeron_client_init(&driver->aeron, driver->config.base) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_subscription(&driver->control_subscription, &driver->aeron,
            driver->config.base->control_channel, driver->config.base->control_stream_id) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_publication(&driver->control_publication, &driver->aeron,
            driver->config.base->control_channel, driver->config.base->control_stream_id) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_publication(&driver->announce_publication, &driver->aeron,
            driver->config.base->announce_channel, driver->config.base->announce_stream_id) < 0)
    {
        return -1;
    }

    if (tp_fragment_assembler_create(&driver->control_assembler, tp_driver_on_control_fragment, driver) < 0)
    {
        return -1;
    }

    driver->next_announce_ns = tp_clock_now_ns();

    if (driver->config.epoch_gc_on_startup && driver->config.epoch_gc_enabled)
    {
        for (i = 0; i < driver->stream_count; i++)
        {
            tp_driver_gc_stream(driver, &tp_driver_streams(driver)[i]);
        }
    }

    if (driver->supervisor_enabled)
    {
        if (tp_supervisor_start(&driver->supervisor) < 0)
        {
            return -1;
        }
    }
    return 0;
}

int tp_driver_do_work(tp_driver_t *driver)
{
    int fragments = 0;
    uint64_t now;
    uint64_t period_ns;
    size_t i;

    if (NULL == driver)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_driver_do_work: null driver");
        return -1;
    }

    if (NULL != driver->control_subscription && NULL != driver->control_assembler)
    {
        fragments += aeron_subscription_poll(
            tp_subscription_handle(driver->control_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(driver->control_assembler),
            10);
    }

    if (driver->supervisor_enabled)
    {
        int work = tp_supervisor_do_work(&driver->supervisor);
        if (work < 0)
        {
            return -1;
        }
        fragments += work;
    }

    tp_driver_handle_expired_leases(driver);

    now = tp_clock_now_ns();
    period_ns = driver->config.announce_period_ms * 1000000ULL;
    if (period_ns > 0 && now >= driver->next_announce_ns)
    {
        for (i = 0; i < driver->stream_count; i++)
        {
            tp_driver_stream_state_t *stream = &tp_driver_streams(driver)[i];
            if (stream->epoch != 0)
            {
                tp_driver_send_announce(driver, stream, stream->require_hugepages);
            }
        }
        driver->next_announce_ns = now + period_ns;
    }

    return fragments;
}

int tp_driver_close(tp_driver_t *driver)
{
    if (NULL == driver)
    {
        return 0;
    }

    tp_driver_send_driver_shutdown(driver, tensor_pool_shutdownReason_NORMAL, NULL);

    tp_fragment_assembler_close(&driver->control_assembler);
    tp_subscription_close(&driver->control_subscription);
    tp_publication_close(&driver->control_publication);
    tp_publication_close(&driver->announce_publication);
    tp_aeron_client_close(&driver->aeron);

    if (driver->supervisor_enabled)
    {
        tp_supervisor_close(&driver->supervisor);
        driver->supervisor_enabled = false;
    }

    free(driver->streams);
    driver->streams = NULL;
    driver->stream_count = 0;

    free(driver->leases);
    driver->leases = NULL;
    driver->lease_count = 0;
    driver->lease_capacity = 0;

    free(driver->node_id_cooldowns);
    driver->node_id_cooldowns = NULL;
    driver->node_id_cooldown_count = 0;
    driver->node_id_cooldown_capacity = 0;

    tp_driver_config_close(&driver->config);
    memset(driver, 0, sizeof(*driver));
    return 0;
}
