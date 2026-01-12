#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_aeron.h"
#include "tensor_pool/tp_control_adapter.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_error.h"

#include "aeron_fragment_assembler.h"

#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/frameProgressState.h"
#include "wire/tensor_pool/messageHeader.h"

#include <inttypes.h>
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

static void tp_print_string_view(const char *label, const tp_string_view_t *view)
{
    if (NULL == view)
    {
        return;
    }

    if (view->data && view->length > 0)
    {
        printf("%s=%.*s", label, (int)view->length, view->data);
    }
    else
    {
        printf("%s=<empty>", label);
    }
}

typedef struct tp_meta_ctx_stct
{
    uint32_t attr_index;
}
tp_meta_ctx_t;

static void tp_on_meta_attr(const tp_data_source_meta_attr_view_t *attr, void *clientd)
{
    tp_meta_ctx_t *ctx = (tp_meta_ctx_t *)clientd;

    if (NULL == attr)
    {
        return;
    }

    printf("  attr[%u] ", ctx ? ctx->attr_index : 0);
    tp_print_string_view("key", &attr->key);
    printf(" ");
    tp_print_string_view("format", &attr->format);
    printf(" ");
    tp_print_string_view("value", &attr->value);
    printf("\n");

    if (ctx)
    {
        ctx->attr_index++;
    }
}

static void tp_on_control_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    struct tensor_pool_messageHeader msg_header;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    (void)clientd;
    (void)header;

    if (NULL == buffer || length < tensor_pool_messageHeader_encoded_length())
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

    if (schema_id != tensor_pool_messageHeader_sbe_schema_id())
    {
        return;
    }

    if (template_id == tensor_pool_consumerHello_sbe_template_id())
    {
        tp_consumer_hello_view_t view;
        if (tp_control_decode_consumer_hello(buffer, length, &view) == 0)
        {
            printf("ConsumerHello stream=%u consumer=%u supports_shm=%u supports_progress=%u mode=%u max_rate=%u\n",
                view.stream_id,
                view.consumer_id,
                view.supports_shm,
                view.supports_progress,
                view.mode,
                view.max_rate_hz);
            printf("  expected_layout=%u progress_interval=%u progress_bytes=%u progress_major=%u\n",
                view.expected_layout_version,
                view.progress_interval_us,
                view.progress_bytes_delta,
                view.progress_major_delta_units);
            printf("  descriptor_stream=%u control_stream=%u ",
                view.descriptor_stream_id,
                view.control_stream_id);
            tp_print_string_view("descriptor_channel", &view.descriptor_channel);
            printf(" ");
            tp_print_string_view("control_channel", &view.control_channel);
            printf("\n");
        }
        return;
    }

    if (template_id == tensor_pool_consumerConfig_sbe_template_id())
    {
        tp_consumer_config_view_t view;
        if (tp_control_decode_consumer_config(buffer, length, &view) == 0)
        {
            printf("ConsumerConfig stream=%u consumer=%u use_shm=%u mode=%u descriptor_stream=%u control_stream=%u\n",
                view.stream_id,
                view.consumer_id,
                view.use_shm,
                view.mode,
                view.descriptor_stream_id,
                view.control_stream_id);
            printf("  ");
            tp_print_string_view("payload_fallback", &view.payload_fallback_uri);
            printf(" ");
            tp_print_string_view("descriptor_channel", &view.descriptor_channel);
            printf(" ");
            tp_print_string_view("control_channel", &view.control_channel);
            printf("\n");
        }
        return;
    }

    if (template_id == tensor_pool_dataSourceAnnounce_sbe_template_id())
    {
        tp_data_source_announce_view_t view;
        if (tp_control_decode_data_source_announce(buffer, length, &view) == 0)
        {
            printf("DataSourceAnnounce stream=%u producer=%u epoch=%" PRIu64 " meta_version=%u ",
                view.stream_id,
                view.producer_id,
                view.epoch,
                view.meta_version);
            tp_print_string_view("name", &view.name);
            printf(" ");
            tp_print_string_view("summary", &view.summary);
            printf("\n");
        }
        return;
    }

    if (template_id == tensor_pool_dataSourceMeta_sbe_template_id())
    {
        tp_data_source_meta_view_t view;
        tp_meta_ctx_t meta_ctx;

        memset(&meta_ctx, 0, sizeof(meta_ctx));
        if (tp_control_decode_data_source_meta(buffer, length, &view, tp_on_meta_attr, &meta_ctx) == 0)
        {
            printf("DataSourceMeta stream=%u meta_version=%u timestamp_ns=%" PRIu64 " attrs=%u\n",
                view.stream_id,
                view.meta_version,
                view.timestamp_ns,
                view.attribute_count);
        }
        return;
    }

    if (template_id == tensor_pool_frameProgress_sbe_template_id())
    {
        struct tensor_pool_frameProgress progress;
        enum tensor_pool_frameProgressState state;
        uint8_t state_value = 0;

        tensor_pool_frameProgress_wrap_for_decode(
            &progress,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            block_length,
            version,
            length);

        if (tensor_pool_frameProgress_state(&progress, &state))
        {
            state_value = (uint8_t)state;
        }

        printf("FrameProgress stream=%u seq=%" PRIu64 " header_index=%u bytes=%" PRIu64 " state=%u\n",
            tensor_pool_frameProgress_streamId(&progress),
            tensor_pool_frameProgress_frameId(&progress),
            tensor_pool_frameProgress_headerIndex(&progress),
            tensor_pool_frameProgress_payloadBytesFilled(&progress),
            state_value);
        return;
    }
}

int main(int argc, char **argv)
{
    const char *aeron_dir = NULL;
    const char *control_channel = "aeron:ipc";
    int32_t control_stream_id = 1000;
    tp_context_t context;
    tp_aeron_client_t aeron;
    aeron_subscription_t *subscription = NULL;
    aeron_fragment_assembler_t *assembler = NULL;

    if (argc > 1)
    {
        aeron_dir = argv[1];
    }
    if (argc > 2)
    {
        control_channel = argv[2];
    }
    if (argc > 3)
    {
        control_stream_id = (int32_t)strtol(argv[3], NULL, 10);
    }

    if (tp_context_init(&context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    if (aeron_dir)
    {
        tp_context_set_aeron_dir(&context, aeron_dir);
    }

    if (tp_aeron_client_init(&aeron, &context) < 0)
    {
        fprintf(stderr, "Aeron init failed: %s\n", tp_errmsg());
        return 1;
    }

    if (tp_aeron_add_subscription(&subscription, &aeron, control_channel, control_stream_id, NULL, NULL, NULL, NULL) < 0)
    {
        fprintf(stderr, "Subscription failed: %s\n", tp_errmsg());
        tp_aeron_client_close(&aeron);
        return 1;
    }

    if (aeron_fragment_assembler_create(&assembler, tp_on_control_fragment, NULL) < 0)
    {
        fprintf(stderr, "Fragment assembler failed: %s\n", tp_errmsg());
        aeron_subscription_close(subscription, NULL, NULL);
        tp_aeron_client_close(&aeron);
        return 1;
    }

    signal(SIGINT, tp_handle_sigint);

    printf("Listening on %s:%d (Ctrl+C to stop)\n", control_channel, control_stream_id);

    while (tp_running)
    {
        int fragments = aeron_subscription_poll(subscription, aeron_fragment_assembler_handler, assembler, 10);
        if (fragments < 0)
        {
            fprintf(stderr, "Poll failed: %d\n", fragments);
            break;
        }

        if (fragments == 0)
        {
            struct timespec sleep_ts;
            sleep_ts.tv_sec = 0;
            sleep_ts.tv_nsec = 1000000;
            nanosleep(&sleep_ts, NULL);
        }
    }

    aeron_fragment_assembler_delete(assembler);
    aeron_subscription_close(subscription, NULL, NULL);
    tp_aeron_client_close(&aeron);

    return 0;
}
