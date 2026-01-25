#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_error.h"

#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/frameDescriptor.h"
#include "wire/tensor_pool/messageHeader.h"

#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t tp_running = 1;

static void tp_handle_sigint(int signo)
{
    (void)signo;
    tp_running = 0;
}

typedef struct tp_listen_state_stct
{
    int json;
    int raw;
    FILE *raw_out;
    int connected;
    int last_images;
    int64_t last_status;
}
tp_listen_state_t;

static void tp_dump_raw_fragment_to(
    FILE *out,
    const char *label,
    const uint8_t *buffer,
    size_t length)
{
    struct tensor_pool_messageHeader msg_header;
    uint16_t template_id = 0;
    uint16_t schema_id = 0;
    uint16_t block_length = 0;
    uint16_t version = 0;
    size_t i;

    if (NULL == out || NULL == buffer)
    {
        return;
    }

    if (length >= tensor_pool_messageHeader_encoded_length())
    {
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
        fprintf(out,
            "RAW %s len=%zu schema=%u template=%u block_length=%u version=%u data=",
            label ? label : "unknown",
            length,
            (unsigned)schema_id,
            (unsigned)template_id,
            (unsigned)block_length,
            (unsigned)version);
    }
    else
    {
        fprintf(out, "RAW %s len=%zu data=", label ? label : "unknown", length);
    }

    for (i = 0; i < length; i++)
    {
        fprintf(out, "%02x", buffer[i]);
    }
    fprintf(out, "\n");
    fflush(out);
}

static void tp_dump_raw_fragment(
    tp_listen_state_t *state,
    const char *label,
    const uint8_t *buffer,
    size_t length)
{
    if (NULL == state || (!state->raw && NULL == state->raw_out))
    {
        return;
    }

    if (state->raw)
    {
        tp_dump_raw_fragment_to(stderr, label, buffer, length);
    }
    if (state->raw_out)
    {
        tp_dump_raw_fragment_to(state->raw_out, label, buffer, length);
    }
}

static void tp_log_subscription_status(tp_listen_state_t *state, tp_subscription_t *subscription)
{
    int image_count;
    int64_t status;

    if (NULL == state || NULL == subscription)
    {
        return;
    }

    image_count = aeron_subscription_image_count(tp_subscription_handle(subscription));
    status = aeron_subscription_channel_status(tp_subscription_handle(subscription));
    if (image_count != state->last_images || status != state->last_status)
    {
        fprintf(stderr, "Descriptor subscription images=%d channel_status=%" PRId64 "\n", image_count, status);
        state->last_images = image_count;
        state->last_status = status;
    }

    if (!state->connected && aeron_subscription_is_connected(tp_subscription_handle(subscription)))
    {
        fprintf(stderr, "Descriptor subscription connected\n");
        state->connected = 1;
    }
}

static void tp_on_descriptor_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_listen_state_t *state = (tp_listen_state_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_frameDescriptor descriptor;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    (void)header;

    if (NULL == buffer)
    {
        return;
    }

    tp_dump_raw_fragment(state, "descriptor", buffer, length);

    if (length < tensor_pool_messageHeader_encoded_length())
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

    if (state && state->json)
    {
        printf("{\"type\":\"FrameDescriptor\",\"stream\":%u,\"epoch\":%" PRIu64 ",\"seq\":%" PRIu64 ",\"timestamp_ns\":%" PRIu64 ",\"meta_version\":%u,\"trace_id\":%" PRIu64 "}\n",
            tensor_pool_frameDescriptor_streamId(&descriptor),
            tensor_pool_frameDescriptor_epoch(&descriptor),
            tensor_pool_frameDescriptor_seq(&descriptor),
            tensor_pool_frameDescriptor_timestampNs(&descriptor),
            tensor_pool_frameDescriptor_metaVersion(&descriptor),
            tensor_pool_frameDescriptor_traceId(&descriptor));
    }
    else
    {
        printf("FrameDescriptor stream=%u epoch=%" PRIu64 " seq=%" PRIu64 " timestamp_ns=%" PRIu64 " meta_version=%u trace_id=%" PRIu64 "\n",
            tensor_pool_frameDescriptor_streamId(&descriptor),
            tensor_pool_frameDescriptor_epoch(&descriptor),
            tensor_pool_frameDescriptor_seq(&descriptor),
            tensor_pool_frameDescriptor_timestampNs(&descriptor),
            tensor_pool_frameDescriptor_metaVersion(&descriptor),
            tensor_pool_frameDescriptor_traceId(&descriptor));
    }
}

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s [--json] [--raw] [--raw-out <path>] [aeron_dir] [channel] [descriptor_stream_id]\n", name);
}

int main(int argc, char **argv)
{
    const char *aeron_dir = NULL;
    const char *channel = "aeron:ipc";
    int32_t descriptor_stream_id = 1100;
    tp_listen_state_t state;
    tp_client_context_t context;
    tp_client_t client;
    tp_subscription_t *descriptor_subscription = NULL;
    tp_fragment_assembler_t *assembler = NULL;
    int arg_index = 1;
    tp_async_add_subscription_t *async_add = NULL;

    memset(&state, 0, sizeof(state));
    state.last_images = -1;
    state.last_status = INT64_MIN;

    while (arg_index < argc && strncmp(argv[arg_index], "--", 2) == 0)
    {
        if (strcmp(argv[arg_index], "--json") == 0)
        {
            state.json = 1;
        }
        else if (strcmp(argv[arg_index], "--raw") == 0)
        {
            state.raw = 1;
        }
        else if (strcmp(argv[arg_index], "--raw-out") == 0)
        {
            if (arg_index + 1 >= argc)
            {
                fprintf(stderr, "Missing path for --raw-out\n");
                return 1;
            }
            state.raw_out = fopen(argv[arg_index + 1], "w");
            if (NULL == state.raw_out)
            {
                fprintf(stderr, "Failed to open raw output: %s\n", argv[arg_index + 1]);
                return 1;
            }
            arg_index++;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[arg_index]);
            usage(argv[0]);
            return 1;
        }
        arg_index++;
    }

    if (arg_index < argc)
    {
        aeron_dir = argv[arg_index++];
    }
    if (arg_index < argc)
    {
        channel = argv[arg_index++];
    }
    if (arg_index < argc)
    {
        descriptor_stream_id = (int32_t)strtol(argv[arg_index++], NULL, 10);
    }

    if (tp_client_context_init(&context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    if (aeron_dir)
    {
        tp_client_context_set_aeron_dir(&context, aeron_dir);
    }

    tp_client_context_set_control_channel(&context, channel, 1000);
    tp_client_context_set_qos_channel(&context, channel, 1200);
    tp_client_context_set_metadata_channel(&context, channel, 1300);

    if (tp_client_init(&client, &context) < 0 || tp_client_start(&client) < 0)
    {
        fprintf(stderr, "Aeron init failed: %s\n", tp_errmsg());
        return 1;
    }

    if (tp_client_async_add_subscription(
        &client,
        channel,
        descriptor_stream_id,
        &async_add) < 0)
    {
        fprintf(stderr, "Descriptor subscription add failed: %s\n", tp_errmsg());
        tp_client_close(&client);
        return 1;
    }

    while (NULL == descriptor_subscription)
    {
        if (tp_client_async_add_subscription_poll(&descriptor_subscription, async_add) < 0)
        {
            fprintf(stderr, "Descriptor subscription poll failed: %s\n", tp_errmsg());
            tp_client_close(&client);
            return 1;
        }
        tp_client_do_work(&client);
    }

    if (tp_fragment_assembler_create(&assembler, tp_on_descriptor_fragment, &state) < 0)
    {
        fprintf(stderr, "Fragment assembler failed: %s\n", tp_errmsg());
        tp_subscription_close(&descriptor_subscription);
        tp_client_close(&client);
        return 1;
    }

    signal(SIGINT, tp_handle_sigint);

    printf("Listening on descriptor=%s:%d (Ctrl+C to stop)\n", channel, descriptor_stream_id);

    while (tp_running)
    {
        int fragments = aeron_subscription_poll(
            tp_subscription_handle(descriptor_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(assembler),
            10);

        if (fragments < 0)
        {
            fprintf(stderr, "Descriptor poll failed: %d\n", fragments);
            break;
        }

        tp_log_subscription_status(&state, descriptor_subscription);

        if (fragments == 0)
        {
            struct timespec sleep_ts;
            sleep_ts.tv_sec = 0;
            sleep_ts.tv_nsec = 1000000;
            nanosleep(&sleep_ts, NULL);
        }
    }

    tp_fragment_assembler_close(&assembler);
    tp_subscription_close(&descriptor_subscription);
    if (state.raw_out)
    {
        fclose(state.raw_out);
    }
    tp_client_close(&client);

    return 0;
}
