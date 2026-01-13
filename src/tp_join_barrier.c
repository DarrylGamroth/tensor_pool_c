#include "tensor_pool/tp_join_barrier.h"

#include <errno.h>
#include <string.h>

#include "aeron_alloc.h"

#include "tensor_pool/tp_error.h"

typedef struct tp_join_input_state_stct
{
    uint32_t stream_id;
    tp_timestamp_source_t timestamp_source;
    uint64_t observed_seq;
    uint64_t processed_seq;
    uint64_t observed_time_ns;
    uint64_t processed_time_ns;
    uint64_t last_observed_update_ns;
    uint64_t last_processed_update_ns;
    bool has_observed_seq;
    bool has_processed_seq;
    bool has_observed_time;
    bool has_processed_time;
}
tp_join_input_state_t;

static void tp_join_barrier_clear(tp_join_barrier_t *barrier)
{
    if (NULL == barrier)
    {
        return;
    }

    barrier->out_stream_id = 0;
    barrier->epoch = 0;
    barrier->stale_timeout_ns = TP_NULL_U64;
    barrier->lateness_ns = TP_NULL_U64;
    barrier->clock_domain = 0;
    barrier->rule_count = 0;
    if (barrier->sequence_rules)
    {
        memset(barrier->sequence_rules, 0, sizeof(*barrier->sequence_rules) * barrier->rule_capacity);
    }
    if (barrier->timestamp_rules)
    {
        memset(barrier->timestamp_rules, 0, sizeof(*barrier->timestamp_rules) * barrier->rule_capacity);
    }
    if (barrier->state)
    {
        memset(barrier->state, 0, sizeof(tp_join_input_state_t) * barrier->rule_capacity);
    }
}

int tp_join_barrier_init(tp_join_barrier_t *barrier, tp_join_barrier_type_t type, size_t rule_capacity)
{
    if (NULL == barrier || rule_capacity == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_init: invalid input");
        return -1;
    }

    memset(barrier, 0, sizeof(*barrier));
    barrier->type = type;
    barrier->rule_capacity = rule_capacity;
    barrier->stale_timeout_ns = TP_NULL_U64;
    barrier->lateness_ns = TP_NULL_U64;

    if (aeron_alloc((void **)&barrier->sequence_rules, sizeof(tp_sequence_merge_rule_t) * rule_capacity) < 0)
    {
        return -1;
    }
    if (aeron_alloc((void **)&barrier->timestamp_rules, sizeof(tp_timestamp_merge_rule_t) * rule_capacity) < 0)
    {
        aeron_free(barrier->sequence_rules);
        barrier->sequence_rules = NULL;
        return -1;
    }
    if (aeron_alloc((void **)&barrier->state, sizeof(tp_join_input_state_t) * rule_capacity) < 0)
    {
        aeron_free(barrier->sequence_rules);
        aeron_free(barrier->timestamp_rules);
        barrier->sequence_rules = NULL;
        barrier->timestamp_rules = NULL;
        return -1;
    }

    return 0;
}

void tp_join_barrier_close(tp_join_barrier_t *barrier)
{
    if (NULL == barrier)
    {
        return;
    }

    if (barrier->sequence_rules)
    {
        aeron_free(barrier->sequence_rules);
    }
    if (barrier->timestamp_rules)
    {
        aeron_free(barrier->timestamp_rules);
    }
    if (barrier->state)
    {
        aeron_free(barrier->state);
    }

    memset(barrier, 0, sizeof(*barrier));
}

void tp_join_barrier_set_allow_stale(tp_join_barrier_t *barrier, bool allow_stale)
{
    if (NULL == barrier)
    {
        return;
    }

    barrier->allow_stale = allow_stale;
}

void tp_join_barrier_set_require_processed(tp_join_barrier_t *barrier, bool require_processed)
{
    if (NULL == barrier)
    {
        return;
    }

    barrier->require_processed = require_processed;
}

static void tp_join_barrier_load_sequence_rules(tp_join_barrier_t *barrier, const tp_sequence_merge_map_t *map)
{
    size_t i;
    tp_join_input_state_t *states = (tp_join_input_state_t *)barrier->state;

    for (i = 0; i < map->rule_count; i++)
    {
        barrier->sequence_rules[i] = map->rules[i];
        states[i].stream_id = map->rules[i].input_stream_id;
    }
}

static void tp_join_barrier_load_timestamp_rules(tp_join_barrier_t *barrier, const tp_timestamp_merge_map_t *map)
{
    size_t i;
    tp_join_input_state_t *states = (tp_join_input_state_t *)barrier->state;

    for (i = 0; i < map->rule_count; i++)
    {
        barrier->timestamp_rules[i] = map->rules[i];
        states[i].stream_id = map->rules[i].input_stream_id;
        states[i].timestamp_source = map->rules[i].timestamp_source;
    }
}

int tp_join_barrier_apply_sequence_map(tp_join_barrier_t *barrier, const tp_sequence_merge_map_t *map)
{
    if (NULL == barrier || NULL == map || NULL == map->rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_sequence_map: invalid input");
        return -1;
    }

    if (map->rule_count > barrier->rule_capacity)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_sequence_map: rule capacity exceeded");
        return -1;
    }

    if (barrier->type != TP_JOIN_BARRIER_SEQUENCE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_sequence_map: barrier type mismatch");
        return -1;
    }

    tp_join_barrier_clear(barrier);
    barrier->out_stream_id = map->out_stream_id;
    barrier->epoch = map->epoch;
    barrier->stale_timeout_ns = map->stale_timeout_ns;
    barrier->rule_count = map->rule_count;
    tp_join_barrier_load_sequence_rules(barrier, map);
    return 0;
}

int tp_join_barrier_apply_timestamp_map(tp_join_barrier_t *barrier, const tp_timestamp_merge_map_t *map)
{
    if (NULL == barrier || NULL == map || NULL == map->rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_timestamp_map: invalid input");
        return -1;
    }

    if (map->rule_count > barrier->rule_capacity)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_timestamp_map: rule capacity exceeded");
        return -1;
    }

    if (barrier->type != TP_JOIN_BARRIER_TIMESTAMP)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_timestamp_map: barrier type mismatch");
        return -1;
    }

    tp_join_barrier_clear(barrier);
    barrier->out_stream_id = map->out_stream_id;
    barrier->epoch = map->epoch;
    barrier->stale_timeout_ns = map->stale_timeout_ns;
    barrier->lateness_ns = (map->lateness_ns == TP_NULL_U64) ? 0 : map->lateness_ns;
    barrier->clock_domain = map->clock_domain;
    barrier->rule_count = map->rule_count;
    tp_join_barrier_load_timestamp_rules(barrier, map);
    return 0;
}

int tp_join_barrier_apply_latest_value_sequence_map(tp_join_barrier_t *barrier, const tp_sequence_merge_map_t *map)
{
    if (NULL == barrier || NULL == map || NULL == map->rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_latest_value_sequence_map: invalid input");
        return -1;
    }

    if (map->rule_count > barrier->rule_capacity)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_latest_value_sequence_map: rule capacity exceeded");
        return -1;
    }

    if (barrier->type != TP_JOIN_BARRIER_LATEST_VALUE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_latest_value_sequence_map: barrier type mismatch");
        return -1;
    }

    tp_join_barrier_clear(barrier);
    barrier->out_stream_id = map->out_stream_id;
    barrier->epoch = map->epoch;
    barrier->stale_timeout_ns = map->stale_timeout_ns;
    barrier->rule_count = map->rule_count;
    tp_join_barrier_load_sequence_rules(barrier, map);
    return 0;
}

int tp_join_barrier_apply_latest_value_timestamp_map(tp_join_barrier_t *barrier, const tp_timestamp_merge_map_t *map)
{
    if (NULL == barrier || NULL == map || NULL == map->rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_latest_value_timestamp_map: invalid input");
        return -1;
    }

    if (map->rule_count > barrier->rule_capacity)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_latest_value_timestamp_map: rule capacity exceeded");
        return -1;
    }

    if (barrier->type != TP_JOIN_BARRIER_LATEST_VALUE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_apply_latest_value_timestamp_map: barrier type mismatch");
        return -1;
    }

    tp_join_barrier_clear(barrier);
    barrier->out_stream_id = map->out_stream_id;
    barrier->epoch = map->epoch;
    barrier->stale_timeout_ns = map->stale_timeout_ns;
    barrier->lateness_ns = (map->lateness_ns == TP_NULL_U64) ? 0 : map->lateness_ns;
    barrier->clock_domain = map->clock_domain;
    barrier->rule_count = map->rule_count;
    tp_join_barrier_load_timestamp_rules(barrier, map);
    return 0;
}

static tp_join_input_state_t *tp_join_barrier_find_state(tp_join_barrier_t *barrier, uint32_t stream_id)
{
    size_t i;
    tp_join_input_state_t *states;

    if (NULL == barrier || NULL == barrier->state)
    {
        return NULL;
    }

    states = (tp_join_input_state_t *)barrier->state;
    for (i = 0; i < barrier->rule_count; i++)
    {
        if (states[i].stream_id == stream_id)
        {
            return &states[i];
        }
    }

    return NULL;
}

int tp_join_barrier_update_observed_seq(
    tp_join_barrier_t *barrier,
    uint32_t stream_id,
    uint64_t seq,
    uint64_t now_ns)
{
    tp_join_input_state_t *state = tp_join_barrier_find_state(barrier, stream_id);

    if (NULL == state)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_observed_seq: stream not tracked");
        return -1;
    }

    if (state->has_observed_seq && seq < state->observed_seq)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_observed_seq: seq regression");
        return -1;
    }

    state->observed_seq = seq;
    state->has_observed_seq = true;
    state->last_observed_update_ns = now_ns;
    return 0;
}

int tp_join_barrier_update_processed_seq(
    tp_join_barrier_t *barrier,
    uint32_t stream_id,
    uint64_t seq,
    uint64_t now_ns)
{
    tp_join_input_state_t *state = tp_join_barrier_find_state(barrier, stream_id);

    if (NULL == state)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_processed_seq: stream not tracked");
        return -1;
    }

    if (state->has_processed_seq && seq < state->processed_seq)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_processed_seq: seq regression");
        return -1;
    }

    state->processed_seq = seq;
    state->has_processed_seq = true;
    state->last_processed_update_ns = now_ns;
    return 0;
}

static int tp_join_barrier_validate_timestamp_update(
    const tp_join_barrier_t *barrier,
    const tp_join_input_state_t *state,
    tp_timestamp_source_t source,
    uint8_t clock_domain)
{
    if (barrier->clock_domain != 0 && clock_domain != barrier->clock_domain)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier: clock domain mismatch");
        return -1;
    }

    if (state->timestamp_source != 0 && source != state->timestamp_source)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier: timestamp source mismatch");
        return -1;
    }

    return 0;
}

int tp_join_barrier_update_observed_time(
    tp_join_barrier_t *barrier,
    uint32_t stream_id,
    uint64_t timestamp_ns,
    tp_timestamp_source_t source,
    uint8_t clock_domain,
    uint64_t now_ns)
{
    tp_join_input_state_t *state = tp_join_barrier_find_state(barrier, stream_id);

    if (NULL == state)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_observed_time: stream not tracked");
        return -1;
    }

    if (tp_join_barrier_validate_timestamp_update(barrier, state, source, clock_domain) < 0)
    {
        return -1;
    }

    if (timestamp_ns == TP_NULL_U64)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_observed_time: timestamp missing");
        return -1;
    }

    if (state->has_observed_time && timestamp_ns < state->observed_time_ns)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_observed_time: time regression");
        return -1;
    }

    state->observed_time_ns = timestamp_ns;
    state->has_observed_time = true;
    state->last_observed_update_ns = now_ns;
    return 0;
}

int tp_join_barrier_update_processed_time(
    tp_join_barrier_t *barrier,
    uint32_t stream_id,
    uint64_t timestamp_ns,
    tp_timestamp_source_t source,
    uint8_t clock_domain,
    uint64_t now_ns)
{
    tp_join_input_state_t *state = tp_join_barrier_find_state(barrier, stream_id);

    if (NULL == state)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_processed_time: stream not tracked");
        return -1;
    }

    if (tp_join_barrier_validate_timestamp_update(barrier, state, source, clock_domain) < 0)
    {
        return -1;
    }

    if (timestamp_ns == TP_NULL_U64)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_processed_time: timestamp missing");
        return -1;
    }

    if (state->has_processed_time && timestamp_ns < state->processed_time_ns)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_update_processed_time: time regression");
        return -1;
    }

    state->processed_time_ns = timestamp_ns;
    state->has_processed_time = true;
    state->last_processed_update_ns = now_ns;
    return 0;
}

static bool tp_join_barrier_is_stale(const tp_join_barrier_t *barrier, const tp_join_input_state_t *state, uint64_t now_ns)
{
    if (!barrier->allow_stale || barrier->stale_timeout_ns == TP_NULL_U64)
    {
        return false;
    }

    if (state->last_observed_update_ns == 0)
    {
        return false;
    }

    return now_ns - state->last_observed_update_ns > barrier->stale_timeout_ns;
}

int tp_join_barrier_collect_stale_inputs(
    tp_join_barrier_t *barrier,
    uint64_t now_ns,
    uint32_t *stream_ids,
    size_t capacity,
    size_t *out_count)
{
    size_t i;
    size_t count = 0;
    tp_join_input_state_t *states;

    if (NULL == barrier || NULL == out_count)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_collect_stale_inputs: invalid input");
        return -1;
    }

    if (!barrier->allow_stale || barrier->stale_timeout_ns == TP_NULL_U64 || barrier->rule_count == 0)
    {
        *out_count = 0;
        return 0;
    }

    states = (tp_join_input_state_t *)barrier->state;
    for (i = 0; i < barrier->rule_count; i++)
    {
        if (tp_join_barrier_is_stale(barrier, &states[i], now_ns))
        {
            count++;
        }
    }

    if (NULL == stream_ids || capacity == 0)
    {
        *out_count = count;
        return 0;
    }

    if (capacity < count)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_collect_stale_inputs: capacity too small");
        return -1;
    }

    count = 0;
    for (i = 0; i < barrier->rule_count; i++)
    {
        if (tp_join_barrier_is_stale(barrier, &states[i], now_ns))
        {
            stream_ids[count++] = states[i].stream_id;
        }
    }

    *out_count = count;
    return 0;
}

int tp_join_barrier_is_ready_sequence(tp_join_barrier_t *barrier, uint64_t out_seq, uint64_t now_ns)
{
    size_t i;
    tp_join_input_state_t *states;

    if (NULL == barrier || barrier->type != TP_JOIN_BARRIER_SEQUENCE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_is_ready_sequence: invalid barrier");
        return -1;
    }

    if (barrier->rule_count == 0)
    {
        return 0;
    }

    states = (tp_join_input_state_t *)barrier->state;

    for (i = 0; i < barrier->rule_count; i++)
    {
        const tp_sequence_merge_rule_t *rule = &barrier->sequence_rules[i];
        const tp_join_input_state_t *state = &states[i];
        int64_t required_seq;

        if (tp_join_barrier_is_stale(barrier, state, now_ns))
        {
            continue;
        }

        if (!state->has_observed_seq)
        {
            return 0;
        }

        if (rule->rule_type == TP_MERGE_RULE_OFFSET)
        {
            required_seq = (int64_t)out_seq + rule->offset;
            if (required_seq < 0)
            {
                return 0;
            }
        }
        else if (rule->rule_type == TP_MERGE_RULE_WINDOW)
        {
            if (rule->window_size == 0)
            {
                return 0;
            }
            if (out_seq + 1 < rule->window_size)
            {
                return 0;
            }
            required_seq = (int64_t)out_seq;
        }
        else
        {
            TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_is_ready_sequence: invalid rule");
            return -1;
        }

        if (state->observed_seq < (uint64_t)required_seq)
        {
            return 0;
        }

        if (barrier->require_processed)
        {
            if (!state->has_processed_seq || state->processed_seq < (uint64_t)required_seq)
            {
                return 0;
            }
        }
    }

    return 1;
}

static int tp_join_barrier_timestamp_ready_for_rule(
    const tp_join_barrier_t *barrier,
    const tp_join_input_state_t *state,
    const tp_timestamp_merge_rule_t *rule,
    uint64_t out_time_ns)
{
    int64_t required_time;
    uint64_t window_ns;
    uint64_t lateness = barrier->lateness_ns == TP_NULL_U64 ? 0 : barrier->lateness_ns;

    if (!state->has_observed_time)
    {
        return 0;
    }

    if (rule->rule_type == TP_MERGE_TIME_OFFSET_NS)
    {
        required_time = (int64_t)out_time_ns + rule->offset_ns;
        if (required_time < 0)
        {
            uint64_t threshold = lateness;
            if (rule->offset_ns < 0)
            {
                threshold += (uint64_t)(-rule->offset_ns);
            }
            if (out_time_ns < threshold)
            {
                return 0;
            }
            required_time = 0;
        }
    }
    else if (rule->rule_type == TP_MERGE_TIME_WINDOW_NS)
    {
        window_ns = rule->window_ns;
        if (window_ns == 0)
        {
            return -1;
        }
        if (out_time_ns < window_ns)
        {
            return 0;
        }
        required_time = (int64_t)out_time_ns;
    }
    else
    {
        return -1;
    }

    if (state->observed_time_ns + lateness < (uint64_t)required_time)
    {
        return 0;
    }

    if (barrier->require_processed)
    {
        if (!state->has_processed_time || state->processed_time_ns + lateness < (uint64_t)required_time)
        {
            return 0;
        }
    }

    return 1;
}

int tp_join_barrier_is_ready_timestamp(tp_join_barrier_t *barrier, uint64_t out_time_ns, uint8_t clock_domain, uint64_t now_ns)
{
    size_t i;
    tp_join_input_state_t *states;

    if (NULL == barrier || barrier->type != TP_JOIN_BARRIER_TIMESTAMP)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_is_ready_timestamp: invalid barrier");
        return -1;
    }

    if (barrier->rule_count == 0)
    {
        return 0;
    }

    if (barrier->clock_domain != 0 && clock_domain != barrier->clock_domain)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_is_ready_timestamp: clock domain mismatch");
        return -1;
    }

    states = (tp_join_input_state_t *)barrier->state;

    for (i = 0; i < barrier->rule_count; i++)
    {
        const tp_timestamp_merge_rule_t *rule = &barrier->timestamp_rules[i];
        const tp_join_input_state_t *state = &states[i];
        int ready;

        if (tp_join_barrier_is_stale(barrier, state, now_ns))
        {
            continue;
        }

        ready = tp_join_barrier_timestamp_ready_for_rule(barrier, state, rule, out_time_ns);
        if (ready <= 0)
        {
            return ready;
        }
    }

    return 1;
}

int tp_join_barrier_is_ready_latest(
    tp_join_barrier_t *barrier,
    uint64_t out_seq,
    uint64_t out_time_ns,
    uint8_t clock_domain,
    uint64_t now_ns)
{
    size_t i;
    tp_join_input_state_t *states;

    (void)out_seq;

    if (NULL == barrier || barrier->type != TP_JOIN_BARRIER_LATEST_VALUE)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_is_ready_latest: invalid barrier");
        return -1;
    }

    if (barrier->rule_count == 0)
    {
        return 0;
    }

    states = (tp_join_input_state_t *)barrier->state;

    for (i = 0; i < barrier->rule_count; i++)
    {
        const tp_join_input_state_t *state = &states[i];

        if (tp_join_barrier_is_stale(barrier, state, now_ns))
        {
            continue;
        }

        if (barrier->clock_domain != 0 && clock_domain != barrier->clock_domain)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_join_barrier_is_ready_latest: clock domain mismatch");
            return -1;
        }

        if (!state->has_observed_seq && !state->has_observed_time)
        {
            return 0;
        }

        if (barrier->clock_domain != 0 && !state->has_observed_time)
        {
            return 0;
        }

        if (state->has_observed_time && state->observed_time_ns > 0 && out_time_ns == 0)
        {
            return 0;
        }
    }

    return 1;
}
