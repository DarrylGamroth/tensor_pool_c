#include "tensor_pool/tp_shm.h"

#include "wire/tensor_pool/shmRegionSuperblock.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "aeron_error.h"

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s <shm-uri>\n", name);
}

int main(int argc, char **argv)
{
    tp_shm_region_t region;
    struct tensor_pool_shmRegionSuperblock block;

    if (argc != 2)
    {
        usage(argv[0]);
        return 1;
    }

    if (tp_shm_map(&region, argv[1], 0, NULL, NULL) < 0)
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

    printf("magic=0x%" PRIx64 "\n", tensor_pool_shmRegionSuperblock_magic(&block));
    printf("layout_version=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_layoutVersion(&block));
    printf("epoch=%" PRIu64 "\n", tensor_pool_shmRegionSuperblock_epoch(&block));
    printf("stream_id=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_streamId(&block));
    printf("region_type=%d\n", tensor_pool_shmRegionSuperblock_regionType(&block));
    printf("pool_id=%" PRIu16 "\n", tensor_pool_shmRegionSuperblock_poolId(&block));
    printf("nslots=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_nslots(&block));
    printf("slot_bytes=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_slotBytes(&block));
    printf("stride_bytes=%" PRIu32 "\n", tensor_pool_shmRegionSuperblock_strideBytes(&block));
    printf("pid=%" PRIu64 "\n", tensor_pool_shmRegionSuperblock_pid(&block));
    printf("start_timestamp_ns=%" PRIu64 "\n", tensor_pool_shmRegionSuperblock_startTimestampNs(&block));
    printf("activity_timestamp_ns=%" PRIu64 "\n", tensor_pool_shmRegionSuperblock_activityTimestampNs(&block));

    tp_shm_unmap(&region, NULL);

    return 0;
}
