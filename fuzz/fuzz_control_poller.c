#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/internal/tp_control_poller.h"

static void tp_fuzz_on_shm_pool_announce(const tp_shm_pool_announce_view_t *announce, void *clientd)
{
    (void)announce;
    (void)clientd;
}

static void tp_fuzz_on_consumer_hello(const tp_consumer_hello_view_t *hello, void *clientd)
{
    (void)hello;
    (void)clientd;
}

static void tp_fuzz_on_consumer_config(const tp_consumer_config_view_t *config, void *clientd)
{
    (void)config;
    (void)clientd;
}

static void tp_fuzz_on_control_response(const tp_control_response_view_t *response, void *clientd)
{
    (void)response;
    (void)clientd;
}

static void tp_fuzz_on_data_source_announce(const tp_data_source_announce_view_t *announce, void *clientd)
{
    (void)announce;
    (void)clientd;
}

static void tp_fuzz_on_data_source_meta_begin(const tp_data_source_meta_view_t *meta, void *clientd)
{
    (void)meta;
    (void)clientd;
}

static void tp_fuzz_on_data_source_meta_attr(const tp_data_source_meta_attr_view_t *attr, void *clientd)
{
    (void)attr;
    (void)clientd;
}

static void tp_fuzz_on_data_source_meta_end(const tp_data_source_meta_view_t *meta, void *clientd)
{
    (void)meta;
    (void)clientd;
}

static void tp_fuzz_on_tracelink_set(const tp_tracelink_set_t *set, void *clientd)
{
    (void)set;
    (void)clientd;
}

static void tp_fuzz_on_detach_response(const tp_driver_detach_info_t *info, void *clientd)
{
    (void)info;
    (void)clientd;
}

static void tp_fuzz_on_lease_revoked(const tp_driver_lease_revoked_t *revoked, void *clientd)
{
    (void)revoked;
    (void)clientd;
}

static void tp_fuzz_on_shutdown(const tp_driver_shutdown_t *shutdown_event, void *clientd)
{
    (void)shutdown_event;
    (void)clientd;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_control_poller_t poller;

    if (data == NULL)
    {
        return 0;
    }

    memset(&poller, 0, sizeof(poller));
    poller.handlers.on_shm_pool_announce = tp_fuzz_on_shm_pool_announce;
    poller.handlers.on_consumer_hello = tp_fuzz_on_consumer_hello;
    poller.handlers.on_consumer_config = tp_fuzz_on_consumer_config;
    poller.handlers.on_control_response = tp_fuzz_on_control_response;
    poller.handlers.on_data_source_announce = tp_fuzz_on_data_source_announce;
    poller.handlers.on_data_source_meta_begin = tp_fuzz_on_data_source_meta_begin;
    poller.handlers.on_data_source_meta_attr = tp_fuzz_on_data_source_meta_attr;
    poller.handlers.on_data_source_meta_end = tp_fuzz_on_data_source_meta_end;
    poller.handlers.on_tracelink_set = tp_fuzz_on_tracelink_set;
    poller.handlers.on_detach_response = tp_fuzz_on_detach_response;
    poller.handlers.on_lease_revoked = tp_fuzz_on_lease_revoked;
    poller.handlers.on_shutdown = tp_fuzz_on_shutdown;
    poller.handlers.clientd = NULL;

    tp_control_poller_handle_fragment(&poller, data, size);

    return 0;
}
