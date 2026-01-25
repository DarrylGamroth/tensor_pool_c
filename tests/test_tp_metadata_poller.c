#include "tensor_pool/internal/tp_metadata_poller.h"

#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/metaBlobAnnounce.h"
#include "wire/tensor_pool/metaBlobChunk.h"
#include "wire/tensor_pool/metaBlobComplete.h"

#include <assert.h>
#include <string.h>

typedef struct tp_metadata_test_state_stct
{
    int announce;
    int meta_begin;
    int meta_attr;
    int meta_end;
    int blob_announce;
    int blob_chunk;
    int blob_complete;
}
tp_metadata_test_state_t;

static void tp_on_announce(const tp_data_source_announce_view_t *view, void *clientd)
{
    tp_metadata_test_state_t *state = (tp_metadata_test_state_t *)clientd;
    (void)view;
    state->announce++;
}

static void tp_on_meta_begin(const tp_data_source_meta_view_t *view, void *clientd)
{
    tp_metadata_test_state_t *state = (tp_metadata_test_state_t *)clientd;
    (void)view;
    state->meta_begin++;
}

static void tp_on_meta_attr(const tp_data_source_meta_attr_view_t *attr, void *clientd)
{
    tp_metadata_test_state_t *state = (tp_metadata_test_state_t *)clientd;
    (void)attr;
    state->meta_attr++;
}

static void tp_on_meta_end(const tp_data_source_meta_view_t *view, void *clientd)
{
    tp_metadata_test_state_t *state = (tp_metadata_test_state_t *)clientd;
    (void)view;
    state->meta_end++;
}

static void tp_on_blob_announce(const tp_meta_blob_announce_view_t *view, void *clientd)
{
    tp_metadata_test_state_t *state = (tp_metadata_test_state_t *)clientd;
    (void)view;
    state->blob_announce++;
}

static void tp_on_blob_chunk(const tp_meta_blob_chunk_view_t *view, void *clientd)
{
    tp_metadata_test_state_t *state = (tp_metadata_test_state_t *)clientd;
    (void)view;
    state->blob_chunk++;
}

static void tp_on_blob_complete(const tp_meta_blob_complete_view_t *view, void *clientd)
{
    tp_metadata_test_state_t *state = (tp_metadata_test_state_t *)clientd;
    (void)view;
    state->blob_complete++;
}

static size_t encode_data_source_announce(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_dataSourceAnnounce announce;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_dataSourceAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_dataSourceAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_dataSourceAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_dataSourceAnnounce_sbe_schema_version());

    tensor_pool_dataSourceAnnounce_wrap_for_encode(&announce, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_dataSourceAnnounce_set_streamId(&announce, 1);
    tensor_pool_dataSourceAnnounce_set_producerId(&announce, 2);
    tensor_pool_dataSourceAnnounce_set_epoch(&announce, 3);
    tensor_pool_dataSourceAnnounce_set_metaVersion(&announce, 1);
    tensor_pool_dataSourceAnnounce_put_name(&announce, "name", 4);
    tensor_pool_dataSourceAnnounce_put_summary(&announce, "summary", 7);

    return (size_t)tensor_pool_dataSourceAnnounce_sbe_position(&announce);
}

static size_t encode_data_source_meta(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_dataSourceMeta meta;
    struct tensor_pool_dataSourceMeta_attributes attrs;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_dataSourceMeta_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_dataSourceMeta_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_dataSourceMeta_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_dataSourceMeta_sbe_schema_version());

    tensor_pool_dataSourceMeta_wrap_for_encode(&meta, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_dataSourceMeta_set_streamId(&meta, 1);
    tensor_pool_dataSourceMeta_set_metaVersion(&meta, 2);
    tensor_pool_dataSourceMeta_set_timestampNs(&meta, 10);

    tensor_pool_dataSourceMeta_attributes_wrap_for_encode(
        &attrs,
        (char *)buffer,
        1,
        tensor_pool_dataSourceMeta_sbe_position_ptr(&meta),
        tensor_pool_dataSourceMeta_sbe_schema_version(),
        capacity);
    tensor_pool_dataSourceMeta_attributes_next(&attrs);
    tensor_pool_dataSourceMeta_attributes_put_key(&attrs, "k", 1);
    tensor_pool_dataSourceMeta_attributes_put_format(&attrs, "s", 1);
    tensor_pool_dataSourceMeta_attributes_put_value(&attrs, "v", 1);

    return (size_t)tensor_pool_dataSourceMeta_sbe_position(&meta);
}

static size_t encode_blob_announce(uint8_t *buffer, size_t capacity, uint64_t checksum, uint32_t total_len)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_metaBlobAnnounce announce;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_metaBlobAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobAnnounce_sbe_schema_version());

    tensor_pool_metaBlobAnnounce_wrap_for_encode(&announce, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_metaBlobAnnounce_set_streamId(&announce, 1);
    tensor_pool_metaBlobAnnounce_set_metaVersion(&announce, 2);
    tensor_pool_metaBlobAnnounce_set_blobType(&announce, 3);
    tensor_pool_metaBlobAnnounce_set_totalLen(&announce, total_len);
    tensor_pool_metaBlobAnnounce_set_checksum(&announce, checksum);

    return (size_t)tensor_pool_metaBlobAnnounce_sbe_position(&announce);
}

static size_t encode_blob_chunk(uint8_t *buffer, size_t capacity, uint32_t offset, const char *bytes, size_t len)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_metaBlobChunk chunk;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_metaBlobChunk_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobChunk_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobChunk_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobChunk_sbe_schema_version());

    tensor_pool_metaBlobChunk_wrap_for_encode(&chunk, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_metaBlobChunk_set_streamId(&chunk, 1);
    tensor_pool_metaBlobChunk_set_metaVersion(&chunk, 2);
    tensor_pool_metaBlobChunk_set_chunkOffset(&chunk, offset);
    tensor_pool_metaBlobChunk_put_bytes(&chunk, bytes, (uint32_t)len);

    return (size_t)tensor_pool_metaBlobChunk_sbe_position(&chunk);
}

static size_t encode_blob_complete(uint8_t *buffer, size_t capacity, uint64_t checksum)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_metaBlobComplete complete;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_metaBlobComplete_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_metaBlobComplete_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_metaBlobComplete_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_metaBlobComplete_sbe_schema_version());

    tensor_pool_metaBlobComplete_wrap_for_encode(&complete, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_metaBlobComplete_set_streamId(&complete, 1);
    tensor_pool_metaBlobComplete_set_metaVersion(&complete, 2);
    tensor_pool_metaBlobComplete_set_checksum(&complete, checksum);

    return (size_t)tensor_pool_metaBlobComplete_sbe_position(&complete);
}

static void test_metadata_poller_invalid_inputs(void)
{
    tp_metadata_poller_t poller;
    tp_client_t client;

    memset(&poller, 0, sizeof(poller));
    memset(&client, 0, sizeof(client));

    assert(tp_metadata_poller_init(NULL, NULL, NULL) < 0);
    assert(tp_metadata_poller_init(&poller, NULL, NULL) < 0);
    assert(tp_metadata_poller_init(&poller, &client, NULL) < 0);
}

static void test_metadata_poller_dispatch(void)
{
    tp_metadata_poller_t poller;
    tp_metadata_handlers_t handlers;
    tp_metadata_test_state_t state;
    uint8_t buffer[512];
    size_t len;

    memset(&poller, 0, sizeof(poller));
    memset(&handlers, 0, sizeof(handlers));
    memset(&state, 0, sizeof(state));

    handlers.on_data_source_announce = tp_on_announce;
    handlers.on_data_source_meta_begin = tp_on_meta_begin;
    handlers.on_data_source_meta_attr = tp_on_meta_attr;
    handlers.on_data_source_meta_end = tp_on_meta_end;
    handlers.on_meta_blob_announce = tp_on_blob_announce;
    handlers.on_meta_blob_chunk = tp_on_blob_chunk;
    handlers.on_meta_blob_complete = tp_on_blob_complete;
    handlers.clientd = &state;
    poller.handlers = handlers;

    tp_metadata_poller_handle_fragment(&poller, buffer, 0);
    assert(state.announce == 0);

    len = encode_data_source_announce(buffer, sizeof(buffer));
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    assert(state.announce == 1);

    len = encode_data_source_meta(buffer, sizeof(buffer));
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    assert(state.meta_begin == 1);
    assert(state.meta_attr == 1);
    assert(state.meta_end == 1);

    len = encode_blob_chunk(buffer, sizeof(buffer), 0, "ab", 2);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    assert(state.blob_chunk == 0);

    len = encode_blob_announce(buffer, sizeof(buffer), 99, 4);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    assert(state.blob_announce == 1);

    len = encode_blob_chunk(buffer, sizeof(buffer), 2, "ab", 2);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    assert(state.blob_chunk == 0);

    len = encode_blob_announce(buffer, sizeof(buffer), 123, 2);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    len = encode_blob_chunk(buffer, sizeof(buffer), 0, "ab", 2);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    assert(state.blob_chunk == 1);

    len = encode_blob_complete(buffer, sizeof(buffer), 999);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    assert(state.blob_complete == 0);

    len = encode_blob_announce(buffer, sizeof(buffer), 111, 2);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    len = encode_blob_chunk(buffer, sizeof(buffer), 0, "ab", 2);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    len = encode_blob_complete(buffer, sizeof(buffer), 111);
    tp_metadata_poller_handle_fragment(&poller, buffer, len);
    assert(state.blob_complete == 1);
}

void tp_test_metadata_poller(void)
{
    test_metadata_poller_invalid_inputs();
    test_metadata_poller_dispatch();
}
