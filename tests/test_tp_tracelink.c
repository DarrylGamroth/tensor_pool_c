#include "tensor_pool/tp_trace.h"
#include "tensor_pool/tp_tracelink.h"

#include "trace/tensor_pool/messageHeader.h"
#include "trace/tensor_pool/traceLinkSet.h"

#include <assert.h>
#include <string.h>

typedef struct test_trace_clock_stct
{
    uint64_t now_ms;
}
test_trace_clock_t;

static uint64_t test_trace_clock_ms(void *clientd)
{
    test_trace_clock_t *clock = (test_trace_clock_t *)clientd;

    return clock->now_ms;
}

static void test_trace_id_generator_basic(void)
{
    tp_trace_id_generator_t generator;
    test_trace_clock_t clock;
    uint64_t trace_a = 0;
    uint64_t trace_b = 0;
    int result = -1;

    memset(&generator, 0, sizeof(generator));
    clock.now_ms = 1234;

    if (tp_trace_id_generator_init(&generator, 2, 2, 3, 0, test_trace_clock_ms, &clock) < 0)
    {
        goto cleanup;
    }

    trace_a = tp_trace_id_generator_next(&generator);
    trace_b = tp_trace_id_generator_next(&generator);

    if (trace_a == 0 || trace_b == 0)
    {
        goto cleanup;
    }

    assert(tp_trace_id_extract_timestamp(&generator, trace_a) == 1234);
    assert(tp_trace_id_extract_node_id(&generator, trace_a) == 3);
    assert(tp_trace_id_extract_sequence(&generator, trace_a) == 0);
    assert(tp_trace_id_extract_sequence(&generator, trace_b) == 1);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_trace_id_generator_invalid_node(void)
{
    tp_trace_id_generator_t generator;
    test_trace_clock_t clock;
    int result = -1;

    memset(&generator, 0, sizeof(generator));
    clock.now_ms = 5;

    if (tp_trace_id_generator_init(&generator, 2, 2, 5, 0, test_trace_clock_ms, &clock) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_resolve_root(void)
{
    tp_trace_id_generator_t generator;
    test_trace_clock_t clock;
    uint64_t trace_id = 0;
    int emit = -1;
    int result = -1;

    memset(&generator, 0, sizeof(generator));
    clock.now_ms = 55;

    if (tp_trace_id_generator_init(&generator, 2, 2, 3, 0, test_trace_clock_ms, &clock) < 0)
    {
        goto cleanup;
    }

    if (tp_tracelink_resolve_trace_id(&generator, NULL, 0, &trace_id, &emit) < 0)
    {
        goto cleanup;
    }

    assert(trace_id != 0);
    assert(emit == 0);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_resolve_single_parent(void)
{
    uint64_t parent = 1234;
    uint64_t trace_id = 0;
    int emit = -1;
    int result = -1;

    if (tp_tracelink_resolve_trace_id(NULL, &parent, 1, &trace_id, &emit) < 0)
    {
        goto cleanup;
    }

    assert(trace_id == parent);
    assert(emit == 0);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_resolve_multi_parent(void)
{
    tp_trace_id_generator_t generator;
    test_trace_clock_t clock;
    uint64_t parents[2] = { 77, 88 };
    uint64_t trace_id = 0;
    int emit = -1;
    int result = -1;

    memset(&generator, 0, sizeof(generator));
    clock.now_ms = 57;

    if (tp_trace_id_generator_init(&generator, 2, 2, 2, 0, test_trace_clock_ms, &clock) < 0)
    {
        goto cleanup;
    }

    if (tp_tracelink_resolve_trace_id(&generator, parents, 2, &trace_id, &emit) < 0)
    {
        goto cleanup;
    }

    assert(trace_id != 0);
    assert(emit == 1);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_resolve_rejects_invalid(void)
{
    tp_trace_id_generator_t generator;
    test_trace_clock_t clock;
    uint64_t parents[2] = { 5, 5 };
    uint64_t trace_id = 0;
    int emit = -1;
    int result = -1;

    memset(&generator, 0, sizeof(generator));
    clock.now_ms = 60;

    if (tp_trace_id_generator_init(&generator, 2, 2, 3, 0, test_trace_clock_ms, &clock) < 0)
    {
        goto cleanup;
    }

    if (tp_tracelink_resolve_trace_id(&generator, parents, 2, &trace_id, &emit) == 0)
    {
        goto cleanup;
    }

    if (tp_tracelink_resolve_trace_id(NULL, NULL, 0, &trace_id, &emit) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_encode_decode(void)
{
    uint8_t buffer[256];
    uint64_t parents[3] = { 10, 20, 30 };
    tp_tracelink_set_t set;
    tp_tracelink_set_t decoded;
    uint64_t decoded_parents[4];
    size_t encoded_len = 0;
    int result = -1;

    memset(&set, 0, sizeof(set));
    set.stream_id = 7;
    set.epoch = 99;
    set.seq = 123;
    set.trace_id = 9001;
    set.parents = parents;
    set.parent_count = 3;

    if (tp_tracelink_set_encode(buffer, sizeof(buffer), &set, &encoded_len) < 0)
    {
        goto cleanup;
    }

    if (tp_tracelink_set_decode(buffer, encoded_len, &decoded, decoded_parents, 4) < 0)
    {
        goto cleanup;
    }

    assert(decoded.stream_id == set.stream_id);
    assert(decoded.epoch == set.epoch);
    assert(decoded.seq == set.seq);
    assert(decoded.trace_id == set.trace_id);
    assert(decoded.parent_count == set.parent_count);
    assert(decoded.parents == decoded_parents);
    assert(decoded.parents[0] == 10);
    assert(decoded.parents[1] == 20);
    assert(decoded.parents[2] == 30);

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_encode_rejects_duplicate(void)
{
    uint8_t buffer[256];
    uint64_t parents[2] = { 42, 42 };
    tp_tracelink_set_t set;
    size_t encoded_len = 0;
    int result = -1;

    memset(&set, 0, sizeof(set));
    set.stream_id = 1;
    set.epoch = 2;
    set.seq = 3;
    set.trace_id = 99;
    set.parents = parents;
    set.parent_count = 2;

    if (tp_tracelink_set_encode(buffer, sizeof(buffer), &set, &encoded_len) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_encode_rejects_empty(void)
{
    uint8_t buffer[256];
    tp_tracelink_set_t set;
    size_t encoded_len = 0;
    int result = -1;

    memset(&set, 0, sizeof(set));
    set.stream_id = 1;
    set.epoch = 2;
    set.seq = 3;
    set.trace_id = 99;
    set.parents = NULL;
    set.parent_count = 0;

    if (tp_tracelink_set_encode(buffer, sizeof(buffer), &set, &encoded_len) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_decode_rejects_duplicate(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_traceLinkSet msg;
    struct tensor_pool_traceLinkSet_parents parents_group;
    tp_tracelink_set_t decoded;
    uint64_t decoded_parents[2];
    int result = -1;

    memset(buffer, 0, sizeof(buffer));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_traceLinkSet_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_traceLinkSet_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_traceLinkSet_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_traceLinkSet_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_traceLinkSet_sbe_schema_version());

    tensor_pool_traceLinkSet_wrap_for_encode(
        &msg,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(buffer));
    tensor_pool_traceLinkSet_set_streamId(&msg, 5);
    tensor_pool_traceLinkSet_set_epoch(&msg, 8);
    tensor_pool_traceLinkSet_set_seq(&msg, 9);
    tensor_pool_traceLinkSet_set_traceId(&msg, 11);

    if (NULL == tensor_pool_traceLinkSet_parents_wrap_for_encode(
        &parents_group,
        (char *)buffer,
        2,
        tensor_pool_traceLinkSet_sbe_position_ptr(&msg),
        tensor_pool_traceLinkSet_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (NULL == tensor_pool_traceLinkSet_parents_next(&parents_group))
    {
        goto cleanup;
    }
    tensor_pool_traceLinkSet_parents_set_traceId(&parents_group, 42);

    if (NULL == tensor_pool_traceLinkSet_parents_next(&parents_group))
    {
        goto cleanup;
    }
    tensor_pool_traceLinkSet_parents_set_traceId(&parents_group, 42);

    if (tp_tracelink_set_decode(
        buffer,
        (size_t)tensor_pool_traceLinkSet_sbe_position(&msg),
        &decoded,
        decoded_parents,
        2) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_decode_rejects_empty(void)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader header;
    struct tensor_pool_traceLinkSet msg;
    struct tensor_pool_traceLinkSet_parents parents_group;
    tp_tracelink_set_t decoded;
    uint64_t decoded_parents[1];
    int result = -1;

    memset(buffer, 0, sizeof(buffer));

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_traceLinkSet_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_traceLinkSet_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_traceLinkSet_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_traceLinkSet_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_traceLinkSet_sbe_schema_version());

    tensor_pool_traceLinkSet_wrap_for_encode(
        &msg,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        sizeof(buffer));
    tensor_pool_traceLinkSet_set_streamId(&msg, 5);
    tensor_pool_traceLinkSet_set_epoch(&msg, 8);
    tensor_pool_traceLinkSet_set_seq(&msg, 9);
    tensor_pool_traceLinkSet_set_traceId(&msg, 11);

    if (NULL == tensor_pool_traceLinkSet_parents_wrap_for_encode(
        &parents_group,
        (char *)buffer,
        0,
        tensor_pool_traceLinkSet_sbe_position_ptr(&msg),
        tensor_pool_traceLinkSet_sbe_schema_version(),
        sizeof(buffer)))
    {
        goto cleanup;
    }

    if (tp_tracelink_set_decode(
        buffer,
        (size_t)tensor_pool_traceLinkSet_sbe_position(&msg),
        &decoded,
        decoded_parents,
        1) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

static void test_tracelink_decode_schema_mismatch(void)
{
    uint8_t buffer[256];
    tp_tracelink_set_t set;
    uint64_t parents[1] = { 101 };
    tp_tracelink_set_t decoded;
    uint64_t decoded_parents[1];
    size_t encoded_len = 0;
    struct tensor_pool_messageHeader header;
    int result = -1;

    memset(&set, 0, sizeof(set));
    set.stream_id = 1;
    set.epoch = 1;
    set.seq = 1;
    set.trace_id = 111;
    set.parents = parents;
    set.parent_count = 1;

    if (tp_tracelink_set_encode(buffer, sizeof(buffer), &set, &encoded_len) < 0)
    {
        goto cleanup;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_traceLinkSet_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_schemaId(&header, 999);

    if (tp_tracelink_set_decode(buffer, encoded_len, &decoded, decoded_parents, 1) != 1)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

void tp_test_tracelink(void)
{
    test_trace_id_generator_basic();
    test_trace_id_generator_invalid_node();
    test_tracelink_resolve_root();
    test_tracelink_resolve_single_parent();
    test_tracelink_resolve_multi_parent();
    test_tracelink_resolve_rejects_invalid();
    test_tracelink_encode_decode();
    test_tracelink_encode_rejects_duplicate();
    test_tracelink_encode_rejects_empty();
    test_tracelink_decode_rejects_duplicate();
    test_tracelink_decode_rejects_empty();
    test_tracelink_decode_schema_mismatch();
}
