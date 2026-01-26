#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_client.h"
#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_driver.h"
#include "tensor_pool/driver/tp_driver_agent.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_control.h"
#include "tensor_pool/tp_discovery_service.h"
#include "tensor_pool/tp_discovery_client.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_types.h"

#include "tp_aeron_wrap.h"
#include "aeron_alloc.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/leaseRevokeReason.h"
#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/shmAttachResponse.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/statfs.h>
#include <time.h>
#include <unistd.h>

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif

#ifndef TP_TEST_CONFIG_DIR
#define TP_TEST_CONFIG_DIR "config"
#endif

#define TP_TEST_CONFIG_PATH(name) TP_TEST_CONFIG_DIR "/" name

typedef struct tp_driver_revoke_state_stct
{
    int revoked;
    tp_driver_lease_revoked_t info;
}
tp_driver_revoke_state_t;

static void tp_on_driver_lease_revoked(const tp_driver_lease_revoked_t *info, void *clientd)
{
    tp_driver_revoke_state_t *state = (tp_driver_revoke_state_t *)clientd;

    state->revoked = 1;
    state->info = *info;
}

static void tp_test_sleep_ms(long millis)
{
    struct timespec req;

    if (millis <= 0)
    {
        return;
    }

    req.tv_sec = millis / 1000;
    req.tv_nsec = (millis % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

static int tp_test_is_hugepages_dir(const char *path)
{
    struct statfs st;

    if (NULL == path || path[0] == '\0')
    {
        return 0;
    }

    if (statfs(path, &st) < 0)
    {
        return 0;
    }

    return (st.f_type == HUGETLBFS_MAGIC) ? 1 : 0;
}

static int tp_test_driver_attach_with_work(
    tp_driver_t *driver,
    tp_driver_client_t *client,
    tp_client_t *base,
    tp_driver_attach_request_t *request,
    tp_driver_attach_info_t *out,
    int64_t timeout_ns)
{
    tp_async_attach_t *async = NULL;
    int64_t deadline_ns;
    int poll_result;

    if (tp_driver_attach_async(client, request, &async) < 0 || NULL == async)
    {
        return -1;
    }

    deadline_ns = tp_clock_now_ns() + timeout_ns;
    for (;;)
    {
        tp_driver_do_work(driver);
        if (base)
        {
            tp_client_do_work(base);
        }
        poll_result = tp_driver_attach_poll(async, out);
        if (poll_result < 0)
        {
            break;
        }
        if (poll_result > 0)
        {
            aeron_free(async);
            return 0;
        }
        if (timeout_ns > 0 && tp_clock_now_ns() > deadline_ns)
        {
            TP_SET_ERR(ETIMEDOUT, "%s", "tp_test_driver_attach_with_work: timeout");
            break;
        }
        tp_test_sleep_ms(1);
    }

    tp_fragment_assembler_close(&async->assembler);
    aeron_free(async);
    return -1;
}

static int tp_test_driver_detach_with_work(
    tp_driver_t *driver,
    tp_driver_client_t *client,
    tp_client_t *base,
    int64_t timeout_ns)
{
    tp_async_detach_t *async = NULL;
    tp_driver_detach_info_t info;
    int64_t deadline_ns;
    int poll_result;

    if (tp_driver_detach_async(client, &async) < 0 || NULL == async)
    {
        return -1;
    }

    deadline_ns = tp_clock_now_ns() + timeout_ns;
    for (;;)
    {
        tp_driver_do_work(driver);
        if (base)
        {
            tp_client_do_work(base);
        }
        poll_result = tp_driver_detach_poll(async, &info);
        if (poll_result < 0)
        {
            break;
        }
        if (poll_result > 0)
        {
            aeron_free(async);
            return 0;
        }
        if (timeout_ns > 0 && tp_clock_now_ns() > deadline_ns)
        {
            TP_SET_ERR(ETIMEDOUT, "%s", "tp_test_driver_detach_with_work: timeout");
            break;
        }
        tp_test_sleep_ms(1);
    }

    tp_fragment_assembler_close(&async->assembler);
    aeron_free(async);
    return -1;
}

static int tp_test_producer_attach_async_with_work(
    tp_driver_t *driver,
    tp_client_t *client,
    tp_producer_t *producer,
    int64_t timeout_ns)
{
    tp_async_attach_t *async = NULL;
    int64_t deadline_ns;
    int poll_result;

    if (tp_producer_attach_driver_async(producer, &async) < 0 || NULL == async)
    {
        return -1;
    }

    deadline_ns = tp_clock_now_ns() + timeout_ns;
    for (;;)
    {
        tp_driver_do_work(driver);
        tp_client_do_work(client);
        poll_result = tp_producer_attach_driver_poll(producer, async);
        if (poll_result < 0)
        {
            break;
        }
        if (poll_result > 0)
        {
            tp_fragment_assembler_close(&async->assembler);
            aeron_free(async);
            return 0;
        }
        if (timeout_ns > 0 && tp_clock_now_ns() > deadline_ns)
        {
            TP_SET_ERR(ETIMEDOUT, "%s", "tp_test_producer_attach_async_with_work: timeout");
            break;
        }
        tp_test_sleep_ms(1);
    }

    tp_fragment_assembler_close(&async->assembler);
    aeron_free(async);
    return -1;
}

static int tp_test_consumer_attach_async_with_work(
    tp_driver_t *driver,
    tp_client_t *client,
    tp_consumer_t *consumer,
    int64_t timeout_ns)
{
    tp_async_attach_t *async = NULL;
    int64_t deadline_ns;
    int poll_result;

    if (tp_consumer_attach_driver_async(consumer, &async) < 0 || NULL == async)
    {
        return -1;
    }

    deadline_ns = tp_clock_now_ns() + timeout_ns;
    for (;;)
    {
        tp_driver_do_work(driver);
        tp_client_do_work(client);
        poll_result = tp_consumer_attach_driver_poll(consumer, async);
        if (poll_result < 0)
        {
            break;
        }
        if (poll_result > 0)
        {
            tp_fragment_assembler_close(&async->assembler);
            aeron_free(async);
            return 0;
        }
        if (timeout_ns > 0 && tp_clock_now_ns() > deadline_ns)
        {
            TP_SET_ERR(ETIMEDOUT, "%s", "tp_test_consumer_attach_async_with_work: timeout");
            break;
        }
        tp_test_sleep_ms(1);
    }

    tp_fragment_assembler_close(&async->assembler);
    aeron_free(async);
    return -1;
}

static int tp_test_driver_attach_with_config(
    const char *config_path,
    const char *shm_namespace,
    uint32_t stream_id,
    uint8_t publish_mode,
    int32_t expected_code)
{
    tp_driver_config_t driver_config;
    tp_driver_t driver;
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver_client = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    int result = -1;

    memset(&driver, 0, sizeof(driver));
    client = NULL;
    memset(&info, 0, sizeof(info));

    if (tp_driver_config_init(&driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_config_load(&driver_config, config_path) < 0)
    {
        goto cleanup;
    }
    if (NULL != shm_namespace)
    {
        strncpy(driver_config.shm_namespace, shm_namespace, sizeof(driver_config.shm_namespace) - 1);
    }

    if (tp_driver_init(&driver, &driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_start(&driver) < 0)
    {
        goto cleanup;
    }

    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    {
        static const char *allowed_paths[] = { "/dev/shm", "/tmp" };
        tp_context_set_allowed_paths(ctx, allowed_paths, 2);
        if (tp_context_finalize_allowed_paths(ctx) < 0)
        {
            goto cleanup;
        }
    }
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(
        ctx,
        tp_context_get_control_channel(driver.config.base),
        tp_context_get_control_stream_id(driver.config.base));

    if (tp_client_init(&client, ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_context_allowed_paths(ctx)->canonical_length == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_test_driver_async_attach_wrappers: allowed paths not configured");
        goto cleanup;
    }
    if (tp_client_start(client) < 0)
    {
        goto cleanup;
    }

    if (tp_driver_client_init(&driver_client, client) < 0)
    {
        goto cleanup;
    }

    memset(&request, 0, sizeof(request));
    request.correlation_id = 1;
    request.stream_id = stream_id;
    request.client_id = 0;
    request.role = tensor_pool_role_PRODUCER;
    request.expected_layout_version = TP_LAYOUT_VERSION;
    request.publish_mode = publish_mode;
    request.require_hugepages = 0;
    request.desired_node_id = 0;

    if (tp_test_driver_attach_with_work(&driver, driver_client, client, &request, &info, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }

    if (info.code != expected_code)
    {
        TP_SET_ERR(EINVAL, "tp_test_driver_attach_with_config: expected code %d got %d",
            expected_code, info.code);
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(driver_client);
    tp_client_close(client);
    tp_driver_close(&driver);
    return result;
}

void tp_test_driver_discovery_integration(void)
{
    tp_driver_config_t driver_config;
    tp_driver_t driver;
    tp_discovery_service_config_t discovery_config;
    tp_discovery_service_t discovery;
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver_client = NULL;
    tp_driver_attach_request_t attach_request;
    tp_driver_attach_info_t attach_info;
    tp_shm_pool_announce_pool_t *announce_pools = NULL;
    tp_shm_pool_announce_t announce;
    tp_discovery_request_t request;
    tp_discovery_response_t response;
    int result = -1;
    int step = 0;
    int poll_result;

    memset(&driver, 0, sizeof(driver));
    memset(&discovery, 0, sizeof(discovery));
    client = NULL;
    memset(&attach_request, 0, sizeof(attach_request));
    memset(&attach_info, 0, sizeof(attach_info));
    memset(&announce, 0, sizeof(announce));
    memset(&response, 0, sizeof(response));

    if (tp_driver_config_init(&driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_config_load(&driver_config, TP_TEST_CONFIG_PATH("driver_integration_example.toml")) < 0)
    {
        goto cleanup;
    }
    strncpy(driver_config.shm_namespace, "test-int", sizeof(driver_config.shm_namespace) - 1);
    driver_config.announce_period_ms = 50;

    if (tp_driver_init(&driver, &driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_start(&driver) < 0)
    {
        goto cleanup;
    }

    if (tp_discovery_service_config_init(&discovery_config) < 0)
    {
        goto cleanup;
    }
    if (tp_discovery_service_config_load(&discovery_config, TP_TEST_CONFIG_PATH("discovery_example.toml")) < 0)
    {
        goto cleanup;
    }
    discovery_config.announce_period_ms = 50;

    if (tp_discovery_service_init(&discovery, &discovery_config) < 0)
    {
        goto cleanup;
    }
    if (tp_discovery_service_start(&discovery) < 0)
    {
        goto cleanup;
    }

    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    {
        const char *allowed_paths[] = { "/dev/shm", "/tmp" };
        tp_context_set_allowed_paths(ctx, allowed_paths, 2);
    }
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(ctx, "aeron:ipc?term-length=4m", 1000);
    tp_context_set_announce_channel(ctx, "aeron:ipc?term-length=4m", 1001);
    tp_context_set_descriptor_channel(ctx, "aeron:ipc?term-length=4m", 1100);
    tp_context_set_qos_channel(ctx, "aeron:ipc?term-length=4m", 1200);
    tp_context_set_metadata_channel(ctx, "aeron:ipc?term-length=4m", 1300);

    if (tp_client_init(&client, ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_client_start(client) < 0)
    {
        goto cleanup;
    }

    if (tp_driver_client_init(&driver_client, client) < 0)
    {
        goto cleanup;
    }
    attach_request.correlation_id = 1;
    attach_request.stream_id = 10000;
    attach_request.client_id = 0;
    attach_request.role = tensor_pool_role_PRODUCER;
    attach_request.expected_layout_version = TP_LAYOUT_VERSION;
    attach_request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    attach_request.require_hugepages = 0;
    attach_request.desired_node_id = 0;
    if (tp_test_driver_attach_with_work(
            &driver,
            driver_client,
            client,
            &attach_request,
            &attach_info,
            2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    if (attach_info.code != tensor_pool_responseCode_OK)
    {
        TP_SET_ERR(EINVAL, "tp_test_driver_discovery_integration: attach failed: %s", attach_info.error_message);
        goto cleanup;
    }

    {
        int64_t warm_deadline_ns = tp_clock_now_ns() + 200 * 1000 * 1000LL;
        while (tp_clock_now_ns() < warm_deadline_ns)
        {
            tp_driver_do_work(&driver);
            tp_discovery_service_do_work(&discovery);
            tp_test_sleep_ms(1);
        }
    }

    if (attach_info.pool_count > 0)
    {
        announce_pools = calloc(attach_info.pool_count, sizeof(*announce_pools));
        if (NULL == announce_pools)
        {
            goto cleanup;
        }
    }

    for (size_t i = 0; i < attach_info.pool_count; i++)
    {
        announce_pools[i].pool_id = attach_info.pools[i].pool_id;
        announce_pools[i].pool_nslots = attach_info.pools[i].nslots;
        announce_pools[i].stride_bytes = attach_info.pools[i].stride_bytes;
        announce_pools[i].region_uri = attach_info.pools[i].region_uri;
    }

    announce.stream_id = attach_info.stream_id;
    announce.producer_id = attach_info.node_id;
    announce.epoch = attach_info.epoch;
    announce.announce_timestamp_ns = tp_clock_now_ns();
    announce.announce_clock_domain = 0;
    announce.layout_version = attach_info.layout_version;
    announce.header_nslots = attach_info.header_nslots;
    announce.header_slot_bytes = attach_info.header_slot_bytes;
    announce.header_region_uri = attach_info.header_region_uri;
    announce.pools = announce_pools;
    announce.pool_count = attach_info.pool_count;

    if (tp_discovery_service_apply_announce(&discovery, &announce) < 0)
    {
        goto cleanup;
    }

    tp_discovery_request_init(&request);
    request.request_id = 1;

    poll_result = tp_discovery_service_query(&discovery, &request, &response);
    if (poll_result < 0)
    {
        goto cleanup;
    }

    result = 0;

    step = 1;
    assert(result == 0);
    assert(response.result_count > 0);
    assert(response.results[0].stream_id == 10000);
    assert(response.results[0].driver_control_stream_id == 1000);
    assert(response.results[0].header_slot_bytes == 256);

cleanup:
    tp_driver_attach_info_close(&attach_info);
    tp_driver_client_close(driver_client);
    if (response.result_count > 0)
    {
        tp_discovery_response_close(&response);
    }
    free(announce_pools);
    tp_client_close(client);
    tp_discovery_service_close(&discovery);
    tp_driver_close(&driver);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_discovery_integration failed at step %d: %s\n", step, tp_errmsg());
    }

    assert(result == 0);
}

void tp_test_driver_exclusive_producer(void)
{
    tp_driver_config_t driver_config;
    tp_driver_t driver;
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver_client = NULL;
    tp_driver_client_t *producer_client = NULL;
    tp_driver_client_t *producer_client2 = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_driver_attach_info_t producer_info;
    tp_driver_attach_info_t producer_info2;
    int result = -1;
    int step = 0;

    memset(&driver, 0, sizeof(driver));
    client = NULL;
    memset(&info, 0, sizeof(info));
    memset(&producer_info, 0, sizeof(producer_info));
    memset(&producer_info2, 0, sizeof(producer_info2));
    if (tp_driver_config_init(&driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_config_load(&driver_config, TP_TEST_CONFIG_PATH("driver_integration_example.toml")) < 0)
    {
        goto cleanup;
    }
    strncpy(driver_config.shm_namespace, "test-exclusive", sizeof(driver_config.shm_namespace) - 1);

    if (tp_driver_init(&driver, &driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_start(&driver) < 0)
    {
        goto cleanup;
    }

    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    {
        static const char *allowed_paths[] = { "/dev/shm", "/tmp" };
        tp_context_set_allowed_paths(ctx, allowed_paths, 2);
    }
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(ctx, "aeron:ipc?term-length=4m", 1000);

    if (tp_client_init(&client, ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_client_start(client) < 0)
    {
        goto cleanup;
    }

    if (tp_driver_client_init(&driver_client, client) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_client_init(&producer_client, client) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_client_init(&producer_client2, client) < 0)
    {
        goto cleanup;
    }

    memset(&request, 0, sizeof(request));
    request.correlation_id = 1;
    request.stream_id = 10000;
    request.client_id = 0;
    request.role = tensor_pool_role_CONSUMER;
    request.expected_layout_version = TP_LAYOUT_VERSION + 1;
    request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    request.require_hugepages = 0;
    request.desired_node_id = 0;

    if (tp_test_driver_attach_with_work(
            &driver,
            driver_client,
            client,
            &request,
            &info,
            2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    step = 1;
    assert(info.code == tensor_pool_responseCode_REJECTED);
    tp_driver_attach_info_close(&info);

    memset(&info, 0, sizeof(info));
    request.correlation_id++;
    request.expected_layout_version = TP_LAYOUT_VERSION;
    request.role = tensor_pool_role_PRODUCER;
    if (tp_test_driver_attach_with_work(
            &driver,
            producer_client,
            client,
            &request,
            &producer_info,
            2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    step = 2;
    assert(producer_info.code == tensor_pool_responseCode_OK);
    assert(producer_info.node_id != tensor_pool_shmAttachResponse_nodeId_null_value());

    request.correlation_id++;
    request.client_id = 0;
    if (tp_test_driver_attach_with_work(
            &driver,
            producer_client2,
            client,
            &request,
            &producer_info2,
            2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    step = 3;
    assert(producer_info2.code == tensor_pool_responseCode_REJECTED);
    tp_driver_attach_info_close(&producer_info2);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    tp_driver_attach_info_close(&producer_info);
    tp_driver_attach_info_close(&producer_info2);
    tp_driver_client_close(driver_client);
    tp_driver_client_close(producer_client);
    tp_driver_client_close(producer_client2);
    tp_client_close(client);
    tp_driver_close(&driver);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_exclusive_producer failed at step %d: %s\n", step, tp_errmsg());
    }

    assert(result == 0);
}

void tp_test_driver_publish_mode_hugepages(void)
{
    tp_driver_config_t driver_config;
    tp_driver_t driver;
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver_client = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    int result = -1;
    int step = 0;
    int expect_hugepages_ok;

    memset(&driver, 0, sizeof(driver));
    client = NULL;
    memset(&info, 0, sizeof(info));

    if (tp_driver_config_init(&driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_config_load(&driver_config, TP_TEST_CONFIG_PATH("driver_integration_example.toml")) < 0)
    {
        goto cleanup;
    }
    strncpy(driver_config.shm_namespace, "test-publish", sizeof(driver_config.shm_namespace) - 1);

    if (tp_driver_init(&driver, &driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_start(&driver) < 0)
    {
        goto cleanup;
    }

    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(
        ctx,
        tp_context_get_control_channel(driver.config.base),
        tp_context_get_control_stream_id(driver.config.base));

    if (tp_client_init(&client, ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_client_start(client) < 0)
    {
        goto cleanup;
    }

    if (tp_driver_client_init(&driver_client, client) < 0)
    {
        goto cleanup;
    }

    memset(&request, 0, sizeof(request));
    request.correlation_id = 1;
    request.stream_id = 20001;
    request.client_id = 0;
    request.role = tensor_pool_role_PRODUCER;
    request.expected_layout_version = TP_LAYOUT_VERSION;
    request.publish_mode = tensor_pool_publishMode_REQUIRE_EXISTING;
    request.require_hugepages = 0;
    request.desired_node_id = 0;

    if (tp_test_driver_attach_with_work(&driver, driver_client, client, &request, &info, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    step = 1;
    assert(info.code == tensor_pool_responseCode_REJECTED);
    tp_driver_attach_info_close(&info);

    expect_hugepages_ok = tp_test_is_hugepages_dir(driver_config.shm_base_dir);

    memset(&info, 0, sizeof(info));
    request.correlation_id++;
    request.stream_id = 10000;
    request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    request.require_hugepages = tensor_pool_hugepagesPolicy_HUGEPAGES;

    if (tp_test_driver_attach_with_work(&driver, driver_client, client, &request, &info, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    step = 2;
    if (expect_hugepages_ok)
    {
        assert(info.code == tensor_pool_responseCode_OK);
        if (tp_test_driver_detach_with_work(&driver, driver_client, client, 2 * 1000 * 1000 * 1000LL) < 0)
        {
            goto cleanup;
        }
    }
    else
    {
        assert(info.code == tensor_pool_responseCode_REJECTED);
    }

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(driver_client);
    tp_client_close(client);
    tp_driver_close(&driver);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_publish_mode_hugepages failed at step %d: %s\n", step, tp_errmsg());
    }

    assert(result == 0);
}

void tp_test_driver_node_id_cooldown(void)
{
    tp_driver_config_t driver_config;
    tp_driver_t driver;
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver_client = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_driver_attach_info_t info2;
    int result = -1;
    int step = 0;

    memset(&driver, 0, sizeof(driver));
    client = NULL;
    memset(&info, 0, sizeof(info));
    memset(&info2, 0, sizeof(info2));

    if (tp_driver_config_init(&driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_config_load(&driver_config, TP_TEST_CONFIG_PATH("driver_integration_example.toml")) < 0)
    {
        goto cleanup;
    }
    strncpy(driver_config.shm_namespace, "test-nodeid", sizeof(driver_config.shm_namespace) - 1);
    driver_config.node_id_reuse_cooldown_ms = 10000;

    if (tp_driver_init(&driver, &driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_start(&driver) < 0)
    {
        goto cleanup;
    }

    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(
        ctx,
        tp_context_get_control_channel(driver.config.base),
        tp_context_get_control_stream_id(driver.config.base));

    if (tp_client_init(&client, ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_client_start(client) < 0)
    {
        goto cleanup;
    }

    if (tp_driver_client_init(&driver_client, client) < 0)
    {
        goto cleanup;
    }

    memset(&request, 0, sizeof(request));
    request.correlation_id = 1;
    request.stream_id = 10000;
    request.client_id = 0;
    request.role = tensor_pool_role_PRODUCER;
    request.expected_layout_version = TP_LAYOUT_VERSION;
    request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    request.require_hugepages = 0;
    request.desired_node_id = 4242;

    if (tp_test_driver_attach_with_work(&driver, driver_client, client, &request, &info, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    step = 1;
    assert(info.code == tensor_pool_responseCode_OK);

    if (tp_test_driver_detach_with_work(&driver, driver_client, client, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }

    request.correlation_id++;
    if (tp_test_driver_attach_with_work(&driver, driver_client, client, &request, &info2, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    step = 2;
    assert(info2.code == tensor_pool_responseCode_REJECTED);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    tp_driver_attach_info_close(&info2);
    tp_driver_client_close(driver_client);
    tp_client_close(client);
    tp_driver_close(&driver);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_node_id_cooldown failed at step %d: %s\n", step, tp_errmsg());
    }

    assert(result == 0);
}

void tp_test_driver_config_matrix(void)
{
    int result = 0;

    result |= tp_test_driver_attach_with_config(
        TP_TEST_CONFIG_PATH("driver_integration_announce_separate.toml"),
        "test-matrix-announce",
        10001,
        tensor_pool_publishMode_EXISTING_OR_CREATE,
        tensor_pool_responseCode_OK);
    result |= tp_test_driver_attach_with_config(
        TP_TEST_CONFIG_PATH("driver_integration_dynamic.toml"),
        "test-matrix-dynamic",
        20001,
        tensor_pool_publishMode_EXISTING_OR_CREATE,
        tensor_pool_responseCode_OK);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_config_matrix failed: %s\n", tp_errmsg());
    }

    assert(result == 0);
}

void tp_test_driver_lease_expiry(void)
{
    tp_driver_config_t driver_config;
    tp_driver_t driver;
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver_client = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    int expired = 0;
    int result = -1;
    int step = 0;
    int64_t deadline_ns;

    memset(&driver, 0, sizeof(driver));
    client = NULL;
    memset(&info, 0, sizeof(info));

    if (tp_driver_config_init(&driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_config_load(&driver_config, TP_TEST_CONFIG_PATH("driver_integration_example.toml")) < 0)
    {
        goto cleanup;
    }
    strncpy(driver_config.shm_namespace, "test-expiry", sizeof(driver_config.shm_namespace) - 1);
    driver_config.lease_keepalive_interval_ms = 5;
    driver_config.lease_expiry_grace_intervals = 1;

    if (tp_driver_init(&driver, &driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_start(&driver) < 0)
    {
        goto cleanup;
    }

    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(ctx, "aeron:ipc?term-length=4m", 1000);

    if (tp_client_init(&client, ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_client_start(client) < 0)
    {
        goto cleanup;
    }

    if (tp_driver_client_init(&driver_client, client) < 0)
    {
        goto cleanup;
    }

    memset(&request, 0, sizeof(request));
    request.correlation_id = 1;
    request.stream_id = 10000;
    request.client_id = 0;
    request.role = tensor_pool_role_CONSUMER;
    request.expected_layout_version = TP_LAYOUT_VERSION;
    request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    request.require_hugepages = 0;
    request.desired_node_id = 0;

    if (tp_test_driver_attach_with_work(
            &driver,
            driver_client,
            client,
            &request,
            &info,
            2 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    step = 1;
    assert(info.code == tensor_pool_responseCode_OK);

    deadline_ns = tp_clock_now_ns() + 500 * 1000 * 1000LL;
    while (tp_clock_now_ns() < deadline_ns && !expired)
    {
        tp_driver_do_work(&driver);
        expired = tp_driver_client_lease_expired(driver_client, (uint64_t)tp_clock_now_ns());
        tp_test_sleep_ms(1);
    }

    step = 2;
    assert(expired);

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(driver_client);
    tp_client_close(client);
    tp_driver_close(&driver);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_lease_expiry failed at step %d: %s\n", step, tp_errmsg());
    }

    assert(result == 0);
}

void tp_test_driver_async_attach_wrappers(void)
{
    tp_driver_config_t driver_config;
    tp_driver_t driver;
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_producer_t *producer = NULL;
    tp_consumer_t *consumer = NULL;
    tp_producer_context_t producer_ctx;
    tp_consumer_context_t consumer_ctx;
    int result = -1;
    int step = 0;

    memset(&driver, 0, sizeof(driver));
    memset(&producer_ctx, 0, sizeof(producer_ctx));
    memset(&consumer_ctx, 0, sizeof(consumer_ctx));

    if (tp_driver_config_init(&driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_config_load(&driver_config, TP_TEST_CONFIG_PATH("driver_integration_example.toml")) < 0)
    {
        goto cleanup;
    }
    strncpy(driver_config.shm_namespace, "test-async", sizeof(driver_config.shm_namespace) - 1);

    if (tp_driver_init(&driver, &driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_start(&driver) < 0)
    {
        goto cleanup;
    }

    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(
        ctx,
        tp_context_get_control_channel(driver.config.base),
        tp_context_get_control_stream_id(driver.config.base));
    tp_context_set_announce_channel(
        ctx,
        tp_context_get_announce_channel(driver.config.base),
        tp_context_get_announce_stream_id(driver.config.base));
    tp_context_set_descriptor_channel(
        ctx,
        tp_context_get_descriptor_channel(driver.config.base),
        tp_context_get_descriptor_stream_id(driver.config.base));
    tp_context_set_qos_channel(
        ctx,
        tp_context_get_qos_channel(driver.config.base),
        tp_context_get_qos_stream_id(driver.config.base));
    tp_context_set_metadata_channel(
        ctx,
        tp_context_get_metadata_channel(driver.config.base),
        tp_context_get_metadata_stream_id(driver.config.base));

    if (tp_client_init(&client, ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_client_start(client) < 0)
    {
        goto cleanup;
    }
    {
        static const char *allowed_paths[] = { "/dev/shm", "/tmp" };
        tp_context_set_allowed_paths(ctx, allowed_paths, 2);
    }
    if (tp_context_finalize_allowed_paths(ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_context_allowed_paths(ctx)->canonical_length == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_test_driver_async_attach_wrappers: allowed paths not configured");
        goto cleanup;
    }

    if (tp_producer_context_init(&producer_ctx) < 0)
    {
        goto cleanup;
    }
    producer_ctx.use_driver = false;
    producer_ctx.use_conductor_polling = false;
    producer_ctx.stream_id = 10000;
    producer_ctx.producer_id = 1;
    producer_ctx.driver_request.expected_layout_version = TP_LAYOUT_VERSION;
    producer_ctx.driver_request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;

    if (tp_producer_init(&producer, client, &producer_ctx) < 0)
    {
        goto cleanup;
    }

    step = 1;
    if (tp_test_producer_attach_async_with_work(&driver, client, producer, 5 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }

    if (tp_consumer_context_init(&consumer_ctx) < 0)
    {
        goto cleanup;
    }
    consumer_ctx.use_driver = false;
    consumer_ctx.use_conductor_polling = false;
    consumer_ctx.stream_id = 10000;
    consumer_ctx.consumer_id = 2;
    consumer_ctx.driver_request.expected_layout_version = TP_LAYOUT_VERSION;
    consumer_ctx.driver_request.publish_mode = tensor_pool_publishMode_REQUIRE_EXISTING;

    if (tp_consumer_init(&consumer, client, &consumer_ctx) < 0)
    {
        goto cleanup;
    }

    step = 2;
    if (tp_test_consumer_attach_async_with_work(&driver, client, consumer, 5 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_consumer_close(consumer);
    tp_producer_close(producer);
    tp_client_close(client);
    tp_context_close(ctx);
    tp_driver_close(&driver);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_async_attach_wrappers failed at step %d: %s\n", step, tp_errmsg());
    }

    assert(result == 0);
}

void tp_test_driver_blocking_attach_wrappers(void)
{
    tp_driver_config_t driver_config;
    tp_driver_t driver;
    tp_driver_agent_t driver_agent;
    tp_context_t *ctx = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver_client = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    int result = -1;
    int step = 0;
    int driver_agent_started = 0;

    memset(&driver, 0, sizeof(driver));
    memset(&request, 0, sizeof(request));
    memset(&info, 0, sizeof(info));

    if (tp_driver_config_init(&driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_config_load(&driver_config, TP_TEST_CONFIG_PATH("driver_integration_example.toml")) < 0)
    {
        goto cleanup;
    }
    strncpy(driver_config.shm_namespace, "test-blocking", sizeof(driver_config.shm_namespace) - 1);

    if (tp_driver_init(&driver, &driver_config) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_start(&driver) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_agent_init(&driver_agent, &driver, 1000000ULL) < 0)
    {
        goto cleanup;
    }
    if (tp_driver_agent_start(&driver_agent) < 0)
    {
        tp_driver_agent_close(&driver_agent);
        goto cleanup;
    }
    driver_agent_started = 1;

    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }
    tp_context_set_use_agent_invoker(ctx, true);
    tp_context_set_control_channel(
        ctx,
        tp_context_get_control_channel(driver.config.base),
        tp_context_get_control_stream_id(driver.config.base));
    tp_context_set_announce_channel(
        ctx,
        tp_context_get_announce_channel(driver.config.base),
        tp_context_get_announce_stream_id(driver.config.base));
    tp_context_set_descriptor_channel(
        ctx,
        tp_context_get_descriptor_channel(driver.config.base),
        tp_context_get_descriptor_stream_id(driver.config.base));
    tp_context_set_qos_channel(
        ctx,
        tp_context_get_qos_channel(driver.config.base),
        tp_context_get_qos_stream_id(driver.config.base));
    tp_context_set_metadata_channel(
        ctx,
        tp_context_get_metadata_channel(driver.config.base),
        tp_context_get_metadata_stream_id(driver.config.base));

    if (tp_client_init(&client, ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_client_start(client) < 0)
    {
        goto cleanup;
    }
    {
        static const char *allowed_paths[] = { "/dev/shm", "/tmp" };
        tp_context_set_allowed_paths(ctx, allowed_paths, 2);
    }
    if (tp_context_finalize_allowed_paths(ctx) < 0)
    {
        goto cleanup;
    }
    if (tp_context_allowed_paths(ctx)->canonical_length == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_test_driver_blocking_attach_wrappers: allowed paths not configured");
        goto cleanup;
    }

    if (tp_driver_client_init(&driver_client, client) < 0)
    {
        goto cleanup;
    }

    request.stream_id = 10000;
    request.client_id = 0;
    request.role = tensor_pool_role_PRODUCER;
    request.expected_layout_version = TP_LAYOUT_VERSION;
    request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
    request.require_hugepages = 0;
    request.desired_node_id = 0;

    step = 1;
    if (tp_driver_attach(driver_client, &request, &info, 5 * 1000 * 1000 * 1000LL) < 0)
    {
        goto cleanup;
    }
    if (info.code != tensor_pool_responseCode_OK)
    {
        TP_SET_ERR(EINVAL, "tp_test_driver_blocking_attach_wrappers: attach failed: %s", info.error_message);
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_driver_attach_info_close(&info);
    if (driver_agent_started)
    {
        tp_driver_agent_stop(&driver_agent);
        tp_driver_agent_close(&driver_agent);
    }
    tp_driver_client_close(driver_client);
    tp_client_close(client);
    tp_context_close(ctx);
    tp_driver_close(&driver);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_driver_blocking_attach_wrappers failed at step %d: %s\n", step, tp_errmsg());
    }

    assert(result == 0);
}
