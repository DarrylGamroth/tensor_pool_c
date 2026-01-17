#include "tensor_pool/tp_metadata_poller.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"

static void tp_metadata_blob_reset(tp_metadata_poller_t *poller)
{
    if (NULL == poller)
    {
        return;
    }

    poller->blob_active = false;
    poller->blob_stream_id = 0;
    poller->blob_meta_version = 0;
    poller->blob_type = 0;
    poller->blob_total_len = 0;
    poller->blob_checksum = 0;
    poller->blob_next_offset = 0;
}

static void tp_metadata_blob_begin(tp_metadata_poller_t *poller, const tp_meta_blob_announce_view_t *announce)
{
    if (NULL == poller || NULL == announce)
    {
        return;
    }

    poller->blob_active = true;
    poller->blob_stream_id = announce->stream_id;
    poller->blob_meta_version = announce->meta_version;
    poller->blob_type = announce->blob_type;
    poller->blob_total_len = announce->total_len;
    poller->blob_checksum = announce->checksum;
    poller->blob_next_offset = 0;
}

static int tp_metadata_blob_validate_chunk(tp_metadata_poller_t *poller, const tp_meta_blob_chunk_view_t *chunk)
{
    uint64_t remaining;

    if (NULL == poller || NULL == chunk)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_chunk: null input");
        return -1;
    }

    if (!poller->blob_active)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_chunk: no active blob");
        return -1;
    }

    if (chunk->stream_id != poller->blob_stream_id ||
        chunk->meta_version != poller->blob_meta_version)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_chunk: blob identity mismatch");
        tp_metadata_blob_reset(poller);
        return -1;
    }

    if (chunk->bytes.length == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_chunk: empty chunk");
        tp_metadata_blob_reset(poller);
        return -1;
    }

    if (chunk->offset != poller->blob_next_offset)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_chunk: non-contiguous chunk");
        tp_metadata_blob_reset(poller);
        return -1;
    }

    if (chunk->offset > poller->blob_total_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_chunk: chunk offset beyond total length");
        tp_metadata_blob_reset(poller);
        return -1;
    }

    remaining = poller->blob_total_len - chunk->offset;
    if ((uint64_t)chunk->bytes.length > remaining)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_chunk: chunk overruns blob");
        tp_metadata_blob_reset(poller);
        return -1;
    }

    poller->blob_next_offset = chunk->offset + (uint64_t)chunk->bytes.length;
    return 0;
}

static int tp_metadata_blob_validate_complete(tp_metadata_poller_t *poller, const tp_meta_blob_complete_view_t *complete)
{
    if (NULL == poller || NULL == complete)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_complete: null input");
        return -1;
    }

    if (!poller->blob_active)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_complete: no active blob");
        return -1;
    }

    if (complete->stream_id != poller->blob_stream_id ||
        complete->meta_version != poller->blob_meta_version)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_complete: blob identity mismatch");
        tp_metadata_blob_reset(poller);
        return -1;
    }

    if (complete->checksum != poller->blob_checksum)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_complete: checksum mismatch");
        tp_metadata_blob_reset(poller);
        return -1;
    }

    if (poller->blob_next_offset != poller->blob_total_len)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_blob_validate_complete: blob incomplete");
        tp_metadata_blob_reset(poller);
        return -1;
    }

    return 0;
}

static void tp_metadata_poller_handler(void *clientd, const uint8_t *buffer, size_t length, aeron_header_t *header)
{
    tp_metadata_poller_t *poller = (tp_metadata_poller_t *)clientd;
    tp_data_source_announce_view_t announce;
    tp_data_source_meta_view_t meta;

    (void)header;

    if (NULL == poller || NULL == buffer)
    {
        return;
    }

    if (poller->handlers.on_data_source_announce &&
        tp_control_decode_data_source_announce(buffer, length, &announce) == 0)
    {
        poller->handlers.on_data_source_announce(&announce, poller->handlers.clientd);
        return;
    }

    if (poller->handlers.on_data_source_meta_begin ||
        poller->handlers.on_data_source_meta_attr ||
        poller->handlers.on_data_source_meta_end)
    {
        if (tp_control_decode_data_source_meta(
            buffer,
            length,
            &meta,
            poller->handlers.on_data_source_meta_attr,
            poller->handlers.clientd) == 0)
        {
            if (poller->handlers.on_data_source_meta_begin)
            {
                poller->handlers.on_data_source_meta_begin(&meta, poller->handlers.clientd);
            }
            if (poller->handlers.on_data_source_meta_end)
            {
                poller->handlers.on_data_source_meta_end(&meta, poller->handlers.clientd);
            }
        }
    }

    {
        tp_meta_blob_announce_view_t blob_announce;
        if (tp_control_decode_meta_blob_announce(buffer, length, &blob_announce) == 0)
        {
            tp_metadata_blob_begin(poller, &blob_announce);
            if (poller->handlers.on_meta_blob_announce)
            {
                poller->handlers.on_meta_blob_announce(&blob_announce, poller->handlers.clientd);
            }
            return;
        }
    }

    {
        tp_meta_blob_chunk_view_t blob_chunk;
        if (tp_control_decode_meta_blob_chunk(buffer, length, &blob_chunk) == 0)
        {
            if (tp_metadata_blob_validate_chunk(poller, &blob_chunk) == 0 &&
                poller->handlers.on_meta_blob_chunk)
            {
                poller->handlers.on_meta_blob_chunk(&blob_chunk, poller->handlers.clientd);
            }
            return;
        }
    }

    {
        tp_meta_blob_complete_view_t blob_complete;
        if (tp_control_decode_meta_blob_complete(buffer, length, &blob_complete) == 0)
        {
            if (tp_metadata_blob_validate_complete(poller, &blob_complete) == 0 &&
                poller->handlers.on_meta_blob_complete)
            {
                poller->handlers.on_meta_blob_complete(&blob_complete, poller->handlers.clientd);
            }
            tp_metadata_blob_reset(poller);
            return;
        }
    }
}

#ifdef TP_ENABLE_FUZZ
void tp_metadata_poller_handle_fragment(tp_metadata_poller_t *poller, const uint8_t *buffer, size_t length)
{
    tp_metadata_poller_handler(poller, buffer, length, NULL);
}
#endif

int tp_metadata_poller_init(tp_metadata_poller_t *poller, tp_client_t *client, const tp_metadata_handlers_t *handlers)
{
    if (NULL == poller || NULL == client || NULL == client->metadata_subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_poller_init: invalid input");
        return -1;
    }

    memset(poller, 0, sizeof(*poller));
    poller->client = client;
    if (handlers)
    {
        poller->handlers = *handlers;
    }
    tp_metadata_blob_reset(poller);

    if (aeron_fragment_assembler_create(&poller->assembler, tp_metadata_poller_handler, poller) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_metadata_poll(tp_metadata_poller_t *poller, int fragment_limit)
{
    if (NULL == poller || NULL == poller->assembler || NULL == poller->client || NULL == poller->client->metadata_subscription)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_metadata_poll: poller not initialized");
        return -1;
    }

    return aeron_subscription_poll(
        poller->client->metadata_subscription,
        aeron_fragment_assembler_handler,
        poller->assembler,
        fragment_limit);
}
