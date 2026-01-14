#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_types.h"
#include "tensor_pool/tp_uri.h"

#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/controlResponse.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/metaBlobAnnounce.h"
#include "wire/tensor_pool/metaBlobChunk.h"
#include "wire/tensor_pool/metaBlobComplete.h"
#include "wire/tensor_pool/shmPoolAnnounce.h"

#include <assert.h>
#include <string.h>

static void test_decode_consumer_hello(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerHello hello;
    tp_consumer_hello_view_t view;
    int result = -1;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_consumerHello_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerHello_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerHello_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerHello_sbe_schema_version());

    tensor_pool_consumerHello_wrap_for_encode(&hello, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_consumerHello_set_streamId(&hello, 10);
    tensor_pool_consumerHello_set_consumerId(&hello, 7);
    tensor_pool_consumerHello_set_supportsShm(&hello, 1);
    tensor_pool_consumerHello_set_supportsProgress(&hello, 0);
    tensor_pool_consumerHello_set_mode(&hello, 1);
    tensor_pool_consumerHello_set_maxRateHz(&hello, 60);
    tensor_pool_consumerHello_set_expectedLayoutVersion(&hello, 5);
    tensor_pool_consumerHello_set_progressIntervalUs(&hello, tensor_pool_consumerHello_progressIntervalUs_null_value());
    tensor_pool_consumerHello_set_progressBytesDelta(&hello, 4096);
    tensor_pool_consumerHello_set_progressMajorDeltaUnits(&hello, tensor_pool_consumerHello_progressMajorDeltaUnits_null_value());
    tensor_pool_consumerHello_set_descriptorStreamId(&hello, 1100);
    tensor_pool_consumerHello_set_controlStreamId(&hello, 1000);
    tensor_pool_consumerHello_put_descriptorChannel(&hello, "aeron:ipc", 9);
    tensor_pool_consumerHello_put_controlChannel(&hello, "aeron:ipc", 9);

    if (tp_control_decode_consumer_hello(buffer, sizeof(buffer), &view) != 0)
    {
        goto cleanup;
    }

    assert(view.stream_id == 10);
    assert(view.consumer_id == 7);
    assert(view.supports_shm == 1);
    assert(view.supports_progress == 0);
    assert(view.mode == TP_MODE_STREAM);
    assert(view.max_rate_hz == 60);
    assert(view.expected_layout_version == 5);
    assert(view.progress_interval_us == TP_NULL_U32);
    assert(view.progress_bytes_delta == 4096);
    assert(view.progress_major_delta_units == TP_NULL_U32);
    assert(view.descriptor_stream_id == 1100);
    assert(view.control_stream_id == 1000);
    assert(view.descriptor_channel.length == 9);
    assert(view.control_channel.length == 9);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_consumer_config_payload_fallback(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerConfig cfg;
    tp_consumer_config_view_t view;
    int result = -1;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_consumerConfig_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerConfig_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerConfig_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerConfig_sbe_schema_version());

    tensor_pool_consumerConfig_wrap_for_encode(&cfg, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_consumerConfig_set_streamId(&cfg, 10);
    tensor_pool_consumerConfig_set_consumerId(&cfg, 7);
    tensor_pool_consumerConfig_set_useShm(&cfg, 0);
    tensor_pool_consumerConfig_set_mode(&cfg, tensor_pool_mode_STREAM);
    tensor_pool_consumerConfig_set_descriptorStreamId(&cfg, 0);
    tensor_pool_consumerConfig_set_controlStreamId(&cfg, 0);
    tensor_pool_consumerConfig_put_payloadFallbackUri(&cfg, "aeron:udp?endpoint=10.0.0.2:40125", 39);

    if (tp_control_decode_consumer_config(buffer, sizeof(buffer), &view) != 0)
    {
        goto cleanup;
    }

    assert(view.consumer_id == 7);
    assert(view.use_shm == 0);
    assert(view.payload_fallback_uri.length == 39);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_consumer_config_version_gate(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerConfig cfg;
    tp_consumer_config_view_t view;
    int result = -1;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_consumerConfig_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerConfig_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerConfig_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerConfig_sbe_schema_version() + 1);

    tensor_pool_consumerConfig_wrap_for_encode(&cfg, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_consumerConfig_set_streamId(&cfg, 10);
    tensor_pool_consumerConfig_set_consumerId(&cfg, 7);
    tensor_pool_consumerConfig_set_useShm(&cfg, 1);
    tensor_pool_consumerConfig_set_mode(&cfg, tensor_pool_mode_STREAM);

    if (tp_control_decode_consumer_config(buffer, sizeof(buffer), &view) < 0)
    {
        result = 0;
    }

    assert(result == 0);
}

static void test_decode_consumer_config_stream_mismatch(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerConfig cfg;
    tp_consumer_config_view_t view;
    int result = -1;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_consumerConfig_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerConfig_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerConfig_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerConfig_sbe_schema_version());

    tensor_pool_consumerConfig_wrap_for_encode(&cfg, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_consumerConfig_set_streamId(&cfg, 10);
    tensor_pool_consumerConfig_set_consumerId(&cfg, 7);
    tensor_pool_consumerConfig_set_useShm(&cfg, 1);
    tensor_pool_consumerConfig_set_mode(&cfg, tensor_pool_mode_STREAM);
    tensor_pool_consumerConfig_set_descriptorStreamId(&cfg, 1200);
    tensor_pool_consumerConfig_set_controlStreamId(&cfg, 0);
    tensor_pool_consumerConfig_put_payloadFallbackUri(&cfg, "", 0);
    tensor_pool_consumerConfig_put_descriptorChannel(&cfg, "aeron:ipc", 9);
    tensor_pool_consumerConfig_put_controlChannel(&cfg, "", 0);

    if (tp_control_decode_consumer_config(buffer, sizeof(buffer), &view) != 0)
    {
        goto cleanup;
    }

    assert(view.descriptor_channel.length == 9);
    assert(view.descriptor_stream_id == 1200);
    assert(view.control_channel.length == 0);
    assert(view.control_stream_id == 0);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_consumer_config_block_length_mismatch(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerConfig cfg;
    tp_consumer_config_view_t view;
    int result = -1;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, 0);
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerConfig_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerConfig_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerConfig_sbe_schema_version());

    tensor_pool_consumerConfig_wrap_for_encode(&cfg, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_consumerConfig_set_streamId(&cfg, 10);
    tensor_pool_consumerConfig_set_consumerId(&cfg, 7);
    tensor_pool_consumerConfig_set_useShm(&cfg, 1);
    tensor_pool_consumerConfig_set_mode(&cfg, tensor_pool_mode_STREAM);

    if (tp_control_decode_consumer_config(buffer, sizeof(buffer), &view) < 0)
    {
        result = 0;
    }

    assert(result == 0);
}

static void test_payload_fallback_uri_scheme(void)
{
    int result = -1;

    assert(tp_payload_fallback_uri_supported("aeron:ipc", 9));
    assert(tp_payload_fallback_uri_supported("bridge://pool", 13));
    assert(!tp_payload_fallback_uri_supported("http://invalid", 15));
    result = 0;

cleanup:
    assert(result == 0);
}

typedef struct test_meta_state_stct
{
    uint32_t attr_count;
    uint32_t key_len;
}
test_meta_state_t;

static void test_meta_attr_handler(const tp_data_source_meta_attr_view_t *attr, void *clientd)
{
    test_meta_state_t *state = (test_meta_state_t *)clientd;

    state->attr_count++;
    if (state->attr_count == 1)
    {
        state->key_len = attr->key.length;
    }
}

static void test_decode_data_source_meta(void)
{
    uint8_t buffer[1024];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_dataSourceMeta meta;
    struct tensor_pool_dataSourceMeta_attributes attrs;
    tp_data_source_meta_view_t view;
    test_meta_state_t state;
    int result = -1;

    memset(&state, 0, sizeof(state));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_dataSourceMeta_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_dataSourceMeta_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_dataSourceMeta_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_dataSourceMeta_sbe_schema_version());

    tensor_pool_dataSourceMeta_wrap_for_encode(&meta, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_dataSourceMeta_set_streamId(&meta, 2);
    tensor_pool_dataSourceMeta_set_metaVersion(&meta, 3);
    tensor_pool_dataSourceMeta_set_timestampNs(&meta, 4);

    if (NULL == tensor_pool_dataSourceMeta_attributes_wrap_for_encode(
        &attrs,
        (char *)buffer,
        2,
        tensor_pool_dataSourceMeta_sbe_position_ptr(&meta),
        tensor_pool_dataSourceMeta_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_dataSourceMeta_attributes_next(&attrs))
    {
        goto cleanup;
    }
    tensor_pool_dataSourceMeta_attributes_put_key(&attrs, "key1", 4);
    tensor_pool_dataSourceMeta_attributes_put_format(&attrs, "u8", 2);
    tensor_pool_dataSourceMeta_attributes_put_value(&attrs, "v1", 2);

    if (NULL == tensor_pool_dataSourceMeta_attributes_next(&attrs))
    {
        goto cleanup;
    }
    tensor_pool_dataSourceMeta_attributes_put_key(&attrs, "key2", 4);
    tensor_pool_dataSourceMeta_attributes_put_format(&attrs, "u16", 3);
    tensor_pool_dataSourceMeta_attributes_put_value(&attrs, "v2", 2);

    if (tp_control_decode_data_source_meta(buffer, sizeof(buffer), &view, test_meta_attr_handler, &state) != 0)
    {
        goto cleanup;
    }

    assert(view.stream_id == 2);
    assert(view.meta_version == 3);
    assert(view.timestamp_ns == 4);
    assert(view.attribute_count == 2);
    assert(state.attr_count == 2);
    assert(state.key_len == 4);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_control_response(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_controlResponse resp;
    tp_control_response_view_t view;
    int result = -1;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_controlResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_controlResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_controlResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_controlResponse_sbe_schema_version());

    tensor_pool_controlResponse_wrap_for_encode(&resp, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
    tensor_pool_controlResponse_set_correlationId(&resp, 42);
    tensor_pool_controlResponse_set_code(&resp, tensor_pool_responseCode_INVALID_PARAMS);
    tensor_pool_controlResponse_put_errorMessage(&resp, "bad", 3);

    if (tp_control_decode_control_response(buffer, sizeof(buffer), &view) != 0)
    {
        goto cleanup;
    }

    assert(view.correlation_id == 42);
    assert(view.code == TP_RESPONSE_INVALID_PARAMS);
    assert(view.error_message.length == 3);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_meta_blobs(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    int result = -1;

    {
        struct tensor_pool_metaBlobAnnounce announce;
        tp_meta_blob_announce_view_t view;

        tensor_pool_messageHeader_wrap(
            &header,
            (char *)buffer,
            0,
            tensor_pool_messageHeader_sbe_schema_version(),
            sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_metaBlobAnnounce_sbe_block_length());
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobAnnounce_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobAnnounce_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobAnnounce_sbe_schema_version());

        tensor_pool_metaBlobAnnounce_wrap_for_encode(&announce, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
        tensor_pool_metaBlobAnnounce_set_streamId(&announce, 9);
        tensor_pool_metaBlobAnnounce_set_metaVersion(&announce, 5);
        tensor_pool_metaBlobAnnounce_set_blobType(&announce, 2);
        tensor_pool_metaBlobAnnounce_set_totalLen(&announce, 1024);
        tensor_pool_metaBlobAnnounce_set_checksum(&announce, 77);

        if (tp_control_decode_meta_blob_announce(buffer, sizeof(buffer), &view) != 0)
        {
            goto cleanup;
        }

        assert(view.stream_id == 9);
        assert(view.meta_version == 5);
        assert(view.blob_type == 2);
        assert(view.total_len == 1024);
        assert(view.checksum == 77);
    }

    {
        struct tensor_pool_metaBlobChunk chunk;
        tp_meta_blob_chunk_view_t view;
        const char data[] = "payload";

        tensor_pool_messageHeader_wrap(
            &header,
            (char *)buffer,
            0,
            tensor_pool_messageHeader_sbe_schema_version(),
            sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_metaBlobChunk_sbe_block_length());
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobChunk_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobChunk_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobChunk_sbe_schema_version());

        tensor_pool_metaBlobChunk_wrap_for_encode(&chunk, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
        tensor_pool_metaBlobChunk_set_streamId(&chunk, 9);
        tensor_pool_metaBlobChunk_set_metaVersion(&chunk, 5);
        tensor_pool_metaBlobChunk_set_chunkOffset(&chunk, 12);
        tensor_pool_metaBlobChunk_put_bytes(&chunk, data, sizeof(data) - 1);

        if (tp_control_decode_meta_blob_chunk(buffer, sizeof(buffer), &view) != 0)
        {
            goto cleanup;
        }

        assert(view.stream_id == 9);
        assert(view.meta_version == 5);
        assert(view.offset == 12);
        assert(view.bytes.length == sizeof(data) - 1);
    }

    {
        struct tensor_pool_metaBlobComplete complete;
        tp_meta_blob_complete_view_t view;

        tensor_pool_messageHeader_wrap(
            &header,
            (char *)buffer,
            0,
            tensor_pool_messageHeader_sbe_schema_version(),
            sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_metaBlobComplete_sbe_block_length());
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobComplete_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobComplete_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobComplete_sbe_schema_version());

        tensor_pool_metaBlobComplete_wrap_for_encode(&complete, (char *)buffer, tensor_pool_messageHeader_encoded_length(), sizeof(buffer));
        tensor_pool_metaBlobComplete_set_streamId(&complete, 9);
        tensor_pool_metaBlobComplete_set_metaVersion(&complete, 5);
        tensor_pool_metaBlobComplete_set_checksum(&complete, 88);

        if (tp_control_decode_meta_blob_complete(buffer, sizeof(buffer), &view) != 0)
        {
            goto cleanup;
        }

        assert(view.stream_id == 9);
        assert(view.meta_version == 5);
        assert(view.checksum == 88);
    }

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_decode_shm_pool_announce_version_gate(void)
{
    uint8_t buffer[64];
    struct tensor_pool_messageHeader header;
    tp_shm_pool_announce_view_t view;
    int result = -1;

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmPoolAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmPoolAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmPoolAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmPoolAnnounce_sbe_schema_version() + 1);

    if (tp_control_decode_shm_pool_announce(buffer, sizeof(buffer), &view) < 0)
    {
        result = 0;
    }

    assert(result == 0);
}

void tp_test_decode_consumer_hello(void)
{
    test_decode_consumer_hello();
}

void tp_test_decode_consumer_config(void)
{
    test_decode_consumer_config_payload_fallback();
    test_decode_consumer_config_version_gate();
    test_decode_consumer_config_stream_mismatch();
    test_decode_consumer_config_block_length_mismatch();
    test_payload_fallback_uri_scheme();
}

void tp_test_decode_data_source_meta(void)
{
    test_decode_data_source_meta();
}

void tp_test_decode_control_response(void)
{
    test_decode_control_response();
}

void tp_test_decode_meta_blobs(void)
{
    test_decode_meta_blobs();
}

void tp_test_decode_shm_pool_announce(void)
{
    test_decode_shm_pool_announce_version_gate();
}
