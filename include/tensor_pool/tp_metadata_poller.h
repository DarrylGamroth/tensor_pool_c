#ifndef TENSOR_POOL_TP_METADATA_POLLER_H
#define TENSOR_POOL_TP_METADATA_POLLER_H

#include <stdbool.h>

#include "aeron_fragment_assembler.h"

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_control_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_metadata_handlers_stct
{
    tp_on_data_source_announce_t on_data_source_announce;
    tp_on_data_source_meta_begin_t on_data_source_meta_begin;
    tp_on_data_source_meta_attr_t on_data_source_meta_attr;
    tp_on_data_source_meta_end_t on_data_source_meta_end;
    tp_on_meta_blob_announce_t on_meta_blob_announce;
    tp_on_meta_blob_chunk_t on_meta_blob_chunk;
    tp_on_meta_blob_complete_t on_meta_blob_complete;
    void *clientd;
}
tp_metadata_handlers_t;

typedef struct tp_metadata_poller_stct
{
    tp_client_t *client;
    aeron_fragment_assembler_t *assembler;
    tp_metadata_handlers_t handlers;
    bool blob_active;
    uint32_t blob_stream_id;
    uint32_t blob_meta_version;
    uint32_t blob_type;
    uint64_t blob_total_len;
    uint64_t blob_checksum;
    uint64_t blob_next_offset;
}
tp_metadata_poller_t;

int tp_metadata_poller_init(tp_metadata_poller_t *poller, tp_client_t *client, const tp_metadata_handlers_t *handlers);
int tp_metadata_poll(tp_metadata_poller_t *poller, int fragment_limit);

#ifdef TP_ENABLE_FUZZ
void tp_metadata_poller_handle_fragment(tp_metadata_poller_t *poller, const uint8_t *buffer, size_t length);
#endif

#ifdef __cplusplus
}
#endif

#endif
