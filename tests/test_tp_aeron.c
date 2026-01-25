#include "tensor_pool/tp_aeron.h"
#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_error.h"
#include "tp_aeron_wrap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void test_aeron_client_null_inputs(void)
{
    tp_aeron_client_t client;
    tp_context_t context;
    tp_publication_t *pub = NULL;
    tp_subscription_t *sub = NULL;

    memset(&client, 0, sizeof(client));
    memset(&context, 0, sizeof(context));

    assert(tp_aeron_client_init(NULL, &context) < 0);
    assert(tp_aeron_client_init(&client, NULL) < 0);
    assert(tp_aeron_client_close(NULL) < 0);

    assert(tp_aeron_add_publication(NULL, &client, "aeron:ipc", 1) < 0);
    assert(tp_aeron_add_publication(&pub, NULL, "aeron:ipc", 1) < 0);
    assert(tp_aeron_add_publication(&pub, &client, NULL, 1) < 0);
    assert(tp_aeron_add_publication(&pub, &client, "aeron:ipc", 1) < 0);

    assert(tp_aeron_add_subscription(NULL, &client, "aeron:ipc", 1) < 0);
    assert(tp_aeron_add_subscription(&sub, NULL, "aeron:ipc", 1) < 0);
    assert(tp_aeron_add_subscription(&sub, &client, NULL, 1) < 0);
    assert(tp_aeron_add_subscription(&sub, &client, "aeron:ipc", 1) < 0);
}

static void test_aeron_client_init_success(void)
{
    tp_aeron_client_t client;
    tp_context_t context;
    tp_publication_t *pub = NULL;
    tp_subscription_t *sub = NULL;
    const char *aeron_dir = getenv("AERON_DIR");
    int result = -1;

    memset(&client, 0, sizeof(client));
    memset(&context, 0, sizeof(context));

    assert(tp_context_init(&context) == 0);
    if (aeron_dir && aeron_dir[0] != '\0')
    {
        tp_context_set_aeron_dir(&context, aeron_dir);
    }

    if (tp_aeron_client_init(&client, &context) < 0)
    {
        goto cleanup;
    }

    if (tp_aeron_add_publication(&pub, &client, "aeron:ipc", 1001) < 0)
    {
        goto cleanup;
    }

    if (tp_aeron_add_subscription(
        &sub,
        &client,
        "aeron:ipc",
        1001) < 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    tp_subscription_close(&sub);
    tp_publication_close(&pub);
    tp_aeron_client_close(&client);
    assert(result == 0);
}

void tp_test_aeron_client(void)
{
    test_aeron_client_null_inputs();
    test_aeron_client_init_success();
}
