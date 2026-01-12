#ifndef TENSOR_POOL_TP_DRIVER_CLIENT_H
#define TENSOR_POOL_TP_DRIVER_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_driver_pool_info_stct
{
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    char region_uri[4096];
}
tp_driver_pool_info_t;

typedef struct tp_driver_attach_info_stct
{
    int32_t code;
    char error_message[1024];
    uint64_t lease_id;
    uint64_t lease_expiry_timestamp_ns;
    uint32_t stream_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint16_t header_slot_bytes;
    uint8_t max_dims;
    char header_region_uri[4096];
    tp_driver_pool_info_t *pools;
    size_t pool_count;
}
tp_driver_attach_info_t;

typedef struct tp_driver_attach_request_stct
{
    int64_t correlation_id;
    uint32_t stream_id;
    uint32_t client_id;
    uint8_t role;
    uint32_t expected_layout_version;
    uint8_t publish_mode;
    uint8_t require_hugepages;
}
tp_driver_attach_request_t;

typedef struct tp_driver_detach_info_stct
{
    int64_t correlation_id;
    int32_t code;
    char error_message[1024];
}
tp_driver_detach_info_t;

typedef struct tp_driver_lease_revoked_stct
{
    uint64_t timestamp_ns;
    uint64_t lease_id;
    uint32_t stream_id;
    uint32_t client_id;
    uint8_t role;
    uint8_t reason;
    char error_message[1024];
}
tp_driver_lease_revoked_t;

typedef struct tp_driver_shutdown_stct
{
    uint64_t timestamp_ns;
    uint8_t reason;
    char error_message[1024];
}
tp_driver_shutdown_t;

typedef void (*tp_on_driver_detach_response_t)(const tp_driver_detach_info_t *info, void *clientd);
typedef void (*tp_on_driver_lease_revoked_t)(const tp_driver_lease_revoked_t *event, void *clientd);
typedef void (*tp_on_driver_shutdown_t)(const tp_driver_shutdown_t *event, void *clientd);

typedef struct tp_driver_event_handlers_stct
{
    tp_on_driver_detach_response_t on_detach_response;
    tp_on_driver_lease_revoked_t on_lease_revoked;
    tp_on_driver_shutdown_t on_shutdown;
    void *clientd;
}
tp_driver_event_handlers_t;

typedef struct tp_driver_event_poller_stct
{
    aeron_subscription_t *subscription;
    aeron_fragment_assembler_t *assembler;
    tp_driver_event_handlers_t handlers;
    bool owns_subscription;
}
tp_driver_event_poller_t;

typedef struct tp_driver_client_stct
{
    tp_client_t *client;
    aeron_publication_t *publication;
    aeron_subscription_t *subscription;
    uint64_t active_lease_id;
    uint64_t lease_expiry_timestamp_ns;
    uint64_t last_keepalive_ns;
    uint32_t active_stream_id;
    uint32_t client_id;
    uint8_t role;
    bool registered;
    struct tp_driver_client_stct *next;
}
tp_driver_client_t;

typedef struct tp_async_attach_stct
{
    tp_driver_client_t *client;
    tp_driver_attach_request_t request;
    int sent;
    int done;
    aeron_fragment_assembler_t *assembler;
    tp_driver_attach_info_t response;
}
tp_async_attach_t;

typedef struct tp_async_detach_stct
{
    tp_driver_client_t *client;
    tp_driver_detach_info_t response;
    int sent;
    int done;
    aeron_fragment_assembler_t *assembler;
}
tp_async_detach_t;

int tp_driver_client_init(tp_driver_client_t *client, tp_client_t *base);
int tp_driver_client_close(tp_driver_client_t *client);

int tp_driver_attach_async(
    tp_driver_client_t *client,
    const tp_driver_attach_request_t *request,
    tp_async_attach_t **out);
int tp_driver_attach_poll(tp_async_attach_t *async, tp_driver_attach_info_t *out);

int tp_driver_detach_async(tp_driver_client_t *client, tp_async_detach_t **out);
int tp_driver_detach_poll(tp_async_detach_t *async, tp_driver_detach_info_t *out);

int tp_driver_attach(
    tp_driver_client_t *client,
    const tp_driver_attach_request_t *request,
    tp_driver_attach_info_t *out,
    int64_t timeout_ns);

int tp_driver_keepalive(tp_driver_client_t *client, uint64_t timestamp_ns);
int tp_driver_detach(tp_driver_client_t *client, int64_t correlation_id, uint64_t lease_id, uint32_t stream_id, uint32_t client_id, uint8_t role);

void tp_driver_attach_info_close(tp_driver_attach_info_t *info);
int tp_driver_client_update_lease(
    tp_driver_client_t *client,
    const tp_driver_attach_info_t *attach_info,
    uint32_t client_id,
    uint8_t role);
int tp_driver_client_record_keepalive(tp_driver_client_t *client, uint64_t now_ns);
int tp_driver_client_keepalive_due(const tp_driver_client_t *client, uint64_t now_ns, uint64_t interval_ns);
int tp_driver_client_lease_expired(const tp_driver_client_t *client, uint64_t now_ns);

int tp_driver_event_poller_init(
    tp_driver_event_poller_t *poller,
    tp_driver_client_t *client,
    const tp_driver_event_handlers_t *handlers);
int tp_driver_event_poller_close(tp_driver_event_poller_t *poller);
int tp_driver_event_poll(tp_driver_event_poller_t *poller, int fragment_limit);

int tp_driver_decode_attach_response(
    const uint8_t *buffer,
    size_t length,
    int64_t correlation_id,
    tp_driver_attach_info_t *out);
int tp_driver_decode_detach_response(
    const uint8_t *buffer,
    size_t length,
    tp_driver_detach_info_t *out);
int tp_driver_decode_lease_revoked(
    const uint8_t *buffer,
    size_t length,
    tp_driver_lease_revoked_t *out);
int tp_driver_decode_shutdown(
    const uint8_t *buffer,
    size_t length,
    tp_driver_shutdown_t *out);

#ifdef __cplusplus
}
#endif

#endif
