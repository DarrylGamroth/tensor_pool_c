#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_control.h"
#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_tensor.h"
#include "tensor_pool/tp_types.h"

#include "aeron_fragment_assembler.h"

#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/responseCode.h"

#include "wire/tensor_pool/frameDescriptor.h"
#include "wire/tensor_pool/messageHeader.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s <aeron_dir> <control_channel> <stream_id> <client_id> <max_frames> [descriptor_channel descriptor_stream_id [progress_channel progress_stream_id]]\n", name);
}

typedef struct tp_descriptor_poll_state_stct
{
    tp_consumer_t *consumer;
    uint32_t consumer_id;
    const char *shared_descriptor_channel;
    int32_t shared_descriptor_stream_id;
    const char *shared_control_channel;
    int32_t shared_control_stream_id;
    char active_descriptor_channel[TP_URI_MAX_LENGTH];
    uint32_t active_descriptor_stream_id;
    char pending_descriptor_channel[TP_URI_MAX_LENGTH];
    uint32_t pending_descriptor_stream_id;
    char pending_control_channel[TP_URI_MAX_LENGTH];
    uint32_t pending_control_stream_id;
    int pending_update;
    int received;
    int limit;
}
tp_descriptor_poll_state_t;

static int64_t tp_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
        return 0;
    }

    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void tp_on_frame_descriptor(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_descriptor_poll_state_t *state = (tp_descriptor_poll_state_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameDescriptor descriptor;
    tp_frame_view_t frame;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    uint64_t seq;
    uint32_t header_index;
    int result;

    (void)header;

    if (NULL == state || NULL == buffer || length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);

    if (schema_id != tensor_pool_frameDescriptor_sbe_schema_id() ||
        template_id != tensor_pool_frameDescriptor_sbe_template_id())
    {
        return;
    }

    tensor_pool_frameDescriptor_wrap_for_decode(
        &descriptor,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    seq = tensor_pool_frameDescriptor_seq(&descriptor);
    header_index = tensor_pool_frameDescriptor_headerIndex(&descriptor);

    result = tp_consumer_read_frame(state->consumer, seq, header_index, &frame);
    if (result == 0)
    {
        printf("Frame seq=%" PRIu64 " dims=%d x %d payload_len=%u\n",
            seq,
            frame.tensor.dims[0],
            frame.tensor.dims[1],
            frame.payload_len);
        if (frame.payload_len >= sizeof(float))
        {
            const float *values = (const float *)frame.payload;
            printf("payload[0]=%f\n", values[0]);
        }
    }
    else
    {
        printf("FrameDescriptor seq=%" PRIu64 " header_index=%u: frame not available (result=%d)\n",
            seq,
            header_index,
            result);
    }

    state->received++;
}

static void tp_on_consumer_config(const tp_consumer_config_view_t *view, void *clientd)
{
    tp_descriptor_poll_state_t *state = (tp_descriptor_poll_state_t *)clientd;

    if (NULL == state || NULL == view || view->consumer_id != state->consumer_id)
    {
        return;
    }

    if (view->descriptor_channel.length > 0 && view->descriptor_stream_id != 0)
    {
        size_t len = view->descriptor_channel.length;
        if (len >= sizeof(state->pending_descriptor_channel))
        {
            len = sizeof(state->pending_descriptor_channel) - 1;
        }
        memcpy(state->pending_descriptor_channel, view->descriptor_channel.data, len);
        state->pending_descriptor_channel[len] = '\0';
        state->pending_descriptor_stream_id = view->descriptor_stream_id;
    }
    else
    {
        state->pending_descriptor_channel[0] = '\0';
        state->pending_descriptor_stream_id = 0;
    }

    if (view->control_channel.length > 0 && view->control_stream_id != 0)
    {
        size_t len = view->control_channel.length;
        if (len >= sizeof(state->pending_control_channel))
        {
            len = sizeof(state->pending_control_channel) - 1;
        }
        memcpy(state->pending_control_channel, view->control_channel.data, len);
        state->pending_control_channel[len] = '\0';
        state->pending_control_stream_id = view->control_stream_id;
    }
    else
    {
        state->pending_control_channel[0] = '\0';
        state->pending_control_stream_id = 0;
    }

    state->pending_update = 1;
}

int main(int argc, char **argv)
{
    tp_context_t context;
    tp_driver_client_t driver;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_consumer_pool_config_t *pool_cfg = NULL;
    tp_consumer_t consumer;
    tp_consumer_config_t consumer_cfg;
    tp_control_subscription_t control_sub;
    tp_control_adapter_t control_adapter;
    aeron_fragment_assembler_t *assembler = NULL;
    tp_descriptor_poll_state_t poll_state;
    uint32_t stream_id;
    uint32_t client_id;
    int max_frames;
    const char *requested_descriptor_channel = NULL;
    uint32_t requested_descriptor_stream_id = 0;
    const char *requested_control_channel = NULL;
    uint32_t requested_control_stream_id = 0;
    size_t i;
    int result;
    int64_t deadline_ns;

    if (argc != 6 && argc != 8 && argc != 10)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);
    max_frames = (int)strtol(argv[5], NULL, 10);
    if (max_frames <= 0)
    {
        fprintf(stderr, "max_frames must be > 0\n");
        return 1;
    }
    if (argc >= 8)
    {
        requested_descriptor_channel = argv[6];
        requested_descriptor_stream_id = (uint32_t)strtoul(argv[7], NULL, 10);
    }
    if (argc >= 10)
    {
        requested_control_channel = argv[8];
        requested_control_stream_id = (uint32_t)strtoul(argv[9], NULL, 10);
    }

    if (tp_context_init(&context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    tp_context_set_aeron_dir(&context, argv[1]);
    tp_context_set_control_channel(&context, argv[2], 1000);
    tp_context_set_descriptor_channel(&context, "aeron:ipc", 1100);
    tp_context_set_qos_channel(&context, "aeron:ipc", 1200);
    tp_context_set_metadata_channel(&context, "aeron:ipc", 1300);

    if (tp_driver_client_init(&driver, &context) < 0)
    {
        fprintf(stderr, "Driver init failed: %s\n", tp_errmsg());
        return 1;
    }

    memset(&request, 0, sizeof(request));
    request.correlation_id = 1;
    request.stream_id = stream_id;
    request.client_id = client_id;
    request.role = tensor_pool_role_CONSUMER;
    request.expected_layout_version = 0;
    request.publish_mode = tensor_pool_publishMode_REQUIRE_EXISTING;
    request.require_hugepages = tensor_pool_hugepagesPolicy_UNSPECIFIED;

    if (tp_driver_attach(&driver, &request, &info, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        fprintf(stderr, "Attach failed: %s\n", tp_errmsg());
        tp_driver_client_close(&driver);
        return 1;
    }

    if (info.code != tensor_pool_responseCode_OK)
    {
        fprintf(stderr, "Attach rejected: code=%d error=%s\n", info.code, info.error_message);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    pool_cfg = calloc(info.pool_count, sizeof(*pool_cfg));
    if (NULL == pool_cfg)
    {
        fprintf(stderr, "Allocation failed\n");
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    for (i = 0; i < info.pool_count; i++)
    {
        pool_cfg[i].pool_id = info.pools[i].pool_id;
        pool_cfg[i].nslots = info.pools[i].nslots;
        pool_cfg[i].stride_bytes = info.pools[i].stride_bytes;
        pool_cfg[i].uri = info.pools[i].region_uri;
    }

    if (tp_consumer_init(&consumer, &context) < 0)
    {
        fprintf(stderr, "Consumer init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    memset(&consumer_cfg, 0, sizeof(consumer_cfg));
    consumer_cfg.stream_id = info.stream_id;
    consumer_cfg.epoch = info.epoch;
    consumer_cfg.layout_version = info.layout_version;
    consumer_cfg.header_nslots = info.header_nslots;
    consumer_cfg.header_uri = info.header_region_uri;
    consumer_cfg.pools = pool_cfg;
    consumer_cfg.pool_count = info.pool_count;

    if (tp_consumer_attach_direct(&consumer, &consumer_cfg) < 0)
    {
        fprintf(stderr, "Consumer attach failed: %s\n", tp_errmsg());
        tp_consumer_close(&consumer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    memset(&poll_state, 0, sizeof(poll_state));
    poll_state.consumer = &consumer;
    poll_state.consumer_id = client_id;
    poll_state.shared_descriptor_channel = context.descriptor_channel;
    poll_state.shared_descriptor_stream_id = context.descriptor_stream_id;
    poll_state.shared_control_channel = context.control_channel;
    poll_state.shared_control_stream_id = context.control_stream_id;
    strncpy(poll_state.active_descriptor_channel, context.descriptor_channel, sizeof(poll_state.active_descriptor_channel) - 1);
    poll_state.active_descriptor_stream_id = (uint32_t)context.descriptor_stream_id;
    poll_state.limit = max_frames;

    memset(&control_adapter, 0, sizeof(control_adapter));
    control_adapter.on_consumer_config = tp_on_consumer_config;
    control_adapter.clientd = &poll_state;
    if (tp_control_subscription_init(
        &control_sub,
        consumer.aeron.aeron,
        context.control_channel,
        context.control_stream_id,
        &control_adapter) < 0)
    {
        fprintf(stderr, "Control subscription init failed: %s\n", tp_errmsg());
        tp_consumer_close(&consumer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    if (NULL == consumer.descriptor_subscription)
    {
        fprintf(stderr, "Descriptor subscription not configured\n");
        tp_control_subscription_close(&control_sub);
        tp_consumer_close(&consumer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    if (aeron_fragment_assembler_create(&assembler, tp_on_frame_descriptor, &poll_state) < 0)
    {
        fprintf(stderr, "Failed to create fragment assembler: %s\n", tp_errmsg());
        tp_control_subscription_close(&control_sub);
        tp_consumer_close(&consumer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    {
        tp_consumer_hello_t hello;
        memset(&hello, 0, sizeof(hello));
        hello.stream_id = info.stream_id;
        hello.consumer_id = client_id;
        hello.supports_shm = 1;
        hello.supports_progress = 0;
        hello.mode = 1;
        hello.max_rate_hz = 0;
        hello.expected_layout_version = info.layout_version;
        hello.progress_interval_us = TP_NULL_U32;
        hello.progress_bytes_delta = TP_NULL_U32;
        hello.progress_major_delta_units = TP_NULL_U32;
        hello.descriptor_stream_id = requested_descriptor_stream_id;
        hello.control_stream_id = requested_control_stream_id;
        hello.descriptor_channel = requested_descriptor_channel ? requested_descriptor_channel : "";
        hello.control_channel = requested_control_channel ? requested_control_channel : "";
        if (tp_consumer_send_hello(&consumer, &hello) < 0)
        {
            fprintf(stderr, "Failed to send ConsumerHello: %s\n", tp_errmsg());
        }
    }

    deadline_ns = tp_now_ns() + (int64_t)10 * 1000 * 1000 * 1000LL;

    while (poll_state.received < poll_state.limit)
    {
        result = tp_control_poll(&control_sub, 10);
        if (result < 0)
        {
            fprintf(stderr, "Control poll failed: %s\n", tp_errmsg());
            break;
        }

        if (poll_state.pending_update)
        {
            const char *next_channel = poll_state.pending_descriptor_channel[0] != '\0'
                ? poll_state.pending_descriptor_channel
                : poll_state.shared_descriptor_channel;
            uint32_t next_stream_id = poll_state.pending_descriptor_stream_id != 0
                ? poll_state.pending_descriptor_stream_id
                : (uint32_t)poll_state.shared_descriptor_stream_id;

            if (next_stream_id != poll_state.active_descriptor_stream_id ||
                strcmp(next_channel, poll_state.active_descriptor_channel) != 0)
            {
                if (consumer.descriptor_subscription)
                {
                    aeron_subscription_close(consumer.descriptor_subscription, NULL, NULL);
                    consumer.descriptor_subscription = NULL;
                }

                if (tp_aeron_add_subscription(
                    &consumer.descriptor_subscription,
                    &consumer.aeron,
                    next_channel,
                    (int32_t)next_stream_id,
                    NULL,
                    NULL,
                    NULL,
                    NULL) < 0)
                {
                    fprintf(stderr, "Failed to switch descriptor subscription: %s\n", tp_errmsg());
                    break;
                }

                strncpy(poll_state.active_descriptor_channel, next_channel, sizeof(poll_state.active_descriptor_channel) - 1);
                poll_state.active_descriptor_stream_id = next_stream_id;
                printf("Switched descriptor stream to %s:%u\n", next_channel, next_stream_id);
            }

            if (poll_state.pending_control_channel[0] != '\0' && poll_state.pending_control_stream_id != 0)
            {
                printf("Per-consumer progress stream available on %s:%u (not consumed in example)\n",
                    poll_state.pending_control_channel,
                    poll_state.pending_control_stream_id);
            }

            poll_state.pending_update = 0;
        }

        result = aeron_subscription_poll(
            consumer.descriptor_subscription,
            aeron_fragment_assembler_handler,
            assembler,
            10);

        if (result < 0)
        {
            fprintf(stderr, "Descriptor poll failed: %d\n", result);
            break;
        }

        if (tp_now_ns() > deadline_ns)
        {
            fprintf(stderr, "Timed out waiting for FrameDescriptors\n");
            break;
        }

        if (result == 0)
        {
            struct timespec sleep_ts;
            sleep_ts.tv_sec = 0;
            sleep_ts.tv_nsec = 1000000;
            nanosleep(&sleep_ts, NULL);
        }
    }

    if (assembler)
    {
        aeron_fragment_assembler_delete(assembler);
    }

    tp_control_subscription_close(&control_sub);
    tp_consumer_close(&consumer);
    free(pool_cfg);
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(&driver);

    return 0;
}
