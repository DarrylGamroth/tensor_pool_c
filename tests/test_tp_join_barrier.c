#include "tensor_pool/tp_join_barrier.h"
#include "tensor_pool/tp_merge_map.h"

#include "merge/tensor_pool/messageHeader.h"
#include "merge/tensor_pool/sequenceMergeMapAnnounce.h"
#include "merge/tensor_pool/timestampMergeMapAnnounce.h"

#include <assert.h>
#include <string.h>

static void test_sequence_merge_map_decode(void)
{
    uint8_t buffer[512];
    tp_sequence_merge_rule_t rules[2];
    tp_sequence_merge_map_t map;
    size_t encoded_len = 0;

    memset(rules, 0, sizeof(rules));
    rules[0].input_stream_id = 10;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = -1;
    rules[1].input_stream_id = 11;
    rules[1].rule_type = TP_MERGE_RULE_WINDOW;
    rules[1].window_size = 4;

    map.out_stream_id = 5;
    map.epoch = 7;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rules = rules;
    map.rule_count = 2;

    assert(tp_sequence_merge_map_encode(buffer, sizeof(buffer), &map, &encoded_len) == 0);
    assert(tp_sequence_merge_map_decode(buffer, encoded_len, &map, rules, 2) == 0);
    assert(map.rule_count == 2);
    assert(map.rules[0].offset == -1);
    assert(map.rules[1].window_size == 4);
}

static void test_sequence_merge_map_decode_invalid_rule(void)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_sequenceMergeMapAnnounce announce;
    struct tensor_pool_sequenceMergeMapAnnounce_rules rules;
    tp_sequence_merge_map_t map;
    tp_sequence_merge_rule_t out_rules[1];

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_sequenceMergeMapAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_sequenceMergeMapAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_sequenceMergeMapAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version());

    tensor_pool_sequenceMergeMapAnnounce_wrap_for_encode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(buffer));
    tensor_pool_sequenceMergeMapAnnounce_set_outStreamId(&announce, 10);
    tensor_pool_sequenceMergeMapAnnounce_set_epoch(&announce, 1);
    tensor_pool_sequenceMergeMapAnnounce_set_staleTimeoutNs(
        &announce,
        tensor_pool_sequenceMergeMapAnnounce_staleTimeoutNs_null_value());

    tensor_pool_sequenceMergeMapAnnounce_rules_wrap_for_encode(
        &rules,
        (char *)buffer,
        1,
        tensor_pool_sequenceMergeMapAnnounce_sbe_position_ptr(&announce),
        tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_sequenceMergeMapAnnounce_rules_next(&rules);
    tensor_pool_sequenceMergeMapAnnounce_rules_set_inputStreamId(&rules, 2);
    tensor_pool_sequenceMergeMapAnnounce_rules_set_ruleType(&rules, tensor_pool_mergeRuleType_OFFSET);
    tensor_pool_sequenceMergeMapAnnounce_rules_set_offset(
        &rules,
        tensor_pool_sequenceMergeMapAnnounce_rules_offset_null_value());
    tensor_pool_sequenceMergeMapAnnounce_rules_set_windowSize(&rules, 1);

    assert(tp_sequence_merge_map_decode(
        buffer,
        (size_t)tensor_pool_sequenceMergeMapAnnounce_sbe_position(&announce),
        &map,
        out_rules,
        1) < 0);
}

static void test_sequence_merge_map_request_decode(void)
{
    uint8_t buffer[128];
    size_t encoded_len = 0;
    uint32_t out_stream_id = 0;
    uint64_t epoch = 0;

    assert(tp_sequence_merge_map_request_encode(buffer, sizeof(buffer), 9, 42, &encoded_len) == 0);
    assert(tp_sequence_merge_map_request_decode(buffer, encoded_len, &out_stream_id, &epoch) == 0);
    assert(out_stream_id == 9);
    assert(epoch == 42);
}

static void test_sequence_merge_map_decode_schema_mismatch(void)
{
    uint8_t buffer[512];
    tp_sequence_merge_rule_t rules[1];
    tp_sequence_merge_map_t map;
    struct tensor_pool_messageHeader header;
    size_t encoded_len = 0;

    memset(rules, 0, sizeof(rules));
    rules[0].input_stream_id = 99;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 0;

    map.out_stream_id = 15;
    map.epoch = 3;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rules = rules;
    map.rule_count = 1;

    assert(tp_sequence_merge_map_encode(buffer, sizeof(buffer), &map, &encoded_len) == 0);
    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_schemaId(&header, 1);

    assert(tp_sequence_merge_map_decode(buffer, encoded_len, &map, rules, 1) == 1);
}

static void test_timestamp_merge_map_decode(void)
{
    uint8_t buffer[512];
    tp_timestamp_merge_rule_t rules[1];
    tp_timestamp_merge_map_t map;
    size_t encoded_len = 0;

    memset(rules, 0, sizeof(rules));
    rules[0].input_stream_id = 20;
    rules[0].rule_type = TP_MERGE_TIME_OFFSET_NS;
    rules[0].timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;
    rules[0].offset_ns = 0;

    map.out_stream_id = 8;
    map.epoch = 2;
    map.stale_timeout_ns = TP_NULL_U64;
    map.clock_domain = TP_CLOCK_DOMAIN_MONOTONIC;
    map.lateness_ns = 0;
    map.rules = rules;
    map.rule_count = 1;

    assert(tp_timestamp_merge_map_encode(buffer, sizeof(buffer), &map, &encoded_len) == 0);
    assert(tp_timestamp_merge_map_decode(buffer, encoded_len, &map, rules, 1) == 0);
    assert(map.rule_count == 1);
    assert(map.rules[0].timestamp_source == TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR);
}

static void test_join_barrier_sequence(void)
{
    tp_join_barrier_t barrier;
    tp_sequence_merge_rule_t rules[2];
    tp_sequence_merge_map_t map;

    rules[0].input_stream_id = 1;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 0;
    rules[0].window_size = 0;
    rules[1].input_stream_id = 2;
    rules[1].rule_type = TP_MERGE_RULE_WINDOW;
    rules[1].offset = 0;
    rules[1].window_size = 3;

    map.out_stream_id = 10;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rules = rules;
    map.rule_count = 2;

    assert(tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_SEQUENCE, 2) == 0);
    assert(tp_join_barrier_apply_sequence_map(&barrier, &map) == 0);
    assert(tp_join_barrier_is_ready_sequence(&barrier, 1, 0) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 1, 5, 1) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 2, 5, 1) == 0);
    assert(tp_join_barrier_is_ready_sequence(&barrier, 5, 2) == 1);
    tp_join_barrier_close(&barrier);
}

static void test_join_barrier_epoch_change_blocks(void)
{
    tp_join_barrier_t barrier;
    tp_sequence_merge_rule_t rules[1];
    tp_sequence_merge_map_t map;

    rules[0].input_stream_id = 8;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 0;
    rules[0].window_size = 0;

    map.out_stream_id = 40;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rules = rules;
    map.rule_count = 1;

    assert(tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_SEQUENCE, 1) == 0);
    assert(tp_join_barrier_apply_sequence_map(&barrier, &map) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 8, 3, 1) == 0);
    assert(tp_join_barrier_is_ready_sequence(&barrier, 3, 2) == 1);

    map.epoch = 2;
    assert(tp_join_barrier_apply_sequence_map(&barrier, &map) == 0);
    assert(tp_join_barrier_is_ready_sequence(&barrier, 3, 3) == 0);
    tp_join_barrier_close(&barrier);
}

static void test_join_barrier_timestamp(void)
{
    tp_join_barrier_t barrier;
    tp_timestamp_merge_rule_t rules[1];
    tp_timestamp_merge_map_t map;

    rules[0].input_stream_id = 3;
    rules[0].rule_type = TP_MERGE_TIME_OFFSET_NS;
    rules[0].timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;
    rules[0].offset_ns = 0;
    rules[0].window_ns = 0;

    map.out_stream_id = 12;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.clock_domain = TP_CLOCK_DOMAIN_MONOTONIC;
    map.lateness_ns = TP_NULL_U64;
    map.rules = rules;
    map.rule_count = 1;

    assert(tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_TIMESTAMP, 1) == 0);
    assert(tp_join_barrier_apply_timestamp_map(&barrier, &map) == 0);
    assert(tp_join_barrier_update_observed_time(&barrier, 3, TP_NULL_U64, TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR, TP_CLOCK_DOMAIN_MONOTONIC, 1) == 0);
    assert(tp_join_barrier_update_observed_time(&barrier, 3, 100, TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR, TP_CLOCK_DOMAIN_MONOTONIC, 1) == 0);
    assert(tp_join_barrier_is_ready_timestamp(&barrier, 100, TP_CLOCK_DOMAIN_MONOTONIC, 2) == 1);
    assert(tp_join_barrier_is_ready_timestamp(&barrier, 99, TP_CLOCK_DOMAIN_MONOTONIC, 3) < 0);
    assert(tp_join_barrier_is_ready_timestamp(&barrier, 100, TP_CLOCK_DOMAIN_REALTIME_SYNCED, 2) < 0);
    tp_join_barrier_close(&barrier);
}

static void test_join_barrier_latest_value(void)
{
    tp_join_barrier_t barrier;
    tp_sequence_merge_rule_t rules[2];
    tp_sequence_merge_map_t map;
    tp_latest_selection_t selections[2];
    size_t selection_count = 0;

    rules[0].input_stream_id = 4;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 0;
    rules[0].window_size = 0;
    rules[1].input_stream_id = 5;
    rules[1].rule_type = TP_MERGE_RULE_WINDOW;
    rules[1].offset = 0;
    rules[1].window_size = 1;

    map.out_stream_id = 20;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rules = rules;
    map.rule_count = 2;

    assert(tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_LATEST_VALUE, 2) == 0);
    assert(tp_join_barrier_apply_latest_value_sequence_map(&barrier, &map) == 0);
    assert(tp_join_barrier_is_ready_latest(&barrier, 0, 0, 0, 0) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 4, 1, 1) == 0);
    assert(tp_join_barrier_is_ready_latest(&barrier, 0, 0, 0, 1) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 5, 2, 1) == 0);
    assert(tp_join_barrier_is_ready_latest(&barrier, 0, 0, 0, 1) == 1);
    assert(tp_join_barrier_collect_latest(&barrier, selections, 2, &selection_count) == 0);
    assert(selection_count == 2);
    assert(selections[0].seq == 1);
    assert(selections[1].seq == 2);
    assert(tp_join_barrier_invalidate_latest(&barrier, 4) == 0);
    assert(tp_join_barrier_is_ready_latest(&barrier, 0, 0, 0, 1) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 4, 3, 2) == 0);
    assert(tp_join_barrier_is_ready_latest(&barrier, 0, 0, 0, 2) == 1);
    tp_join_barrier_close(&barrier);
}

static void test_join_barrier_latest_timestamp_ordering(void)
{
    tp_join_barrier_t barrier;
    tp_timestamp_merge_rule_t rules[1];
    tp_timestamp_merge_map_t map;

    rules[0].input_stream_id = 6;
    rules[0].rule_type = TP_MERGE_TIME_OFFSET_NS;
    rules[0].timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;
    rules[0].offset_ns = 0;
    rules[0].window_ns = 0;

    map.out_stream_id = 50;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.clock_domain = TP_CLOCK_DOMAIN_MONOTONIC;
    map.lateness_ns = 0;
    map.rules = rules;
    map.rule_count = 1;

    assert(tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_LATEST_VALUE, 1) == 0);
    tp_join_barrier_set_latest_ordering(&barrier, TP_LATEST_ORDERING_TIMESTAMP);
    assert(tp_join_barrier_apply_latest_value_timestamp_map(&barrier, &map) == 0);
    assert(tp_join_barrier_update_observed_time(&barrier, 6, 100, TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR, TP_CLOCK_DOMAIN_MONOTONIC, 1) == 0);
    assert(tp_join_barrier_is_ready_latest(&barrier, 0, 100, TP_CLOCK_DOMAIN_MONOTONIC, 2) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 6, 1, 2) == 0);
    assert(tp_join_barrier_is_ready_latest(&barrier, 0, 100, TP_CLOCK_DOMAIN_MONOTONIC, 3) == 1);
    tp_join_barrier_close(&barrier);
}

static void test_join_barrier_stale_inputs(void)
{
    tp_join_barrier_t barrier;
    tp_sequence_merge_rule_t rules[2];
    tp_sequence_merge_map_t map;
    uint32_t stale_ids[2];
    size_t stale_count = 0;

    rules[0].input_stream_id = 6;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 0;
    rules[0].window_size = 0;
    rules[1].input_stream_id = 7;
    rules[1].rule_type = TP_MERGE_RULE_OFFSET;
    rules[1].offset = 0;
    rules[1].window_size = 0;

    map.out_stream_id = 30;
    map.epoch = 1;
    map.stale_timeout_ns = 5;
    map.rules = rules;
    map.rule_count = 2;

    assert(tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_SEQUENCE, 2) == 0);
    tp_join_barrier_set_allow_stale(&barrier, true);
    assert(tp_join_barrier_apply_sequence_map(&barrier, &map) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 6, 1, 1) == 0);
    assert(tp_join_barrier_update_observed_seq(&barrier, 7, 1, 6) == 0);
    assert(tp_join_barrier_collect_stale_inputs(&barrier, 10, stale_ids, 2, &stale_count) == 0);
    assert(stale_count == 1);
    assert(stale_ids[0] == 6);
    tp_join_barrier_close(&barrier);
}

void tp_test_join_barrier(void)
{
    test_sequence_merge_map_decode();
    test_sequence_merge_map_decode_invalid_rule();
    test_sequence_merge_map_request_decode();
    test_sequence_merge_map_decode_schema_mismatch();
    test_timestamp_merge_map_decode();
    test_join_barrier_sequence();
    test_join_barrier_epoch_change_blocks();
    test_join_barrier_timestamp();
    test_join_barrier_latest_value();
    test_join_barrier_latest_timestamp_ordering();
    test_join_barrier_stale_inputs();
}
