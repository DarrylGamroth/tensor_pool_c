#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_trace.h"
#include "tensor_pool/tp_tracelink.h"
#include "tp_aeron_wrap.h"

#include "trace/tensor_pool/messageHeader.h"
#include "trace/tensor_pool/traceLinkSet.h"

#include "aeronc.h"

#include <assert.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static void test_tracelink_resolve_parent_limit(void)
{
    tp_trace_id_generator_t generator;
    test_trace_clock_t clock;
    uint64_t parents[TP_TRACELINK_MAX_PARENTS + 1];
    uint64_t trace_id = 0;
    int emit = -1;
    int result = -1;
    size_t i;

    memset(&generator, 0, sizeof(generator));
    clock.now_ms = 61;

    if (tp_trace_id_generator_init(&generator, 2, 2, 3, 0, test_trace_clock_ms, &clock) < 0)
    {
        goto cleanup;
    }

    for (i = 0; i < sizeof(parents) / sizeof(parents[0]); i++)
    {
        parents[i] = (uint64_t)(100 + i);
    }

    if (tp_tracelink_resolve_trace_id(
        &generator,
        parents,
        TP_TRACELINK_MAX_PARENTS + 1,
        &trace_id,
        &emit) < 0)
    {
        result = 0;
    }

cleanup:
    assert(result == 0);
}

static void test_tracelink_decode_rejects_too_many(void)
{
    uint8_t buffer[256];
    tp_tracelink_set_t set;
    uint64_t parents[2] = { 10, 20 };
    tp_tracelink_set_t decoded;
    uint64_t decoded_parents[1];
    size_t encoded_len = 0;
    int result = -1;

    memset(&set, 0, sizeof(set));
    set.stream_id = 1;
    set.epoch = 1;
    set.seq = 1;
    set.trace_id = 5;
    set.parents = parents;
    set.parent_count = 2;

    if (tp_tracelink_set_encode(buffer, sizeof(buffer), &set, &encoded_len) < 0)
    {
        goto cleanup;
    }

    if (tp_tracelink_set_decode(buffer, encoded_len, &decoded, decoded_parents, 1) < 0)
    {
        result = 0;
    }

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

static int tp_test_driver_active(const char *aeron_dir)
{
    aeron_cnc_t *cnc = NULL;
    int64_t heartbeat = 0;
    int64_t now_ms = 0;
    int64_t age_ms = 0;

    if (NULL == aeron_dir || aeron_dir[0] == '\0')
    {
        return 0;
    }

    if (aeron_cnc_init(&cnc, aeron_dir, 100) < 0)
    {
        return 0;
    }

    heartbeat = aeron_cnc_to_driver_heartbeat(cnc);
    now_ms = aeron_epoch_clock();
    age_ms = now_ms - heartbeat;

    aeron_cnc_close(cnc);
    return heartbeat > 0 && age_ms <= 1000;
}

static int tp_test_wait_for_publication(tp_client_t *client, tp_publication_t *publication)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        if (aeron_publication_is_connected(tp_publication_handle(publication)))
        {
            return 0;
        }
        tp_client_do_work(client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

    return -1;
}

static int tp_test_wait_for_subscription(tp_client_t *client, tp_subscription_t *subscription)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        if (aeron_subscription_is_connected(tp_subscription_handle(subscription)))
        {
            return 0;
        }
        tp_client_do_work(client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

    return -1;
}

static int tp_test_add_publication(tp_client_t *client, const char *channel, int32_t stream_id, tp_publication_t **out)
{
    tp_async_add_publication_t *async_add = NULL;

    if (tp_client_async_add_publication(client, channel, stream_id, &async_add) < 0)
    {
        return -1;
    }

    *out = NULL;
    while (NULL == *out)
    {
        if (tp_client_async_add_publication_poll(out, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(client);
    }

    return 0;
}

static void test_tracelink_send_errors(void)
{
    tp_producer_t producer;
    tp_tracelink_set_t set;
    uint64_t parent = 101;
    tp_publication_t *publication = NULL;
    int result = -1;

    memset(&producer, 0, sizeof(producer));
    memset(&set, 0, sizeof(set));

    set.stream_id = 1;
    set.epoch = 2;
    set.seq = 3;
    set.trace_id = 5;
    set.parents = &parent;
    set.parent_count = 1;

    assert(tp_producer_send_tracelink_set(NULL, NULL) < 0);
    assert(tp_producer_send_tracelink_set(&producer, &set) < 0);

    if (tp_publication_wrap(&publication, (aeron_publication_t *)0x1) < 0)
    {
        goto cleanup;
    }
    producer.control_publication = publication;
    producer.stream_id = 10;
    producer.epoch = 20;
    assert(tp_producer_send_tracelink_set(&producer, NULL) < 0);

    set.stream_id = producer.stream_id + 1;
    set.epoch = producer.epoch;
    assert(tp_producer_send_tracelink_set(&producer, &set) < 0);

    result = 0;

cleanup:
    tp_publication_close(&publication);
    assert(result == 0);
}

static void test_tracelink_set_from_claim(void)
{
    tp_producer_t producer;
    tp_buffer_claim_t claim;
    tp_tracelink_set_t set;
    uint64_t parents[2] = { 11, 22 };
    int result = -1;

    memset(&producer, 0, sizeof(producer));
    memset(&claim, 0, sizeof(claim));
    memset(&set, 0, sizeof(set));

    producer.stream_id = 100;
    producer.epoch = 7;
    claim.seq = 9;
    claim.trace_id = 555;

    if (tp_tracelink_set_from_claim(&producer, &claim, parents, 2, &set) != 0)
    {
        goto cleanup;
    }

    assert(set.stream_id == producer.stream_id);
    assert(set.epoch == producer.epoch);
    assert(set.seq == claim.seq);
    assert(set.trace_id == claim.trace_id);
    assert(set.parent_count == 2);

    claim.trace_id = 0;
    if (tp_tracelink_set_from_claim(&producer, &claim, parents, 2, &set) == 0)
    {
        goto cleanup;
    }

    if (tp_tracelink_set_from_claim(NULL, &claim, parents, 2, &set) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

static int tp_test_tracelink_validator_fail(const tp_tracelink_set_t *set, void *clientd)
{
    (void)set;
    (void)clientd;
    return -1;
}

static void test_tracelink_send(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_producer_t producer;
    tp_publication_t *control_pub = NULL;
    uint64_t parents[80];
    tp_tracelink_set_t set;
    const char *aeron_dir = getenv("AERON_DIR");
    int result = -1;
    size_t i;

    if (!tp_test_driver_active(aeron_dir))
    {
        return;
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(&client, 0, sizeof(client));
    memset(&producer, 0, sizeof(producer));
    memset(&set, 0, sizeof(set));

    if (tp_client_context_init(&ctx) < 0)
    {
        return;
    }
    tp_client_context_set_aeron_dir(&ctx, aeron_dir);
    tp_client_context_set_use_agent_invoker(&ctx, true);
    tp_client_context_set_control_channel(&ctx, "aeron:ipc", 1000);
    tp_client_context_set_announce_channel(&ctx, "aeron:ipc", 1001);
    tp_client_context_set_qos_channel(&ctx, "aeron:ipc", 1200);
    tp_client_context_set_metadata_channel(&ctx, "aeron:ipc", 1300);
    tp_client_context_set_descriptor_channel(&ctx, "aeron:ipc", 1100);

    if (tp_client_init(&client, &ctx) < 0 || tp_client_start(&client) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1000, &control_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_wait_for_publication(&client, control_pub) < 0 ||
        tp_test_wait_for_subscription(&client, tp_client_control_subscription(&client)) < 0)
    {
        goto cleanup;
    }

    producer.control_publication = control_pub;
    producer.stream_id = 10000;
    producer.epoch = 1;

    for (i = 0; i < sizeof(parents) / sizeof(parents[0]); i++)
    {
        parents[i] = (uint64_t)(1000 + i);
    }

    set.stream_id = producer.stream_id;
    set.epoch = producer.epoch;
    set.seq = 1;
    set.trace_id = 42;
    set.parents = parents;
    set.parent_count = sizeof(parents) / sizeof(parents[0]);

    if (tp_producer_send_tracelink_set(&producer, &set) != 0)
    {
        goto cleanup;
    }

    set.stream_id = 9999;
    if (tp_producer_send_tracelink_set(&producer, &set) == 0)
    {
        goto cleanup;
    }

    set.stream_id = producer.stream_id;
    producer.tracelink_validator = tp_test_tracelink_validator_fail;
    if (tp_producer_send_tracelink_set(&producer, &set) == 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_publication_close(&control_pub);
    tp_client_close(&client);
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
    test_tracelink_resolve_parent_limit();
    test_tracelink_encode_decode();
    test_tracelink_encode_rejects_duplicate();
    test_tracelink_encode_rejects_empty();
    test_tracelink_decode_rejects_duplicate();
    test_tracelink_decode_rejects_empty();
    test_tracelink_decode_rejects_too_many();
    test_tracelink_decode_schema_mismatch();
    test_tracelink_send_errors();
    test_tracelink_set_from_claim();
    test_tracelink_send();
}
