#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/internal/tp_consumer_internal.h"
#include "tensor_pool/internal/tp_producer_internal.h"
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

#ifdef TP_TEST_EMBEDDED_DRIVER
#include "aeronmd.h"
#include "aeronc.h"
#include "aeron_common.h"
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void tp_test_decode_consumer_hello(void);
void tp_test_decode_consumer_config(void);
void tp_test_decode_data_source_meta(void);
void tp_test_decode_control_response(void);
void tp_test_decode_meta_blobs(void);
void tp_test_decode_shm_pool_announce(void);
void tp_test_control_listen_json(void);
void tp_test_agent_runner(void);
void tp_test_accessors(void);
void tp_test_consumer_registry(void);
void tp_test_aeron_client(void);
void tp_test_client_errors(void);
void tp_test_client_context_setters(void);
void tp_test_driver_client_decoders(void);
void tp_test_driver_client_attach_detach_live(void);
void tp_test_driver_config(void);
void tp_test_driver_discovery_integration(void);
void tp_test_driver_exclusive_producer(void);
void tp_test_driver_publish_mode_hugepages(void);
void tp_test_driver_node_id_cooldown(void);
void tp_test_driver_config_matrix(void);
void tp_test_driver_lease_expiry(void);
void tp_test_discovery_service(void);
void tp_test_supervisor(void);
void tp_test_discovery_client_decoders(void);
void tp_test_discovery_client_live(void);
void tp_test_merge_map(void);
void tp_test_qos_poller(void);
void tp_test_metadata_poller(void);
void tp_test_join_barrier(void);
void tp_test_tracelink(void);
void tp_test_client_conductor_lifecycle(void);
void tp_test_control_poller(void);
void tp_test_control_poller_driver(void);
void tp_test_control_poller_errors(void);
void tp_test_tensor_header_validate_extra(void);
void tp_test_pollers(void);
void tp_test_log(void);
void tp_test_consumer_errors(void);
void tp_test_producer_errors(void);
void tp_test_cadence(void);
void tp_test_shm_announce_freshness(void);
void tp_test_rate_limit(void);
void tp_test_qos_liveness(void);
void tp_test_epoch_regression(void);
void tp_test_meta_blob_ordering(void);
void tp_test_activity_liveness(void);
void tp_test_pid_liveness(void);
void tp_test_consumer_fallback_state(void);
void tp_test_consumer_fallback_recover(void);
void tp_test_consumer_fallback_invalid_announce(void);
void tp_test_consumer_fallback_layout_version(void);
void tp_test_qos_drop_counts(void);
void tp_test_epoch_remap(void);
void tp_test_progress_per_consumer_control(void);
void tp_test_progress_layout_validation(void);
void tp_test_progress_regression_rejected(void);
void tp_test_progress_poller_misc(void);
void tp_test_progress_poller_monotonic_capacity(void);
void tp_test_producer_claim_lifecycle(void);
void tp_test_shm_roundtrip(void);
void tp_test_rollover(void);
void tp_test_shm_security(void);
void tp_test_consumer_lease_revoked(void);
void tp_test_producer_lease_revoked(void);

#ifdef TP_TEST_EMBEDDED_DRIVER
typedef struct tp_test_driver_state_stct
{
    aeron_driver_context_t *context;
    aeron_driver_t *driver;
    char dir[AERON_MAX_PATH];
    char prev_dir[AERON_MAX_PATH];
    int has_prev;
}
tp_test_driver_state_t;

static tp_test_driver_state_t tp_test_driver_state;

static void tp_test_stop_embedded_driver(void)
{
    tp_test_driver_state_t *state = &tp_test_driver_state;

    if (state->driver)
    {
        aeron_driver_close(state->driver);
        state->driver = NULL;
    }
    if (state->context)
    {
        aeron_driver_context_close(state->context);
        state->context = NULL;
    }
    if (state->has_prev)
    {
        setenv("AERON_DIR", state->prev_dir, 1);
    }
    else
    {
        unsetenv("AERON_DIR");
    }
    if (state->dir[0] != '\0')
    {
        aeron_delete_directory(state->dir);
        state->dir[0] = '\0';
    }
}

static int tp_test_wait_for_driver(const char *dir)
{
    aeron_cnc_t *cnc = NULL;

    if (aeron_cnc_init(&cnc, dir, 1000) < 0)
    {
        return -1;
    }
    aeron_cnc_close(cnc);
    return 0;
}

static int tp_test_start_embedded_driver(void)
{
    tp_test_driver_state_t *state = &tp_test_driver_state;
    char dir_template[] = "/tmp/tp_aeron_test_XXXXXX";
    const char *prev = getenv("AERON_DIR");
    char *dir = NULL;

    memset(state, 0, sizeof(*state));

    dir = mkdtemp(dir_template);
    if (NULL == dir)
    {
        return -1;
    }
    strncpy(state->dir, dir, sizeof(state->dir) - 1);

    if (prev && prev[0] != '\0')
    {
        strncpy(state->prev_dir, prev, sizeof(state->prev_dir) - 1);
        state->has_prev = 1;
    }
    if (setenv("AERON_DIR", state->dir, 1) != 0)
    {
        tp_test_stop_embedded_driver();
        return -1;
    }

    if (aeron_driver_context_init(&state->context) < 0)
    {
        tp_test_stop_embedded_driver();
        return -1;
    }
    if (aeron_driver_context_set_dir(state->context, state->dir) < 0)
    {
        tp_test_stop_embedded_driver();
        return -1;
    }
    if (aeron_driver_context_set_dir_delete_on_start(state->context, true) < 0 ||
        aeron_driver_context_set_dir_delete_on_shutdown(state->context, true) < 0 ||
        aeron_driver_context_set_threading_mode(state->context, AERON_THREADING_MODE_SHARED) < 0)
    {
        tp_test_stop_embedded_driver();
        return -1;
    }
    if (aeron_driver_init(&state->driver, state->context) < 0)
    {
        tp_test_stop_embedded_driver();
        return -1;
    }
    if (aeron_driver_start(state->driver, false) < 0)
    {
        tp_test_stop_embedded_driver();
        return -1;
    }
    if (tp_test_wait_for_driver(state->dir) < 0)
    {
        tp_test_stop_embedded_driver();
        return -1;
    }

    atexit(tp_test_stop_embedded_driver);
    return 0;
}
#endif

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

static void test_shm_stride_alignment(void)
{
    assert(tp_shm_validate_stride_alignment("shm:file?path=/tmp", 128, NULL) == 0);
    assert(tp_shm_validate_stride_alignment("shm:file?path=/tmp", 192, NULL) == 0);
    assert(tp_shm_validate_stride_alignment("shm:file?path=/tmp", 96, NULL) < 0);
    assert(tp_shm_validate_stride_alignment("shm:file?path=/tmp", 32, NULL) < 0);
    assert(tp_shm_validate_stride_alignment("shm:file?path=/tmp|require_hugepages=true", 128, NULL) < 0);
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
    tensor_pool_shmRegionSuperblock_set_activityTimestampNs(&block, (uint64_t)tp_clock_now_ns());

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
    tp_context_t *ctx = NULL;
    size_t file_size = TP_SUPERBLOCK_SIZE_BYTES + (TP_HEADER_SLOT_BYTES * 4);
    int result = -1;

    region.fd = -1;
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
    tp_context_set_allowed_paths(ctx, allowed_paths, 1);
    if (tp_context_finalize_allowed_paths(ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_shm_map(&region, uri, 1, tp_context_allowed_paths(ctx), NULL) != 0)
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
    if (NULL != ctx)
    {
        tp_context_close(ctx);
    }
    if (fd >= 0)
    {
        tp_shm_unmap(&region, NULL);
        close(fd);
        unlink(path_template);
    }

    assert(result == 0);
}

static void test_shm_superblock_fail_closed(void)
{
    char path_template[] = "/tmp/tp_shm_badXXXXXX";
    int fd = mkstemp(path_template);
    tp_shm_region_t region = { 0 };
    tp_shm_expected_t expected;
    struct tensor_pool_shmRegionSuperblock block;
    char uri[512];
    const char *allowed_paths[1];
    tp_context_t *ctx = NULL;
    size_t file_size = TP_SUPERBLOCK_SIZE_BYTES + (TP_HEADER_SLOT_BYTES * 4);
    int result = -1;

    region.fd = -1;
    if (fd < 0)
    {
        goto cleanup;
    }

    if (ftruncate(fd, (off_t)file_size) != 0)
    {
        goto cleanup;
    }

    write_superblock(fd, 101, 7, tensor_pool_regionType_HEADER_RING, 0, 4, TP_HEADER_SLOT_BYTES, 0);

    snprintf(uri, sizeof(uri), "shm:file?path=%s", path_template);
    allowed_paths[0] = "/tmp";
    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    tp_context_set_allowed_paths(ctx, allowed_paths, 1);
    if (tp_context_finalize_allowed_paths(ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_shm_map(&region, uri, 1, tp_context_allowed_paths(ctx), NULL) != 0)
    {
        goto cleanup;
    }

    memset(&expected, 0, sizeof(expected));
    expected.stream_id = 101;
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

    tensor_pool_shmRegionSuperblock_wrap_for_encode(&block, (char *)region.addr, 0, TP_SUPERBLOCK_SIZE_BYTES);

    tensor_pool_shmRegionSuperblock_set_magic(&block, 0);
    if (tp_shm_validate_superblock(&region, &expected, NULL) == 0)
    {
        goto cleanup;
    }
    tensor_pool_shmRegionSuperblock_set_magic(&block, TP_MAGIC_U64);

    expected.layout_version = 2;
    if (tp_shm_validate_superblock(&region, &expected, NULL) == 0)
    {
        goto cleanup;
    }
    expected.layout_version = 1;

    expected.region_type = tensor_pool_regionType_PAYLOAD_POOL;
    if (tp_shm_validate_superblock(&region, &expected, NULL) == 0)
    {
        goto cleanup;
    }
    expected.region_type = tensor_pool_regionType_HEADER_RING;

    expected.nslots = 8;
    if (tp_shm_validate_superblock(&region, &expected, NULL) == 0)
    {
        goto cleanup;
    }
    expected.nslots = 4;

    tensor_pool_shmRegionSuperblock_set_slotBytes(&block, TP_HEADER_SLOT_BYTES + 4);
    if (tp_shm_validate_superblock(&region, &expected, NULL) == 0)
    {
        goto cleanup;
    }
    tensor_pool_shmRegionSuperblock_set_slotBytes(&block, TP_HEADER_SLOT_BYTES);

    result = 0;

cleanup:
    if (NULL != ctx)
    {
        tp_context_close(ctx);
    }
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

    memset(&header, 0, sizeof(header));
    header.dtype = tensor_pool_dtype_FLOAT32;
    header.major_order = tensor_pool_majorOrder_ROW;
    header.ndims = 2;
    header.progress_unit = tensor_pool_progressUnit_NONE;
    header.dims[0] = -1;
    header.dims[1] = 3;

    assert(tp_tensor_header_validate(&header, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.dtype = tensor_pool_dtype_FLOAT32;
    header.major_order = tensor_pool_majorOrder_ROW;
    header.ndims = 2;
    header.progress_unit = tensor_pool_progressUnit_ROWS;
    header.progress_stride_bytes = 8;
    header.dims[0] = 2;
    header.dims[1] = 3;

    assert(tp_tensor_header_validate(&header, NULL) < 0);

    memset(&header, 0, sizeof(header));
    header.dtype = tensor_pool_dtype_FLOAT32;
    header.major_order = tensor_pool_majorOrder_ROW;
    header.ndims = 0;
    header.progress_unit = tensor_pool_progressUnit_NONE;

    assert(tp_tensor_header_validate(&header, NULL) < 0);

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

static void test_driver_attach_rejects_manual_config(void)
{
    tp_producer_t producer;
    tp_producer_config_t producer_cfg;
    tp_consumer_t consumer;
    tp_consumer_config_t consumer_cfg;

    memset(&producer, 0, sizeof(producer));
    memset(&producer_cfg, 0, sizeof(producer_cfg));
    producer.context.use_driver = true;
    assert( tp_producer_attach(&producer, &producer_cfg) < 0);

    memset(&consumer, 0, sizeof(consumer));
    memset(&consumer_cfg, 0, sizeof(consumer_cfg));
    consumer.context.use_driver = true;
    assert( tp_consumer_attach(&consumer, &consumer_cfg) < 0);
}

int main(void)
{
#ifdef TP_TEST_EMBEDDED_DRIVER
    if (tp_test_start_embedded_driver() < 0)
    {
        fprintf(stderr, "Failed to start embedded Aeron driver\n");
        return 1;
    }
#endif

    test_version();
    test_seqlock();
    test_uri_parse();
    test_shm_stride_alignment();
    test_shm_superblock();
    test_shm_superblock_fail_closed();
    test_tensor_header();
    tp_test_tensor_header_validate_extra();
    test_driver_attach_rejects_manual_config();
    tp_test_decode_consumer_hello();
    tp_test_decode_consumer_config();
    tp_test_decode_data_source_meta();
    tp_test_decode_control_response();
    tp_test_decode_meta_blobs();
    tp_test_decode_shm_pool_announce();
    tp_test_control_listen_json();
    tp_test_agent_runner();
    tp_test_accessors();
    tp_test_consumer_registry();
    tp_test_aeron_client();
    tp_test_client_errors();
    tp_test_client_context_setters();
    tp_test_driver_client_decoders();
    tp_test_driver_client_attach_detach_live();
    tp_test_driver_config();
    tp_test_driver_discovery_integration();
    tp_test_driver_exclusive_producer();
    tp_test_driver_publish_mode_hugepages();
    tp_test_driver_node_id_cooldown();
    tp_test_driver_config_matrix();
    tp_test_driver_lease_expiry();
    tp_test_discovery_service();
    tp_test_supervisor();
    tp_test_discovery_client_decoders();
    tp_test_discovery_client_live();
    tp_test_merge_map();
    tp_test_qos_poller();
    tp_test_metadata_poller();
    tp_test_join_barrier();
    tp_test_tracelink();
    tp_test_client_conductor_lifecycle();
    tp_test_control_poller();
    tp_test_control_poller_driver();
    tp_test_control_poller_errors();
    tp_test_pollers();
    tp_test_log();
    tp_test_consumer_errors();
    tp_test_producer_errors();
    tp_test_cadence();
    tp_test_shm_announce_freshness();
    tp_test_rate_limit();
    tp_test_qos_liveness();
    tp_test_qos_drop_counts();
    tp_test_epoch_regression();
    tp_test_epoch_remap();
    tp_test_meta_blob_ordering();
    tp_test_activity_liveness();
    tp_test_pid_liveness();
    tp_test_consumer_fallback_state();
    tp_test_consumer_fallback_recover();
    tp_test_consumer_fallback_invalid_announce();
    tp_test_consumer_fallback_layout_version();
    tp_test_progress_per_consumer_control();
    tp_test_progress_layout_validation();
    tp_test_progress_regression_rejected();
    tp_test_progress_poller_misc();
    tp_test_progress_poller_monotonic_capacity();
    tp_test_producer_claim_lifecycle();
    tp_test_shm_roundtrip();
    tp_test_rollover();
    tp_test_shm_security();
    tp_test_consumer_lease_revoked();
    tp_test_producer_lease_revoked();

    return 0;
}
