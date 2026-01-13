#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_shm.h"
#include "tensor_pool/tp_tensor.h"
#include "tensor_pool/tp_uri.h"
#include "tensor_pool/tp_version.h"

#include "wire/tensor_pool/shmRegionSuperblock.h"
#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/dtype.h"
#include "wire/tensor_pool/majorOrder.h"
#include "wire/tensor_pool/progressUnit.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/tensorHeader.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void tp_test_decode_consumer_hello(void);
void tp_test_decode_data_source_meta(void);
void tp_test_decode_control_response(void);
void tp_test_decode_meta_blobs(void);
void tp_test_decode_shm_pool_announce(void);
void tp_test_consumer_registry(void);
void tp_test_driver_client_decoders(void);
void tp_test_discovery_client_decoders(void);
void tp_test_client_conductor_lifecycle(void);
void tp_test_pollers(void);
void tp_test_cadence(void);
void tp_test_shm_announce_freshness(void);
void tp_test_rate_limit(void);
void tp_test_progress_layout_validation(void);
void tp_test_producer_claim_lifecycle(void);
void tp_test_rollover(void);
void tp_test_shm_security(void);

static void test_version(void)
{
    uint32_t version = tp_version_compose(TP_VERSION_MAJOR, TP_VERSION_MINOR, TP_VERSION_PATCH);
    assert(version == 0x000100);
}

static void test_seqlock(void)
{
    uint64_t seq = 42;
    uint64_t in_progress = tp_seq_in_progress(seq);
    uint64_t committed = tp_seq_committed(seq);

    assert(tp_seq_is_committed(in_progress) == false);
    assert(tp_seq_is_committed(committed) == true);
    assert(tp_seq_value(committed) == seq);
}

static void test_uri_parse(void)
{
    tp_shm_uri_t uri;

    assert(tp_shm_uri_parse(&uri, "shm:file?path=/dev/shm/test|require_hugepages=false", NULL) == 0);
    assert(strcmp(uri.path, "/dev/shm/test") == 0);
    assert(uri.require_hugepages == false);

    assert(tp_shm_uri_parse(&uri, "shm:file?path=/dev/shm/test", NULL) == 0);
    assert(uri.require_hugepages == false);

    assert(tp_shm_uri_parse(&uri, "shm:file?require_hugepages=true", NULL) < 0);
    assert(tp_shm_uri_parse(&uri, "shm:mem?path=/dev/shm/test", NULL) < 0);
}

static void write_superblock(int fd, uint32_t stream_id, uint64_t epoch, int16_t region_type, uint16_t pool_id, uint32_t nslots, uint32_t slot_bytes, uint32_t stride_bytes)
{
    uint8_t buffer[TP_SUPERBLOCK_SIZE_BYTES];
    struct tensor_pool_shmRegionSuperblock block;

    memset(buffer, 0, sizeof(buffer));

    tensor_pool_shmRegionSuperblock_wrap_for_encode(&block, (char *)buffer, 0, sizeof(buffer));
    tensor_pool_shmRegionSuperblock_set_magic(&block, TP_MAGIC_U64);
    tensor_pool_shmRegionSuperblock_set_layoutVersion(&block, 1);
    tensor_pool_shmRegionSuperblock_set_epoch(&block, epoch);
    tensor_pool_shmRegionSuperblock_set_streamId(&block, stream_id);
    tensor_pool_shmRegionSuperblock_set_regionType(&block, region_type);
    tensor_pool_shmRegionSuperblock_set_poolId(&block, pool_id);
    tensor_pool_shmRegionSuperblock_set_nslots(&block, nslots);
    tensor_pool_shmRegionSuperblock_set_slotBytes(&block, slot_bytes);
    tensor_pool_shmRegionSuperblock_set_strideBytes(&block, stride_bytes);
    tensor_pool_shmRegionSuperblock_set_pid(&block, (uint64_t)getpid());
    tensor_pool_shmRegionSuperblock_set_startTimestampNs(&block, 1);
    tensor_pool_shmRegionSuperblock_set_activityTimestampNs(&block, 2);

    assert(pwrite(fd, buffer, sizeof(buffer), 0) == (ssize_t)sizeof(buffer));
}

static void test_shm_superblock(void)
{
    char path_template[] = "/tmp/tp_shm_testXXXXXX";
    int fd = mkstemp(path_template);
    tp_shm_region_t region = { 0 };
    tp_shm_expected_t expected;
    char uri[512];
    const char *allowed_paths[1];
    tp_context_t ctx;
    size_t file_size = TP_SUPERBLOCK_SIZE_BYTES + (TP_HEADER_SLOT_BYTES * 4);
    int result = -1;

    if (fd < 0)
    {
        goto cleanup;
    }

    if (ftruncate(fd, (off_t)file_size) != 0)
    {
        goto cleanup;
    }

    write_superblock(fd, 100, 7, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);

    snprintf(uri, sizeof(uri), "shm:file?path=%s", path_template);
    allowed_paths[0] = "/tmp";
    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    tp_context_set_allowed_paths(&ctx, allowed_paths, 1);
    if (tp_context_finalize_allowed_paths(&ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_shm_map(&region, uri, 0, &ctx.allowed_paths, NULL) != 0)
    {
        goto cleanup;
    }

    memset(&expected, 0, sizeof(expected));
    expected.stream_id = 100;
    expected.layout_version = 1;
    expected.epoch = 7;
    expected.region_type = tensor_pool_regionType_HEADER_RING;
    expected.pool_id = 0;
    expected.nslots = 4;
    expected.slot_bytes = TP_HEADER_SLOT_BYTES;
    expected.stride_bytes = TP_NULL_U32;

    if (tp_shm_validate_superblock(&region, &expected, NULL) != 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_context_clear_allowed_paths(&ctx);
    if (fd >= 0)
    {
        tp_shm_unmap(&region, NULL);
        close(fd);
        unlink(path_template);
    }

    assert(result == 0);
}

static void test_tensor_header(void)
{
    tp_tensor_header_t header;
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_tensorHeader tensor_header;

    memset(&header, 0, sizeof(header));
    header.dtype = tensor_pool_dtype_FLOAT32;
    header.major_order = tensor_pool_majorOrder_ROW;
    header.ndims = 2;
    header.progress_unit = tensor_pool_progressUnit_NONE;
    header.dims[0] = 2;
    header.dims[1] = 3;

    assert(tp_tensor_header_validate(&header, NULL) == 0);
    assert(header.strides[1] == 4);
    assert(header.strides[0] == 12);

    memset(&header, 0, sizeof(header));
    header.dtype = tensor_pool_dtype_FLOAT32;
    header.major_order = tensor_pool_majorOrder_ROW;
    header.ndims = 2;
    header.progress_unit = tensor_pool_progressUnit_NONE;
    header.dims[0] = 0;
    header.dims[1] = 3;

    assert(tp_tensor_header_validate(&header, NULL) == 0);

    memset(buffer, 0, sizeof(buffer));
    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_tensorHeader_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_tensorHeader_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_tensorHeader_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version());
    tensor_pool_tensorHeader_wrap_for_encode(
        &tensor_header,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(buffer));
    tensor_pool_tensorHeader_set_dtype(&tensor_header, tensor_pool_dtype_FLOAT32);
    tensor_pool_tensorHeader_set_majorOrder(&tensor_header, tensor_pool_majorOrder_ROW);
    tensor_pool_tensorHeader_set_ndims(&tensor_header, 1);
    tensor_pool_tensorHeader_set_padAlign(&tensor_header, 0);
    tensor_pool_tensorHeader_set_progressUnit(&tensor_header, tensor_pool_progressUnit_NONE);
    tensor_pool_tensorHeader_set_progressStrideBytes(&tensor_header, 0);
    tensor_pool_tensorHeader_set_dims(&tensor_header, 0, 1);
    tensor_pool_tensorHeader_set_strides(&tensor_header, 0, 4);

    assert(tp_tensor_header_decode(&header, buffer, sizeof(buffer), NULL) == 0);

    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version() + 1);
    assert(tp_tensor_header_decode(&header, buffer, sizeof(buffer), NULL) < 0);

    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version());
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_tensorHeader_sbe_block_length() - 1);
    assert(tp_tensor_header_decode(&header, buffer, sizeof(buffer), NULL) < 0);
}

int main(void)
{
    test_version();
    test_seqlock();
    test_uri_parse();
    test_shm_superblock();
    test_tensor_header();
    tp_test_decode_consumer_hello();
    tp_test_decode_data_source_meta();
    tp_test_decode_control_response();
    tp_test_decode_meta_blobs();
    tp_test_decode_shm_pool_announce();
    tp_test_consumer_registry();
    tp_test_driver_client_decoders();
    tp_test_discovery_client_decoders();
    tp_test_client_conductor_lifecycle();
    tp_test_pollers();
    tp_test_cadence();
    tp_test_shm_announce_freshness();
    tp_test_rate_limit();
    tp_test_progress_layout_validation();
    tp_test_producer_claim_lifecycle();
    tp_test_rollover();
    tp_test_shm_security();

    return 0;
}
