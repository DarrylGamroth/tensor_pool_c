#include "tensor_pool/tp_client_conductor.h"
#include "tensor_pool/tp_context.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void test_client_conductor_lifecycle(void)
{
    tp_context_t context;
    tp_client_conductor_t conductor;
    int result = -1;

    if (tp_context_init(&context) != 0)
    {
        goto cleanup;
    }

    {
        const char *aeron_dir = getenv("AERON_DIR");
        if (NULL != aeron_dir && aeron_dir[0] != '\0')
        {
            tp_context_set_aeron_dir(&context, aeron_dir);
        }
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
