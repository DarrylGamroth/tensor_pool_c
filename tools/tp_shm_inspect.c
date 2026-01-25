#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_shm.h"

#include "wire/tensor_pool/shmRegionSuperblock.h"

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "util/aeron_error.h"

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options] <shm-uri>\n"
        "Options:\n"
        "  -a, --allow <dir>  Allow SHM base dir (repeatable)\n"
        "  -j, --json         JSON output\n"
        "  -h, --help         Show help\n"
        "Example shm-uri: shm:file?path=/dev/shm/tensorpool-${USER}/demo/10000/1/header.ring\n",
        name);
}

int main(int argc, char **argv)
{
    tp_shm_region_t region;
    tp_context_t ctx;
    struct tensor_pool_shmRegionSuperblock block;
    const char *allowed_paths[16];
    size_t allowed_count = 0;
    const char *uri = NULL;
    int json = 0;
    int opt;
    int option_index = 0;
    static struct option long_opts[] = {
        {"allow", required_argument, NULL, 'a'},
        {"json", no_argument, NULL, 'j'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "a:jh", long_opts, &option_index)) != -1)
    {
        switch (opt)
        {
            case 'a':
                if (allowed_count >= (sizeof(allowed_paths) / sizeof(allowed_paths[0])))
                {
                    usage(argv[0]);
                    return 1;
                }
                allowed_paths[allowed_count++] = optarg;
                break;
            case 'j':
                json = 1;
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (optind < argc)
    {
        uri = argv[optind++];
    }
    if (optind < argc)
    {
        usage(argv[0]);
        return 1;
    }

    if (NULL == uri || allowed_count == 0)
    {
        usage(argv[0]);
        return 1;
    }

    if (tp_context_init(&ctx) < 0)
    {
        fprintf(stderr, "Failed to init context: %s\n", aeron_errmsg());
        return 1;
    }
    tp_context_set_allowed_paths(&ctx, allowed_paths, allowed_count);
    if (tp_context_finalize_allowed_paths(&ctx) < 0)
    {
        fprintf(stderr, "Failed to finalize allowlist: %s\n", aeron_errmsg());
        tp_context_clear_allowed_paths(&ctx);
        return 1;
    }

    if (tp_shm_map(&region, uri, 0, &ctx.allowed_paths, NULL) < 0)
    {
        fprintf(stderr, "Failed to map: %s\n", aeron_errmsg());
        tp_context_clear_allowed_paths(&ctx);
        return 1;
    }

    tensor_pool_shmRegionSuperblock_wrap_for_decode(
        &block,
        (char *)region.addr,
        0,
        tensor_pool_shmRegionSuperblock_sbe_block_length(),
        tensor_pool_shmRegionSuperblock_sbe_schema_version(),
        region.length);

    if (json)
    {
        enum tensor_pool_regionType region_type;
        int region_ok = tensor_pool_shmRegionSuperblock_regionType(&block, &region_type);
        printf("{\"magic\":\"0x%" PRIx64 "\",\"layout_version\":%" PRIu32 ",\"epoch\":%" PRIu64 ",\"stream_id\":%" PRIu32 ",\"region_type\":",
            tensor_pool_shmRegionSuperblock_magic(&block),
            tensor_pool_shmRegionSuperblock_layoutVersion(&block),
            tensor_pool_shmRegionSuperblock_epoch(&block),
            tensor_pool_shmRegionSuperblock_streamId(&block));
        if (region_ok)
        {
            printf("%d", (int)region_type);
        }
        else
        {
            printf("\"INVALID\"");
        }
        printf(",\"pool_id\":%" PRIu16 ",\"nslots\":%" PRIu32 ",\"slot_bytes\":%" PRIu32 ",\"stride_bytes\":%" PRIu32 ",\"pid\":%" PRIu64 ",\"start_timestamp_ns\":%" PRIu64 ",\"activity_timestamp_ns\":%" PRIu64 "}\n",
            tensor_pool_shmRegionSuperblock_poolId(&block),
            tensor_pool_shmRegionSuperblock_nslots(&block),
            tensor_pool_shmRegionSuperblock_slotBytes(&block),
            tensor_pool_shmRegionSuperblock_strideBytes(&block),
            tensor_pool_shmRegionSuperblock_pid(&block),
            tensor_pool_shmRegionSuperblock_startTimestampNs(&block),
            tensor_pool_shmRegionSuperblock_activityTimestampNs(&block));
    }
    else
    {
        printf("magic=0x%" PRIx64 "\n", tensor_pool_shmRegionSuperblock_magic(&block));
        printf("layout_version=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_layoutVersion(&block));
        printf("epoch=%" PRIu64 "\n", tensor_pool_shmRegionSuperblock_epoch(&block));
        printf("stream_id=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_streamId(&block));
        {
            enum tensor_pool_regionType region_type;
            if (tensor_pool_shmRegionSuperblock_regionType(&block, &region_type))
            {
                printf("region_type=%d\n", (int)region_type);
            }
            else
            {
                printf("region_type=INVALID\n");
            }
        }
        printf("pool_id=%" PRIu16 "\n", tensor_pool_shmRegionSuperblock_poolId(&block));
        printf("nslots=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_nslots(&block));
        printf("slot_bytes=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_slotBytes(&block));
        printf("stride_bytes=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_strideBytes(&block));
        printf("pid=%" PRIu64 "\n", tensor_pool_shmRegionSuperblock_pid(&block));
        printf("start_timestamp_ns=%" PRIu64 "\n", tensor_pool_shmRegionSuperblock_startTimestampNs(&block));
        printf("activity_timestamp_ns=%" PRIu64 "\n", tensor_pool_shmRegionSuperblock_activityTimestampNs(&block));
    }

    tp_shm_unmap(&region, NULL);
    tp_context_clear_allowed_paths(&ctx);

    return 0;
}
