#include "tensor_pool/tp_join_barrier.h"

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int tp_demo_sequence(void)
{
    tp_join_barrier_t barrier;
    tp_sequence_merge_rule_t rules[2];
    tp_sequence_merge_map_t map;
    int result = -1;

    memset(&barrier, 0, sizeof(barrier));
    memset(rules, 0, sizeof(rules));
    memset(&map, 0, sizeof(map));

    if (tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_SEQUENCE, 2) < 0)
    {
        return -1;
    }

    rules[0].input_stream_id = 100;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 0;

    rules[1].input_stream_id = 200;
    rules[1].rule_type = TP_MERGE_RULE_WINDOW;
    rules[1].window_size = 4;

    map.out_stream_id = 10;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.rules = rules;
    map.rule_count = 2;

    if (tp_join_barrier_apply_sequence_map(&barrier, &map) < 0)
    {
        goto cleanup;
    }

    (void)tp_join_barrier_update_observed_seq(&barrier, 100, 7, 1000);
    (void)tp_join_barrier_update_observed_seq(&barrier, 200, 7, 1000);

    printf("sequence ready=%d\n", tp_join_barrier_is_ready_sequence(&barrier, 7, 2000));
    result = 0;

cleanup:
    tp_join_barrier_close(&barrier);
    return result;
}

static int tp_demo_timestamp(void)
{
    tp_join_barrier_t barrier;
    tp_timestamp_merge_rule_t rules[1];
    tp_timestamp_merge_map_t map;
    int result = -1;

    memset(&barrier, 0, sizeof(barrier));
    memset(rules, 0, sizeof(rules));
    memset(&map, 0, sizeof(map));

    if (tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_TIMESTAMP, 1) < 0)
    {
        return -1;
    }

    rules[0].input_stream_id = 300;
    rules[0].rule_type = TP_MERGE_TIME_OFFSET_NS;
    rules[0].timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;
    rules[0].offset_ns = 0;

    map.out_stream_id = 11;
    map.epoch = 1;
    map.stale_timeout_ns = TP_NULL_U64;
    map.clock_domain = TP_CLOCK_DOMAIN_MONOTONIC;
    map.lateness_ns = 0;
    map.rules = rules;
    map.rule_count = 1;

    if (tp_join_barrier_apply_timestamp_map(&barrier, &map) < 0)
    {
        goto cleanup;
    }

    (void)tp_join_barrier_update_observed_time(
        &barrier,
        300,
        123000,
        TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR,
        TP_CLOCK_DOMAIN_MONOTONIC,
        2000);

    printf("timestamp ready=%d\n", tp_join_barrier_is_ready_timestamp(&barrier, 123000, TP_CLOCK_DOMAIN_MONOTONIC, 3000));
    result = 0;

cleanup:
    tp_join_barrier_close(&barrier);
    return result;
}

static int tp_demo_latest(void)
{
    tp_join_barrier_t barrier;
    tp_sequence_merge_rule_t rules[2];
    tp_sequence_merge_map_t map;
    int result = -1;

    memset(&barrier, 0, sizeof(barrier));
    memset(rules, 0, sizeof(rules));
    memset(&map, 0, sizeof(map));

    if (tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_LATEST_VALUE, 2) < 0)
    {
        return -1;
    }

    rules[0].input_stream_id = 400;
    rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    rules[0].offset = 0;

    rules[1].input_stream_id = 500;
    rules[1].rule_type = TP_MERGE_RULE_OFFSET;
    rules[1].offset = 0;

    map.out_stream_id = 12;
    map.epoch = 1;
    map.stale_timeout_ns = 5;
    map.rules = rules;
    map.rule_count = 2;

    tp_join_barrier_set_allow_stale(&barrier, true);
    if (tp_join_barrier_apply_latest_value_sequence_map(&barrier, &map) < 0)
    {
        goto cleanup;
    }

    (void)tp_join_barrier_update_observed_seq(&barrier, 400, 1, 1);
    (void)tp_join_barrier_update_observed_seq(&barrier, 500, 2, 1);
    printf("latest ready=%d\n", tp_join_barrier_is_ready_latest(&barrier, 0, 0, 0, 10));
    result = 0;

cleanup:
    tp_join_barrier_close(&barrier);
    return result;
}

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s [-h]\n", name);
}

int main(int argc, char **argv)
{
    int opt;

    while ((opt = getopt(argc, argv, "h")) != -1)
    {
        switch (opt)
        {
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (optind < argc)
    {
        usage(argv[0]);
        return 1;
    }

    if (tp_demo_sequence() < 0)
    {
        fprintf(stderr, "sequence demo failed\n");
        return 1;
    }

    if (tp_demo_timestamp() < 0)
    {
        fprintf(stderr, "timestamp demo failed\n");
        return 1;
    }

    if (tp_demo_latest() < 0)
    {
        fprintf(stderr, "latest demo failed\n");
        return 1;
    }

    return 0;
}
