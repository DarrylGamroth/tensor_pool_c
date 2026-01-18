#include "tensor_pool/tp_control_poller.h"
#include "tensor_pool/tp_join_barrier.h"
#include "tensor_pool/tp_merge_map.h"
#include "tensor_pool/tp_tracelink.h"
#include "tensor_pool/tp_types.h"

#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/controlResponse.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/shmPoolAnnounce.h"

#include <assert.h>
#include <string.h>

typedef struct tp_control_poller_state_stct
{
    int shm_pool;
    int consumer_hello;
    int consumer_config;
    int control_response;
    int data_source_announce;
    int data_source_meta_begin;
    int data_source_meta_attr;
    int data_source_meta_end;
    int tracelink;
} 
tp_control_poller_state_t;

static void tp_on_shm_pool_announce(const tp_shm_pool_announce_view_t *view, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)view;
    state->shm_pool++;
}

static void tp_on_consumer_hello(const tp_consumer_hello_view_t *view, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)view;
    state->consumer_hello++;
}

static void tp_on_consumer_config(const tp_consumer_config_view_t *view, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)view;
    state->consumer_config++;
}

static void tp_on_control_response(const tp_control_response_view_t *view, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)view;
    state->control_response++;
}

static void tp_on_data_source_announce(const tp_data_source_announce_view_t *view, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)view;
    state->data_source_announce++;
}

static void tp_on_data_source_meta_begin(const tp_data_source_meta_view_t *view, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)view;
    state->data_source_meta_begin++;
}

static void tp_on_data_source_meta_attr(const tp_data_source_meta_attr_view_t *attr, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)attr;
    state->data_source_meta_attr++;
}

static void tp_on_data_source_meta_end(const tp_data_source_meta_view_t *view, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)view;
    state->data_source_meta_end++;
}

static void tp_on_tracelink_set(const tp_tracelink_set_t *set, void *clientd)
{
    tp_control_poller_state_t *state = (tp_control_poller_state_t *)clientd;
    (void)set;
    state->tracelink++;
}

static size_t encode_consumer_hello(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerHello hello;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_consumerHello_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerHello_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerHello_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerHello_sbe_schema_version());

    tensor_pool_consumerHello_wrap_for_encode(&hello, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_consumerHello_set_streamId(&hello, 10);
    tensor_pool_consumerHello_set_consumerId(&hello, 2);
    tensor_pool_consumerHello_set_supportsShm(&hello, 1);
    tensor_pool_consumerHello_set_supportsProgress(&hello, 1);
    tensor_pool_consumerHello_set_mode(&hello, tensor_pool_mode_STREAM);
    tensor_pool_consumerHello_set_maxRateHz(&hello, 60);
    tensor_pool_consumerHello_set_expectedLayoutVersion(&hello, 1);
    tensor_pool_consumerHello_set_progressIntervalUs(&hello, tensor_pool_consumerHello_progressIntervalUs_null_value());
    tensor_pool_consumerHello_set_progressBytesDelta(&hello, tensor_pool_consumerHello_progressBytesDelta_null_value());
    tensor_pool_consumerHello_set_progressMajorDeltaUnits(&hello, tensor_pool_consumerHello_progressMajorDeltaUnits_null_value());
    tensor_pool_consumerHello_set_descriptorStreamId(&hello, 1100);
    tensor_pool_consumerHello_set_controlStreamId(&hello, 1000);
    tensor_pool_consumerHello_put_descriptorChannel(&hello, "aeron:ipc", 9);
    tensor_pool_consumerHello_put_controlChannel(&hello, "aeron:ipc", 9);

    return (size_t)tensor_pool_consumerHello_sbe_position(&hello);
}

static size_t encode_consumer_config(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_consumerConfig cfg;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_consumerConfig_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerConfig_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerConfig_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerConfig_sbe_schema_version());

    tensor_pool_consumerConfig_wrap_for_encode(&cfg, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_consumerConfig_set_streamId(&cfg, 10);
    tensor_pool_consumerConfig_set_consumerId(&cfg, 2);
    tensor_pool_consumerConfig_set_useShm(&cfg, 1);
    tensor_pool_consumerConfig_set_mode(&cfg, tensor_pool_mode_STREAM);
    tensor_pool_consumerConfig_set_descriptorStreamId(&cfg, 1100);
    tensor_pool_consumerConfig_set_controlStreamId(&cfg, 1000);
    tensor_pool_consumerConfig_put_payloadFallbackUri(&cfg, "shm:file?path=/dev/shm/pool", 27);
    tensor_pool_consumerConfig_put_descriptorChannel(&cfg, "aeron:ipc", 9);
    tensor_pool_consumerConfig_put_controlChannel(&cfg, "aeron:ipc", 9);

    return (size_t)tensor_pool_consumerConfig_sbe_position(&cfg);
}

static size_t encode_control_response(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_controlResponse response;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_controlResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_controlResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_controlResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_controlResponse_sbe_schema_version());

    tensor_pool_controlResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_controlResponse_set_correlationId(&response, 7);
    tensor_pool_controlResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_controlResponse_put_errorMessage(&response, "ok", 2);

    return (size_t)tensor_pool_controlResponse_sbe_position(&response);
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
    tensor_pool_dataSourceAnnounce_set_streamId(&announce, 9);
    tensor_pool_dataSourceAnnounce_set_producerId(&announce, 3);
    tensor_pool_dataSourceAnnounce_set_epoch(&announce, 7);
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
    tensor_pool_dataSourceMeta_set_streamId(&meta, 9);
    tensor_pool_dataSourceMeta_set_metaVersion(&meta, 1);
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

static size_t encode_shm_pool_announce(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmPoolAnnounce announce;
    struct tensor_pool_shmPoolAnnounce_payloadPools pools;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmPoolAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmPoolAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmPoolAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmPoolAnnounce_sbe_schema_version());

    tensor_pool_shmPoolAnnounce_wrap_for_encode(&announce, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_shmPoolAnnounce_set_streamId(&announce, 10);
    tensor_pool_shmPoolAnnounce_set_producerId(&announce, 2);
    tensor_pool_shmPoolAnnounce_set_epoch(&announce, 3);
    tensor_pool_shmPoolAnnounce_set_announceTimestampNs(&announce, 4);
    tensor_pool_shmPoolAnnounce_set_layoutVersion(&announce, 1);
    tensor_pool_shmPoolAnnounce_set_headerNslots(&announce, 16);
    tensor_pool_shmPoolAnnounce_set_headerSlotBytes(&announce, TP_HEADER_SLOT_BYTES);
    tensor_pool_shmPoolAnnounce_set_announceClockDomain(&announce, tensor_pool_clockDomain_MONOTONIC);

    tensor_pool_shmPoolAnnounce_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_shmPoolAnnounce_sbe_position_ptr(&announce),
        tensor_pool_shmPoolAnnounce_sbe_schema_version(),
        capacity);
    tensor_pool_shmPoolAnnounce_payloadPools_next(&pools);
    tensor_pool_shmPoolAnnounce_payloadPools_set_poolId(&pools, 1);
    tensor_pool_shmPoolAnnounce_payloadPools_set_poolNslots(&pools, 16);
    tensor_pool_shmPoolAnnounce_payloadPools_set_strideBytes(&pools, 4096);
    tensor_pool_shmPoolAnnounce_payloadPools_put_regionUri(&pools, "shm:file?path=/dev/shm/pool", 27);

    tensor_pool_shmPoolAnnounce_put_headerRegionUri(&announce, "shm:file?path=/dev/shm/hdr", 26);

    return (size_t)tensor_pool_shmPoolAnnounce_sbe_position(&announce);
}

static size_t encode_tracelink_set(uint8_t *buffer, size_t capacity)
{
    tp_tracelink_set_t set;
    uint64_t parents[2] = { 11, 22 };
    size_t encoded = 0;

    memset(&set, 0, sizeof(set));
    set.stream_id = 10;
    set.epoch = 1;
    set.seq = 2;
    set.trace_id = 100;
    set.parents = parents;
    set.parent_count = 2;

    assert(tp_tracelink_set_encode(buffer, capacity, &set, &encoded) == 0);
    return encoded;
}

static size_t encode_sequence_merge_map(uint8_t *buffer, size_t capacity)
{
    tp_sequence_merge_rule_t rules[1];
    tp_sequence_merge_map_t map;
    size_t encoded = 0;

    rules[0].input_stream_id = 1;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 5;
    rules[0].window_size = 0;

    memset(&map, 0, sizeof(map));
    map.out_stream_id = 9;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rule_count = 1;
    map.rules = rules;

    assert(tp_sequence_merge_map_encode(buffer, capacity, &map, &encoded) == 0);
    return encoded;
}

static size_t encode_timestamp_merge_map(uint8_t *buffer, size_t capacity)
{
    tp_timestamp_merge_rule_t rules[1];
    tp_timestamp_merge_map_t map;
    size_t encoded = 0;

    rules[0].input_stream_id = 1;
    rules[0].rule_type = TP_MERGE_TIME_OFFSET_NS;
    rules[0].timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;
    rules[0].offset_ns = 1;
    rules[0].window_ns = 0;

    memset(&map, 0, sizeof(map));
    map.out_stream_id = 9;
    map.epoch = 1;
    map.clock_domain = TP_CLOCK_DOMAIN_MONOTONIC;
    map.stale_timeout_ns = TP_NULL_U64;
    map.lateness_ns = TP_NULL_U64;
    map.rule_count = 1;
    map.rules = rules;

    assert(tp_timestamp_merge_map_encode(buffer, capacity, &map, &encoded) == 0);
    return encoded;
}

static void test_control_poller_dispatch(void)
{
    tp_control_poller_state_t state;
    tp_control_poller_t poller;
    tp_control_handlers_t handlers;
    tp_join_barrier_t seq_barrier;
    tp_join_barrier_t ts_barrier;
    tp_join_barrier_t latest_barrier;
    uint8_t buffer[1024];
    size_t len;

    memset(&state, 0, sizeof(state));
    memset(&poller, 0, sizeof(poller));
    memset(&handlers, 0, sizeof(handlers));

    assert(tp_join_barrier_init(&seq_barrier, TP_JOIN_BARRIER_SEQUENCE, 4) == 0);
    assert(tp_join_barrier_init(&ts_barrier, TP_JOIN_BARRIER_TIMESTAMP, 4) == 0);
    assert(tp_join_barrier_init(&latest_barrier, TP_JOIN_BARRIER_LATEST_VALUE, 4) == 0);

    handlers.on_shm_pool_announce = tp_on_shm_pool_announce;
    handlers.on_consumer_hello = tp_on_consumer_hello;
    handlers.on_consumer_config = tp_on_consumer_config;
    handlers.on_control_response = tp_on_control_response;
    handlers.on_data_source_announce = tp_on_data_source_announce;
    handlers.on_data_source_meta_begin = tp_on_data_source_meta_begin;
    handlers.on_data_source_meta_attr = tp_on_data_source_meta_attr;
    handlers.on_data_source_meta_end = tp_on_data_source_meta_end;
    handlers.on_tracelink_set = tp_on_tracelink_set;
    handlers.sequence_join_barrier = &seq_barrier;
    handlers.timestamp_join_barrier = &ts_barrier;
    handlers.latest_join_barrier = &latest_barrier;
    handlers.clientd = &state;

    poller.handlers = handlers;

    len = encode_shm_pool_announce(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_consumer_hello(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_consumer_config(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_control_response(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_data_source_announce(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_data_source_meta(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_tracelink_set(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_sequence_merge_map(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_timestamp_merge_map(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    assert(state.shm_pool == 1);
    assert(state.consumer_hello == 1);
    assert(state.consumer_config == 1);
    assert(state.control_response == 1);
    assert(state.data_source_announce == 1);
    assert(state.data_source_meta_begin == 1);
    assert(state.data_source_meta_attr == 1);
    assert(state.data_source_meta_end == 1);
    assert(state.tracelink == 1);
    assert(seq_barrier.rule_count == 1);
    assert(ts_barrier.rule_count == 1);
    assert(latest_barrier.rule_count == 1);

    tp_join_barrier_close(&seq_barrier);
    tp_join_barrier_close(&ts_barrier);
    tp_join_barrier_close(&latest_barrier);
}

static void test_control_poller_init_errors(void)
{
    tp_control_poller_t poller;
    tp_client_t client;

    memset(&poller, 0, sizeof(poller));
    memset(&client, 0, sizeof(client));

    assert(tp_control_poller_init(NULL, NULL, NULL) < 0);
    assert(tp_control_poller_init(&poller, NULL, NULL) < 0);
    assert(tp_control_poller_init(&poller, &client, NULL) < 0);
}

void tp_test_control_poller(void)
{
    test_control_poller_init_errors();
    test_control_poller_dispatch();
}
