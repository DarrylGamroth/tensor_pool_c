#include "tensor_pool/internal/tp_producer_internal.h"
#include "tensor_pool/internal/tp_consumer_internal.h"
#include "tensor_pool/internal/tp_driver_client_internal.h"

#include <assert.h>
#include <string.h>

static void test_producer_accessors(void)
{
    tp_producer_t producer;
    tp_publication_t *descriptor_pub = (tp_publication_t *)0x1;
    tp_publication_t *control_pub = (tp_publication_t *)0x2;
    tp_publication_t *qos_pub = (tp_publication_t *)0x3;
    tp_publication_t *metadata_pub = (tp_publication_t *)0x4;
    bool has_consumers = true;

    memset(&producer, 0, sizeof(producer));
    producer.descriptor_publication = descriptor_pub;
    producer.control_publication = control_pub;
    producer.qos_publication = qos_pub;
    producer.metadata_publication = metadata_pub;

    assert(tp_producer_descriptor_publication(&producer) == descriptor_pub);
    assert(tp_producer_control_publication(&producer) == control_pub);
    assert(tp_producer_qos_publication(&producer) == qos_pub);
    assert(tp_producer_metadata_publication(&producer) == metadata_pub);
    assert(tp_producer_has_consumers(&producer, &has_consumers) == 0);
    assert(has_consumers == false);
}

static void test_consumer_accessors(void)
{
    tp_consumer_t consumer;
    tp_subscription_t *descriptor_sub = (tp_subscription_t *)0x10;
    tp_subscription_t *control_sub = (tp_subscription_t *)0x11;
    tp_publication_t *control_pub = (tp_publication_t *)0x12;
    tp_publication_t *qos_pub = (tp_publication_t *)0x13;

    memset(&consumer, 0, sizeof(consumer));
    consumer.descriptor_subscription = descriptor_sub;
    consumer.control_subscription = control_sub;
    consumer.control_publication = control_pub;
    consumer.qos_publication = qos_pub;
    consumer.assigned_descriptor_stream_id = 42;
    consumer.assigned_control_stream_id = 84;

    assert(tp_consumer_descriptor_subscription(&consumer) == descriptor_sub);
    assert(tp_consumer_control_subscription(&consumer) == control_sub);
    assert(tp_consumer_control_publication(&consumer) == control_pub);
    assert(tp_consumer_qos_publication(&consumer) == qos_pub);
    assert(tp_consumer_assigned_descriptor_stream_id(&consumer) == 42);
    assert(tp_consumer_assigned_control_stream_id(&consumer) == 84);
}

static void test_driver_client_accessors(void)
{
    tp_driver_client_t driver;
    tp_publication_t *publication = (tp_publication_t *)0x21;

    memset(&driver, 0, sizeof(driver));
    driver.active_lease_id = 9;
    driver.active_stream_id = 100;
    driver.client_id = 77;
    driver.role = 4;
    driver.publication = publication;

    assert(tp_driver_client_active_lease_id(&driver) == 9);
    assert(tp_driver_client_active_stream_id(&driver) == 100);
    assert(tp_driver_client_id(&driver) == 77);
    assert(tp_driver_client_role(&driver) == 4);
    assert(tp_driver_client_publication(&driver) == publication);
}

static void test_consumer_handler_helpers(void)
{
    tp_consumer_t consumer;
    tp_frame_descriptor_handler_t descriptor_handler = (tp_frame_descriptor_handler_t)0x1;
    tp_frame_progress_handler_t progress_handler = (tp_frame_progress_handler_t)0x2;

    memset(&consumer, 0, sizeof(consumer));
    tp_consumer_set_descriptor_handler_self(&consumer, descriptor_handler);
    assert(consumer.descriptor_handler == descriptor_handler);
    assert(consumer.descriptor_clientd == &consumer);

    consumer.client = (tp_client_t *)0x1;
    consumer.progress_poller_initialized = true;
    tp_consumer_set_progress_handler_self(&consumer, progress_handler);
    assert(consumer.progress_handler == progress_handler);
    assert(consumer.progress_clientd == &consumer);
}

void tp_test_accessors(void)
{
    test_producer_accessors();
    test_consumer_accessors();
    test_driver_client_accessors();
    test_consumer_handler_helpers();
}
