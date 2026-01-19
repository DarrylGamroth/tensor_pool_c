#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_tensor.h"

#include <assert.h>
#include <string.h>

static void test_producer_offer_errors(void)
{
    tp_producer_t producer;
    tp_frame_t frame;
    tp_tensor_header_t tensor;

    memset(&producer, 0, sizeof(producer));
    memset(&frame, 0, sizeof(frame));
    memset(&tensor, 0, sizeof(tensor));

    assert(tp_producer_offer_frame(NULL, NULL, NULL) < 0);

    frame.tensor = &tensor;
    frame.payload = NULL;
    frame.payload_len = 0;
    frame.pool_id = 0;

    assert(tp_producer_offer_frame(&producer, &frame, NULL) < 0);

    producer.header_nslots = 4;
    producer.pool_count = 0;
    producer.header_region.addr = NULL;
    assert(tp_producer_offer_frame(&producer, &frame, NULL) < 0);
}

static void test_producer_claim_errors(void)
{
    tp_producer_t producer;
    tp_buffer_claim_t claim;

    memset(&producer, 0, sizeof(producer));
    memset(&claim, 0, sizeof(claim));

    assert(tp_producer_try_claim(NULL, 0, NULL) < 0);
    assert(tp_producer_try_claim(&producer, 64, &claim) < 0);

    assert(tp_producer_commit_claim(NULL, NULL, NULL) < 0);
    assert(tp_producer_commit_claim(&producer, NULL, NULL) < 0);
    assert(tp_producer_abort_claim(NULL, NULL) < 0);
    assert(tp_producer_abort_claim(&producer, NULL) < 0);
    assert(tp_producer_queue_claim(NULL, NULL) < 0);
    assert(tp_producer_queue_claim(&producer, NULL) < 0);
}

static void test_producer_publication_errors(void)
{
    tp_producer_t producer;
    tp_frame_progress_t progress;

    memset(&producer, 0, sizeof(producer));
    memset(&progress, 0, sizeof(progress));

    assert(tp_producer_offer_progress(&producer, &progress) < 0);
}

void tp_test_producer_errors(void)
{
    test_producer_offer_errors();
    test_producer_claim_errors();
    test_producer_publication_errors();
}
