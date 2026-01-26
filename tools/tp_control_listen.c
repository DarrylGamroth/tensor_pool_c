#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/internal/tp_control_adapter.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_merge_map.h"
#include "tensor_pool/tp_tracelink.h"

#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/controlResponse.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/frameProgress.h"
#include "wire/tensor_pool/frameProgressState.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/qosConsumer.h"
#include "wire/tensor_pool/qosProducer.h"
#include "wire/tensor_pool/mode.h"
#include "wire/tensor_pool/shmPoolAnnounce.h"

#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TP_MERGE_SCHEMA_ID 903
#define TP_TRACELINK_SCHEMA_ID 904
#define TP_MERGE_MAX_RULES 256

static volatile sig_atomic_t tp_running = 1;

static void tp_handle_sigint(int signo)
{
    (void)signo;
    tp_running = 0;
}

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options] [aeron_dir [control_channel [control_stream_id]]]\n"
        "Options:\n"
        "  -a <dir>     Aeron directory\n"
        "  -c <chan>    Control channel (default: aeron:ipc)\n"
        "  -C <id>      Control stream id (default: 1000)\n"
        "  -m <chan>    Metadata channel (default: control channel)\n"
        "  -M <id>      Metadata stream id (default: 1300)\n"
        "  -q <chan>    QoS channel (default: control channel)\n"
        "  -Q <id>      QoS stream id (default: 1200)\n"
        "  -j           JSON output\n"
        "  -r           Dump raw fragments to stderr\n"
        "  -o <path>    Dump raw fragments to file\n"
        "  -h           Show help\n",
        name);
}

typedef struct tp_listen_state_stct
{
    int json;
    int raw;
    FILE *raw_out;
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

static void tp_print_json_string_view(const char *label, const tp_string_view_t *view)
{
    size_t i;

    printf("\"%s\":\"", label);
    if (view && view->data)
    {
        for (i = 0; i < view->length; i++)
        {
            char c = view->data[i];
            if (c == '\\' || c == '\"')
            {
                putchar('\\');
            }
            putchar(c);
        }
    }
    printf("\"");
}

typedef struct tp_meta_ctx_stct
{
    uint32_t attr_index;
    void *clientd;
}
tp_meta_ctx_t;

static void tp_on_meta_attr(const tp_data_source_meta_attr_view_t *attr, void *clientd)
{
    tp_meta_ctx_t *ctx = (tp_meta_ctx_t *)clientd;
    tp_listen_state_t *state = NULL;

    if (NULL == attr)
    {
        return;
    }

    if (ctx)
    {
        state = (tp_listen_state_t *)ctx->clientd;
    }

    if (state && state->json)
    {
        printf("{\"type\":\"DataSourceMetaAttr\",\"index\":%u,", ctx ? ctx->attr_index : 0);
        tp_print_json_string_view("key", &attr->key);
        printf(",");
        tp_print_json_string_view("format", &attr->format);
        printf(",");
        tp_print_json_string_view("value", &attr->value);
        printf("}\n");
    }
    else
    {
        printf("  attr[%u] ", ctx ? ctx->attr_index : 0);
        tp_print_string_view("key", &attr->key);
        printf(" ");
        tp_print_string_view("format", &attr->format);
        printf(" ");
        tp_print_string_view("value", &attr->value);
        printf("\n");
    }

    if (ctx)
    {
        ctx->attr_index++;
    }
}

static void tp_handle_control_fragment(
    tp_listen_state_t *state,
    const char *label,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    struct tensor_pool_messageHeader msg_header;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    (void)header;

    if (NULL == buffer)
    {
        return;
    }

    tp_dump_raw_fragment(state, label, buffer, length);

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

    if (schema_id != tensor_pool_messageHeader_sbe_schema_id() &&
        schema_id != TP_MERGE_SCHEMA_ID &&
        schema_id != TP_TRACELINK_SCHEMA_ID)
    {
        return;
    }

    if (schema_id == TP_MERGE_SCHEMA_ID)
    {
        tp_sequence_merge_rule_t seq_rules[TP_MERGE_MAX_RULES];
        tp_timestamp_merge_rule_t ts_rules[TP_MERGE_MAX_RULES];
        tp_sequence_merge_map_t seq_map;
        tp_timestamp_merge_map_t ts_map;
        uint32_t out_stream_id = 0;
        uint64_t epoch = 0;

        if (tp_sequence_merge_map_decode(buffer, length, &seq_map, seq_rules, TP_MERGE_MAX_RULES) == 0)
        {
            size_t i;
            if (state && state->json)
            {
                printf("{\"type\":\"SequenceMergeMapAnnounce\",\"stream\":%u,\"epoch\":%" PRIu64 ",\"stale_ns\":%" PRIu64 ",\"rules\":[",
                    seq_map.out_stream_id,
                    seq_map.epoch,
                    seq_map.stale_timeout_ns == TP_NULL_U64 ? 0 : seq_map.stale_timeout_ns);
                for (i = 0; i < seq_map.rule_count; i++)
                {
                    const tp_sequence_merge_rule_t *rule = &seq_map.rules[i];
                    if (i > 0)
                    {
                        printf(",");
                    }
                    printf("{\"input\":%u,\"type\":%u,\"offset\":%d,\"window\":%u}",
                        rule->input_stream_id,
                        rule->rule_type,
                        rule->offset,
                        rule->window_size);
                }
                printf("]}\n");
            }
            else
            {
                printf("SequenceMergeMapAnnounce stream=%u epoch=%" PRIu64 " stale_ns=%" PRIu64 "\n",
                    seq_map.out_stream_id,
                    seq_map.epoch,
                    seq_map.stale_timeout_ns == TP_NULL_U64 ? 0 : seq_map.stale_timeout_ns);
                for (i = 0; i < seq_map.rule_count; i++)
                {
                    const tp_sequence_merge_rule_t *rule = &seq_map.rules[i];
                    printf("  input=%u type=%u offset=%d window=%u\n",
                        rule->input_stream_id,
                        rule->rule_type,
                        rule->offset,
                        rule->window_size);
                }
            }
            return;
        }

        if (tp_sequence_merge_map_request_decode(buffer, length, &out_stream_id, &epoch) == 0)
        {
            if (state && state->json)
            {
                printf("{\"type\":\"SequenceMergeMapRequest\",\"stream\":%u,\"epoch\":%" PRIu64 "}\n",
                    out_stream_id,
                    epoch);
            }
            else
            {
                printf("SequenceMergeMapRequest stream=%u epoch=%" PRIu64 "\n", out_stream_id, epoch);
            }
            return;
        }

        if (tp_timestamp_merge_map_decode(buffer, length, &ts_map, ts_rules, TP_MERGE_MAX_RULES) == 0)
        {
            size_t i;
            if (state && state->json)
            {
                printf("{\"type\":\"TimestampMergeMapAnnounce\",\"stream\":%u,\"epoch\":%" PRIu64 ",\"stale_ns\":%" PRIu64 ",",
                    ts_map.out_stream_id,
                    ts_map.epoch,
                    ts_map.stale_timeout_ns == TP_NULL_U64 ? 0 : ts_map.stale_timeout_ns);
                printf("\"clock_domain\":%u,\"lateness_ns\":%" PRIu64 ",\"rules\":[",
                    ts_map.clock_domain,
                    ts_map.lateness_ns == TP_NULL_U64 ? 0 : ts_map.lateness_ns);
                for (i = 0; i < ts_map.rule_count; i++)
                {
                    const tp_timestamp_merge_rule_t *rule = &ts_map.rules[i];
                    if (i > 0)
                    {
                        printf(",");
                    }
                    printf("{\"input\":%u,\"type\":%u,\"source\":%u,\"offset\":%" PRIi64 ",\"window\":%" PRIu64 "}",
                        rule->input_stream_id,
                        rule->rule_type,
                        rule->timestamp_source,
                        rule->offset_ns,
                        rule->window_ns);
                }
                printf("]}\n");
            }
            else
            {
                printf("TimestampMergeMapAnnounce stream=%u epoch=%" PRIu64 " stale_ns=%" PRIu64 " clock=%u lateness_ns=%" PRIu64 "\n",
                    ts_map.out_stream_id,
                    ts_map.epoch,
                    ts_map.stale_timeout_ns == TP_NULL_U64 ? 0 : ts_map.stale_timeout_ns,
                    ts_map.clock_domain,
                    ts_map.lateness_ns == TP_NULL_U64 ? 0 : ts_map.lateness_ns);
                for (i = 0; i < ts_map.rule_count; i++)
                {
                    const tp_timestamp_merge_rule_t *rule = &ts_map.rules[i];
                    printf("  input=%u type=%u source=%u offset=%" PRIi64 " window=%" PRIu64 "\n",
                        rule->input_stream_id,
                        rule->rule_type,
                        rule->timestamp_source,
                        rule->offset_ns,
                        rule->window_ns);
                }
            }
            return;
        }

        if (tp_timestamp_merge_map_request_decode(buffer, length, &out_stream_id, &epoch) == 0)
        {
            if (state && state->json)
            {
                printf("{\"type\":\"TimestampMergeMapRequest\",\"stream\":%u,\"epoch\":%" PRIu64 "}\n",
                    out_stream_id,
                    epoch);
            }
            else
            {
                printf("TimestampMergeMapRequest stream=%u epoch=%" PRIu64 "\n", out_stream_id, epoch);
            }
            return;
        }

        if (tp_errcode() != 0)
        {
            fprintf(stderr, "MergeMap decode error: %s\n", tp_errmsg());
        }
        return;
    }

    if (schema_id == TP_TRACELINK_SCHEMA_ID)
    {
        tp_tracelink_set_t set;
        uint64_t parents[TP_TRACELINK_MAX_PARENTS];
        size_t i;

        if (tp_tracelink_set_decode(buffer, length, &set, parents, TP_TRACELINK_MAX_PARENTS) == 0)
        {
            if (state && state->json)
            {
                printf("{\"type\":\"TraceLinkSet\",\"stream\":%u,\"epoch\":%" PRIu64 ",\"seq\":%" PRIu64 ",\"trace_id\":%" PRIu64 ",\"parents\":[",
                    set.stream_id,
                    set.epoch,
                    set.seq,
                    set.trace_id);
                for (i = 0; i < set.parent_count; i++)
                {
                    if (i > 0)
                    {
                        printf(",");
                    }
                    printf("%" PRIu64, set.parents[i]);
                }
                printf("]}\n");
            }
            else
            {
                printf("TraceLinkSet stream=%u epoch=%" PRIu64 " seq=%" PRIu64 " trace_id=%" PRIu64 "\n",
                    set.stream_id,
                    set.epoch,
                    set.seq,
                    set.trace_id);
                for (i = 0; i < set.parent_count; i++)
                {
                    printf("  parent_trace_id=%" PRIu64 "\n", set.parents[i]);
                }
            }
            return;
        }

        if (tp_errcode() != 0)
        {
            fprintf(stderr, "TraceLink decode error: %s\n", tp_errmsg());
        }
        return;
    }

    if (template_id == tensor_pool_shmPoolAnnounce_sbe_template_id())
    {
        tp_shm_pool_announce_view_t view;
        if (tp_control_decode_shm_pool_announce(buffer, length, &view) == 0)
        {
            if (state && state->json)
            {
                size_t i;
                printf("{\"type\":\"ShmPoolAnnounce\",\"stream\":%u,\"producer\":%u,\"epoch\":%" PRIu64 ",\"layout\":%u,",
                    view.stream_id, view.producer_id, view.epoch, view.layout_version);
                printf("\"header_nslots\":%u,\"header_slot_bytes\":%u,", view.header_nslots, view.header_slot_bytes);
                printf("\"announce_ts\":%" PRIu64 ",\"clock_domain\":%u,", view.announce_timestamp_ns, view.announce_clock_domain);
                tp_print_json_string_view("header_region_uri", &view.header_region_uri);
                printf(",\"pools\":[");
                for (i = 0; i < view.pool_count; i++)
                {
                    const tp_shm_pool_desc_t *pool = &view.pools[i];
                    if (i > 0)
                    {
                        printf(",");
                    }
                    printf("{\"pool_id\":%u,\"nslots\":%u,\"stride\":%u,", pool->pool_id, pool->nslots, pool->stride_bytes);
                    tp_print_json_string_view("uri", &pool->region_uri);
                    printf("}");
                }
                printf("]}\n");
            }
            else
            {
                size_t i;
                printf("ShmPoolAnnounce stream=%u producer=%u epoch=%" PRIu64 " layout=%u header_nslots=%u header_slot_bytes=%u\n",
                    view.stream_id,
                    view.producer_id,
                    view.epoch,
                    view.layout_version,
                    view.header_nslots,
                    view.header_slot_bytes);
                printf("  announce_ts=%" PRIu64 " clock_domain=%u ", view.announce_timestamp_ns, view.announce_clock_domain);
                tp_print_string_view("header_region_uri", &view.header_region_uri);
                printf("\n");
                for (i = 0; i < view.pool_count; i++)
                {
                    const tp_shm_pool_desc_t *pool = &view.pools[i];
                    printf("  pool_id=%u nslots=%u stride=%u ", pool->pool_id, pool->nslots, pool->stride_bytes);
                    tp_print_string_view("uri", &pool->region_uri);
                    printf("\n");
                }
            }
            tp_control_shm_pool_announce_close(&view);
        }
        return;
    }

    if (template_id == tensor_pool_controlResponse_sbe_template_id())
    {
        tp_control_response_view_t view;
        if (tp_control_decode_control_response(buffer, length, &view) == 0)
        {
            if (state && state->json)
            {
                printf("{\"type\":\"ControlResponse\",\"correlation_id\":%" PRIi64 ",\"code\":%d,",
                    view.correlation_id,
                    view.code);
                tp_print_json_string_view("error_message", &view.error_message);
                printf("}\n");
            }
            else
            {
                printf("ControlResponse correlation_id=%" PRIi64 " code=%d ", view.correlation_id, view.code);
                tp_print_string_view("error_message", &view.error_message);
                printf("\n");
            }
        }
        return;
    }

    if (template_id == tensor_pool_consumerHello_sbe_template_id())
    {
        tp_consumer_hello_view_t view;
        if (tp_control_decode_consumer_hello(buffer, length, &view) == 0)
        {
            if (state && state->json)
            {
                printf("{\"type\":\"ConsumerHello\",\"stream\":%u,\"consumer\":%u,", view.stream_id, view.consumer_id);
                printf("\"supports_shm\":%u,\"supports_progress\":%u,\"mode\":%u,\"max_rate\":%u,",
                    view.supports_shm, view.supports_progress, view.mode, view.max_rate_hz);
                printf("\"expected_layout\":%u,\"progress_interval\":%u,\"progress_bytes\":%u,\"progress_major\":%u,",
                    view.expected_layout_version, view.progress_interval_us, view.progress_bytes_delta, view.progress_major_delta_units);
                printf("\"descriptor_stream\":%u,\"control_stream\":%u,", view.descriptor_stream_id, view.control_stream_id);
                tp_print_json_string_view("descriptor_channel", &view.descriptor_channel);
                printf(",");
                tp_print_json_string_view("control_channel", &view.control_channel);
                printf("}\n");
            }
            else
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
        }
        return;
    }

    if (template_id == tensor_pool_consumerConfig_sbe_template_id())
    {
        tp_consumer_config_view_t view;
        if (tp_control_decode_consumer_config(buffer, length, &view) == 0)
        {
            if (state && state->json)
            {
                printf("{\"type\":\"ConsumerConfig\",\"stream\":%u,\"consumer\":%u,", view.stream_id, view.consumer_id);
                printf("\"use_shm\":%u,\"mode\":%u,\"descriptor_stream\":%u,\"control_stream\":%u,",
                    view.use_shm, view.mode, view.descriptor_stream_id, view.control_stream_id);
                tp_print_json_string_view("payload_fallback", &view.payload_fallback_uri);
                printf(",");
                tp_print_json_string_view("descriptor_channel", &view.descriptor_channel);
                printf(",");
                tp_print_json_string_view("control_channel", &view.control_channel);
                printf("}\n");
            }
            else
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
        }
        return;
    }

    if (template_id == tensor_pool_dataSourceAnnounce_sbe_template_id())
    {
        tp_data_source_announce_view_t view;
        if (tp_control_decode_data_source_announce(buffer, length, &view) == 0)
        {
            if (state && state->json)
            {
                printf("{\"type\":\"DataSourceAnnounce\",\"stream\":%u,\"producer\":%u,\"epoch\":%" PRIu64 ",\"meta_version\":%u,",
                    view.stream_id, view.producer_id, view.epoch, view.meta_version);
                tp_print_json_string_view("name", &view.name);
                printf(",");
                tp_print_json_string_view("summary", &view.summary);
                printf("}\n");
            }
            else
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
        }
        return;
    }

    if (template_id == tensor_pool_dataSourceMeta_sbe_template_id())
    {
        tp_data_source_meta_view_t view;
        tp_meta_ctx_t meta_ctx;

        memset(&meta_ctx, 0, sizeof(meta_ctx));
        meta_ctx.clientd = state;
        if (tp_control_decode_data_source_meta(buffer, length, &view, tp_on_meta_attr, &meta_ctx) == 0)
        {
            if (state && state->json)
            {
                printf("{\"type\":\"DataSourceMeta\",\"stream\":%u,\"meta_version\":%u,\"timestamp_ns\":%" PRIu64 ",\"attrs\":%u}\n",
                    view.stream_id,
                    view.meta_version,
                    view.timestamp_ns,
                    view.attribute_count);
            }
            else
            {
                printf("DataSourceMeta stream=%u meta_version=%u timestamp_ns=%" PRIu64 " attrs=%u\n",
                    view.stream_id,
                    view.meta_version,
                    view.timestamp_ns,
                    view.attribute_count);
            }
        }
        return;
    }

    if (template_id == tensor_pool_frameProgress_sbe_template_id())
    {
        struct tensor_pool_frameProgress progress;
        enum tensor_pool_frameProgressState progress_state;
        uint8_t state_value = 0;

        tensor_pool_frameProgress_wrap_for_decode(
            &progress,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            block_length,
            version,
            length);

        if (tensor_pool_frameProgress_state(&progress, &progress_state))
        {
            state_value = (uint8_t)progress_state;
        }

        if (state && state->json)
        {
            printf("{\"type\":\"FrameProgress\",\"stream\":%u,\"seq\":%" PRIu64 ",\"bytes\":%" PRIu64 ",\"state\":%u}\n",
                tensor_pool_frameProgress_streamId(&progress),
                tensor_pool_frameProgress_seq(&progress),
                tensor_pool_frameProgress_payloadBytesFilled(&progress),
                state_value);
        }
        else
        {
            printf("FrameProgress stream=%u seq=%" PRIu64 " bytes=%" PRIu64 " state=%u\n",
                tensor_pool_frameProgress_streamId(&progress),
                tensor_pool_frameProgress_seq(&progress),
                tensor_pool_frameProgress_payloadBytesFilled(&progress),
                state_value);
        }
        return;
    }
}

static void tp_on_control_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_listen_state_t *state = (tp_listen_state_t *)clientd;

    tp_handle_control_fragment(state, "control", buffer, length, header);
}

static void tp_on_metadata_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_listen_state_t *state = (tp_listen_state_t *)clientd;

    tp_handle_control_fragment(state, "metadata", buffer, length, header);
}

static void tp_on_qos_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_listen_state_t *state = (tp_listen_state_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;

    (void)header;

    if (NULL == buffer)
    {
        return;
    }

    tp_dump_raw_fragment(state, "qos", buffer, length);

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

    if (schema_id != tensor_pool_messageHeader_sbe_schema_id())
    {
        return;
    }

    if (template_id == tensor_pool_qosProducer_sbe_template_id())
    {
        struct tensor_pool_qosProducer qos;
        tensor_pool_qosProducer_wrap_for_decode(
            &qos,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            block_length,
            version,
            length);
        if (state && state->json)
        {
            printf("{\"type\":\"QosProducer\",\"stream\":%u,\"producer\":%u,\"epoch\":%" PRIu64 ",\"current_seq\":%" PRIu64 "}\n",
                tensor_pool_qosProducer_streamId(&qos),
                tensor_pool_qosProducer_producerId(&qos),
                tensor_pool_qosProducer_epoch(&qos),
                tensor_pool_qosProducer_currentSeq(&qos));
        }
        else
        {
            printf("QosProducer stream=%u producer=%u epoch=%" PRIu64 " current_seq=%" PRIu64 "\n",
                tensor_pool_qosProducer_streamId(&qos),
                tensor_pool_qosProducer_producerId(&qos),
                tensor_pool_qosProducer_epoch(&qos),
                tensor_pool_qosProducer_currentSeq(&qos));
        }
        return;
    }

    if (template_id == tensor_pool_qosConsumer_sbe_template_id())
    {
        struct tensor_pool_qosConsumer qos;
        enum tensor_pool_mode mode;
        uint8_t mode_value = 0;
        tensor_pool_qosConsumer_wrap_for_decode(
            &qos,
            (char *)buffer,
            tensor_pool_messageHeader_encoded_length(),
            block_length,
            version,
            length);
        if (tensor_pool_qosConsumer_mode(&qos, &mode))
        {
            mode_value = (uint8_t)mode;
        }
        if (state && state->json)
        {
            printf("{\"type\":\"QosConsumer\",\"stream\":%u,\"consumer\":%u,\"epoch\":%" PRIu64 ",\"last_seq\":%" PRIu64 ",\"drops_gap\":%" PRIu64 ",\"drops_late\":%" PRIu64 ",\"mode\":%u}\n",
                tensor_pool_qosConsumer_streamId(&qos),
                tensor_pool_qosConsumer_consumerId(&qos),
                tensor_pool_qosConsumer_epoch(&qos),
                tensor_pool_qosConsumer_lastSeqSeen(&qos),
                tensor_pool_qosConsumer_dropsGap(&qos),
                tensor_pool_qosConsumer_dropsLate(&qos),
                mode_value);
        }
        else
        {
            printf("QosConsumer stream=%u consumer=%u epoch=%" PRIu64 " last_seq=%" PRIu64 " drops_gap=%" PRIu64 " drops_late=%" PRIu64 " mode=%u\n",
                tensor_pool_qosConsumer_streamId(&qos),
                tensor_pool_qosConsumer_consumerId(&qos),
                tensor_pool_qosConsumer_epoch(&qos),
                tensor_pool_qosConsumer_lastSeqSeen(&qos),
                tensor_pool_qosConsumer_dropsGap(&qos),
                tensor_pool_qosConsumer_dropsLate(&qos),
                mode_value);
        }
        return;
    }
}

int main(int argc, char **argv)
{
    const char *aeron_dir = NULL;
    const char *control_channel = "aeron:ipc";
    const char *metadata_channel = NULL;
    const char *qos_channel = NULL;
    int32_t control_stream_id = 1000;
    int32_t metadata_stream_id = 1300;
    int32_t qos_stream_id = 1200;
    tp_listen_state_t state;
    tp_context_t *context = NULL;
    tp_client_t *client = NULL;
    tp_subscription_t *control_subscription = NULL;
    tp_subscription_t *metadata_subscription = NULL;
    tp_subscription_t *qos_subscription = NULL;
    tp_fragment_assembler_t *control_assembler = NULL;
    tp_fragment_assembler_t *metadata_assembler = NULL;
    tp_fragment_assembler_t *qos_assembler = NULL;
    int opt;
    int metadata_channel_set = 0;
    int qos_channel_set = 0;

    memset(&state, 0, sizeof(state));

    while ((opt = getopt(argc, argv, "a:c:C:m:M:q:Q:jro:h")) != -1)
    {
        switch (opt)
        {
            case 'a':
                aeron_dir = optarg;
                break;
            case 'c':
                control_channel = optarg;
                break;
            case 'C':
                control_stream_id = (int32_t)strtol(optarg, NULL, 10);
                break;
            case 'm':
                metadata_channel = optarg;
                metadata_channel_set = 1;
                break;
            case 'M':
                metadata_stream_id = (int32_t)strtol(optarg, NULL, 10);
                break;
            case 'q':
                qos_channel = optarg;
                qos_channel_set = 1;
                break;
            case 'Q':
                qos_stream_id = (int32_t)strtol(optarg, NULL, 10);
                break;
            case 'j':
                state.json = 1;
                break;
            case 'r':
                state.raw = 1;
                break;
            case 'o':
                state.raw_out = fopen(optarg, "w");
                if (NULL == state.raw_out)
                {
                    fprintf(stderr, "Failed to open raw output: %s\n", optarg);
                    return 1;
                }
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (optind < argc)
    {
        aeron_dir = argv[optind++];
    }
    if (optind < argc)
    {
        control_channel = argv[optind++];
    }
    if (optind < argc)
    {
        control_stream_id = (int32_t)strtol(argv[optind++], NULL, 10);
    }
    if (optind < argc)
    {
        usage(argv[0]);
        return 1;
    }

    if (!metadata_channel_set)
    {
        metadata_channel = control_channel;
    }
    if (!qos_channel_set)
    {
        qos_channel = control_channel;
    }

    if (tp_context_init(&context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    if (aeron_dir)
    {
        tp_context_set_aeron_dir(context, aeron_dir);
    }

    tp_context_set_control_channel(context, control_channel, control_stream_id);
    tp_context_set_metadata_channel(context, metadata_channel, metadata_stream_id);
    tp_context_set_qos_channel(context, qos_channel, qos_stream_id);

    if (tp_client_init(&client, context) < 0 || tp_client_start(client) < 0)
    {
        fprintf(stderr, "Aeron init failed: %s\n", tp_errmsg());
        return 1;
    }

    control_subscription = tp_client_control_subscription(client);
    metadata_subscription = tp_client_metadata_subscription(client);
    qos_subscription = tp_client_qos_subscription(client);

    if (NULL == control_subscription || NULL == metadata_subscription || NULL == qos_subscription)
    {
        fprintf(stderr, "Subscription setup failed: %s\n", tp_errmsg());
        tp_client_close(client);
        return 1;
    }

    if (tp_fragment_assembler_create(&control_assembler, tp_on_control_fragment, &state) < 0)
    {
        fprintf(stderr, "Fragment assembler failed: %s\n", tp_errmsg());
        tp_client_close(client);
        return 1;
    }

    if (tp_fragment_assembler_create(&metadata_assembler, tp_on_metadata_fragment, &state) < 0)
    {
        fprintf(stderr, "Metadata assembler failed: %s\n", tp_errmsg());
        tp_fragment_assembler_close(&control_assembler);
        tp_client_close(client);
        return 1;
    }

    if (tp_fragment_assembler_create(&qos_assembler, tp_on_qos_fragment, &state) < 0)
    {
        fprintf(stderr, "QoS assembler failed: %s\n", tp_errmsg());
        tp_fragment_assembler_close(&metadata_assembler);
        tp_fragment_assembler_close(&control_assembler);
        tp_client_close(client);
        return 1;
    }

    signal(SIGINT, tp_handle_sigint);

    printf("Listening on control=%s:%d metadata=%s:%d qos=%s:%d (Ctrl+C to stop)\n",
        control_channel, control_stream_id,
        metadata_channel, metadata_stream_id,
        qos_channel, qos_stream_id);

    while (tp_running)
    {
        int fragments;

        fragments = aeron_subscription_poll(
            tp_subscription_handle(control_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(control_assembler),
            10);
        if (fragments < 0)
        {
            fprintf(stderr, "Control poll failed: %d\n", fragments);
            break;
        }

        fragments = aeron_subscription_poll(
            tp_subscription_handle(metadata_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(metadata_assembler),
            10);
        if (fragments < 0)
        {
            fprintf(stderr, "Metadata poll failed: %d\n", fragments);
            break;
        }

        fragments = aeron_subscription_poll(
            tp_subscription_handle(qos_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(qos_assembler),
            10);
        if (fragments < 0)
        {
            fprintf(stderr, "QoS poll failed: %d\n", fragments);
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

    tp_fragment_assembler_close(&qos_assembler);
    tp_fragment_assembler_close(&metadata_assembler);
    tp_fragment_assembler_close(&control_assembler);
    if (state.raw_out)
    {
        fclose(state.raw_out);
    }
    tp_client_close(client);

    return 0;
}
