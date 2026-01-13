#include "tensor_pool/tp_tracelink.h"

#include <errno.h>
#include <string.h>

#include "aeronc.h"

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_producer.h"

#include "trace/tensor_pool/messageHeader.h"
#include "trace/tensor_pool/traceLinkSet.h"

static int tp_tracelink_validate_parents(const uint64_t *parents, size_t parent_count)
{
    size_t i;
    size_t j;

    if (parent_count == 0 || NULL == parents)
    {
        return -1;
    }

    for (i = 0; i < parent_count; i++)
    {
        if (parents[i] == 0)
        {
            return -1;
        }
        for (j = 0; j < i; j++)
        {
            if (parents[i] == parents[j])
            {
                return -1;
            }
        }
    }

    return 0;
}

int tp_tracelink_set_encode(
    uint8_t *buffer,
    size_t length,
    const tp_tracelink_set_t *set,
    size_t *out_len)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_traceLinkSet msg;
    struct tensor_pool_traceLinkSet_parents parents_group;
    size_t i;

    if (NULL == buffer || NULL == set || NULL == out_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_encode: null input");
        return -1;
    }

    if (set->trace_id == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_encode: trace_id is required");
        return -1;
    }

    if (set->parent_count == 0 || set->parent_count > UINT16_MAX)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_encode: invalid parent count");
        return -1;
    }

    if (tp_tracelink_validate_parents(set->parents, set->parent_count) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_encode: invalid parent list");
        return -1;
    }

    {
        const size_t required = tensor_pool_messageHeader_encoded_length() +
            tensor_pool_traceLinkSet_sbe_block_length() +
            4 + (set->parent_count * sizeof(uint64_t));
        if (length < required)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_encode: buffer too small");
            return -1;
        }
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_traceLinkSet_sbe_schema_version(),
        length);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_traceLinkSet_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_traceLinkSet_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_traceLinkSet_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_traceLinkSet_sbe_schema_version());

    tensor_pool_traceLinkSet_wrap_for_encode(
        &msg,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        length);
    tensor_pool_traceLinkSet_set_streamId(&msg, set->stream_id);
    tensor_pool_traceLinkSet_set_epoch(&msg, set->epoch);
    tensor_pool_traceLinkSet_set_seq(&msg, set->seq);
    tensor_pool_traceLinkSet_set_traceId(&msg, set->trace_id);

    tensor_pool_traceLinkSet_parents_wrap_for_encode(
        &parents_group,
        (char *)buffer,
        (uint16_t)set->parent_count,
        tensor_pool_traceLinkSet_sbe_position_ptr(&msg),
        tensor_pool_traceLinkSet_sbe_schema_version(),
        length);

    for (i = 0; i < set->parent_count; i++)
    {
        tensor_pool_traceLinkSet_parents_next(&parents_group);
        tensor_pool_traceLinkSet_parents_set_traceId(&parents_group, set->parents[i]);
    }

    *out_len = (size_t)tensor_pool_traceLinkSet_sbe_position(&msg);
    return 0;
}

int tp_tracelink_set_decode(
    const uint8_t *buffer,
    size_t length,
    tp_tracelink_set_t *out,
    uint64_t *parents,
    size_t max_parents)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_traceLinkSet msg;
    struct tensor_pool_traceLinkSet_parents parents_group;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version;
    uint16_t block_length;
    size_t parent_count;
    size_t i;

    if (NULL == buffer || NULL == out || NULL == parents)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_decode: null input");
        return -1;
    }

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_decode: buffer too short");
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &header,
        (char *)buffer,
        0,
        tensor_pool_traceLinkSet_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&header);
    schema_id = tensor_pool_messageHeader_schemaId(&header);
    version = tensor_pool_messageHeader_version(&header);
    block_length = tensor_pool_messageHeader_blockLength(&header);

    if (schema_id != tensor_pool_traceLinkSet_sbe_schema_id() ||
        template_id != tensor_pool_traceLinkSet_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_traceLinkSet_sbe_schema_version())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_decode: unsupported schema version");
        return -1;
    }

    if (block_length != tensor_pool_traceLinkSet_sbe_block_length())
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_decode: block length mismatch");
        return -1;
    }

    tensor_pool_traceLinkSet_wrap_for_decode(
        &msg,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(out, 0, sizeof(*out));
    out->stream_id = tensor_pool_traceLinkSet_streamId(&msg);
    out->epoch = tensor_pool_traceLinkSet_epoch(&msg);
    out->seq = tensor_pool_traceLinkSet_seq(&msg);
    out->trace_id = tensor_pool_traceLinkSet_traceId(&msg);
    if (out->trace_id == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_decode: trace_id missing");
        return -1;
    }

    tensor_pool_traceLinkSet_parents_wrap_for_decode(
        &parents_group,
        (char *)buffer,
        tensor_pool_traceLinkSet_sbe_position_ptr(&msg),
        version,
        length);

    parent_count = (size_t)tensor_pool_traceLinkSet_parents_count(&parents_group);
    if (parent_count < 1 || parent_count > max_parents)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_decode: parent count invalid");
        return -1;
    }

    for (i = 0; i < parent_count; i++)
    {
        size_t j;
        tensor_pool_traceLinkSet_parents_next(&parents_group);
        parents[i] = tensor_pool_traceLinkSet_parents_traceId(&parents_group);
        if (parents[i] == 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_decode: parent trace_id invalid");
            return -1;
        }
        for (j = 0; j < i; j++)
        {
            if (parents[i] == parents[j])
            {
                TP_SET_ERR(EINVAL, "%s", "tp_tracelink_set_decode: duplicate parent trace_id");
                return -1;
            }
        }
    }

    out->parents = parents;
    out->parent_count = parent_count;
    return 0;
}

static int tp_offer_message(aeron_publication_t *pub, const uint8_t *buffer, size_t length)
{
    int64_t result = aeron_publication_offer(pub, buffer, length, NULL, NULL);
    if (result < 0)
    {
        return (int)result;
    }
    return 0;
}

int tp_producer_send_tracelink_set(tp_producer_t *producer, const tp_tracelink_set_t *set)
{
    uint8_t buffer[1024];
    size_t encoded_len = 0;

    if (NULL == producer || NULL == producer->control_publication || NULL == set)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_tracelink_set: control publication unavailable");
        return -1;
    }

    if (set->stream_id != producer->stream_id || set->epoch != producer->epoch)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_producer_send_tracelink_set: stream/epoch mismatch");
        return -1;
    }

    if (tp_tracelink_set_encode(buffer, sizeof(buffer), set, &encoded_len) < 0)
    {
        return -1;
    }

    return tp_offer_message(producer->control_publication, buffer, encoded_len);
}
