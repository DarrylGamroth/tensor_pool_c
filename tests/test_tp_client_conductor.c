#include "tensor_pool/tp_client_conductor.h"
#include "tensor_pool/tp_context.h"

#include "aeronc.h"
#include "aeron_common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int tp_test_driver_active(const char *aeron_dir)
{
    aeron_cnc_t *cnc = NULL;
    int64_t heartbeat = 0;
    int64_t now_ms = 0;
    int64_t age_ms = 0;

    if (NULL == aeron_dir || aeron_dir[0] == '\0')
    {
        return 0;
    }

    if (aeron_cnc_init(&cnc, aeron_dir, 100) < 0)
    {
        return 0;
    }

    heartbeat = aeron_cnc_to_driver_heartbeat(cnc);
    now_ms = aeron_epoch_clock();
    age_ms = now_ms - heartbeat;

    aeron_cnc_close(cnc);
    return heartbeat > 0 && age_ms <= 1000;
}

static int tp_test_poller(void *clientd, int fragment_limit)
{
    int *count = (int *)clientd;

    (void)fragment_limit;
    if (count)
    {
        (*count)++;
    }
    return 1;
}

static void test_client_conductor_lifecycle(void)
{
    tp_context_t context;
    tp_client_conductor_t conductor;
    char default_dir[AERON_MAX_PATH];
    const char *aeron_dir = getenv("AERON_DIR");
    int result = -1;

    memset(&conductor, 0, sizeof(conductor));
    default_dir[0] = '\0';
    if (NULL == aeron_dir || aeron_dir[0] == '\0')
    {
        if (aeron_default_path(default_dir, sizeof(default_dir)) >= 0 && default_dir[0] != '\0')
        {
            aeron_dir = default_dir;
        }
    }
    if (NULL == aeron_dir || aeron_dir[0] == '\0')
    {
        return;
    }
    if (!tp_test_driver_active(aeron_dir))
    {
        return;
    }

    if (tp_context_init(&context) != 0)
    {
        goto cleanup;
    }

    {
        tp_context_set_aeron_dir(&context, aeron_dir);
    }

    if (tp_client_conductor_init(&conductor, &context, true) != 0)
    {
        goto cleanup;
    }

    if (tp_client_conductor_set_idle_sleep_duration_ns(&conductor, 1000000) != 0)
    {
        goto cleanup;
    }

    if (tp_client_conductor_start(&conductor) != 0)
    {
        goto cleanup;
    }

    {
        int poll_count = 0;
        if (tp_client_conductor_register_poller(&conductor, tp_test_poller, &poll_count, 1) != 0)
        {
            goto cleanup;
        }
        if (tp_client_conductor_do_work(&conductor) < 0)
        {
            tp_client_conductor_unregister_poller(&conductor, tp_test_poller, &poll_count);
            goto cleanup;
        }
        if (poll_count <= 0)
        {
            tp_client_conductor_unregister_poller(&conductor, tp_test_poller, &poll_count);
            goto cleanup;
        }
        tp_client_conductor_unregister_poller(&conductor, tp_test_poller, &poll_count);
    }

    if (tp_client_conductor_do_work(&conductor) < 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_client_conductor_close(&conductor);
    assert(result == 0);
}

static void test_client_conductor_errors(void)
{
    tp_client_conductor_t conductor;

    memset(&conductor, 0, sizeof(conductor));

    assert(tp_client_conductor_init(NULL, NULL, false) < 0);
    assert(tp_client_conductor_init_with_client_context(NULL, NULL) < 0);
    assert(tp_client_conductor_init_with_aeron(NULL, NULL, false, false) < 0);
    assert(tp_client_conductor_start(NULL) < 0);
    assert(tp_client_conductor_do_work(NULL) < 0);
    assert(tp_client_conductor_set_idle_sleep_duration_ns(NULL, 1) < 0);
    assert(tp_client_conductor_async_add_publication(NULL, NULL, "aeron:ipc", 1) < 0);
    {
        tp_async_add_publication_t *async = NULL;
        assert(tp_client_conductor_async_add_publication(&async, NULL, "aeron:ipc", 1) < 0);
    }
    assert(tp_client_conductor_async_add_publication_poll(NULL, NULL) < 0);
    assert(tp_client_conductor_async_add_subscription(NULL, NULL, "aeron:ipc", 1) < 0);
    {
        tp_async_add_subscription_t *async = NULL;
        assert(tp_client_conductor_async_add_subscription(&async, NULL, "aeron:ipc", 1) < 0);
    }
    assert(tp_client_conductor_async_add_subscription_poll(NULL, NULL) < 0);

    assert(tp_client_conductor_start(&conductor) < 0);
    assert(tp_client_conductor_do_work(&conductor) < 0);
    assert(tp_client_conductor_set_idle_sleep_duration_ns(&conductor, 1) < 0);
}

void tp_test_client_conductor_lifecycle(void)
{
    test_client_conductor_lifecycle();
    test_client_conductor_errors();
}
