#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_discovery_client.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"
#include "tp_aeron_wrap.h"

#include "discovery/tensor_pool/messageHeader.h"
#include "discovery/tensor_pool/discoveryResponse.h"
#include "discovery/tensor_pool/discoveryStatus.h"
#include "discovery/tensor_pool/varAsciiEncoding.h"

#include "aeronc.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static int tp_test_add_subscription(tp_client_t *client, const char *channel, int32_t stream_id, tp_subscription_t **out)
{
    tp_async_add_subscription_t *async_add = NULL;

    if (tp_client_async_add_subscription(client, channel, stream_id, &async_add) < 0)
    {
        return -1;
    }

    *out = NULL;
    while (NULL == *out)
    {
        if (tp_client_async_add_subscription_poll(out, async_add) < 0)
        {
            return -1;
        }
        tp_client_do_work(client);
    }

    return 0;
}

static int tp_test_offer(tp_client_t *client, tp_publication_t *publication, const uint8_t *buffer, size_t length)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        int64_t result = aeron_publication_offer(tp_publication_handle(publication), buffer, length, NULL, NULL);
        if (result >= 0)
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

static size_t tp_test_encode_discovery_response(uint8_t *buffer, size_t length, uint64_t request_id)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_discoveryResponse response;
    struct tensor_pool_discoveryResponse_results results;
    struct tensor_pool_discoveryResponse_results_payloadPools pools;
    struct tensor_pool_discoveryResponse_results_tags tags;
    const char *tag = "vision";
    const char *header_uri = "shm:file?path=/tmp/tp_hdr";
    const char *pool_uri = "shm:file?path=/tmp/tp_pool";
    const char *driver_id = "drv";
    const char *driver_channel = "aeron:ipc";
    const char *data_source_name = "camera";
    size_t header_len = tensor_pool_messageHeader_encoded_length();

    memset(buffer, 0, length);

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_discoveryResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_discoveryResponse_sbe_schema_version());

    tensor_pool_discoveryResponse_wrap_for_encode(&response, (char *)buffer, header_len, length);
    tensor_pool_discoveryResponse_set_requestId(&response, request_id);
    tensor_pool_discoveryResponse_set_status(&response, tensor_pool_discoveryStatus_OK);

    if (NULL == tensor_pool_discoveryResponse_results_wrap_for_encode(
        &results,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_sbe_position_ptr(&response),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        length))
    {
        return 0;
    }

    if (NULL == tensor_pool_discoveryResponse_results_next(&results))
    {
        return 0;
    }

    tensor_pool_discoveryResponse_results_set_streamId(&results, 10000);
    tensor_pool_discoveryResponse_results_set_producerId(&results, 7);
    tensor_pool_discoveryResponse_results_set_epoch(&results, 1);
    tensor_pool_discoveryResponse_results_set_layoutVersion(&results, TP_LAYOUT_VERSION);
    tensor_pool_discoveryResponse_results_set_headerNslots(&results, 4);
    tensor_pool_discoveryResponse_results_set_headerSlotBytes(&results, TP_HEADER_SLOT_BYTES);
    tensor_pool_discoveryResponse_results_set_maxDims(&results, TP_MAX_DIMS);
    tensor_pool_discoveryResponse_results_set_dataSourceId(&results, 42);
    tensor_pool_discoveryResponse_results_set_driverControlStreamId(&results, 1000);

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        length))
    {
        return 0;
    }

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_next(&pools))
    {
        return 0;
    }
    tensor_pool_discoveryResponse_results_payloadPools_set_poolId(&pools, 1);
    tensor_pool_discoveryResponse_results_payloadPools_set_poolNslots(&pools, 4);
    tensor_pool_discoveryResponse_results_payloadPools_set_strideBytes(&pools, 64);
    tensor_pool_discoveryResponse_results_payloadPools_put_regionUri(&pools, pool_uri, (uint32_t)strlen(pool_uri));

    if (NULL == tensor_pool_discoveryResponse_results_tags_wrap_for_encode(
        &tags,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        length))
    {
        return 0;
    }

    if (NULL == tensor_pool_discoveryResponse_results_tags_next(&tags))
    {
        return 0;
    }
    {
        struct tensor_pool_varAsciiEncoding tag_codec;
        if (NULL == tensor_pool_discoveryResponse_results_tags_tag(&tags, &tag_codec))
        {
            return 0;
        }
        tensor_pool_varAsciiEncoding_set_length(&tag_codec, (uint32_t)strlen(tag));
        memcpy(
            (char *)tensor_pool_varAsciiEncoding_mut_buffer(&tag_codec) +
                tensor_pool_varAsciiEncoding_offset(&tag_codec) +
                tensor_pool_varAsciiEncoding_varData_encoding_offset(),
            tag,
            strlen(tag));
        if (!tensor_pool_discoveryResponse_results_tags_set_sbe_position(
            &tags,
            tensor_pool_varAsciiEncoding_offset(&tag_codec) +
                tensor_pool_varAsciiEncoding_varData_encoding_offset() + strlen(tag)))
        {
            return 0;
        }
    }

    tensor_pool_discoveryResponse_results_put_headerRegionUri(&results, header_uri, (uint32_t)strlen(header_uri));
    tensor_pool_discoveryResponse_results_put_dataSourceName(&results, data_source_name, (uint32_t)strlen(data_source_name));
    tensor_pool_discoveryResponse_results_put_driverInstanceId(&results, driver_id, (uint32_t)strlen(driver_id));
    tensor_pool_discoveryResponse_results_put_driverControlChannel(&results, driver_channel, (uint32_t)strlen(driver_channel));

    return (size_t)tensor_pool_discoveryResponse_sbe_position(&response);
}

typedef struct tp_discovery_test_state_stct
{
    uint32_t count;
    uint64_t last_request;
}
tp_discovery_test_state_t;

static void tp_on_discovery_response(void *clientd, const tp_discovery_response_t *response)
{
    tp_discovery_test_state_t *state = (tp_discovery_test_state_t *)clientd;

    if (NULL == state || NULL == response)
    {
        return;
    }

    state->count++;
    state->last_request = response->request_id;
}

void tp_test_discovery_client_live(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_discovery_context_t discovery_ctx;
    tp_discovery_client_t discovery;
    tp_discovery_request_t request;
    tp_discovery_response_t response;
    tp_discovery_handlers_t handlers;
    tp_discovery_poller_t poller;
    tp_discovery_test_state_t state;
    tp_subscription_t *request_sub = NULL;
    tp_publication_t *response_pub = NULL;
    uint8_t buffer[2048];
    const char *aeron_dir = getenv("AERON_DIR");
    int64_t deadline;
    int result = -1;
    int step = 0;

    if (!tp_test_driver_active(aeron_dir))
    {
        return;
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(&client, 0, sizeof(client));
    memset(&discovery_ctx, 0, sizeof(discovery_ctx));
    memset(&discovery, 0, sizeof(discovery));
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    memset(&handlers, 0, sizeof(handlers));
    memset(&poller, 0, sizeof(poller));
    memset(&state, 0, sizeof(state));

    step = 1;
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

    step = 2;
    if (tp_client_init(&client, &ctx) < 0 || tp_client_start(&client) < 0)
    {
        goto cleanup;
    }

    step = 3;
    if (tp_discovery_context_init(&discovery_ctx) < 0)
    {
        goto cleanup;
    }
    tp_discovery_context_set_channel(&discovery_ctx, "aeron:ipc", 1500);
    tp_discovery_context_set_response_channel(&discovery_ctx, "aeron:ipc", 1501);

    step = 4;
    if (tp_discovery_client_init(&discovery, &client, &discovery_ctx) < 0)
    {
        goto cleanup;
    }

    step = 5;
    if (tp_test_add_subscription(&client, "aeron:ipc", 1500, &request_sub) < 0)
    {
        goto cleanup;
    }

    step = 6;
    if (tp_test_add_publication(&client, "aeron:ipc", 1501, &response_pub) < 0)
    {
        goto cleanup;
    }

    step = 7;
    if (tp_test_wait_for_publication(&client, discovery.publication) < 0 ||
        tp_test_wait_for_publication(&client, response_pub) < 0)
    {
        goto cleanup;
    }

    step = 8;
    if (tp_test_wait_for_subscription(&client, request_sub) < 0 ||
        tp_test_wait_for_subscription(&client, discovery.subscription) < 0)
    {
        goto cleanup;
    }

    tp_discovery_request_init(&request);
    request.request_id = 99;
    request.client_id = 7;
    request.response_channel = "aeron:ipc";
    request.response_stream_id = 1501;
    request.stream_id = 10000;
    request.producer_id = 7;
    request.data_source_id = 42;
    request.data_source_name = "camera";
    {
        const char *tags[] = { "vision" };
        request.tags = tags;
        request.tag_count = 1;
    }

    step = 9;
    if (tp_discovery_request(&discovery, &request) < 0)
    {
        goto cleanup;
    }

    {
        size_t len = tp_test_encode_discovery_response(buffer, sizeof(buffer), request.request_id);
        if (len == 0 || tp_test_offer(&client, response_pub, buffer, len) < 0)
        {
            goto cleanup;
        }
    }

    step = 10;
    if (tp_discovery_poll(&discovery, request.request_id, &response, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }

    if (response.status != tensor_pool_discoveryStatus_OK || response.result_count != 1)
    {
        goto cleanup;
    }

    tp_discovery_response_close(&response);
    memset(&response, 0, sizeof(response));

    handlers.on_response = tp_on_discovery_response;
    handlers.clientd = &state;
    step = 11;
    if (tp_discovery_poller_init(&poller, &discovery, &handlers) < 0)
    {
        goto cleanup;
    }

    {
        size_t len = tp_test_encode_discovery_response(buffer, sizeof(buffer), 100);
        if (len == 0 || tp_test_offer(&client, response_pub, buffer, len) < 0)
        {
            goto cleanup;
        }
    }

    step = 12;
    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline && state.count == 0)
    {
        if (tp_discovery_poller_poll(&poller, 10) < 0)
        {
            goto cleanup;
        }
        tp_client_do_work(&client);
    }

    if (state.count != 1 || state.last_request != 100)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    if (result != 0)
    {
        fprintf(stderr, "tp_test_discovery_client_live failed at step %d: %s\n", step, tp_errmsg());
    }
    tp_discovery_response_close(&response);
    if (poller.assembler)
    {
        tp_fragment_assembler_close(&poller.assembler);
    }
    if (response_pub)
    {
        tp_publication_close(&response_pub);
    }
    if (request_sub)
    {
        tp_subscription_close(&request_sub);
    }
    tp_discovery_client_close(&discovery);
    tp_client_close(&client);
    assert(result == 0);
}
