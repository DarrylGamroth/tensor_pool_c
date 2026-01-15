#include "tensor_pool/tp_discovery_client.h"
#include "tensor_pool/tp_types.h"

#include "discovery/tensor_pool/messageHeader.h"
#include "discovery/tensor_pool/discoveryResponse.h"
#include "discovery/tensor_pool/discoveryStatus.h"
#include "discovery/tensor_pool/varAsciiEncoding.h"

#include <assert.h>
#include <string.h>

static void test_decode_discovery_response_with_tags(void)
{
    uint8_t buffer[2048];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_discoveryResponse response;
    struct tensor_pool_discoveryResponse_results results;
    struct tensor_pool_discoveryResponse_results_payloadPools pools;
    struct tensor_pool_discoveryResponse_results_tags tags;
    tp_discovery_response_t out;
    int result = -1;

    memset(&out, 0, sizeof(out));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_discoveryResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_discoveryResponse_sbe_schema_version());

    tensor_pool_discoveryResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_discoveryResponse_set_requestId(&response, 77);
    tensor_pool_discoveryResponse_set_status(&response, tensor_pool_discoveryStatus_OK);

    if (NULL == tensor_pool_discoveryResponse_results_wrap_for_encode(
        &results,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_sbe_position_ptr(&response),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_discoveryResponse_results_next(&results))
    {
        goto cleanup;
    }

    tensor_pool_discoveryResponse_results_set_streamId(&results, 10);
    tensor_pool_discoveryResponse_results_set_producerId(&results, 20);
    tensor_pool_discoveryResponse_results_set_epoch(&results, 30);
    tensor_pool_discoveryResponse_results_set_layoutVersion(&results, 1);
    tensor_pool_discoveryResponse_results_set_headerNslots(&results, 16);
    tensor_pool_discoveryResponse_results_set_headerSlotBytes(&results, TP_HEADER_SLOT_BYTES);
    tensor_pool_discoveryResponse_results_set_maxDims(&results, TP_MAX_DIMS);
    tensor_pool_discoveryResponse_results_set_dataSourceId(&results, 99);
    tensor_pool_discoveryResponse_results_set_driverControlStreamId(&results, 500);

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_next(&pools))
    {
        goto cleanup;
    }
    tensor_pool_discoveryResponse_results_payloadPools_set_poolId(&pools, 1);
    tensor_pool_discoveryResponse_results_payloadPools_set_poolNslots(&pools, 16);
    tensor_pool_discoveryResponse_results_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_discoveryResponse_results_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);

    if (NULL == tensor_pool_discoveryResponse_results_tags_wrap_for_encode(
        &tags,
        (char *)buffer,
        2,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_discoveryResponse_results_tags_next(&tags))
    {
        goto cleanup;
    }
    {
        struct tensor_pool_varAsciiEncoding tag_codec;
        if (NULL == tensor_pool_discoveryResponse_results_tags_tag(&tags, &tag_codec))
        {
            goto cleanup;
        }
        tensor_pool_varAsciiEncoding_set_length(&tag_codec, 6);
        memcpy(
            (char *)tensor_pool_varAsciiEncoding_mut_buffer(&tag_codec) +
                tensor_pool_varAsciiEncoding_offset(&tag_codec) +
                tensor_pool_varAsciiEncoding_varData_encoding_offset(),
            "vision",
            6);
        if (!tensor_pool_discoveryResponse_results_tags_set_sbe_position(
            &tags,
            tensor_pool_varAsciiEncoding_offset(&tag_codec) +
                tensor_pool_varAsciiEncoding_varData_encoding_offset() + 6))
        {
            goto cleanup;
        }
    }

    if (NULL == tensor_pool_discoveryResponse_results_tags_next(&tags))
    {
        goto cleanup;
    }
    {
        struct tensor_pool_varAsciiEncoding tag_codec;
        if (NULL == tensor_pool_discoveryResponse_results_tags_tag(&tags, &tag_codec))
        {
            goto cleanup;
        }
        tensor_pool_varAsciiEncoding_set_length(&tag_codec, 4);
        memcpy(
            (char *)tensor_pool_varAsciiEncoding_mut_buffer(&tag_codec) +
                tensor_pool_varAsciiEncoding_offset(&tag_codec) +
                tensor_pool_varAsciiEncoding_varData_encoding_offset(),
            "fp32",
            4);
        if (!tensor_pool_discoveryResponse_results_tags_set_sbe_position(
            &tags,
            tensor_pool_varAsciiEncoding_offset(&tag_codec) +
                tensor_pool_varAsciiEncoding_varData_encoding_offset() + 4))
        {
            goto cleanup;
        }
    }

    tensor_pool_discoveryResponse_results_put_headerRegionUri(&results, "shm:file?path=/dev/shm/hdr", 26);
    tensor_pool_discoveryResponse_results_put_dataSourceName(&results, "camera", 6);
    tensor_pool_discoveryResponse_results_put_driverInstanceId(&results, "drv", 3);
    tensor_pool_discoveryResponse_results_put_driverControlChannel(&results, "aeron:ipc", 9);

    if (tp_discovery_decode_response(buffer, sizeof(buffer), 77, &out) != 0)
    {
        goto cleanup;
    }

    assert(out.status == tensor_pool_discoveryStatus_OK);
    assert(out.result_count == 1);
    assert(out.results[0].tag_count == 2);
    assert(tp_discovery_result_has_tag(&out.results[0], "vision") == 1);
    assert(tp_discovery_result_matches(&out.results[0], 10, TP_NULL_U32, "camera") == 1);

    result = 0;

cleanup:
    tp_discovery_response_close(&out);
    assert(result == 0);
}

static void test_decode_discovery_response_invalid_dims(void)
{
    uint8_t buffer[1024];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_discoveryResponse response;
    struct tensor_pool_discoveryResponse_results results;
    tp_discovery_response_t out;
    int result = -1;

    memset(&out, 0, sizeof(out));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_discoveryResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_discoveryResponse_sbe_schema_version());

    tensor_pool_discoveryResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_discoveryResponse_set_requestId(&response, 88);
    tensor_pool_discoveryResponse_set_status(&response, tensor_pool_discoveryStatus_OK);

    if (NULL == tensor_pool_discoveryResponse_results_wrap_for_encode(
        &results,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_sbe_position_ptr(&response),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_discoveryResponse_results_next(&results))
    {
        goto cleanup;
    }

    tensor_pool_discoveryResponse_results_set_streamId(&results, 10);
    tensor_pool_discoveryResponse_results_set_producerId(&results, 20);
    tensor_pool_discoveryResponse_results_set_epoch(&results, 30);
    tensor_pool_discoveryResponse_results_set_layoutVersion(&results, 1);
    tensor_pool_discoveryResponse_results_set_headerNslots(&results, 16);
    tensor_pool_discoveryResponse_results_set_headerSlotBytes(&results, 128);
    tensor_pool_discoveryResponse_results_set_maxDims(&results, TP_MAX_DIMS);
    tensor_pool_discoveryResponse_results_set_dataSourceId(&results, 99);
    tensor_pool_discoveryResponse_results_set_driverControlStreamId(&results, 500);

    if (tp_discovery_decode_response(buffer, sizeof(buffer), 88, &out) != 0)
    {
        goto cleanup;
    }

    assert(out.status == tensor_pool_discoveryStatus_ERROR);
    assert(out.error_message[0] != '\0');

    result = 0;

cleanup:
    tp_discovery_response_close(&out);
    assert(result == 0);
}

static void test_decode_discovery_response_version_mismatch(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_discoveryResponse response;
    tp_discovery_response_t out;
    int result = -1;

    memset(&out, 0, sizeof(out));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_discoveryResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_discoveryResponse_sbe_schema_version() + 1);

    tensor_pool_discoveryResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_discoveryResponse_set_requestId(&response, 77);
    tensor_pool_discoveryResponse_set_status(&response, tensor_pool_discoveryStatus_OK);

    assert(tp_discovery_decode_response(buffer, sizeof(buffer), 77, &out) < 0);
    result = 0;

    tp_discovery_response_close(&out);
    assert(result == 0);
}

static void test_discovery_request_requires_response_channel(void)
{
    tp_discovery_client_t client;
    tp_discovery_request_t request;
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&request, 0, sizeof(request));

    request.response_channel = "";
    request.response_stream_id = 100;

    assert(tp_discovery_request(&client, &request) < 0);
    result = 0;

    assert(result == 0);
}

static void test_decode_discovery_response_pool_nslots_mismatch(void)
{
    uint8_t buffer[1024];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_discoveryResponse response;
    struct tensor_pool_discoveryResponse_results results;
    struct tensor_pool_discoveryResponse_results_payloadPools pools;
    struct tensor_pool_discoveryResponse_results_tags tags;
    tp_discovery_response_t out;
    int result = -1;

    memset(&out, 0, sizeof(out));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_discoveryResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_discoveryResponse_sbe_schema_version());

    tensor_pool_discoveryResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_discoveryResponse_set_requestId(&response, 99);
    tensor_pool_discoveryResponse_set_status(&response, tensor_pool_discoveryStatus_OK);

    if (NULL == tensor_pool_discoveryResponse_results_wrap_for_encode(
        &results,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_sbe_position_ptr(&response),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_discoveryResponse_results_next(&results))
    {
        goto cleanup;
    }

    tensor_pool_discoveryResponse_results_set_streamId(&results, 10);
    tensor_pool_discoveryResponse_results_set_producerId(&results, 20);
    tensor_pool_discoveryResponse_results_set_epoch(&results, 30);
    tensor_pool_discoveryResponse_results_set_layoutVersion(&results, 1);
    tensor_pool_discoveryResponse_results_set_headerNslots(&results, 16);
    tensor_pool_discoveryResponse_results_set_headerSlotBytes(&results, TP_HEADER_SLOT_BYTES);
    tensor_pool_discoveryResponse_results_set_maxDims(&results, TP_MAX_DIMS);
    tensor_pool_discoveryResponse_results_set_driverControlStreamId(&results, 500);

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_next(&pools))
    {
        goto cleanup;
    }
    tensor_pool_discoveryResponse_results_payloadPools_set_poolId(&pools, 1);
    tensor_pool_discoveryResponse_results_payloadPools_set_poolNslots(&pools, 8);
    tensor_pool_discoveryResponse_results_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_discoveryResponse_results_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);

    tensor_pool_discoveryResponse_results_put_headerRegionUri(&results, "shm:file?path=/dev/shm/hdr", 26);
    tensor_pool_discoveryResponse_results_put_driverInstanceId(&results, "drv", 3);
    tensor_pool_discoveryResponse_results_put_driverControlChannel(&results, "aeron:ipc", 9);

    if (tp_discovery_decode_response(buffer, sizeof(buffer), 99, &out) != 0)
    {
        goto cleanup;
    }

    assert(out.status == tensor_pool_discoveryStatus_ERROR);

    result = 0;

cleanup:
    tp_discovery_response_close(&out);
    assert(result == 0);
}

static void test_decode_discovery_response_missing_driver_control(void)
{
    uint8_t buffer[1024];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_discoveryResponse response;
    struct tensor_pool_discoveryResponse_results results;
    struct tensor_pool_discoveryResponse_results_payloadPools pools;
    struct tensor_pool_discoveryResponse_results_tags tags;
    tp_discovery_response_t out;

    memset(&out, 0, sizeof(out));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_discoveryResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_discoveryResponse_sbe_schema_version());

    tensor_pool_discoveryResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_discoveryResponse_set_requestId(&response, 91);
    tensor_pool_discoveryResponse_set_status(&response, tensor_pool_discoveryStatus_OK);

    if (NULL == tensor_pool_discoveryResponse_results_wrap_for_encode(
        &results,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_sbe_position_ptr(&response),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        return;
    }

    if (NULL == tensor_pool_discoveryResponse_results_next(&results))
    {
        return;
    }

    tensor_pool_discoveryResponse_results_set_streamId(&results, 10);
    tensor_pool_discoveryResponse_results_set_producerId(&results, 20);
    tensor_pool_discoveryResponse_results_set_epoch(&results, 30);
    tensor_pool_discoveryResponse_results_set_layoutVersion(&results, 1);
    tensor_pool_discoveryResponse_results_set_headerNslots(&results, 16);
    tensor_pool_discoveryResponse_results_set_headerSlotBytes(&results, TP_HEADER_SLOT_BYTES);
    tensor_pool_discoveryResponse_results_set_maxDims(&results, TP_MAX_DIMS);
    tensor_pool_discoveryResponse_results_set_dataSourceId(&results, 99);
    tensor_pool_discoveryResponse_results_set_driverControlStreamId(&results, 0);

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        return;
    }

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_next(&pools))
    {
        return;
    }
    tensor_pool_discoveryResponse_results_payloadPools_set_poolId(&pools, 1);
    tensor_pool_discoveryResponse_results_payloadPools_set_poolNslots(&pools, 16);
    tensor_pool_discoveryResponse_results_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_discoveryResponse_results_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);
    tensor_pool_discoveryResponse_results_put_headerRegionUri(&results, "shm:file?path=/dev/shm/hdr", 26);
    tensor_pool_discoveryResponse_results_put_dataSourceName(&results, "camera", 6);
    tensor_pool_discoveryResponse_results_put_driverInstanceId(&results, "drv", 3);
    tensor_pool_discoveryResponse_results_put_driverControlChannel(&results, "", 0);

    if (tp_discovery_decode_response(buffer, sizeof(buffer), 91, &out) != 0)
    {
        return;
    }

    assert(out.status == tensor_pool_discoveryStatus_ERROR);
    tp_discovery_response_close(&out);
}

static void test_decode_discovery_response_null_data_source_id(void)
{
    uint8_t buffer[1024];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_discoveryResponse response;
    struct tensor_pool_discoveryResponse_results results;
    struct tensor_pool_discoveryResponse_results_payloadPools pools;
    struct tensor_pool_discoveryResponse_results_tags tags;
    tp_discovery_response_t out;
    int result = -1;

    memset(&out, 0, sizeof(out));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_discoveryResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_discoveryResponse_sbe_schema_version());

    tensor_pool_discoveryResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_discoveryResponse_set_requestId(&response, 92);
    tensor_pool_discoveryResponse_set_status(&response, tensor_pool_discoveryStatus_OK);

    if (NULL == tensor_pool_discoveryResponse_results_wrap_for_encode(
        &results,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_sbe_position_ptr(&response),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_discoveryResponse_results_next(&results))
    {
        goto cleanup;
    }

    tensor_pool_discoveryResponse_results_set_streamId(&results, 10);
    tensor_pool_discoveryResponse_results_set_producerId(&results, 20);
    tensor_pool_discoveryResponse_results_set_epoch(&results, 30);
    tensor_pool_discoveryResponse_results_set_layoutVersion(&results, 1);
    tensor_pool_discoveryResponse_results_set_headerNslots(&results, 16);
    tensor_pool_discoveryResponse_results_set_headerSlotBytes(&results, TP_HEADER_SLOT_BYTES);
    tensor_pool_discoveryResponse_results_set_maxDims(&results, TP_MAX_DIMS);
    tensor_pool_discoveryResponse_results_set_dataSourceId(
        &results,
        tensor_pool_discoveryResponse_results_dataSourceId_null_value());
    tensor_pool_discoveryResponse_results_set_driverControlStreamId(&results, 500);

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_next(&pools))
    {
        goto cleanup;
    }
    tensor_pool_discoveryResponse_results_payloadPools_set_poolId(&pools, 1);
    tensor_pool_discoveryResponse_results_payloadPools_set_poolNslots(&pools, 16);
    tensor_pool_discoveryResponse_results_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_discoveryResponse_results_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);

    if (NULL == tensor_pool_discoveryResponse_results_tags_wrap_for_encode(
        &tags,
        (char *)buffer,
        0,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    tensor_pool_discoveryResponse_results_put_headerRegionUri(&results, "shm:file?path=/dev/shm/hdr", 26);
    tensor_pool_discoveryResponse_results_put_dataSourceName(&results, "", 0);
    tensor_pool_discoveryResponse_results_put_driverInstanceId(&results, "", 0);
    tensor_pool_discoveryResponse_results_put_driverControlChannel(&results, "aeron:ipc", 9);

    if (tp_discovery_decode_response(buffer, sizeof(buffer), 92, &out) != 0)
    {
        goto cleanup;
    }

    assert(out.status == tensor_pool_discoveryStatus_OK);
    assert(out.result_count == 1);
    assert(out.results[0].data_source_id == TP_NULL_U64);
    result = 0;

cleanup:
    tp_discovery_response_close(&out);
    assert(result == 0);
}

void tp_test_discovery_client_decoders(void)
{
    test_decode_discovery_response_with_tags();
    test_decode_discovery_response_invalid_dims();
    test_decode_discovery_response_version_mismatch();
    test_decode_discovery_response_pool_nslots_mismatch();
    test_decode_discovery_response_missing_driver_control();
    test_decode_discovery_response_null_data_source_id();
    test_discovery_request_requires_response_channel();
}
