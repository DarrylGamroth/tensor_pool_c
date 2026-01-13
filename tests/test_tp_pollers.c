#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_control_poller.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_metadata_poller.h"
#include "tensor_pool/tp_progress_poller.h"
#include "tensor_pool/tp_qos.h"

#include "aeronc.h"

#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/frameDescriptor.h"
#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/mode.h"
#include "wire/tensor_pool/qosConsumer.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct tp_poll_state_stct
{
    int saw_hello;
    int saw_qos;
    int saw_announce;
    int saw_meta;
    int saw_progress;
    int saw_descriptor;
}
tp_poll_state_t;

static int tp_test_start_client(tp_client_t *client, tp_client_context_t *ctx, const char *aeron_dir)
{
    if (NULL == aeron_dir)
    {
        return -1;
    }

    if (tp_client_context_init(ctx) < 0)
    {
        return -1;
    }

    tp_client_context_set_aeron_dir(ctx, aeron_dir);
    tp_client_context_set_control_channel(ctx, "aeron:ipc", 1000);
    tp_client_context_set_qos_channel(ctx, "aeron:ipc", 1200);
    tp_client_context_set_metadata_channel(ctx, "aeron:ipc", 1300);
    tp_client_context_set_descriptor_channel(ctx, "aeron:ipc", 1100);

    if (tp_client_init(client, ctx) < 0)
    {
        return -1;
    }

    if (tp_client_start(client) < 0)
    {
        tp_client_close(client);
        return -1;
    }

    return 0;
}

static int tp_test_add_publication(tp_client_t *client, const char *channel, int32_t stream_id, aeron_publication_t **out)
{
    aeron_async_add_publication_t *async_add = NULL;

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

static int tp_test_add_subscription(tp_client_t *client, const char *channel, int32_t stream_id, aeron_subscription_t **out)
{
    aeron_async_add_subscription_t *async_add = NULL;

    if (tp_client_async_add_subscription(client, channel, stream_id, NULL, NULL, NULL, NULL, &async_add) < 0)
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

static int tp_test_offer(tp_client_t *client, aeron_publication_t *pub, const uint8_t *buffer, size_t length)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        int64_t result = aeron_publication_offer(pub, buffer, length, NULL, NULL);
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

static void tp_on_consumer_hello(const tp_consumer_hello_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_hello = 1;
    }
}

static void tp_on_qos_event(void *clientd, const tp_qos_event_t *event)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (event)
    {
        state->saw_qos = 1;
    }
}

static void tp_on_announce(const tp_data_source_announce_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_announce = 1;
    }
}

static void tp_on_meta_begin(const tp_data_source_meta_view_t *view, void *clientd)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (view)
    {
        state->saw_meta = 1;
    }
}

static void tp_on_progress(void *clientd, const tp_frame_progress_t *progress)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (progress)
    {
        state->saw_progress = 1;
    }
}

static void tp_on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_poll_state_t *state = (tp_poll_state_t *)clientd;
    if (desc)
    {
        state->saw_descriptor = 1;
    }
}

void tp_test_pollers(void)
{
    tp_client_context_t ctx;
    tp_client_t client;
    tp_consumer_context_t consumer_ctx;
    tp_consumer_t consumer;
    aeron_publication_t *control_pub = NULL;
    aeron_publication_t *qos_pub = NULL;
    aeron_publication_t *meta_pub = NULL;
    aeron_publication_t *progress_pub = NULL;
    aeron_publication_t *descriptor_pub = NULL;
    aeron_subscription_t *progress_sub = NULL;
    tp_control_poller_t control_poller;
    tp_qos_poller_t qos_poller;
    tp_metadata_poller_t meta_poller;
    tp_progress_poller_t progress_poller;
    tp_control_handlers_t control_handlers;
    tp_qos_handlers_t qos_handlers;
    tp_metadata_handlers_t meta_handlers;
    tp_progress_handlers_t progress_handlers;
    tp_poll_state_t state;
    int result = -1;
    uint8_t buffer[512];
    int64_t deadline;

    memset(&state, 0, sizeof(state));
    memset(&consumer, 0, sizeof(consumer));
    memset(&control_handlers, 0, sizeof(control_handlers));
    memset(&qos_handlers, 0, sizeof(qos_handlers));
    memset(&meta_handlers, 0, sizeof(meta_handlers));
    memset(&progress_handlers, 0, sizeof(progress_handlers));

    {
        const char *candidates[] = { getenv("AERON_DIR"), "/dev/shm/aeron-dgamroth", "/dev/shm/aeron" };
        size_t i;
        int started = 0;

        for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        {
            if (tp_test_start_client(&client, &ctx, candidates[i]) == 0)
            {
                started = 1;
                break;
            }
        }

        if (!started)
        {
            return;
        }
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1000, &control_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1200, &qos_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1300, &meta_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1003, &progress_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_publication(&client, "aeron:ipc", 1100, &descriptor_pub) < 0)
    {
        goto cleanup;
    }

    if (tp_test_add_subscription(&client, "aeron:ipc", 1003, &progress_sub) < 0)
    {
        goto cleanup;
    }

    control_handlers.on_consumer_hello = tp_on_consumer_hello;
    control_handlers.clientd = &state;
    if (tp_control_poller_init(&control_poller, &client, &control_handlers) < 0)
    {
        goto cleanup;
    }

    qos_handlers.on_qos_event = tp_on_qos_event;
    qos_handlers.clientd = &state;
    if (tp_qos_poller_init(&qos_poller, &client, &qos_handlers) < 0)
    {
        goto cleanup;
    }

    meta_handlers.on_data_source_announce = tp_on_announce;
    meta_handlers.on_data_source_meta_begin = tp_on_meta_begin;
    meta_handlers.clientd = &state;
    if (tp_metadata_poller_init(&meta_poller, &client, &meta_handlers) < 0)
    {
        goto cleanup;
    }

    progress_handlers.on_progress = tp_on_progress;
    progress_handlers.clientd = &state;
    if (tp_progress_poller_init_with_subscription(&progress_poller, progress_sub, &progress_handlers) < 0)
    {
        goto cleanup;
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_consumerHello hello;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_consumerHello_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_consumerHello_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_consumerHello_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_consumerHello_sbe_schema_version());

        tensor_pool_consumerHello_wrap_for_encode(&hello, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_consumerHello_set_streamId(&hello, 1);
        tensor_pool_consumerHello_set_consumerId(&hello, 2);
        tensor_pool_consumerHello_set_supportsShm(&hello, 1);
        tensor_pool_consumerHello_set_supportsProgress(&hello, 1);
        tensor_pool_consumerHello_set_mode(&hello, tensor_pool_mode_STREAM);
        tensor_pool_consumerHello_set_maxRateHz(&hello, 0);
        tensor_pool_consumerHello_set_expectedLayoutVersion(&hello, 0);
        tensor_pool_consumerHello_set_progressIntervalUs(&hello, tensor_pool_consumerHello_progressIntervalUs_null_value());
        tensor_pool_consumerHello_set_progressBytesDelta(&hello, tensor_pool_consumerHello_progressBytesDelta_null_value());
        tensor_pool_consumerHello_set_progressMajorDeltaUnits(&hello, tensor_pool_consumerHello_progressMajorDeltaUnits_null_value());
        tensor_pool_consumerHello_set_descriptorStreamId(&hello, 0);
        tensor_pool_consumerHello_set_controlStreamId(&hello, 0);
        tensor_pool_consumerHello_put_descriptorChannel(&hello, "", 0);
        tensor_pool_consumerHello_put_controlChannel(&hello, "", 0);

        if (tp_test_offer(&client, control_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_qosConsumer qos;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_qosConsumer_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_qosConsumer_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_qosConsumer_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_qosConsumer_sbe_schema_version());

        tensor_pool_qosConsumer_wrap_for_encode(&qos, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_qosConsumer_set_streamId(&qos, 1);
        tensor_pool_qosConsumer_set_consumerId(&qos, 2);
        tensor_pool_qosConsumer_set_epoch(&qos, 3);
        tensor_pool_qosConsumer_set_lastSeqSeen(&qos, 4);
        tensor_pool_qosConsumer_set_dropsGap(&qos, 0);
        tensor_pool_qosConsumer_set_dropsLate(&qos, 0);
        tensor_pool_qosConsumer_set_mode(&qos, tensor_pool_mode_STREAM);

        if (tp_test_offer(&client, qos_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_dataSourceAnnounce announce;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_dataSourceAnnounce_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_dataSourceAnnounce_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_dataSourceAnnounce_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_dataSourceAnnounce_sbe_schema_version());

        tensor_pool_dataSourceAnnounce_wrap_for_encode(&announce, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_dataSourceAnnounce_set_streamId(&announce, 9);
        tensor_pool_dataSourceAnnounce_set_producerId(&announce, 3);
        tensor_pool_dataSourceAnnounce_set_epoch(&announce, 7);
        tensor_pool_dataSourceAnnounce_set_metaVersion(&announce, 1);
        tensor_pool_dataSourceAnnounce_put_name(&announce, "name", 4);
        tensor_pool_dataSourceAnnounce_put_summary(&announce, "summary", 7);

        if (tp_test_offer(&client, meta_pub, buffer, (size_t)tensor_pool_dataSourceAnnounce_sbe_position(&announce)) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_dataSourceMeta meta;
        struct tensor_pool_dataSourceMeta_attributes attrs;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_dataSourceMeta_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_dataSourceMeta_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_dataSourceMeta_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_dataSourceMeta_sbe_schema_version());

        tensor_pool_dataSourceMeta_wrap_for_encode(&meta, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_dataSourceMeta_set_streamId(&meta, 9);
        tensor_pool_dataSourceMeta_set_metaVersion(&meta, 1);
        tensor_pool_dataSourceMeta_set_timestampNs(&meta, 10);

        tensor_pool_dataSourceMeta_attributes_wrap_for_encode(
            &attrs,
            (char *)buffer,
            1,
            tensor_pool_dataSourceMeta_sbe_position_ptr(&meta),
            tensor_pool_dataSourceMeta_sbe_schema_version(),
            sizeof(buffer));
        tensor_pool_dataSourceMeta_attributes_next(&attrs);
        tensor_pool_dataSourceMeta_attributes_put_key(&attrs, "k", 1);
        tensor_pool_dataSourceMeta_attributes_put_format(&attrs, "s", 1);
        tensor_pool_dataSourceMeta_attributes_put_value(&attrs, "v", 1);

        if (tp_test_offer(&client, meta_pub, buffer, (size_t)tensor_pool_dataSourceMeta_sbe_position(&meta)) < 0)
        {
            goto cleanup;
        }
    }

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_frameProgress progress;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_frameProgress_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_frameProgress_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_frameProgress_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_frameProgress_sbe_schema_version());

        tensor_pool_frameProgress_wrap_for_encode(&progress, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_frameProgress_set_streamId(&progress, 1);
        tensor_pool_frameProgress_set_epoch(&progress, 1);
        tensor_pool_frameProgress_set_seq(&progress, 5);
        tensor_pool_frameProgress_set_payloadBytesFilled(&progress, 128);
        tensor_pool_frameProgress_set_state(&progress, tensor_pool_frameProgressState_STARTED);

        if (tp_test_offer(&client, progress_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }

    if (tp_consumer_init(&consumer, &client, &consumer_ctx) < 0)
    {
        goto cleanup;
    }

    tp_consumer_set_descriptor_handler(&consumer, tp_on_descriptor, &state);

    {
        struct tensor_pool_messageHeader header;
        struct tensor_pool_frameDescriptor descriptor;
        size_t header_len = tensor_pool_messageHeader_encoded_length();
        size_t body_len = tensor_pool_frameDescriptor_sbe_block_length();

        tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), sizeof(buffer));
        tensor_pool_messageHeader_set_blockLength(&header, (uint16_t)body_len);
        tensor_pool_messageHeader_set_templateId(&header, tensor_pool_frameDescriptor_sbe_template_id());
        tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_frameDescriptor_sbe_schema_id());
        tensor_pool_messageHeader_set_version(&header, tensor_pool_frameDescriptor_sbe_schema_version());

        tensor_pool_frameDescriptor_wrap_for_encode(&descriptor, (char *)buffer, header_len, sizeof(buffer));
        tensor_pool_frameDescriptor_set_streamId(&descriptor, 9);
        tensor_pool_frameDescriptor_set_epoch(&descriptor, 1);
        tensor_pool_frameDescriptor_set_seq(&descriptor, 42);
        tensor_pool_frameDescriptor_set_timestampNs(&descriptor, 123);
        tensor_pool_frameDescriptor_set_metaVersion(&descriptor, 1);
        tensor_pool_frameDescriptor_set_traceId(&descriptor, 9);

        if (tp_test_offer(&client, descriptor_pub, buffer, header_len + body_len) < 0)
        {
            goto cleanup;
        }
    }

    deadline = tp_clock_now_ns() + 5 * 1000 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline)
    {
        tp_control_poll(&control_poller, 10);
        tp_qos_poll(&qos_poller, 10);
        tp_metadata_poll(&meta_poller, 10);
        tp_progress_poll(&progress_poller, 10);
        tp_consumer_poll_descriptors(&consumer, 10);
        tp_client_do_work(&client);

        if (state.saw_hello && state.saw_qos && state.saw_announce && state.saw_meta && state.saw_progress && state.saw_descriptor)
        {
            result = 0;
            break;
        }

        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

cleanup:
    if (consumer.client)
    {
        tp_consumer_close(&consumer);
    }
    if (control_poller.assembler)
    {
        aeron_fragment_assembler_delete(control_poller.assembler);
    }
    if (qos_poller.assembler)
    {
        aeron_fragment_assembler_delete(qos_poller.assembler);
    }
    if (meta_poller.assembler)
    {
        aeron_fragment_assembler_delete(meta_poller.assembler);
    }
    if (progress_poller.assembler)
    {
        aeron_fragment_assembler_delete(progress_poller.assembler);
    }
    if (control_pub)
    {
        aeron_publication_close(control_pub, NULL, NULL);
    }
    if (qos_pub)
    {
        aeron_publication_close(qos_pub, NULL, NULL);
    }
    if (meta_pub)
    {
        aeron_publication_close(meta_pub, NULL, NULL);
    }
    if (progress_pub)
    {
        aeron_publication_close(progress_pub, NULL, NULL);
    }
    if (descriptor_pub)
    {
        aeron_publication_close(descriptor_pub, NULL, NULL);
    }
    if (progress_sub)
    {
        aeron_subscription_close(progress_sub, NULL, NULL);
    }
    tp_client_close(&client);

    if (result != 0)
    {
        fprintf(stderr,
            "pollers: hello=%d qos=%d announce=%d meta=%d progress=%d desc=%d\n",
            state.saw_hello,
            state.saw_qos,
            state.saw_announce,
            state.saw_meta,
            state.saw_progress,
            state.saw_descriptor);
    }

    assert(result == 0);
}
