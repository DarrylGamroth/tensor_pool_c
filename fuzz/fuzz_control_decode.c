#include <stddef.h>
#include <stdint.h>

#include "tensor_pool/internal/tp_control_adapter.h"

static void tp_fuzz_on_meta_attr(const tp_data_source_meta_attr_view_t *attr, void *clientd)
{
    (void)attr;
    (void)clientd;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tp_consumer_hello_view_t hello;
    tp_consumer_config_view_t config;
    tp_data_source_announce_view_t announce;
    tp_data_source_meta_view_t meta;
    tp_meta_blob_announce_view_t blob_announce;
    tp_meta_blob_chunk_view_t blob_chunk;
    tp_meta_blob_complete_view_t blob_complete;
    tp_control_response_view_t response;
    tp_shm_pool_announce_view_t shm_announce;

    if (data == NULL || size == 0)
    {
        return 0;
    }

    (void)tp_control_decode_consumer_hello(data, size, &hello);
    (void)tp_control_decode_consumer_config(data, size, &config);
    (void)tp_control_decode_data_source_announce(data, size, &announce);
    (void)tp_control_decode_data_source_meta(data, size, &meta, tp_fuzz_on_meta_attr, NULL);
    (void)tp_control_decode_meta_blob_announce(data, size, &blob_announce);
    (void)tp_control_decode_meta_blob_chunk(data, size, &blob_chunk);
    (void)tp_control_decode_meta_blob_complete(data, size, &blob_complete);
    (void)tp_control_decode_control_response(data, size, &response);

    if (tp_control_decode_shm_pool_announce(data, size, &shm_announce) == 0)
    {
        tp_control_shm_pool_announce_close(&shm_announce);
    }

    return 0;
}
