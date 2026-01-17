#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tensor_pool/tp_metadata_poller.h"

static void tp_fuzz_on_data_source_announce(const tp_data_source_announce_view_t *announce, void *clientd)
{
    (void)announce;
    (void)clientd;
}

static void tp_fuzz_on_data_source_meta_begin(const tp_data_source_meta_view_t *meta, void *clientd)
{
    (void)meta;
    (void)clientd;
}

static void tp_fuzz_on_data_source_meta_attr(const tp_data_source_meta_attr_view_t *attr, void *clientd)
{
    (void)attr;
    (void)clientd;
}

static void tp_fuzz_on_data_source_meta_end(const tp_data_source_meta_view_t *meta, void *clientd)
{
    (void)meta;
    (void)clientd;
}

static void tp_fuzz_on_meta_blob_announce(const tp_meta_blob_announce_view_t *announce, void *clientd)
{
    (void)announce;
    (void)clientd;
}

static void tp_fuzz_on_meta_blob_chunk(const tp_meta_blob_chunk_view_t *chunk, void *clientd)
{
    (void)chunk;
    (void)clientd;
}

static void tp_fuzz_on_meta_blob_complete(const tp_meta_blob_complete_view_t *complete, void *clientd)
{
    (void)complete;
    (void)clientd;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_metadata_poller_t poller;

    if (data == NULL)
    {
        return 0;
    }

    memset(&poller, 0, sizeof(poller));
    poller.handlers.on_data_source_announce = tp_fuzz_on_data_source_announce;
    poller.handlers.on_data_source_meta_begin = tp_fuzz_on_data_source_meta_begin;
    poller.handlers.on_data_source_meta_attr = tp_fuzz_on_data_source_meta_attr;
    poller.handlers.on_data_source_meta_end = tp_fuzz_on_data_source_meta_end;
    poller.handlers.on_meta_blob_announce = tp_fuzz_on_meta_blob_announce;
    poller.handlers.on_meta_blob_chunk = tp_fuzz_on_meta_blob_chunk;
    poller.handlers.on_meta_blob_complete = tp_fuzz_on_meta_blob_complete;
    poller.handlers.clientd = NULL;

    tp_metadata_poller_handle_fragment(&poller, data, size);

    return 0;
}
