#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/shmRegionSuperblock.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage:\n"
        "  Canonical layout (recommended):\n"
        "    %s --shm-base-dir <dir> --namespace <ns> --region <header|pool> \\\n"
        "       --stream-id <id> --epoch <epoch> --pool-id <pool_id> --nslots <nslots> \\\n"
        "       --stride-bytes <stride_bytes> --layout-version <layout_version>\n"
        "  Explicit path (non-canonical, test-only):\n"
        "    %s --allow-noncompliant --noncanonical <path> <region> <stream_id> <epoch> <pool_id> <nslots> <stride_bytes> <layout_version>\n",
        name,
        name);
}

static int parse_region(const char *value, int16_t *out_type)
{
    if (0 == strcmp(value, "header"))
    {
        *out_type = tensor_pool_regionType_HEADER_RING;
        return 0;
    }
    if (0 == strcmp(value, "pool"))
    {
        *out_type = tensor_pool_regionType_PAYLOAD_POOL;
        return 0;
    }

    return -1;
}

static void sanitize_component(const char *input, char *output, size_t output_len)
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

static int validate_namespace(const char *ns)
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

static int mkdir_p(const char *path, mode_t mode)
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

int main(int argc, char **argv)
{
    struct tensor_pool_shmRegionSuperblock block;
    uint8_t buffer[TP_SUPERBLOCK_SIZE_BYTES];
    const char *path;
    int16_t region_type;
    uint32_t stream_id;
    uint64_t epoch;
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    uint32_t layout_version;
    uint32_t slot_bytes;
    size_t file_size;
    int fd;
    int use_noncanonical = 0;
    int use_canonical = 0;
    int allow_noncompliant = 0;
    const char *shm_base_dir = NULL;
    const char *namespace = NULL;
    const char *region_str = NULL;
    const char *stream_id_str = NULL;
    const char *epoch_str = NULL;
    const char *pool_id_str = NULL;
    const char *nslots_str = NULL;
    const char *stride_str = NULL;
    const char *layout_str = NULL;
    char user_buf[128];
    char path_buf[4096];
    struct passwd *pw = NULL;

    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }

    if (0 == strcmp(argv[1], "--allow-noncompliant"))
    {
        allow_noncompliant = 1;
        if (argc < 3)
        {
            usage(argv[0]);
            return 1;
        }
    }

    if (0 == strcmp(argv[allow_noncompliant ? 2 : 1], "--noncanonical"))
    {
        use_noncanonical = 1;
        if (!allow_noncompliant)
        {
            fprintf(stderr, "Noncanonical layout requires --allow-noncompliant\n");
            usage(argv[0]);
            return 1;
        }
        if (argc != (allow_noncompliant ? 11 : 10))
        {
            usage(argv[0]);
            return 1;
        }

        fprintf(stderr, "Warning: using noncanonical SHM paths (test-only)\n");
        path = argv[allow_noncompliant ? 3 : 2];
        region_str = argv[allow_noncompliant ? 4 : 3];
        stream_id_str = argv[allow_noncompliant ? 5 : 4];
        epoch_str = argv[allow_noncompliant ? 6 : 5];
        pool_id_str = argv[allow_noncompliant ? 7 : 6];
        nslots_str = argv[allow_noncompliant ? 8 : 7];
        stride_str = argv[allow_noncompliant ? 9 : 8];
        layout_str = argv[allow_noncompliant ? 10 : 9];
    }
    else if (0 == strncmp(argv[allow_noncompliant ? 2 : 1], "--", 2))
    {
        int i;

        if (allow_noncompliant)
        {
            fprintf(stderr, "--allow-noncompliant is only valid with --noncanonical\n");
            return 1;
        }
        use_canonical = 1;
        for (i = 1; i + 1 < argc; i += 2)
        {
            const char *key = argv[i];
            const char *value = argv[i + 1];

            if (0 == strcmp(key, "--shm-base-dir"))
            {
                shm_base_dir = value;
            }
            else if (0 == strcmp(key, "--namespace"))
            {
                namespace = value;
            }
            else if (0 == strcmp(key, "--region"))
            {
                region_str = value;
            }
            else if (0 == strcmp(key, "--stream-id"))
            {
                stream_id_str = value;
            }
            else if (0 == strcmp(key, "--epoch"))
            {
                epoch_str = value;
            }
            else if (0 == strcmp(key, "--pool-id"))
            {
                pool_id_str = value;
            }
            else if (0 == strcmp(key, "--nslots"))
            {
                nslots_str = value;
            }
            else if (0 == strcmp(key, "--stride-bytes"))
            {
                stride_str = value;
            }
            else if (0 == strcmp(key, "--layout-version"))
            {
                layout_str = value;
            }
            else
            {
                fprintf(stderr, "Unknown option: %s\n", key);
                return 1;
            }
        }
    }
    else
    {
        fprintf(stderr, "Explicit paths require --noncanonical\n");
        usage(argv[0]);
        return 1;
    }

    if (NULL == region_str || parse_region(region_str, &region_type) < 0)
    {
        fprintf(stderr, "Invalid region type: %s\n", region_str ? region_str : "(null)");
        return 1;
    }

    if (NULL == stream_id_str || NULL == epoch_str || NULL == pool_id_str ||
        NULL == nslots_str || NULL == stride_str || NULL == layout_str)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(stream_id_str, NULL, 10);
    epoch = (uint64_t)strtoull(epoch_str, NULL, 10);
    pool_id = (uint16_t)strtoul(pool_id_str, NULL, 10);
    nslots = (uint32_t)strtoul(nslots_str, NULL, 10);
    stride_bytes = (uint32_t)strtoul(stride_str, NULL, 10);
    layout_version = (uint32_t)strtoul(layout_str, NULL, 10);

    if (use_canonical)
    {
        if (NULL == shm_base_dir || NULL == namespace)
        {
            usage(argv[0]);
            return 1;
        }
        if (shm_base_dir[0] != '/')
        {
            fprintf(stderr, "shm-base-dir must be absolute\n");
            return 1;
        }
        if (validate_namespace(namespace) < 0)
        {
            fprintf(stderr, "Invalid namespace: %s\n", namespace);
            return 1;
        }

        pw = getpwuid(geteuid());
        sanitize_component(pw ? pw->pw_name : "unknown", user_buf, sizeof(user_buf));
        if (user_buf[0] == '\0')
        {
            strncpy(user_buf, "unknown", sizeof(user_buf) - 1);
            user_buf[sizeof(user_buf) - 1] = '\0';
        }

        {
            const char *file_name = (region_type == tensor_pool_regionType_HEADER_RING) ? "header.ring" : NULL;
            if (NULL == file_name)
            {
                static char pool_name[64];
                snprintf(pool_name, sizeof(pool_name), "%u.pool", pool_id);
                file_name = pool_name;
            }

            if (snprintf(
                path_buf,
                sizeof(path_buf),
                "%s/tensorpool-%s/%s/%u/%" PRIu64 "/%s",
                shm_base_dir,
                user_buf,
                namespace,
                stream_id,
                epoch,
                file_name) >= (int)sizeof(path_buf))
            {
                fprintf(stderr, "Canonical path too long\n");
                return 1;
            }
        }

        {
            char dir_path[4096];
            char *slash;

            strncpy(dir_path, path_buf, sizeof(dir_path) - 1);
            dir_path[sizeof(dir_path) - 1] = '\0';
            slash = strrchr(dir_path, '/');
            if (NULL == slash)
            {
                fprintf(stderr, "Invalid canonical path: %s\n", path_buf);
                return 1;
            }
            *slash = '\0';
            if (mkdir_p(dir_path, 0700) < 0)
            {
                fprintf(stderr, "mkdir failed for %s: %s\n", dir_path, strerror(errno));
                return 1;
            }
        }

        path = path_buf;
    }

    if (region_type == tensor_pool_regionType_HEADER_RING)
    {
        slot_bytes = TP_HEADER_SLOT_BYTES;
        file_size = TP_SUPERBLOCK_SIZE_BYTES + ((size_t)nslots * TP_HEADER_SLOT_BYTES);
    }
    else
    {
        if (stride_bytes == 0)
        {
            fprintf(stderr, "stride_bytes must be > 0 for payload pools\n");
            return 1;
        }
        slot_bytes = TP_NULL_U32;
        file_size = TP_SUPERBLOCK_SIZE_BYTES + ((size_t)nslots * stride_bytes);
    }

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
    {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        return 1;
    }

    if (ftruncate(fd, (off_t)file_size) != 0)
    {
        fprintf(stderr, "ftruncate failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    memset(buffer, 0, sizeof(buffer));
    tensor_pool_shmRegionSuperblock_wrap_for_encode(&block, (char *)buffer, 0, sizeof(buffer));
    tensor_pool_shmRegionSuperblock_set_magic(&block, TP_MAGIC_U64);
    tensor_pool_shmRegionSuperblock_set_layoutVersion(&block, layout_version);
    tensor_pool_shmRegionSuperblock_set_epoch(&block, epoch);
    tensor_pool_shmRegionSuperblock_set_streamId(&block, stream_id);
    tensor_pool_shmRegionSuperblock_set_regionType(&block, region_type);
    tensor_pool_shmRegionSuperblock_set_poolId(&block, pool_id);
    tensor_pool_shmRegionSuperblock_set_nslots(&block, nslots);
    tensor_pool_shmRegionSuperblock_set_slotBytes(&block, slot_bytes);
    tensor_pool_shmRegionSuperblock_set_strideBytes(&block, stride_bytes);
    tensor_pool_shmRegionSuperblock_set_pid(&block, (uint64_t)getpid());
    tensor_pool_shmRegionSuperblock_set_startTimestampNs(&block, 1);
    tensor_pool_shmRegionSuperblock_set_activityTimestampNs(&block, 1);

    if (pwrite(fd, buffer, sizeof(buffer), 0) != (ssize_t)sizeof(buffer))
    {
        fprintf(stderr, "superblock write failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
