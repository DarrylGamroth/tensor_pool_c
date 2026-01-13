#include "tensor_pool/tp_merge_map.h"

#include <errno.h>
#include <string.h>

#include "aeron_alloc.h"

#include "tensor_pool/tp_error.h"

#include "merge/tensor_pool/messageHeader.h"
#include "merge/tensor_pool/sequenceMergeMapAnnounce.h"
#include "merge/tensor_pool/sequenceMergeMapRequest.h"
#include "merge/tensor_pool/timestampMergeMapAnnounce.h"
#include "merge/tensor_pool/timestampMergeMapRequest.h"
#include "merge/tensor_pool/clockDomain.h"
#include "merge/tensor_pool/mergeRuleType.h"
#include "merge/tensor_pool/mergeTimeRuleType.h"
#include "merge/tensor_pool/timestampSource.h"

static int tp_sequence_rule_valid(const tp_sequence_merge_rule_t *rule)
{
    if (NULL == rule)
    {
        return -1;
    }

    if (rule->rule_type == TP_MERGE_RULE_OFFSET)
    {
        return 0;
    }
    if (rule->rule_type == TP_MERGE_RULE_WINDOW)
    {
        if (rule->window_size == 0)
        {
            return -1;
        }
        return 0;
    }

    return -1;
}

static int tp_timestamp_rule_valid(const tp_timestamp_merge_rule_t *rule)
{
    if (NULL == rule)
    {
        return -1;
    }

    if (rule->timestamp_source != TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR &&
        rule->timestamp_source != TP_TIMESTAMP_SOURCE_SLOT_HEADER)
    {
        return -1;
    }

    if (rule->rule_type == TP_MERGE_TIME_OFFSET_NS)
    {
        return 0;
    }
    if (rule->rule_type == TP_MERGE_TIME_WINDOW_NS)
    {
        if (rule->window_ns == 0)
        {
            return -1;
        }
        return 0;
    }

    return -1;
}

int tp_sequence_merge_map_encode(
    uint8_t *buffer,
    size_t length,
    const tp_sequence_merge_map_t *map,
    size_t *out_len)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_sequenceMergeMapAnnounce announce;
    struct tensor_pool_sequenceMergeMapAnnounce_rules rules;
    size_t i;

    if (NULL == buffer || NULL == map || NULL == out_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_encode: null input");
        return -1;
    }

    if (map->rule_count > UINT16_MAX)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_encode: rule count too large");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version(),
        length);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_sequenceMergeMapAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_sequenceMergeMapAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_sequenceMergeMapAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version());

    tensor_pool_sequenceMergeMapAnnounce_wrap_for_encode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        length);
    tensor_pool_sequenceMergeMapAnnounce_set_outStreamId(&announce, map->out_stream_id);
    tensor_pool_sequenceMergeMapAnnounce_set_epoch(&announce, map->epoch);
    if (map->stale_timeout_ns == TP_NULL_U64)
    {
        tensor_pool_sequenceMergeMapAnnounce_set_staleTimeoutNs(
            &announce,
            tensor_pool_sequenceMergeMapAnnounce_staleTimeoutNs_null_value());
    }
    else
    {
        tensor_pool_sequenceMergeMapAnnounce_set_staleTimeoutNs(&announce, map->stale_timeout_ns);
    }

    tensor_pool_sequenceMergeMapAnnounce_rules_wrap_for_encode(
        &rules,
        (char *)buffer,
        (uint16_t)map->rule_count,
        tensor_pool_sequenceMergeMapAnnounce_sbe_position_ptr(&announce),
        tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version(),
        length);

    for (i = 0; i < map->rule_count; i++)
    {
        const tp_sequence_merge_rule_t *rule = &map->rules[i];
        if (tp_sequence_rule_valid(rule) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_encode: invalid rule");
            return -1;
        }

        tensor_pool_sequenceMergeMapAnnounce_rules_next(&rules);
        tensor_pool_sequenceMergeMapAnnounce_rules_set_inputStreamId(&rules, rule->input_stream_id);
        tensor_pool_sequenceMergeMapAnnounce_rules_set_ruleType(&rules, (enum tensor_pool_mergeRuleType)rule->rule_type);

        if (rule->rule_type == TP_MERGE_RULE_OFFSET)
        {
            tensor_pool_sequenceMergeMapAnnounce_rules_set_offset(&rules, rule->offset);
            tensor_pool_sequenceMergeMapAnnounce_rules_set_windowSize(
                &rules,
                tensor_pool_sequenceMergeMapAnnounce_rules_windowSize_null_value());
        }
        else
        {
            tensor_pool_sequenceMergeMapAnnounce_rules_set_windowSize(&rules, rule->window_size);
            tensor_pool_sequenceMergeMapAnnounce_rules_set_offset(
                &rules,
                tensor_pool_sequenceMergeMapAnnounce_rules_offset_null_value());
        }
    }

    *out_len = (size_t)tensor_pool_sequenceMergeMapAnnounce_sbe_position(&announce);
    return 0;
}

int tp_sequence_merge_map_decode(
    const uint8_t *buffer,
    size_t length,
    tp_sequence_merge_map_t *out,
    tp_sequence_merge_rule_t *rules,
    size_t max_rules)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_sequenceMergeMapAnnounce announce;
    struct tensor_pool_sequenceMergeMapAnnounce_rules rules_group;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version;
    uint16_t block_length;
    size_t rule_count;
    size_t i;

    if (NULL == buffer || NULL == out || NULL == rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    version = tensor_pool_messageHeader_version(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);

    if (schema_id != tensor_pool_sequenceMergeMapAnnounce_sbe_schema_id() ||
        template_id != tensor_pool_sequenceMergeMapAnnounce_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_sequenceMergeMapAnnounce_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_sequenceMergeMapAnnounce_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: block length mismatch");
        return -1;
    }

    tensor_pool_sequenceMergeMapAnnounce_wrap_for_decode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->out_stream_id = tensor_pool_sequenceMergeMapAnnounce_outStreamId(&announce);
    out->epoch = tensor_pool_sequenceMergeMapAnnounce_epoch(&announce);
    out->stale_timeout_ns = tensor_pool_sequenceMergeMapAnnounce_staleTimeoutNs(&announce);
    if (out->stale_timeout_ns == tensor_pool_sequenceMergeMapAnnounce_staleTimeoutNs_null_value())
    {
        out->stale_timeout_ns = TP_NULL_U64;
    }

    tensor_pool_sequenceMergeMapAnnounce_rules_wrap_for_decode(
        &rules_group,
        (char *)buffer,
        tensor_pool_sequenceMergeMapAnnounce_sbe_position_ptr(&announce),
        version,
        length);

    rule_count = (size_t)tensor_pool_sequenceMergeMapAnnounce_rules_count(&rules_group);
    if (rule_count > max_rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: rule capacity exceeded");
        return -1;
    }

    out->rules = rules;
    out->rule_count = rule_count;

    for (i = 0; i < rule_count; i++)
    {
        enum tensor_pool_mergeRuleType rule_type;
        tp_sequence_merge_rule_t *rule = &rules[i];
        int32_t offset;
        uint32_t window;

        tensor_pool_sequenceMergeMapAnnounce_rules_next(&rules_group);

        if (!tensor_pool_sequenceMergeMapAnnounce_rules_ruleType(&rules_group, &rule_type))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: invalid rule type");
            return -1;
        }

        rule->input_stream_id = tensor_pool_sequenceMergeMapAnnounce_rules_inputStreamId(&rules_group);
        rule->rule_type = (tp_merge_rule_type_t)rule_type;
        offset = tensor_pool_sequenceMergeMapAnnounce_rules_offset(&rules_group);
        window = tensor_pool_sequenceMergeMapAnnounce_rules_windowSize(&rules_group);

        if (rule->rule_type == TP_MERGE_RULE_OFFSET)
        {
            if (offset == tensor_pool_sequenceMergeMapAnnounce_rules_offset_null_value() ||
                window != tensor_pool_sequenceMergeMapAnnounce_rules_windowSize_null_value())
            {
                TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: invalid offset rule");
                return -1;
            }
            rule->offset = offset;
            rule->window_size = 0;
        }
        else if (rule->rule_type == TP_MERGE_RULE_WINDOW)
        {
            if (window == tensor_pool_sequenceMergeMapAnnounce_rules_windowSize_null_value() ||
                offset != tensor_pool_sequenceMergeMapAnnounce_rules_offset_null_value())
            {
                TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: invalid window rule");
                return -1;
            }
            rule->window_size = window;
            rule->offset = 0;
            if (rule->window_size == 0)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: window size invalid");
                return -1;
            }
        }
        else
        {
            TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_decode: unknown rule type");
            return -1;
        }
    }

    return 0;
}

int tp_sequence_merge_map_request_encode(
    uint8_t *buffer,
    size_t length,
    uint32_t out_stream_id,
    uint64_t epoch,
    size_t *out_len)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_sequenceMergeMapRequest request;

    if (NULL == buffer || NULL == out_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_request_encode: null input");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_sequenceMergeMapRequest_sbe_schema_version(),
        length);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_sequenceMergeMapRequest_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_sequenceMergeMapRequest_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_sequenceMergeMapRequest_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_sequenceMergeMapRequest_sbe_schema_version());

    tensor_pool_sequenceMergeMapRequest_wrap_for_encode(
        &request,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        length);
    tensor_pool_sequenceMergeMapRequest_set_outStreamId(&request, out_stream_id);
    tensor_pool_sequenceMergeMapRequest_set_epoch(&request, epoch);

    *out_len = (size_t)tensor_pool_sequenceMergeMapRequest_sbe_block_length() +
        tensor_pool_messageHeader_encoded_length();
    return 0;
}

int tp_sequence_merge_map_request_decode(
    const uint8_t *buffer,
    size_t length,
    uint32_t *out_stream_id,
    uint64_t *epoch)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_sequenceMergeMapRequest request;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version;
    uint16_t block_length;

    if (NULL == buffer || NULL == out_stream_id || NULL == epoch)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_request_decode: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_request_decode: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_sequenceMergeMapRequest_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    version = tensor_pool_messageHeader_version(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);

    if (schema_id != tensor_pool_sequenceMergeMapRequest_sbe_schema_id() ||
        template_id != tensor_pool_sequenceMergeMapRequest_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_sequenceMergeMapRequest_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_request_decode: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_sequenceMergeMapRequest_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_sequence_merge_map_request_decode: block length mismatch");
        return -1;
    }

    tensor_pool_sequenceMergeMapRequest_wrap_for_decode(
        &request,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    *out_stream_id = tensor_pool_sequenceMergeMapRequest_outStreamId(&request);
    *epoch = tensor_pool_sequenceMergeMapRequest_epoch(&request);
    return 0;
}

int tp_timestamp_merge_map_encode(
    uint8_t *buffer,
    size_t length,
    const tp_timestamp_merge_map_t *map,
    size_t *out_len)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_timestampMergeMapAnnounce announce;
    struct tensor_pool_timestampMergeMapAnnounce_rules rules;
    size_t i;

    if (NULL == buffer || NULL == map || NULL == out_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_encode: null input");
        return -1;
    }

    if (map->rule_count > UINT16_MAX)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_encode: rule count too large");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_timestampMergeMapAnnounce_sbe_schema_version(),
        length);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_timestampMergeMapAnnounce_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_timestampMergeMapAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_timestampMergeMapAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_timestampMergeMapAnnounce_sbe_schema_version());

    tensor_pool_timestampMergeMapAnnounce_wrap_for_encode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        length);
    tensor_pool_timestampMergeMapAnnounce_set_outStreamId(&announce, map->out_stream_id);
    tensor_pool_timestampMergeMapAnnounce_set_epoch(&announce, map->epoch);
    tensor_pool_timestampMergeMapAnnounce_set_clockDomain(&announce, map->clock_domain);

    if (map->stale_timeout_ns == TP_NULL_U64)
    {
        tensor_pool_timestampMergeMapAnnounce_set_staleTimeoutNs(
            &announce,
            tensor_pool_timestampMergeMapAnnounce_staleTimeoutNs_null_value());
    }
    else
    {
        tensor_pool_timestampMergeMapAnnounce_set_staleTimeoutNs(&announce, map->stale_timeout_ns);
    }

    if (map->lateness_ns == TP_NULL_U64)
    {
        tensor_pool_timestampMergeMapAnnounce_set_latenessNs(
            &announce,
            tensor_pool_timestampMergeMapAnnounce_latenessNs_null_value());
    }
    else
    {
        tensor_pool_timestampMergeMapAnnounce_set_latenessNs(&announce, map->lateness_ns);
    }

    tensor_pool_timestampMergeMapAnnounce_rules_wrap_for_encode(
        &rules,
        (char *)buffer,
        (uint16_t)map->rule_count,
        tensor_pool_timestampMergeMapAnnounce_sbe_position_ptr(&announce),
        tensor_pool_timestampMergeMapAnnounce_sbe_schema_version(),
        length);

    for (i = 0; i < map->rule_count; i++)
    {
        const tp_timestamp_merge_rule_t *rule = &map->rules[i];
        if (tp_timestamp_rule_valid(rule) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_encode: invalid rule");
            return -1;
        }

        tensor_pool_timestampMergeMapAnnounce_rules_next(&rules);
        tensor_pool_timestampMergeMapAnnounce_rules_set_inputStreamId(&rules, rule->input_stream_id);
        tensor_pool_timestampMergeMapAnnounce_rules_set_ruleType(&rules, (enum tensor_pool_mergeTimeRuleType)rule->rule_type);
        tensor_pool_timestampMergeMapAnnounce_rules_set_timestampSource(
            &rules,
            (enum tensor_pool_timestampSource)rule->timestamp_source);

        if (rule->rule_type == TP_MERGE_TIME_OFFSET_NS)
        {
            tensor_pool_timestampMergeMapAnnounce_rules_set_offsetNs(&rules, rule->offset_ns);
            tensor_pool_timestampMergeMapAnnounce_rules_set_windowNs(
                &rules,
                tensor_pool_timestampMergeMapAnnounce_rules_windowNs_null_value());
        }
        else
        {
            tensor_pool_timestampMergeMapAnnounce_rules_set_windowNs(&rules, rule->window_ns);
            tensor_pool_timestampMergeMapAnnounce_rules_set_offsetNs(
                &rules,
                tensor_pool_timestampMergeMapAnnounce_rules_offsetNs_null_value());
        }
    }

    *out_len = (size_t)tensor_pool_timestampMergeMapAnnounce_sbe_position(&announce);
    return 0;
}

int tp_timestamp_merge_map_decode(
    const uint8_t *buffer,
    size_t length,
    tp_timestamp_merge_map_t *out,
    tp_timestamp_merge_rule_t *rules,
    size_t max_rules)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_timestampMergeMapAnnounce announce;
    struct tensor_pool_timestampMergeMapAnnounce_rules rules_group;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version;
    uint16_t block_length;
    size_t rule_count;
    size_t i;
    enum tensor_pool_clockDomain clock_domain = tensor_pool_clockDomain_NULL_VALUE;

    if (NULL == buffer || NULL == out || NULL == rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_timestampMergeMapAnnounce_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    version = tensor_pool_messageHeader_version(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);

    if (schema_id != tensor_pool_timestampMergeMapAnnounce_sbe_schema_id() ||
        template_id != tensor_pool_timestampMergeMapAnnounce_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_timestampMergeMapAnnounce_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_timestampMergeMapAnnounce_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: block length mismatch");
        return -1;
    }

    tensor_pool_timestampMergeMapAnnounce_wrap_for_decode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->out_stream_id = tensor_pool_timestampMergeMapAnnounce_outStreamId(&announce);
    out->epoch = tensor_pool_timestampMergeMapAnnounce_epoch(&announce);
    out->stale_timeout_ns = tensor_pool_timestampMergeMapAnnounce_staleTimeoutNs(&announce);
    if (out->stale_timeout_ns == tensor_pool_timestampMergeMapAnnounce_staleTimeoutNs_null_value())
    {
        out->stale_timeout_ns = TP_NULL_U64;
    }

    out->lateness_ns = tensor_pool_timestampMergeMapAnnounce_latenessNs(&announce);
    if (out->lateness_ns == tensor_pool_timestampMergeMapAnnounce_latenessNs_null_value())
    {
        out->lateness_ns = TP_NULL_U64;
    }

    if (!tensor_pool_timestampMergeMapAnnounce_clockDomain(&announce, &clock_domain))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: invalid clock domain");
        return -1;
    }
    out->clock_domain = (uint8_t)clock_domain;

    tensor_pool_timestampMergeMapAnnounce_rules_wrap_for_decode(
        &rules_group,
        (char *)buffer,
        tensor_pool_timestampMergeMapAnnounce_sbe_position_ptr(&announce),
        version,
        length);

    rule_count = (size_t)tensor_pool_timestampMergeMapAnnounce_rules_count(&rules_group);
    if (rule_count > max_rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: rule capacity exceeded");
        return -1;
    }

    out->rules = rules;
    out->rule_count = rule_count;

    for (i = 0; i < rule_count; i++)
    {
        enum tensor_pool_mergeTimeRuleType rule_type;
        enum tensor_pool_timestampSource source;
        tp_timestamp_merge_rule_t *rule = &rules[i];
        int64_t offset;
        uint64_t window;

        tensor_pool_timestampMergeMapAnnounce_rules_next(&rules_group);

        if (!tensor_pool_timestampMergeMapAnnounce_rules_ruleType(&rules_group, &rule_type))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: invalid rule type");
            return -1;
        }

        if (!tensor_pool_timestampMergeMapAnnounce_rules_timestampSource(&rules_group, &source))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: invalid timestamp source");
            return -1;
        }

        rule->input_stream_id = tensor_pool_timestampMergeMapAnnounce_rules_inputStreamId(&rules_group);
        rule->rule_type = (tp_merge_time_rule_type_t)rule_type;
        rule->timestamp_source = (tp_timestamp_source_t)source;
        offset = tensor_pool_timestampMergeMapAnnounce_rules_offsetNs(&rules_group);
        window = tensor_pool_timestampMergeMapAnnounce_rules_windowNs(&rules_group);

        if (rule->rule_type == TP_MERGE_TIME_OFFSET_NS)
        {
            if (offset == tensor_pool_timestampMergeMapAnnounce_rules_offsetNs_null_value() ||
                window != tensor_pool_timestampMergeMapAnnounce_rules_windowNs_null_value())
            {
                TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: invalid offset rule");
                return -1;
            }
            rule->offset_ns = offset;
            rule->window_ns = 0;
        }
        else if (rule->rule_type == TP_MERGE_TIME_WINDOW_NS)
        {
            if (window == tensor_pool_timestampMergeMapAnnounce_rules_windowNs_null_value() ||
                offset != tensor_pool_timestampMergeMapAnnounce_rules_offsetNs_null_value())
            {
                TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: invalid window rule");
                return -1;
            }
            rule->window_ns = window;
            rule->offset_ns = 0;
            if (rule->window_ns == 0)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: window size invalid");
                return -1;
            }
        }
        else
        {
            TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_decode: unknown rule type");
            return -1;
        }
    }

    return 0;
}

int tp_timestamp_merge_map_request_encode(
    uint8_t *buffer,
    size_t length,
    uint32_t out_stream_id,
    uint64_t epoch,
    size_t *out_len)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_timestampMergeMapRequest request;

    if (NULL == buffer || NULL == out_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_request_encode: null input");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_timestampMergeMapRequest_sbe_schema_version(),
        length);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_timestampMergeMapRequest_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_timestampMergeMapRequest_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_timestampMergeMapRequest_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_timestampMergeMapRequest_sbe_schema_version());

    tensor_pool_timestampMergeMapRequest_wrap_for_encode(
        &request,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        length);
    tensor_pool_timestampMergeMapRequest_set_outStreamId(&request, out_stream_id);
    tensor_pool_timestampMergeMapRequest_set_epoch(&request, epoch);

    *out_len = (size_t)tensor_pool_timestampMergeMapRequest_sbe_block_length() +
        tensor_pool_messageHeader_encoded_length();
    return 0;
}

int tp_timestamp_merge_map_request_decode(
    const uint8_t *buffer,
    size_t length,
    uint32_t *out_stream_id,
    uint64_t *epoch)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_timestampMergeMapRequest request;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version;
    uint16_t block_length;

    if (NULL == buffer || NULL == out_stream_id || NULL == epoch)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_request_decode: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_request_decode: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_timestampMergeMapRequest_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    version = tensor_pool_messageHeader_version(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);

    if (schema_id != tensor_pool_timestampMergeMapRequest_sbe_schema_id() ||
        template_id != tensor_pool_timestampMergeMapRequest_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_timestampMergeMapRequest_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_request_decode: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_timestampMergeMapRequest_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_timestamp_merge_map_request_decode: block length mismatch");
        return -1;
    }

    tensor_pool_timestampMergeMapRequest_wrap_for_decode(
        &request,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    *out_stream_id = tensor_pool_timestampMergeMapRequest_outStreamId(&request);
    *epoch = tensor_pool_timestampMergeMapRequest_epoch(&request);
    return 0;
}

int tp_merge_map_registry_init(tp_merge_map_registry_t *registry, size_t capacity, tp_log_t *log)
{
    if (NULL == registry || capacity == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_merge_map_registry_init: invalid input");
        return -1;
    }

    memset(registry, 0, sizeof(*registry));
    if (aeron_alloc((void **)&registry->entries, sizeof(tp_merge_map_entry_t) * capacity) < 0)
    {
        return -1;
    }

    registry->capacity = capacity;
    registry->log = log;
    return 0;
}

void tp_merge_map_registry_set_log(tp_merge_map_registry_t *registry, tp_log_t *log)
{
    if (NULL == registry)
    {
        return;
    }

    registry->log = log;
}

static void tp_merge_map_entry_clear(tp_merge_map_entry_t *entry)
{
    if (NULL == entry)
    {
        return;
    }

    if (entry->sequence_rules)
    {
        aeron_free(entry->sequence_rules);
        entry->sequence_rules = NULL;
    }
    if (entry->timestamp_rules)
    {
        aeron_free(entry->timestamp_rules);
        entry->timestamp_rules = NULL;
    }

    memset(entry, 0, sizeof(*entry));
}

static void tp_merge_map_registry_log(tp_merge_map_registry_t *registry, tp_log_level_t level, const char *message)
{
    if (NULL == registry || NULL == registry->log)
    {
        return;
    }

    tp_log_emit(registry->log, level, "%s", message);
}

void tp_merge_map_registry_close(tp_merge_map_registry_t *registry)
{
    size_t i;

    if (NULL == registry || NULL == registry->entries)
    {
        return;
    }

    for (i = 0; i < registry->capacity; i++)
    {
        tp_merge_map_entry_clear(&registry->entries[i]);
    }

    aeron_free(registry->entries);
    registry->entries = NULL;
    registry->capacity = 0;
}

static tp_merge_map_entry_t *tp_merge_map_registry_find_entry(
    tp_merge_map_registry_t *registry,
    tp_merge_map_kind_t kind,
    uint32_t out_stream_id,
    uint64_t epoch)
{
    size_t i;

    if (NULL == registry || NULL == registry->entries)
    {
        return NULL;
    }

    for (i = 0; i < registry->capacity; i++)
    {
        tp_merge_map_entry_t *entry = &registry->entries[i];
        if (entry->in_use && entry->kind == kind &&
            entry->out_stream_id == out_stream_id &&
            entry->epoch == epoch)
        {
            return entry;
        }
    }

    return NULL;
}

static tp_merge_map_entry_t *tp_merge_map_registry_find_slot(
    tp_merge_map_registry_t *registry,
    tp_merge_map_kind_t kind,
    uint32_t out_stream_id,
    uint64_t epoch)
{
    size_t i;
    tp_merge_map_entry_t *slot = NULL;

    for (i = 0; i < registry->capacity; i++)
    {
        tp_merge_map_entry_t *entry = &registry->entries[i];
        if (entry->in_use)
        {
            if (entry->kind == kind &&
                entry->out_stream_id == out_stream_id &&
                entry->epoch == epoch)
            {
                return entry;
            }
            continue;
        }
        if (NULL == slot)
        {
            slot = entry;
        }
    }

    return slot;
}

static void tp_merge_map_registry_invalidate_stream(
    tp_merge_map_registry_t *registry,
    tp_merge_map_kind_t kind,
    uint32_t out_stream_id,
    uint64_t epoch)
{
    size_t i;

    if (NULL == registry || NULL == registry->entries)
    {
        return;
    }

    for (i = 0; i < registry->capacity; i++)
    {
        tp_merge_map_entry_t *entry = &registry->entries[i];
        if (entry->in_use && entry->kind == kind &&
            entry->out_stream_id == out_stream_id &&
            entry->epoch != epoch)
        {
            tp_merge_map_entry_clear(entry);
        }
    }
}

int tp_merge_map_registry_upsert_sequence(
    tp_merge_map_registry_t *registry,
    const tp_sequence_merge_map_t *map,
    uint64_t now_ns)
{
    tp_merge_map_entry_t *entry;
    size_t i;

    if (NULL == registry || NULL == map || NULL == map->rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_merge_map_registry_upsert_sequence: null input");
        tp_merge_map_registry_log(registry, TP_LOG_WARN, "merge map registry: invalid sequence map");
        return -1;
    }

    tp_merge_map_registry_invalidate_stream(registry, TP_MERGE_MAP_SEQUENCE, map->out_stream_id, map->epoch);
    entry = tp_merge_map_registry_find_slot(registry, TP_MERGE_MAP_SEQUENCE, map->out_stream_id, map->epoch);
    if (NULL == entry)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_merge_map_registry_upsert_sequence: registry full");
        tp_merge_map_registry_log(registry, TP_LOG_WARN, "merge map registry: sequence map rejected (registry full)");
        return -1;
    }

    tp_merge_map_entry_clear(entry);
    entry->sequence_rules = NULL;
    if (map->rule_count > 0)
    {
        if (aeron_alloc((void **)&entry->sequence_rules, sizeof(tp_sequence_merge_rule_t) * map->rule_count) < 0)
        {
            return -1;
        }
    }

    for (i = 0; i < map->rule_count; i++)
    {
        entry->sequence_rules[i] = map->rules[i];
    }

    entry->in_use = true;
    entry->kind = TP_MERGE_MAP_SEQUENCE;
    entry->out_stream_id = map->out_stream_id;
    entry->epoch = map->epoch;
    entry->last_announce_ns = now_ns;
    entry->rule_count = map->rule_count;
    entry->sequence_map = *map;
    entry->sequence_map.rules = entry->sequence_rules;
    return 0;
}

int tp_merge_map_registry_upsert_timestamp(
    tp_merge_map_registry_t *registry,
    const tp_timestamp_merge_map_t *map,
    uint64_t now_ns)
{
    tp_merge_map_entry_t *entry;
    size_t i;

    if (NULL == registry || NULL == map || NULL == map->rules)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_merge_map_registry_upsert_timestamp: null input");
        tp_merge_map_registry_log(registry, TP_LOG_WARN, "merge map registry: invalid timestamp map");
        return -1;
    }

    tp_merge_map_registry_invalidate_stream(registry, TP_MERGE_MAP_TIMESTAMP, map->out_stream_id, map->epoch);
    entry = tp_merge_map_registry_find_slot(registry, TP_MERGE_MAP_TIMESTAMP, map->out_stream_id, map->epoch);
    if (NULL == entry)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_merge_map_registry_upsert_timestamp: registry full");
        tp_merge_map_registry_log(registry, TP_LOG_WARN, "merge map registry: timestamp map rejected (registry full)");
        return -1;
    }

    tp_merge_map_entry_clear(entry);
    entry->timestamp_rules = NULL;
    if (map->rule_count > 0)
    {
        if (aeron_alloc((void **)&entry->timestamp_rules, sizeof(tp_timestamp_merge_rule_t) * map->rule_count) < 0)
        {
            return -1;
        }
    }

    for (i = 0; i < map->rule_count; i++)
    {
        entry->timestamp_rules[i] = map->rules[i];
    }

    entry->in_use = true;
    entry->kind = TP_MERGE_MAP_TIMESTAMP;
    entry->out_stream_id = map->out_stream_id;
    entry->epoch = map->epoch;
    entry->last_announce_ns = now_ns;
    entry->rule_count = map->rule_count;
    entry->timestamp_map = *map;
    entry->timestamp_map.rules = entry->timestamp_rules;
    return 0;
}

const tp_sequence_merge_map_t *tp_merge_map_registry_find_sequence(
    const tp_merge_map_registry_t *registry,
    uint32_t out_stream_id,
    uint64_t epoch)
{
    tp_merge_map_entry_t *entry;

    if (NULL == registry)
    {
        return NULL;
    }

    entry = tp_merge_map_registry_find_entry(
        (tp_merge_map_registry_t *)registry,
        TP_MERGE_MAP_SEQUENCE,
        out_stream_id,
        epoch);
    if (NULL == entry)
    {
        return NULL;
    }

    return &entry->sequence_map;
}

const tp_timestamp_merge_map_t *tp_merge_map_registry_find_timestamp(
    const tp_merge_map_registry_t *registry,
    uint32_t out_stream_id,
    uint64_t epoch)
{
    tp_merge_map_entry_t *entry;

    if (NULL == registry)
    {
        return NULL;
    }

    entry = tp_merge_map_registry_find_entry(
        (tp_merge_map_registry_t *)registry,
        TP_MERGE_MAP_TIMESTAMP,
        out_stream_id,
        epoch);
    if (NULL == entry)
    {
        return NULL;
    }

    return &entry->timestamp_map;
}
