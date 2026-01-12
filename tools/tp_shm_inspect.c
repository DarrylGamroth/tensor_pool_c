#include "tensor_pool/tp_shm.h"

#include "wire/tensor_pool/shmRegionSuperblock.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "util/aeron_error.h"

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s [--json] <shm-uri>\n", name);
}

int main(int argc, char **argv)
{
    tp_shm_region_t region;
    struct tensor_pool_shmRegionSuperblock block;
    const char *uri = NULL;
    int json = 0;

    if (argc == 2)
    {
        uri = argv[1];
    }
    else if (argc == 3 && strcmp(argv[1], "--json") == 0)
    {
        json = 1;
        uri = argv[2];
    }
    else
    {
        usage(argv[0]);
        return 1;
    }

    if (tp_shm_map(&region, uri, 0, NULL, NULL) < 0)
    {
        fprintf(stderr, "Failed to map: %s\n", aeron_errmsg());
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

    return 0;
}
