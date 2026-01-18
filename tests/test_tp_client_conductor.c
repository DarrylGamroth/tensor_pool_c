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

    if (tp_client_conductor_do_work(&conductor) < 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_client_conductor_close(&conductor);
    assert(result == 0);
}

void tp_test_client_conductor_lifecycle(void)
{
    test_client_conductor_lifecycle();
}
