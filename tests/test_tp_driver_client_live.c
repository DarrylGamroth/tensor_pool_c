#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"
#include "tp_aeron_wrap.h"

#include "driver/tensor_pool/messageHeader.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/shmAttachResponse.h"
#include "driver/tensor_pool/shmDetachResponse.h"

#include "aeron_alloc.h"
#include "aeronc.h"

#include <assert.h>
#include <stdio.h>
#include <errno.h>
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

static size_t tp_test_encode_attach_response(
    uint8_t *buffer,
    size_t length,
    int64_t correlation_id,
    enum tensor_pool_responseCode code,
    const char *error_message)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmAttachResponse response;
    struct tensor_pool_shmAttachResponse_payloadPools pools;
    const char *header_uri = "shm:file?path=/tmp/tp_hdr";
    const char *pool_uri = "shm:file?path=/tmp/tp_pool";
    size_t header_len = tensor_pool_messageHeader_encoded_length();

    memset(buffer, 0, length);

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_shmAttachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmAttachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmAttachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmAttachResponse_sbe_schema_version());

    tensor_pool_shmAttachResponse_wrap_for_encode(
        &response,
        (char *)buffer,
        header_len,
        length);
    tensor_pool_shmAttachResponse_set_correlationId(&response, correlation_id);
    tensor_pool_shmAttachResponse_set_code(&response, code);
    tensor_pool_shmAttachResponse_set_leaseId(&response, 7);
    tensor_pool_shmAttachResponse_set_leaseExpiryTimestampNs(&response, 100);
    tensor_pool_shmAttachResponse_set_streamId(&response, 10000);
    tensor_pool_shmAttachResponse_set_epoch(&response, 1);
    tensor_pool_shmAttachResponse_set_layoutVersion(&response, TP_LAYOUT_VERSION);
    tensor_pool_shmAttachResponse_set_headerNslots(&response, 4);
    tensor_pool_shmAttachResponse_set_headerSlotBytes(&response, TP_HEADER_SLOT_BYTES);

    if (code == tensor_pool_responseCode_OK)
    {
        if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
            &pools,
            (char *)buffer,
            1,
            tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
            tensor_pool_shmAttachResponse_sbe_schema_version(),
            length))
        {
            return 0;
        }
        if (NULL == tensor_pool_shmAttachResponse_payloadPools_next(&pools))
        {
            return 0;
        }

        tensor_pool_shmAttachResponse_payloadPools_set_poolId(&pools, 1);
        tensor_pool_shmAttachResponse_payloadPools_set_poolNslots(&pools, 4);
        tensor_pool_shmAttachResponse_payloadPools_set_strideBytes(&pools, 64);
        tensor_pool_shmAttachResponse_payloadPools_put_regionUri(&pools, pool_uri, (uint32_t)strlen(pool_uri));
        tensor_pool_shmAttachResponse_put_headerRegionUri(&response, header_uri, (uint32_t)strlen(header_uri));
    }
    else
    {
        if (NULL == tensor_pool_shmAttachResponse_payloadPools_wrap_for_encode(
            &pools,
            (char *)buffer,
            0,
            tensor_pool_shmAttachResponse_sbe_position_ptr(&response),
            tensor_pool_shmAttachResponse_sbe_schema_version(),
            length))
        {
            return 0;
        }
        tensor_pool_shmAttachResponse_put_headerRegionUri(&response, "", 0);
    }
    if (NULL != error_message)
    {
        tensor_pool_shmAttachResponse_put_errorMessage(
            &response,
            error_message,
            (uint32_t)strlen(error_message));
    }

    return (size_t)tensor_pool_shmAttachResponse_sbe_position(&response);
}

static size_t tp_test_encode_detach_response(uint8_t *buffer, size_t length, int64_t correlation_id)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmDetachResponse response;
    size_t header_len = tensor_pool_messageHeader_encoded_length();

    memset(buffer, 0, length);

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_shmDetachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmDetachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmDetachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmDetachResponse_sbe_schema_version());

    tensor_pool_shmDetachResponse_wrap_for_encode(
        &response,
        (char *)buffer,
        header_len,
        length);
    tensor_pool_shmDetachResponse_set_correlationId(&response, correlation_id);
    tensor_pool_shmDetachResponse_set_code(&response, tensor_pool_responseCode_OK);

    return (size_t)tensor_pool_shmDetachResponse_sbe_position(&response);
}

void tp_test_driver_client_attach_detach_live(void)
{
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_driver_detach_info_t detach_info;
    tp_async_attach_t *attach_async = NULL;
    tp_async_detach_t *detach_async = NULL;
    tp_publication_t *response_pub = NULL;
    uint8_t buffer[512];
    const char *aeron_dir = getenv("AERON_DIR");
    int64_t deadline;
    int result = -1;
    int step = 0;

    if (!tp_test_driver_active(aeron_dir))
    {
        return;
    }

    memset(&ctx, 0, sizeof(ctx));
    client = NULL;
    driver = NULL;
    memset(&request, 0, sizeof(request));
    memset(&info, 0, sizeof(info));
    memset(&detach_info, 0, sizeof(detach_info));

    step = 1;
    if (tp_context_init(&ctx) < 0)
    {
        return;
    }
    tp_context_set_aeron_dir(ctx, aeron_dir);
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(ctx, "aeron:ipc", 1000);
    tp_context_set_announce_channel(ctx, "aeron:ipc", 1001);
    tp_context_set_qos_channel(ctx, "aeron:ipc", 1200);
    tp_context_set_metadata_channel(ctx, "aeron:ipc", 1300);
    tp_context_set_descriptor_channel(ctx, "aeron:ipc", 1100);

    step = 2;
    if (tp_client_init(&client, ctx) < 0 || tp_client_start(client) < 0)
    {
        goto cleanup;
    }

    step = 3;
    if (tp_driver_client_init(&driver, client) < 0)
    {
        goto cleanup;
    }

    step = 4;
    if (tp_test_wait_for_publication(client, tp_driver_client_publication(driver)) < 0)
    {
        goto cleanup;
    }

    step = 5;
    if (tp_test_add_publication(client, "aeron:ipc", 1000, &response_pub) < 0)
    {
        goto cleanup;
    }

    step = 6;
    if (tp_test_wait_for_publication(client, response_pub) < 0)
    {
        goto cleanup;
    }

    step = 7;
    if (tp_test_wait_for_subscription(client, tp_client_control_subscription(client)) < 0)
    {
        goto cleanup;
    }

    request.correlation_id = 0;
    request.stream_id = 10000;
    request.client_id = 0;
    request.role = tensor_pool_role_PRODUCER;
    request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    request.require_hugepages = tensor_pool_hugepagesPolicy_UNSPECIFIED;
    request.expected_layout_version = TP_LAYOUT_VERSION;
    request.desired_node_id = TP_NULL_U32;

    step = 8;
    if (tp_driver_attach_async(driver, &request, &attach_async) < 0)
    {
        goto cleanup;
    }

    if (attach_async->request.correlation_id == 0 || attach_async->request.client_id == 0)
    {
        goto cleanup;
    }

    step = 9;
    if (tp_driver_attach_poll(attach_async, &info) < 0)
    {
        goto cleanup;
    }

    step = 10;
    {
        int64_t initial_correlation = attach_async->request.correlation_id;
        uint32_t initial_client_id = attach_async->request.client_id;
        size_t len = tp_test_encode_attach_response(
            buffer,
            sizeof(buffer),
            initial_correlation,
            tensor_pool_responseCode_REJECTED,
            "client_id already attached");
        if (len == 0 || tp_test_offer(client, response_pub, buffer, len) < 0)
        {
            goto cleanup;
        }

        int saw_retry = 0;

        deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
        while (tp_clock_now_ns() < deadline)
        {
            if (tp_driver_attach_poll(attach_async, &info) < 0)
            {
                goto cleanup;
            }
            if (attach_async->request.correlation_id != initial_correlation ||
                attach_async->request.client_id != initial_client_id)
            {
                saw_retry = 1;
                break;
            }
            tp_client_do_work(client);
        }

        if (!saw_retry)
        {
            goto cleanup;
        }
    }

    step = 11;
    {
        size_t len = tp_test_encode_attach_response(
            buffer,
            sizeof(buffer),
            attach_async->request.correlation_id,
            tensor_pool_responseCode_OK,
            NULL);
        if (len == 0 || tp_test_offer(client, response_pub, buffer, len) < 0)
        {
            goto cleanup;
        }
    }

    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    step = 12;
    while (tp_clock_now_ns() < deadline)
    {
        int poll = tp_driver_attach_poll(attach_async, &info);
        if (poll < 0)
        {
            goto cleanup;
        }
        if (poll == 1)
        {
            break;
        }
        tp_client_do_work(client);
    }

    if (info.code != tensor_pool_responseCode_OK)
    {
        goto cleanup;
    }

    step = 13;
    if (tp_driver_client_active_lease_id(driver) != info.lease_id ||
        tp_driver_client_active_stream_id(driver) != request.stream_id ||
        tp_driver_client_id(driver) != attach_async->request.client_id ||
        tp_driver_client_role(driver) != request.role)
    {
        goto cleanup;
    }

    step = 14;
    {
        size_t len = tp_test_encode_attach_response(
            buffer,
            sizeof(buffer),
            attach_async->request.correlation_id,
            tensor_pool_responseCode_REJECTED,
            "client_id already attached");
        if (len == 0 || tp_test_offer(client, response_pub, buffer, len) < 0)
        {
            goto cleanup;
        }
    }

    step = 15;
    if (tp_driver_attach_poll(attach_async, &info) < 0)
    {
        goto cleanup;
    }
    if (info.code != tensor_pool_responseCode_OK)
    {
        goto cleanup;
    }

    if (tp_driver_detach_async(driver, &detach_async) < 0)
    {
        goto cleanup;
    }

    step = 16;
    if (tp_driver_detach_poll(detach_async, &detach_info) < 0)
    {
        goto cleanup;
    }

    step = 17;
    {
        size_t len = tp_test_encode_detach_response(buffer, sizeof(buffer), detach_async->response.correlation_id);
        if (len == 0 || tp_test_offer(client, response_pub, buffer, len) < 0)
        {
            goto cleanup;
        }
    }

    deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;
    step = 18;
    while (tp_clock_now_ns() < deadline)
    {
        int poll = tp_driver_detach_poll(detach_async, &detach_info);
        if (poll < 0)
        {
            goto cleanup;
        }
        if (poll == 1)
        {
            break;
        }
        tp_client_do_work(client);
    }

    if (detach_info.code != tensor_pool_responseCode_OK)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_client_attach_detach_live failed at step %d: %s\n", step, tp_errmsg());
    }
    if (attach_async)
    {
        aeron_free(attach_async);
    }
    if (detach_async)
    {
        aeron_free(detach_async);
    }
    if (response_pub)
    {
        tp_publication_close(&response_pub);
    }
    tp_driver_client_close(driver);
    tp_client_close(client);
    assert(result == 0);
}
