#include "tensor_pool/tp_discovery_service.h"

#include <assert.h>
#include <string.h>

#include "discovery/tensor_pool/discoveryStatus.h"

static void tp_test_discovery_service_query(void)
{
    tp_discovery_service_config_t config;
    tp_discovery_service_t service;
    tp_shm_pool_announce_t announce;
    tp_shm_pool_announce_pool_t pool;
    tp_data_source_announce_t data_source;
    tp_discovery_request_t request;
    tp_discovery_response_t response;
    const char *tags[] = {"fast", "sensor"};
    const char *missing_tags[] = {"fast", "missing"};

    assert(tp_discovery_service_config_init(&config) == 0);
    strncpy(config.request_channel, "aeron:ipc", sizeof(config.request_channel) - 1);
    config.request_stream_id = 9000;
    strncpy(config.announce_channel, "aeron:ipc", sizeof(config.announce_channel) - 1);
    config.announce_stream_id = 1001;
    strncpy(config.metadata_channel, "aeron:ipc", sizeof(config.metadata_channel) - 1);
    config.metadata_stream_id = 1300;
    strncpy(config.driver_instance_id, "driver-test", sizeof(config.driver_instance_id) - 1);
    strncpy(config.driver_control_channel, "aeron:ipc", sizeof(config.driver_control_channel) - 1);
    config.driver_control_stream_id = 1000;

    assert(tp_discovery_service_init(&service, &config) == 0);

    memset(&announce, 0, sizeof(announce));
    memset(&pool, 0, sizeof(pool));
    pool.pool_id = 1;
    pool.pool_nslots = 64;
    pool.stride_bytes = 1024;
    pool.region_uri = "shm:file?path=/tmp/pool1|require_hugepages=false";
    announce.stream_id = 10000;
    announce.producer_id = 42;
    announce.epoch = 10;
    announce.layout_version = TP_LAYOUT_VERSION;
    announce.header_nslots = 64;
    announce.header_slot_bytes = TP_HEADER_SLOT_BYTES;
    announce.header_region_uri = "shm:file?path=/tmp/header|require_hugepages=false";
    announce.pools = &pool;
    announce.pool_count = 1;

    assert(tp_discovery_service_apply_announce(&service, &announce) == 0);

    memset(&data_source, 0, sizeof(data_source));
    data_source.stream_id = 10000;
    data_source.producer_id = 42;
    data_source.epoch = 10;
    data_source.meta_version = 1;
    data_source.name = "camera-1";
    data_source.summary = "test";
    assert(tp_discovery_service_apply_data_source(&service, &data_source) == 0);
    assert(tp_discovery_service_set_tags(&service, 10000, tags, 2) == 0);

    tp_discovery_request_init(&request);
    request.request_id = 1;
    request.stream_id = 10000;
    request.data_source_name = "camera-1";
    request.tags = tags;
    request.tag_count = 2;
    assert(tp_discovery_service_query(&service, &request, &response) == 0);
    assert(response.status == tensor_pool_discoveryStatus_OK);
    assert(response.result_count == 1);
    assert(response.results[0].stream_id == 10000);
    assert(strcmp(response.results[0].driver_instance_id, "driver-test") == 0);
    tp_discovery_service_response_close(&response);

    request.stream_id = 10001;
    request.data_source_name = NULL;
    request.tags = NULL;
    request.tag_count = 0;
    assert(tp_discovery_service_query(&service, &request, &response) == 0);
    assert(response.result_count == 0);
    tp_discovery_service_response_close(&response);

    request.stream_id = 10000;
    request.data_source_name = "camera-1";
    request.tags = missing_tags;
    request.tag_count = 2;
    assert(tp_discovery_service_query(&service, &request, &response) == 0);
    assert(response.result_count == 0);
    tp_discovery_service_response_close(&response);

    tp_discovery_service_close(&service);
}

void tp_test_discovery_service(void)
{
    tp_test_discovery_service_query();
}
