#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/internal/tp_control_adapter.h"

#include <assert.h>
#include <string.h>

int tp_consumer_apply_announce_for_test(tp_consumer_t *consumer, const tp_shm_pool_announce_view_t *announce);

void tp_test_consumer_fallback_invalid_announce(void)
{
    tp_client_t client;
    tp_consumer_t consumer;
    tp_shm_pool_announce_view_t announce;
    tp_shm_pool_desc_t pool;
    const char *header_uri = "shm:file?path=/tmp/tp_invalid_header";
    const char *pool_uri = "shm:file?path=/tmp/tp_invalid_pool";
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&consumer, 0, sizeof(consumer));
    memset(&announce, 0, sizeof(announce));
    memset(&pool, 0, sizeof(pool));

    if (tp_context_init(&client.context.base) < 0)
    {
        goto cleanup;
    }

    consumer.client = &client;
    strncpy(consumer.payload_fallback_uri, "bridge://pool", sizeof(consumer.payload_fallback_uri) - 1);
    consumer.payload_fallback_uri[sizeof(consumer.payload_fallback_uri) - 1] = '\0';

    announce.stream_id = 91002;
    announce.epoch = 1;
    announce.layout_version = 1;
    announce.header_nslots = 4;
    announce.header_slot_bytes = TP_HEADER_SLOT_BYTES;
    announce.header_region_uri.data = header_uri;
    announce.header_region_uri.length = (uint32_t)strlen(header_uri);
    announce.pool_count = 1;
    announce.pools = &pool;

    pool.pool_id = 1;
    pool.nslots = 4;
    pool.stride_bytes = 64;
    pool.region_uri.data = pool_uri;
    pool.region_uri.length = (uint32_t)strlen(pool_uri);

    if (tp_consumer_apply_announce_for_test(&consumer, &announce) != 0)
    {
        goto cleanup;
    }

    if (consumer.state != TP_CONSUMER_STATE_FALLBACK)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}

void tp_test_consumer_fallback_layout_version(void)
{
    tp_client_t client;
    tp_consumer_t consumer;
    tp_shm_pool_announce_view_t announce;
    tp_shm_pool_desc_t pool;
    const char *header_uri = "shm:file?path=/tmp/tp_invalid_header";
    const char *pool_uri = "shm:file?path=/tmp/tp_invalid_pool";
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&consumer, 0, sizeof(consumer));
    memset(&announce, 0, sizeof(announce));
    memset(&pool, 0, sizeof(pool));

    if (tp_context_init(&client.context.base) < 0)
    {
        goto cleanup;
    }

    consumer.client = &client;
    strncpy(consumer.payload_fallback_uri, "bridge://pool", sizeof(consumer.payload_fallback_uri) - 1);
    consumer.payload_fallback_uri[sizeof(consumer.payload_fallback_uri) - 1] = '\0';

    announce.stream_id = 91003;
    announce.epoch = 1;
    announce.layout_version = TP_LAYOUT_VERSION + 1;
    announce.header_nslots = 4;
    announce.header_slot_bytes = TP_HEADER_SLOT_BYTES;
    announce.header_region_uri.data = header_uri;
    announce.header_region_uri.length = (uint32_t)strlen(header_uri);
    announce.pool_count = 1;
    announce.pools = &pool;

    pool.pool_id = 1;
    pool.nslots = 4;
    pool.stride_bytes = 64;
    pool.region_uri.data = pool_uri;
    pool.region_uri.length = (uint32_t)strlen(pool_uri);

    if (tp_consumer_apply_announce_for_test(&consumer, &announce) != 0)
    {
        goto cleanup;
    }

    if (consumer.state != TP_CONSUMER_STATE_FALLBACK)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    assert(result == 0);
}
