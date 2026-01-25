#include "tensor_pool/internal/tp_control_poller.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_merge_map.h"
#include "tensor_pool/tp_error.h"
#include "tp_aeron_wrap.h"

static void tp_control_poller_handle_meta(
    tp_control_poller_t *poller,
    const uint8_t *buffer,
    size_t length)
{
    tp_data_source_meta_view_t view;

    if (NULL == poller)
    {
        return;
    }

    if (tp_control_decode_data_source_meta(buffer, length, &view, poller->handlers.on_data_source_meta_attr, poller->handlers.clientd) == 0)
    {
        if (poller->handlers.on_data_source_meta_begin)
        {
            poller->handlers.on_data_source_meta_begin(&view, poller->handlers.clientd);
        }
        if (poller->handlers.on_data_source_meta_end)
        {
            poller->handlers.on_data_source_meta_end(&view, poller->handlers.clientd);
        }
    }
}

static void tp_control_poller_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_control_poller_t *poller = (tp_control_poller_t *)clientd;
    tp_shm_pool_announce_view_t pool_announce;
    tp_consumer_hello_view_t hello;
    tp_consumer_config_view_t config;
    tp_control_response_view_t response;
    tp_data_source_announce_view_t announce;
    tp_driver_lease_revoked_t revoked;
    tp_driver_shutdown_t shutdown_event;
    tp_driver_detach_info_t detach_info;
    tp_sequence_merge_rule_t seq_rules[256];
    tp_timestamp_merge_rule_t ts_rules[256];
    tp_sequence_merge_map_t seq_map;
    tp_timestamp_merge_map_t ts_map;
    tp_tracelink_set_t tracelink;
    uint64_t tracelink_parents[TP_TRACELINK_MAX_PARENTS];

    (void)header;

    if (NULL == poller || NULL == buffer)
    {
        return;
    }

    if (poller->handlers.on_shm_pool_announce &&
        tp_control_decode_shm_pool_announce(buffer, length, &pool_announce) == 0)
    {
        poller->handlers.on_shm_pool_announce(&pool_announce, poller->handlers.clientd);
        tp_control_shm_pool_announce_close(&pool_announce);
        return;
    }

    if (poller->handlers.on_consumer_hello && tp_control_decode_consumer_hello(buffer, length, &hello) == 0)
    {
        poller->handlers.on_consumer_hello(&hello, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.on_consumer_config && tp_control_decode_consumer_config(buffer, length, &config) == 0)
    {
        poller->handlers.on_consumer_config(&config, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.on_control_response && tp_control_decode_control_response(buffer, length, &response) == 0)
    {
        poller->handlers.on_control_response(&response, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.on_data_source_announce && tp_control_decode_data_source_announce(buffer, length, &announce) == 0)
    {
        poller->handlers.on_data_source_announce(&announce, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.on_tracelink_set &&
        tp_tracelink_set_decode(buffer, length, &tracelink, tracelink_parents, TP_TRACELINK_MAX_PARENTS) == 0)
    {
        poller->handlers.on_tracelink_set(&tracelink, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.sequence_join_barrier || poller->handlers.timestamp_join_barrier || poller->handlers.latest_join_barrier)
    {
        if (tp_sequence_merge_map_decode(buffer, length, &seq_map, seq_rules, 256) == 0)
        {
            if (poller->handlers.sequence_join_barrier)
            {
                (void)tp_join_barrier_apply_sequence_map(poller->handlers.sequence_join_barrier, &seq_map);
            }
            if (poller->handlers.latest_join_barrier)
            {
                (void)tp_join_barrier_apply_latest_value_sequence_map(poller->handlers.latest_join_barrier, &seq_map);
            }
            return;
        }

        if (tp_timestamp_merge_map_decode(buffer, length, &ts_map, ts_rules, 256) == 0)
        {
            if (poller->handlers.timestamp_join_barrier)
            {
                (void)tp_join_barrier_apply_timestamp_map(poller->handlers.timestamp_join_barrier, &ts_map);
            }
            if (poller->handlers.latest_join_barrier)
            {
                (void)tp_join_barrier_apply_latest_value_timestamp_map(poller->handlers.latest_join_barrier, &ts_map);
            }
            return;
        }
    }

    if (poller->handlers.on_data_source_meta_begin || poller->handlers.on_data_source_meta_attr || poller->handlers.on_data_source_meta_end)
    {
        tp_control_poller_handle_meta(poller, buffer, length);
    }

    if (poller->handlers.on_detach_response && tp_driver_decode_detach_response(buffer, length, &detach_info) == 0)
    {
        poller->handlers.on_detach_response(&detach_info, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.on_lease_revoked && tp_driver_decode_lease_revoked(buffer, length, &revoked) == 0)
    {
        poller->handlers.on_lease_revoked(&revoked, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.on_shutdown && tp_driver_decode_shutdown(buffer, length, &shutdown_event) == 0)
    {
        poller->handlers.on_shutdown(&shutdown_event, poller->handlers.clientd);
        return;
    }
}

#if defined(TP_ENABLE_FUZZ) || defined(TP_TESTING)
void tp_control_poller_handle_fragment(tp_control_poller_t *poller, const uint8_t *buffer, size_t length)
{
    tp_control_poller_handler(poller, buffer, length, NULL);
}
#endif

int tp_control_poller_init(tp_control_poller_t *poller, tp_client_t *client, const tp_control_handlers_t *handlers)
{
    if (NULL == poller || NULL == client || NULL == tp_client_control_subscription(client))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_poller_init: invalid input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->client = client;
    if (handlers)
    {
        poller->handlers = *handlers;
    }

    if (tp_fragment_assembler_create(&poller->assembler, tp_control_poller_handler, poller) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_control_poll(tp_control_poller_t *poller, int fragment_limit)
{
    if (NULL == poller || NULL == poller->assembler || NULL == poller->client ||
        NULL == tp_client_control_subscription(poller->client))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_control_poll: poller not initialized");
        return -1;
    }

    return aeron_subscription_poll(
        tp_subscription_handle(tp_client_control_subscription(poller->client)),
        aeron_fragment_assembler_handler,
        tp_fragment_assembler_handle(poller->assembler),
        fragment_limit);
}
