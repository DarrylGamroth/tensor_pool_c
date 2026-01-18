#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_types.h"

#include "driver/tensor_pool/messageHeader.h"
#include "driver/tensor_pool/shmAttachResponse.h"
#include "driver/tensor_pool/shmDetachResponse.h"
#include "driver/tensor_pool/shmLeaseRevoked.h"
#include "driver/tensor_pool/shmDriverShutdown.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/leaseRevokeReason.h"
#include "driver/tensor_pool/shutdownReason.h"
#include "driver/tensor_pool/role.h"

#include <assert.h>
#include <string.h>

static void test_decode_attach_response_valid(void)
{
    uint8_t buffer[1024];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 101);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmAttachResponse_set_leaseId(&response, 10);
    tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(&response, 20);
    tensor_pool_shmAttachResponse_set_streamId(&response, 30);
    tensor_pool_shmAttachResponse_set_epoch(&response, 40);
    tensor_pool_shmAttachResponse_set_layoutVersion(&response, 1);
    tensor_pool_shmAttachResponse_set_headerNslots(&response, 16);
    tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, TP_HEADER_SLOT_BYTES);

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
        tensor_pool_shmAttachResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
    {
        goto cleanup;
    }
    tensor_pool_shmAttachResponse_payloadPools_set_poolId(&pools, 2);
    tensor_pool_shmAttachResponse_payloadPools_set_poolNslots(&pools, 16);
    tensor_pool_shmAttachResponse_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_shmAttachResponse_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);
    tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "shm:file?path=/dev/shm/hdr", 26);

    if (tp_driver_decode_attach_response(buffer, sizeof(buffer), 101, &info) != 0)
    {
        goto cleanup;
    }

    assert(info.code == tensor_pool_responseCode_OK);
    assert(info.pool_count == 1);
    assert(info.header_slot_bytes == TP_HEADER_SLOT_BYTES);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_invalid_slot_bytes(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 202);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmAttachResponse_set_leaseId(&response, 10);
    tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(&response, 20);
    tensor_pool_shmAttachResponse_set_streamId(&response, 30);
    tensor_pool_shmAttachResponse_set_epoch(&response, 40);
    tensor_pool_shmAttachResponse_set_layoutVersion(&response, 1);
    tensor_pool_shmAttachResponse_set_headerNslots(&response, 16);
    tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, 128);
    {
        struct tensor_pool_shmAttachResponse_payloadPools pools;

        if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
            &pools,
            (char *)buffer,
            1,
            tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
            tensor_pool_shmAttachResponse_sbe_schema_version(),
            sizeof(buffer)))
        {
            goto cleanup;
        }

        if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
        {
            goto cleanup;
        }
        tensor_pool_shmAttachResponse_payloadPools_set_poolId(&pools, 2);
        tensor_pool_shmAttachResponse_payloadPools_set_poolNslots(&pools, 16);
        tensor_pool_shmAttachResponse_payloadPools_set_strideBytes(&pools, 4096);
        tensor_pool_shmAttachResponse_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);
    }

    tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "shm:file?path=/dev/shm/hdr", 26);

    if (tp_driver_decode_attach_response(buffer, sizeof(buffer), 202, &info) != 0)
    {
        goto cleanup;
    }

    assert(info.code == tensor_pool_responseCode_INVALID_PARAMS);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_version_mismatch(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version() + 1);

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 404);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_OK);

    assert(tp_driver_decode_attach_response(buffer, sizeof(buffer), 404, &info) < 0);
    result = 0;

    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_invalid_code(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 405);
    tensor_pool_shmAttachResponse_set_code(&response, (enum tensor_pool_responseCode)99);

    assert(tp_driver_decode_attach_response(buffer, sizeof(buffer), 405, &info) < 0);
    result = 0;

    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_null_expiry(void)
{
    uint8_t buffer[1024];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 303);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmAttachResponse_set_leaseId(&response, 10);
    tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(
        &response,
        tensor_pool_shmAttachResponse_leaseExpiryTimestampNs_null_value());
    tensor_pool_shmAttachResponse_set_streamId(&response, 30);
    tensor_pool_shmAttachResponse_set_epoch(&response, 40);
    tensor_pool_shmAttachResponse_set_layoutVersion(&response, 1);
    tensor_pool_shmAttachResponse_set_headerNslots(&response, 16);
    tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, TP_HEADER_SLOT_BYTES);

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
        tensor_pool_shmAttachResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
    {
        goto cleanup;
    }
    tensor_pool_shmAttachResponse_payloadPools_set_poolId(&pools, 2);
    tensor_pool_shmAttachResponse_payloadPools_set_poolNslots(&pools, 16);
    tensor_pool_shmAttachResponse_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_shmAttachResponse_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);
    tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "shm:file?path=/dev/shm/hdr", 26);

    if (tp_driver_decode_attach_response(buffer, sizeof(buffer), 303, &info) != 0)
    {
        goto cleanup;
    }

    assert(info.code == tensor_pool_responseCode_OK);
    assert(info.lease_expiry_timestamp_ns == TP_NULL_U64);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_missing_required_fields(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 304);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmAttachResponse_set_leaseId(&response, tensor_pool_shmAttachResponse_leaseId_null_value());
    tensor_pool_shmAttachResponse_set_streamId(&response, 30);
    tensor_pool_shmAttachResponse_set_epoch(&response, 40);
    tensor_pool_shmAttachResponse_set_layoutVersion(&response, 1);
    tensor_pool_shmAttachResponse_set_headerNslots(&response, 16);
    tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, TP_HEADER_SLOT_BYTES);

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
        tensor_pool_shmAttachResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
    {
        goto cleanup;
    }
    tensor_pool_shmAttachResponse_payloadPools_set_poolId(&pools, 2);
    tensor_pool_shmAttachResponse_payloadPools_set_poolNslots(&pools, 16);
    tensor_pool_shmAttachResponse_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_shmAttachResponse_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);
    tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "shm:file?path=/dev/shm/hdr", 26);

    if (tp_driver_decode_attach_response(buffer, sizeof(buffer), 304, &info) != 0)
    {
        goto cleanup;
    }

    assert(info.code == tensor_pool_responseCode_INTERNAL_ERROR);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_missing_pools(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 305);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmAttachResponse_set_leaseId(&response, 10);
    tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(&response, 20);
    tensor_pool_shmAttachResponse_set_streamId(&response, 30);
    tensor_pool_shmAttachResponse_set_epoch(&response, 40);
    tensor_pool_shmAttachResponse_set_layoutVersion(&response, 1);
    tensor_pool_shmAttachResponse_set_headerNslots(&response, 16);
    tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, TP_HEADER_SLOT_BYTES);

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        0,
        tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
        tensor_pool_shmAttachResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "shm:file?path=/dev/shm/hdr", 26);

    if (tp_driver_decode_attach_response(buffer, sizeof(buffer), 305, &info) != 0)
    {
        goto cleanup;
    }

    assert(info.code == tensor_pool_responseCode_INVALID_PARAMS);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_pool_mismatch(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 306);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmAttachResponse_set_leaseId(&response, 10);
    tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(&response, 20);
    tensor_pool_shmAttachResponse_set_streamId(&response, 30);
    tensor_pool_shmAttachResponse_set_epoch(&response, 40);
    tensor_pool_shmAttachResponse_set_layoutVersion(&response, 1);
    tensor_pool_shmAttachResponse_set_headerNslots(&response, 16);
    tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, TP_HEADER_SLOT_BYTES);

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
        tensor_pool_shmAttachResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
    {
        goto cleanup;
    }
    tensor_pool_shmAttachResponse_payloadPools_set_poolId(&pools, 2);
    tensor_pool_shmAttachResponse_payloadPools_set_poolNslots(&pools, 8);
    tensor_pool_shmAttachResponse_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_shmAttachResponse_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);
    tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "shm:file?path=/dev/shm/hdr", 26);

    if (tp_driver_decode_attach_response(buffer, sizeof(buffer), 306, &info) != 0)
    {
        goto cleanup;
    }

    assert(info.code == tensor_pool_responseCode_INVALID_PARAMS);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_missing_header_uri(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 307);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmAttachResponse_set_leaseId(&response, 10);
    tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(&response, 20);
    tensor_pool_shmAttachResponse_set_streamId(&response, 30);
    tensor_pool_shmAttachResponse_set_epoch(&response, 40);
    tensor_pool_shmAttachResponse_set_layoutVersion(&response, 1);
    tensor_pool_shmAttachResponse_set_headerNslots(&response, 16);
    tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, TP_HEADER_SLOT_BYTES);

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
        tensor_pool_shmAttachResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
    {
        goto cleanup;
    }
    tensor_pool_shmAttachResponse_payloadPools_set_poolId(&pools, 2);
    tensor_pool_shmAttachResponse_payloadPools_set_poolNslots(&pools, 16);
    tensor_pool_shmAttachResponse_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_shmAttachResponse_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);
    tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "", 0);

    if (tp_driver_decode_attach_response(buffer, sizeof(buffer), 307, &info) != 0)
    {
        goto cleanup;
    }

    assert(info.code == tensor_pool_responseCode_INVALID_PARAMS);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_attach_response_rejected(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmAttachResponse_set_correlationId(&response, 308);
    tensor_pool_shmAttachResponse_set_code(&response, tensor_pool_responseCode_REJECTED);

    if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        0,
        tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
        tensor_pool_shmAttachResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "", 0);
    tensor_pool_shmAttachResponse_put_errorMessage(&response, "nope", 4);

    if (tp_driver_decode_attach_response(buffer, sizeof(buffer), 308, &info) != 0)
    {
        goto cleanup;
    }

    assert(info.code == tensor_pool_responseCode_REJECTED);
    assert(strstr(info.error_message, "nope") != NULL);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    assert(result == 0);
}

static void test_decode_detach_response(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmDetachResponse response;
    tp_driver_detach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmDetachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmDetachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmDetachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmDetachResponse_sbe_schema_version());

    tensor_pool_shmDetachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmDetachResponse_set_correlationId(&response, 55);
    tensor_pool_shmDetachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmDetachResponse_put_errorMessage(&response, "ok", 2);

    if (tp_driver_decode_detach_response(buffer, sizeof(buffer), &info) != 0)
    {
        goto cleanup;
    }

    assert(info.correlation_id == 55);
    assert(info.code == tensor_pool_responseCode_OK);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_detach_response_block_length_mismatch(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmDetachResponse response;
    tp_driver_detach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, 0);
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmDetachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmDetachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmDetachResponse_sbe_schema_version());

    tensor_pool_shmDetachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmDetachResponse_set_correlationId(&response, 56);
    tensor_pool_shmDetachResponse_set_code(&response, tensor_pool_responseCode_OK);

    assert(tp_driver_decode_detach_response(buffer, sizeof(buffer), &info) < 0);
    result = 0;

    assert(result == 0);
}

static void test_decode_lease_revoked(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmLeaseRevoked revoked;
    tp_driver_lease_revoked_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmLeaseRevoked_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmLeaseRevoked_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmLeaseRevoked_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmLeaseRevoked_sbe_schema_version());

    tensor_pool_shmLeaseRevoked_wrap_for_encode(&revoked, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmLeaseRevoked_set_timestampNs(&revoked, 99);
    tensor_pool_shmLeaseRevoked_set_leaseId(&revoked, 101);
    tensor_pool_shmLeaseRevoked_set_streamId(&revoked, 202);
    tensor_pool_shmLeaseRevoked_set_clientId(&revoked, 303);
    tensor_pool_shmLeaseRevoked_set_role(&revoked, tensor_pool_role_PRODUCER);
    tensor_pool_shmLeaseRevoked_set_reason(&revoked, tensor_pool_leaseRevokeReason_EXPIRED);
    tensor_pool_shmLeaseRevoked_put_errorMessage(&revoked, "expired", 7);

    if (tp_driver_decode_lease_revoked(buffer, sizeof(buffer), &info) != 0)
    {
        goto cleanup;
    }

    assert(info.timestamp_ns == 99);
    assert(info.stream_id == 202);
    assert(info.reason == tensor_pool_leaseRevokeReason_EXPIRED);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_detach_response_invalid_code(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmDetachResponse response;
    tp_driver_detach_info_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmDetachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmDetachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmDetachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmDetachResponse_sbe_schema_version());

    tensor_pool_shmDetachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmDetachResponse_set_correlationId(&response, 57);
    tensor_pool_shmDetachResponse_set_code(&response, (enum tensor_pool_responseCode)99);

    assert(tp_driver_decode_detach_response(buffer, sizeof(buffer), &info) < 0);
    result = 0;

    assert(result == 0);
}

static void test_decode_lease_revoked_invalid_role(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmLeaseRevoked revoked;
    tp_driver_lease_revoked_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmLeaseRevoked_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmLeaseRevoked_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmLeaseRevoked_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmLeaseRevoked_sbe_schema_version());

    tensor_pool_shmLeaseRevoked_wrap_for_encode(&revoked, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmLeaseRevoked_set_timestampNs(&revoked, 99);
    tensor_pool_shmLeaseRevoked_set_leaseId(&revoked, 101);
    tensor_pool_shmLeaseRevoked_set_streamId(&revoked, 202);
    tensor_pool_shmLeaseRevoked_set_clientId(&revoked, 303);
    tensor_pool_shmLeaseRevoked_set_role(&revoked, (enum tensor_pool_role)99);
    tensor_pool_shmLeaseRevoked_set_reason(&revoked, tensor_pool_leaseRevokeReason_EXPIRED);

    assert(tp_driver_decode_lease_revoked(buffer, sizeof(buffer), &info) < 0);
    result = 0;

    assert(result == 0);
}

static void test_decode_lease_revoked_invalid_reason(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmLeaseRevoked revoked;
    tp_driver_lease_revoked_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmLeaseRevoked_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmLeaseRevoked_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmLeaseRevoked_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmLeaseRevoked_sbe_schema_version());

    tensor_pool_shmLeaseRevoked_wrap_for_encode(&revoked, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmLeaseRevoked_set_timestampNs(&revoked, 99);
    tensor_pool_shmLeaseRevoked_set_leaseId(&revoked, 101);
    tensor_pool_shmLeaseRevoked_set_streamId(&revoked, 202);
    tensor_pool_shmLeaseRevoked_set_clientId(&revoked, 303);
    tensor_pool_shmLeaseRevoked_set_role(&revoked, tensor_pool_role_PRODUCER);
    tensor_pool_shmLeaseRevoked_set_reason(&revoked, (enum tensor_pool_leaseRevokeReason)99);

    assert(tp_driver_decode_lease_revoked(buffer, sizeof(buffer), &info) < 0);
    result = 0;

    assert(result == 0);
}

static void test_decode_shutdown(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmDriverShutdown shutdown;
    tp_driver_shutdown_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmDriverShutdown_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmDriverShutdown_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmDriverShutdown_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmDriverShutdown_sbe_schema_version());

    tensor_pool_shmDriverShutdown_wrap_for_encode(&shutdown, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmDriverShutdown_set_timestampNs(&shutdown, 500);
    tensor_pool_shmDriverShutdown_set_reason(&shutdown, tensor_pool_shutdownReason_ADMIN);
    tensor_pool_shmDriverShutdown_put_errorMessage(&shutdown, "admin", 5);

    if (tp_driver_decode_shutdown(buffer, sizeof(buffer), &info) != 0)
    {
        goto cleanup;
    }

    assert(info.timestamp_ns == 500);
    assert(info.reason == tensor_pool_shutdownReason_ADMIN);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_shutdown_invalid_reason(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmDriverShutdown shutdown;
    tp_driver_shutdown_t info;
    int result = -1;

    memset(&info, 0, sizeof(info));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmDriverShutdown_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmDriverShutdown_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmDriverShutdown_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmDriverShutdown_sbe_schema_version());

    tensor_pool_shmDriverShutdown_wrap_for_encode(&shutdown, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_shmDriverShutdown_set_timestampNs(&shutdown, 500);
    tensor_pool_shmDriverShutdown_set_reason(&shutdown, (enum tensor_pool_shutdownReason)99);

    assert(tp_driver_decode_shutdown(buffer, sizeof(buffer), &info) < 0);
    result = 0;

    assert(result == 0);
}

static void test_driver_keepalive_helpers(void)
{
    tp_driver_client_t client;
    int due = 0;
    int expired = 0;
    int result = -1;

    memset(&client, 0, sizeof(client));
    client.active_lease_id = 10;
    client.lease_expiry_timestamp_ns = 200;
    client.last_keepalive_ns = 50;

    due = tp_driver_client_keepalive_due(&client, 120, 50);
    if (due < 0)
    {
        goto cleanup;
    }
    assert(due == 1);

    expired = tp_driver_client_lease_expired(&client, 199);
    if (expired < 0)
    {
        goto cleanup;
    }
    assert(expired == 0);

    expired = tp_driver_client_lease_expired(&client, 200);
    if (expired < 0)
    {
        goto cleanup;
    }
    assert(expired == 1);

    client.lease_expiry_timestamp_ns = TP_NULL_U64;
    expired = tp_driver_client_lease_expired(&client, 500);
    if (expired < 0)
    {
        goto cleanup;
    }
    assert(expired == 0);

    if (tp_driver_client_record_keepalive(&client, 180) != 0)
    {
        goto cleanup;
    }
    assert(client.last_keepalive_ns == 180);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_driver_keepalive_errors(void)
{
    tp_driver_client_t client;
    tp_driver_attach_info_t info;
    tp_client_t owner;

    memset(&client, 0, sizeof(client));
    memset(&info, 0, sizeof(info));
    memset(&owner, 0, sizeof(owner));

    assert(tp_driver_client_keepalive_due(NULL, 0, 0) < 0);
    assert(tp_driver_client_lease_expired(NULL, 0) < 0);
    assert(tp_driver_client_record_keepalive(NULL, 0) < 0);
    assert(tp_driver_client_update_lease(NULL, NULL, 0, 0) < 0);
    assert(tp_driver_client_update_lease(&client, NULL, 0, 0) < 0);

    info.lease_id = 5;
    info.lease_expiry_timestamp_ns = 100;
    info.stream_id = 7;
    assert(tp_driver_client_update_lease(&client, &info, 2, tensor_pool_role_CONSUMER) == 0);
    assert(client.active_lease_id == 5);
    assert(client.client_id == 2);

    owner.context.keepalive_interval_ns = 10;
    owner.context.lease_expiry_grace_intervals = 2;
    client.client = &owner;
    assert(tp_driver_client_record_keepalive(&client, 200) == 0);
    assert(client.lease_expiry_timestamp_ns >= 220);
}

void tp_test_driver_client_decoders(void)
{
    test_decode_attach_response_valid();
    test_decode_attach_response_invalid_slot_bytes();
    test_decode_attach_response_version_mismatch();
    test_decode_attach_response_invalid_code();
    test_decode_attach_response_null_expiry();
    test_decode_attach_response_missing_required_fields();
    test_decode_attach_response_missing_pools();
    test_decode_attach_response_pool_mismatch();
    test_decode_attach_response_missing_header_uri();
    test_decode_attach_response_rejected();
    test_decode_detach_response();
    test_decode_detach_response_block_length_mismatch();
    test_decode_detach_response_invalid_code();
    test_decode_lease_revoked();
    test_decode_lease_revoked_invalid_role();
    test_decode_lease_revoked_invalid_reason();
    test_decode_shutdown();
    test_decode_shutdown_invalid_reason();
    test_driver_keepalive_helpers();
    test_driver_keepalive_errors();
}
