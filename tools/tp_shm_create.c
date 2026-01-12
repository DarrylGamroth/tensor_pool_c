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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s <path> <region> <stream_id> <epoch> <pool_id> <nslots> <stride_bytes> <layout_version>\n"
        "  region: header | pool\n",
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

    if (argc != 9)
    {
        usage(argv[0]);
        return 1;
    }

    path = argv[1];
    if (parse_region(argv[2], &region_type) < 0)
    {
        fprintf(stderr, "Invalid region type: %s\n", argv[2]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    epoch = (uint64_t)strtoull(argv[4], NULL, 10);
    pool_id = (uint16_t)strtoul(argv[5], NULL, 10);
    nslots = (uint32_t)strtoul(argv[6], NULL, 10);
    stride_bytes = (uint32_t)strtoul(argv[7], NULL, 10);
    layout_version = (uint32_t)strtoul(argv[8], NULL, 10);

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
