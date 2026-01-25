#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/shmRegionSuperblock.h"

#include <errno.h>
#include <getopt.h>
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
        "    %s --allow-noncompliant --noncanonical <path> <region> <stream_id> <epoch> <pool_id> <nslots> <stride_bytes> <layout_version>\n"
        "Options:\n"
        "  -b, --shm-base-dir <dir>     Base SHM directory\n"
        "  -n, --namespace <ns>         Namespace (e.g., default)\n"
        "  -r, --region <header|pool>   Region type\n"
        "  -s, --stream-id <id>         Stream id\n"
        "  -e, --epoch <epoch>          Epoch\n"
        "  -p, --pool-id <id>           Pool id\n"
        "  -N, --nslots <nslots>        Slots per ring\n"
        "  -S, --stride-bytes <bytes>   Slot stride bytes\n"
        "  -l, --layout-version <ver>   Layout version\n"
        "  -x, --noncanonical <path>    Explicit path (requires --allow-noncompliant)\n"
        "  -A, --allow-noncompliant     Allow non-canonical paths\n"
        "  -h, --help                   Show help\n",
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

    {
        const char *noncanonical_path = NULL;
        int opt;
        int option_index = 0;
        static struct option long_opts[] = {
            {"shm-base-dir", required_argument, NULL, 'b'},
            {"namespace", required_argument, NULL, 'n'},
            {"region", required_argument, NULL, 'r'},
            {"stream-id", required_argument, NULL, 's'},
            {"epoch", required_argument, NULL, 'e'},
            {"pool-id", required_argument, NULL, 'p'},
            {"nslots", required_argument, NULL, 'N'},
            {"stride-bytes", required_argument, NULL, 'S'},
            {"layout-version", required_argument, NULL, 'l'},
            {"noncanonical", required_argument, NULL, 'x'},
            {"allow-noncompliant", no_argument, NULL, 'A'},
            {"help", no_argument, NULL, 'h'},
            {NULL, 0, NULL, 0}
        };

        while ((opt = getopt_long(argc, argv, "b:n:r:s:e:p:N:S:l:x:Ah", long_opts, &option_index)) != -1)
        {
            switch (opt)
            {
                case 'b':
                    shm_base_dir = optarg;
                    break;
                case 'n':
                    namespace = optarg;
                    break;
                case 'r':
                    region_str = optarg;
                    break;
                case 's':
                    stream_id_str = optarg;
                    break;
                case 'e':
                    epoch_str = optarg;
                    break;
                case 'p':
                    pool_id_str = optarg;
                    break;
                case 'N':
                    nslots_str = optarg;
                    break;
                case 'S':
                    stride_str = optarg;
                    break;
                case 'l':
                    layout_str = optarg;
                    break;
                case 'x':
                    noncanonical_path = optarg;
                    break;
                case 'A':
                    allow_noncompliant = 1;
                    break;
                case 'h':
                    usage(argv[0]);
                    return 0;
                default:
                    usage(argv[0]);
                    return 1;
            }
        }

        if (noncanonical_path)
        {
            use_noncanonical = 1;
            if (!allow_noncompliant)
            {
                fprintf(stderr, "Noncanonical layout requires --allow-noncompliant\n");
                usage(argv[0]);
                return 1;
            }
            fprintf(stderr, "Warning: using noncanonical SHM paths (test-only)\n");
            path = noncanonical_path;
        }
    }

    if (use_noncanonical)
    {
        if (optind < argc && NULL == region_str) { region_str = argv[optind++]; }
        if (optind < argc && NULL == stream_id_str) { stream_id_str = argv[optind++]; }
        if (optind < argc && NULL == epoch_str) { epoch_str = argv[optind++]; }
        if (optind < argc && NULL == pool_id_str) { pool_id_str = argv[optind++]; }
        if (optind < argc && NULL == nslots_str) { nslots_str = argv[optind++]; }
        if (optind < argc && NULL == stride_str) { stride_str = argv[optind++]; }
        if (optind < argc && NULL == layout_str) { layout_str = argv[optind++]; }
        if (optind < argc)
        {
            usage(argv[0]);
            return 1;
        }
    }
    else
    {
        if (allow_noncompliant)
        {
            fprintf(stderr, "--allow-noncompliant is only valid with --noncanonical\n");
            return 1;
        }
        use_canonical = 1;
        if (optind < argc)
        {
            shm_base_dir = shm_base_dir ? shm_base_dir : argv[optind++];
            namespace = namespace ? namespace : argv[optind++];
            region_str = region_str ? region_str : argv[optind++];
            stream_id_str = stream_id_str ? stream_id_str : argv[optind++];
            epoch_str = epoch_str ? epoch_str : argv[optind++];
            pool_id_str = pool_id_str ? pool_id_str : argv[optind++];
            nslots_str = nslots_str ? nslots_str : argv[optind++];
            stride_str = stride_str ? stride_str : argv[optind++];
            layout_str = layout_str ? layout_str : argv[optind++];
        }
        if (optind < argc)
        {
            usage(argv[0]);
            return 1;
        }
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
