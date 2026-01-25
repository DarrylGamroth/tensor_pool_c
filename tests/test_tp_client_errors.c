#include "tensor_pool/internal/tp_client_internal.h"
#include "tensor_pool/internal/tp_driver_client_internal.h"

#include <assert.h>
#include <string.h>

static void test_client_context_null_inputs(void)
{
    assert(tp_client_context_init(NULL) < 0);
}

static void test_client_null_inputs(void)
{
    tp_client_context_t context;

    memset(&context, 0, sizeof(context));

    assert(tp_client_init(NULL, &context) < 0);
    assert(tp_client_init((tp_client_t **)1, NULL) < 0);
    assert(tp_client_start(NULL) < 0);
    assert(tp_client_do_work(NULL) < 0);
    assert(tp_client_close(NULL) < 0);
    assert(tp_client_register_driver_client(NULL, NULL) < 0);
    assert(tp_client_unregister_driver_client(NULL, NULL) < 0);
    assert(tp_client_async_add_publication(NULL, "aeron:ipc", 10, NULL) < 0);
    assert(tp_client_async_add_subscription(NULL, "aeron:ipc", 10, NULL) < 0);
}

static void test_client_driver_registration(void)
{
    tp_client_t client;
    tp_driver_client_t driver;

    memset(&client, 0, sizeof(client));
    memset(&driver, 0, sizeof(driver));

    assert(tp_client_register_driver_client(&client, &driver) == 0);
    assert(driver.registered);
    assert(client.driver_clients == &driver);

    assert(tp_client_register_driver_client(&client, &driver) == 0);
    assert(driver.registered);

    assert(tp_client_unregister_driver_client(&client, &driver) == 0);
    assert(!driver.registered);
    assert(client.driver_clients == NULL);

    assert(tp_client_unregister_driver_client(&client, &driver) < 0);
}

static void test_client_async_add_nulls(void)
{
    tp_client_t client;
    tp_async_add_publication_t *pub = NULL;
    tp_async_add_subscription_t *sub = NULL;

    memset(&client, 0, sizeof(client));

    assert(tp_client_async_add_publication(&client, NULL, 10, &pub) < 0);
    assert(tp_client_async_add_publication(&client, "aeron:ipc", 10, NULL) < 0);

    assert(tp_client_async_add_subscription(&client, NULL, 10, &sub) < 0);
    assert(tp_client_async_add_subscription(&client, "aeron:ipc", 10, NULL) < 0);
}

void tp_test_client_errors(void)
{
    test_client_context_null_inputs();
    test_client_null_inputs();
    test_client_driver_registration();
    test_client_async_add_nulls();
}
