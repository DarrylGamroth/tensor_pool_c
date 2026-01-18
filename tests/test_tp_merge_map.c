#include "tensor_pool/tp_merge_map.h"
#include "tensor_pool/tp_log.h"

#include "merge/tensor_pool/messageHeader.h"
#include "merge/tensor_pool/sequenceMergeMapAnnounce.h"
#include "merge/tensor_pool/sequenceMergeMapRequest.h"
#include "merge/tensor_pool/timestampMergeMapAnnounce.h"
#include "merge/tensor_pool/timestampMergeMapRequest.h"
#include "merge/tensor_pool/mergeRuleType.h"
#include "merge/tensor_pool/mergeTimeRuleType.h"
#include "merge/tensor_pool/timestampSource.h"

#include <assert.h>
#include <string.h>

static void test_sequence_merge_map_roundtrip(void)
{
    uint8_t buffer[512];
    tp_sequence_merge_rule_t rules[2];
    tp_sequence_merge_rule_t out_rules[2];
    tp_sequence_merge_map_t map;
    tp_sequence_merge_map_t out;
    size_t encoded = 0;

    memset(&map, 0, sizeof(map));
    memset(&out, 0, sizeof(out));

    rules[0].input_stream_id = 1;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 5;
    rules[0].window_size = 0;

    rules[1].input_stream_id = 2;
    rules[1].rule_type = TP_MERGE_RULE_WINDOW;
    rules[1].offset = 0;
    rules[1].window_size = 10;

    map.out_stream_id = 11;
    map.epoch = 42;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rule_count = 2;
    map.rules = rules;

    assert(tp_sequence_merge_map_encode(buffer, sizeof(buffer), &map, &encoded) == 0);
    assert(tp_sequence_merge_map_decode(buffer, encoded, &out, out_rules, 2) == 0);
    assert(out.out_stream_id == 11);
    assert(out.epoch == 42);
    assert(out.rule_count == 2);
    assert(out.rules[0].rule_type == TP_MERGE_RULE_OFFSET);
    assert(out.rules[1].rule_type == TP_MERGE_RULE_WINDOW);
}

static void test_sequence_merge_map_encode_invalid_rule(void)
{
    uint8_t buffer[128];
    tp_sequence_merge_rule_t rule;
    tp_sequence_merge_map_t map;
    size_t encoded = 0;

    memset(&map, 0, sizeof(map));
    memset(&rule, 0, sizeof(rule));

    rule.input_stream_id = 1;
    rule.rule_type = (tp_merge_rule_type_t)99;

    map.out_stream_id = 3;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rule_count = 1;
    map.rules = &rule;

    assert(tp_sequence_merge_map_encode(buffer, sizeof(buffer), &map, &encoded) < 0);
}

static void test_sequence_merge_map_decode_invalid_rule_type(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_sequenceMergeMapAnnounce announce;
    struct tensor_pool_sequenceMergeMapAnnounce_rules rules;
    tp_sequence_merge_map_t out;
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
    tensor_pool_sequenceMergeMapAnnounce_set_outStreamId(&announce, 9);
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
    tensor_pool_sequenceMergeMapAnnounce_rules_set_inputStreamId(&rules, 1);
    tensor_pool_sequenceMergeMapAnnounce_rules_set_ruleType(&rules, (enum tensor_pool_mergeRuleType)99);
    tensor_pool_sequenceMergeMapAnnounce_rules_set_offset(&rules, 5);
    tensor_pool_sequenceMergeMapAnnounce_rules_set_windowSize(
        &rules,
        tensor_pool_sequenceMergeMapAnnounce_rules_windowSize_null_value());

    assert(tp_sequence_merge_map_decode(
        buffer,
        (size_t)tensor_pool_sequenceMergeMapAnnounce_sbe_position(&announce),
        &out,
        out_rules,
        1) < 0);
}

static void test_sequence_merge_map_request_roundtrip(void)
{
    uint8_t buffer[128];
    size_t encoded = 0;
    uint32_t out_stream_id = 0;
    uint64_t epoch = 0;

    assert(tp_sequence_merge_map_request_encode(buffer, sizeof(buffer), 7, 9, &encoded) == 0);
    assert(tp_sequence_merge_map_request_decode(buffer, encoded, &out_stream_id, &epoch) == 0);
    assert(out_stream_id == 7);
    assert(epoch == 9);
}

static void test_timestamp_merge_map_roundtrip(void)
{
    uint8_t buffer[512];
    tp_timestamp_merge_rule_t rules[1];
    tp_timestamp_merge_rule_t out_rules[1];
    tp_timestamp_merge_map_t map;
    tp_timestamp_merge_map_t out;
    size_t encoded = 0;

    memset(&map, 0, sizeof(map));
    memset(&out, 0, sizeof(out));

    rules[0].input_stream_id = 3;
    rules[0].rule_type = TP_MERGE_TIME_OFFSET_NS;
    rules[0].offset_ns = 100;
    rules[0].window_ns = 0;
    rules[0].timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;

    map.out_stream_id = 12;
    map.epoch = 2;
    map.clock_domain = TP_CLOCK_DOMAIN_MONOTONIC;
    map.stale_timeout_ns = TP_NULL_U64;
    map.lateness_ns = TP_NULL_U64;
    map.rule_count = 1;
    map.rules = rules;

    assert(tp_timestamp_merge_map_encode(buffer, sizeof(buffer), &map, &encoded) == 0);
    assert(tp_timestamp_merge_map_decode(buffer, encoded, &out, out_rules, 1) == 0);
    assert(out.out_stream_id == 12);
    assert(out.rule_count == 1);
    assert(out.rules[0].timestamp_source == TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR);
}

static void test_timestamp_merge_map_encode_invalid_rule(void)
{
    uint8_t buffer[128];
    tp_timestamp_merge_rule_t rule;
    tp_timestamp_merge_map_t map;
    size_t encoded = 0;

    memset(&map, 0, sizeof(map));
    memset(&rule, 0, sizeof(rule));

    rule.input_stream_id = 4;
    rule.rule_type = TP_MERGE_TIME_WINDOW_NS;
    rule.window_ns = 0;
    rule.timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;

    map.out_stream_id = 4;
    map.epoch = 1;
    map.rule_count = 1;
    map.rules = &rule;

    assert(tp_timestamp_merge_map_encode(buffer, sizeof(buffer), &map, &encoded) < 0);
}

static void test_timestamp_merge_map_decode_invalid_source(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_timestampMergeMapAnnounce announce;
    struct tensor_pool_timestampMergeMapAnnounce_rules rules;
    tp_timestamp_merge_map_t out;
    tp_timestamp_merge_rule_t out_rules[1];

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_timestampMergeMapAnnounce_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_timestampMergeMapAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_timestampMergeMapAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_timestampMergeMapAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_timestampMergeMapAnnounce_sbe_schema_version());

    tensor_pool_timestampMergeMapAnnounce_wrap_for_encode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(buffer));
    tensor_pool_timestampMergeMapAnnounce_set_outStreamId(&announce, 9);
    tensor_pool_timestampMergeMapAnnounce_set_epoch(&announce, 1);
    tensor_pool_timestampMergeMapAnnounce_set_clockDomain(&announce, tensor_pool_clockDomain_MONOTONIC);
    tensor_pool_timestampMergeMapAnnounce_set_staleTimeoutNs(
        &announce,
        tensor_pool_timestampMergeMapAnnounce_staleTimeoutNs_null_value());
    tensor_pool_timestampMergeMapAnnounce_set_latenessNs(
        &announce,
        tensor_pool_timestampMergeMapAnnounce_latenessNs_null_value());

    tensor_pool_timestampMergeMapAnnounce_rules_wrap_for_encode(
        &rules,
        (char *)buffer,
        1,
        tensor_pool_timestampMergeMapAnnounce_sbe_position_ptr(&announce),
        tensor_pool_timestampMergeMapAnnounce_sbe_schema_version(),
        sizeof(buffer));

    tensor_pool_timestampMergeMapAnnounce_rules_next(&rules);
    tensor_pool_timestampMergeMapAnnounce_rules_set_inputStreamId(&rules, 1);
    tensor_pool_timestampMergeMapAnnounce_rules_set_ruleType(&rules, tensor_pool_mergeTimeRuleType_OFFSET_NS);
    tensor_pool_timestampMergeMapAnnounce_rules_set_timestampSource(&rules, (enum tensor_pool_timestampSource)99);
    tensor_pool_timestampMergeMapAnnounce_rules_set_offsetNs(&rules, 5);
    tensor_pool_timestampMergeMapAnnounce_rules_set_windowNs(
        &rules,
        tensor_pool_timestampMergeMapAnnounce_rules_windowNs_null_value());

    assert(tp_timestamp_merge_map_decode(
        buffer,
        (size_t)tensor_pool_timestampMergeMapAnnounce_sbe_position(&announce),
        &out,
        out_rules,
        1) < 0);
}

static void test_timestamp_merge_map_request_roundtrip(void)
{
    uint8_t buffer[128];
    size_t encoded = 0;
    uint32_t out_stream_id = 0;
    uint64_t epoch = 0;

    assert(tp_timestamp_merge_map_request_encode(buffer, sizeof(buffer), 7, 9, &encoded) == 0);
    assert(tp_timestamp_merge_map_request_decode(buffer, encoded, &out_stream_id, &epoch) == 0);
    assert(out_stream_id == 7);
    assert(epoch == 9);
}

static void test_merge_map_registry(void)
{
    tp_merge_map_registry_t registry;
    tp_log_t log;
    tp_sequence_merge_rule_t seq_rules[1];
    tp_sequence_merge_map_t seq_map;
    tp_timestamp_merge_rule_t ts_rules[1];
    tp_timestamp_merge_map_t ts_map;
    const tp_sequence_merge_map_t *found_seq = NULL;
    const tp_timestamp_merge_map_t *found_ts = NULL;

    assert(tp_merge_map_registry_init(NULL, 1, NULL) < 0);

    tp_log_init(&log);
    assert(tp_merge_map_registry_init(&registry, 1, &log) == 0);
    tp_merge_map_registry_set_log(&registry, &log);

    memset(&seq_map, 0, sizeof(seq_map));
    seq_rules[0].input_stream_id = 1;
    seq_rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    seq_rules[0].offset = 5;
    seq_map.out_stream_id = 10;
    seq_map.epoch = 1;
    seq_map.rule_count = 1;
    seq_map.rules = seq_rules;

    assert(tp_merge_map_registry_upsert_sequence(&registry, &seq_map, 100) == 0);
    found_seq = tp_merge_map_registry_find_sequence(&registry, 10, 1);
    assert(found_seq != NULL);
    assert(found_seq->rule_count == 1);

    memset(&ts_map, 0, sizeof(ts_map));
    ts_rules[0].input_stream_id = 2;
    ts_rules[0].rule_type = TP_MERGE_TIME_OFFSET_NS;
    ts_rules[0].offset_ns = 7;
    ts_rules[0].timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;
    ts_map.out_stream_id = 11;
    ts_map.epoch = 2;
    ts_map.rule_count = 1;
    ts_map.rules = ts_rules;

    assert(tp_merge_map_registry_upsert_timestamp(&registry, &ts_map, 200) < 0);

    found_ts = tp_merge_map_registry_find_timestamp(&registry, 11, 2);
    assert(found_ts == NULL);

    assert(tp_merge_map_registry_upsert_sequence(&registry, NULL, 0) < 0);
    assert(tp_merge_map_registry_upsert_timestamp(&registry, NULL, 0) < 0);

    tp_merge_map_registry_close(&registry);
}

void tp_test_merge_map(void)
{
    test_sequence_merge_map_roundtrip();
    test_sequence_merge_map_encode_invalid_rule();
    test_sequence_merge_map_decode_invalid_rule_type();
    test_sequence_merge_map_request_roundtrip();
    test_timestamp_merge_map_roundtrip();
    test_timestamp_merge_map_encode_invalid_rule();
    test_timestamp_merge_map_decode_invalid_source();
    test_timestamp_merge_map_request_roundtrip();
    test_merge_map_registry();
}
