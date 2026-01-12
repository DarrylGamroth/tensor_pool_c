#ifndef TENSOR_POOL_TP_DISCOVERY_CLIENT_H
#define TENSOR_POOL_TP_DISCOVERY_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_discovery_pool_info_stct
{
    uint16_t pool_id;
    uint32_t nslots;
    uint32_t stride_bytes;
    char region_uri[4096];
}
tp_discovery_pool_info_t;

typedef struct tp_discovery_result_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint16_t header_slot_bytes;
    uint8_t max_dims;
    uint64_t data_source_id;
    uint32_t driver_control_stream_id;
    char header_region_uri[4096];
    char data_source_name[256];
    char driver_instance_id[256];
    char driver_control_channel[1024];
    tp_discovery_pool_info_t *pools;
    size_t pool_count;
    char **tags;
    size_t tag_count;
}
tp_discovery_result_t;

typedef struct tp_discovery_response_stct
{
    uint64_t request_id;
    uint8_t status;
    char error_message[1024];
    tp_discovery_result_t *results;
    size_t result_count;
}
tp_discovery_response_t;

typedef struct tp_discovery_request_stct
{
    uint64_t request_id;
    uint32_t client_id;
    const char *response_channel;
    uint32_t response_stream_id;
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t data_source_id;
    const char *data_source_name;
    const char **tags;
    size_t tag_count;
}
tp_discovery_request_t;

typedef struct tp_discovery_context_stct
{
    char request_channel[1024];
    int32_t request_stream_id;
    char response_channel[1024];
    int32_t response_stream_id;
}
tp_discovery_context_t;

typedef struct tp_discovery_client_stct
{
    tp_client_t *client;
    tp_discovery_context_t context;
    aeron_publication_t *publication;
    aeron_subscription_t *subscription;
}
tp_discovery_client_t;

typedef void (*tp_discovery_handler_t)(void *clientd, const tp_discovery_response_t *response);

typedef struct tp_discovery_handlers_stct
{
    tp_discovery_handler_t on_response;
    void *clientd;
}
tp_discovery_handlers_t;

typedef struct tp_discovery_poller_stct
{
    tp_discovery_client_t *client;
    aeron_fragment_assembler_t *assembler;
    tp_discovery_handlers_t handlers;
}
tp_discovery_poller_t;

int tp_discovery_context_init(tp_discovery_context_t *ctx);
void tp_discovery_context_set_channel(tp_discovery_context_t *ctx, const char *channel, int32_t stream_id);
void tp_discovery_context_set_response_channel(tp_discovery_context_t *ctx, const char *channel, int32_t stream_id);

int tp_discovery_client_init(
    tp_discovery_client_t *client,
    tp_client_t *base,
    const tp_discovery_context_t *context);
int tp_discovery_client_close(tp_discovery_client_t *client);

int tp_discovery_request(
    tp_discovery_client_t *client,
    const tp_discovery_request_t *request);

void tp_discovery_request_init(tp_discovery_request_t *request);
int tp_discovery_result_has_tag(const tp_discovery_result_t *result, const char *tag);
int tp_discovery_result_matches(
    const tp_discovery_result_t *result,
    uint32_t stream_id,
    uint32_t producer_id,
    const char *data_source_name);

int tp_discovery_poll(
    tp_discovery_client_t *client,
    uint64_t request_id,
    tp_discovery_response_t *out,
    int64_t timeout_ns);

int tp_discovery_poller_init(
    tp_discovery_poller_t *poller,
    tp_discovery_client_t *client,
    const tp_discovery_handlers_t *handlers);
int tp_discovery_poller_poll(tp_discovery_poller_t *poller, int fragment_limit);

int tp_discovery_decode_response(
    const uint8_t *buffer,
    size_t length,
    uint64_t request_id,
    tp_discovery_response_t *out);

void tp_discovery_response_close(tp_discovery_response_t *response);

#ifdef __cplusplus
}
#endif

#endif
