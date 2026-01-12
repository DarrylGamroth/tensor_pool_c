#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_types.h"

#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/messageHeader.h"

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
    assert(view.mode == 1);
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

void tp_test_decode_consumer_hello(void)
{
    test_decode_consumer_hello();
}

void tp_test_decode_data_source_meta(void)
{
    test_decode_data_source_meta();
}
