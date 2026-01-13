# C Client API Usage Guide

Authoritative references:
- `docs/AERON_LIKE_API_PROPOSAL.md`
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`

This guide focuses on practical usage patterns and callback flow. The API uses TensorPool enums (`tp_mode_t`, `tp_progress_state_t`) so SBE symbols do not leak into application code.

Include the umbrella header for client-facing APIs:

```c
#include "tensor_pool/tp.h"
```

## 1. Client Setup

```c
tp_client_context_t ctx;
tp_client_t client;

tp_client_context_init(&ctx);
tp_client_context_set_aeron_dir(&ctx, "/dev/shm/aeron-dgamroth");
tp_client_context_set_control_channel(&ctx, "aeron:ipc", 1000);
tp_client_context_set_descriptor_channel(&ctx, "aeron:ipc", 1100);
tp_client_context_set_qos_channel(&ctx, "aeron:ipc", 1200);
tp_client_context_set_metadata_channel(&ctx, "aeron:ipc", 1300);

tp_client_init(&client, &ctx);
tp_client_start(&client);
```

Call `tp_client_do_work(&client)` in your poll loop to drive keepalives and conductor work.

## 2. Discovery → Consumer (Driver Model)

```c
tp_discovery_context_t discovery_ctx;
tp_discovery_client_t discovery;
tp_discovery_request_t request;
tp_discovery_response_t response;

tp_discovery_context_init(&discovery_ctx);
tp_discovery_context_set_channel(&discovery_ctx, "aeron:ipc", 9000);
tp_discovery_client_init(&discovery, &client, &discovery_ctx);

memset(&request, 0, sizeof(request));
request.request_id = 1;
request.match_stream_id = TP_NULL_U32;
request.max_results = 4;
tp_discovery_request(&discovery, &request);
tp_discovery_poll(&discovery, request.request_id, &response, 5 * 1000 * 1000 * 1000LL);

// Pick a stream id.
const tp_discovery_result_t *result = response.result_count ? &response.results[0] : NULL;
```

## 3. Consumer: Descriptor Callback Path (12.1)

The descriptor callback is the primary hook for reading frames.

```c
static void on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_consumer_t *consumer = (tp_consumer_t *)clientd;
    tp_frame_view_t view;

    if (tp_consumer_read_frame(consumer, desc->seq, &view) == 0)
    {
        // view.payload/view.payload_len are valid here.
        // desc->trace_id carries the producer trace id (0 if unset).
    }
}

tp_consumer_context_t consumer_ctx;
tp_consumer_t consumer;

tp_consumer_context_init(&consumer_ctx);
consumer_ctx.stream_id = result ? result->stream_id : 10000;
consumer_ctx.consumer_id = 42;
consumer_ctx.use_driver = true;
consumer_ctx.hello.descriptor_channel = "aeron:ipc";
consumer_ctx.hello.descriptor_stream_id = 31001;

tp_consumer_init(&consumer, &client, &consumer_ctx);
tp_consumer_set_descriptor_handler(&consumer, on_descriptor, &consumer);

while (running)
{
    tp_consumer_poll_control(&consumer, 10);
    tp_consumer_poll_descriptors(&consumer, 10);
}
```

Notes:
- `tp_consumer_init` auto-attaches when `use_driver` is true.
- `tp_consumer_attach` (direct SHM) sends `ConsumerHello` on success.

## 4. Producer: Driver Model

```c
tp_producer_context_t prod_ctx;
tp_producer_t producer;

tp_producer_context_init(&prod_ctx);
prod_ctx.stream_id = 10000;
prod_ctx.producer_id = 7;
prod_ctx.use_driver = true;
prod_ctx.fixed_pool_mode = true;

tp_producer_init(&producer, &client, &prod_ctx);
tp_producer_enable_consumer_manager(&producer, 128);

while (running)
{
    tp_producer_poll_control(&producer, 10);
    (void)tp_producer_offer_frame(&producer, &frame, &meta);
}
```

## 5. Zero-Copy Try-Claim

```c
tp_buffer_claim_t claim;
tp_frame_metadata_t meta;
int64_t result;

result = tp_producer_try_claim(&producer, payload_len, &claim);
if (result >= 0)
{
    // claim.payload points at the slot; fill directly (DMA/SDK).
    tp_producer_commit_claim(&producer, &claim, &meta);
}
```

## 6. BGAPI-Style Fixed Pool Queue

```c
tp_buffer_claim_t slots[8];

for (size_t i = 0; i < 8; ++i)
{
    tp_producer_try_claim(&producer, payload_len, &slots[i]);
}

// When a buffer is filled:
tp_producer_commit_claim(&producer, &slots[i], &meta);
tp_producer_queue_claim(&producer, &slots[i]); // re-queue same slot
```

## 7. QoS

```c
tp_qos_publish_consumer(&consumer, consumer_id, last_seq_seen, drops_gap, drops_late, TP_MODE_STREAM);

tp_qos_handlers_t qos_handlers = {
    .on_qos_event = on_qos_event,
    .clientd = NULL
};
tp_qos_poller_t qos_poller;
tp_qos_poller_init(&qos_poller, &client, &qos_handlers);
tp_qos_poll(&qos_poller, 10);
```

## 8. Metadata

```c
// Optional: set a cadence (default is 1s).
tp_client_context_set_announce_period_ns(&ctx, 1000 * 1000 * 1000ULL);

tp_data_source_announce_t announce = {
    .stream_id = stream_id,
    .producer_id = producer_id,
    .epoch = epoch,
    .meta_version = 1,
    .name = "camera0",
    .summary = "primary sensor"
};
tp_producer_send_data_source_announce(&producer, &announce);

// Cache announce/meta for periodic re-broadcast on tp_producer_poll_control().
tp_producer_set_data_source_announce(&producer, &announce);

tp_metadata_handlers_t meta_handlers = {
    .on_data_source_announce = on_announce,
    .on_data_source_meta_begin = on_meta_begin,
    .on_data_source_meta_attr = on_meta_attr,
    .on_data_source_meta_end = on_meta_end,
    .clientd = NULL
};
tp_metadata_poller_t meta_poller;
tp_metadata_poller_init(&meta_poller, &client, &meta_handlers);
tp_metadata_poll(&meta_poller, 10);

// Example metadata publish + cache.
uint32_t fmt = 1;
tp_meta_attribute_t attrs[1] = {
    { .key = "pixel_format", .format = "u32", .value = (const uint8_t *)&fmt, .value_length = sizeof(fmt) }
};
tp_data_source_meta_t meta = {
    .stream_id = stream_id,
    .meta_version = 1,
    .timestamp_ns = tp_clock_now_ns(),
    .attributes = attrs,
    .attribute_count = 1
};
tp_producer_send_data_source_meta(&producer, &meta);
tp_producer_set_data_source_meta(&producer, &meta);
```

## 9. Progress

```c
tp_frame_progress_t progress = {
    .seq = seq,
    .payload_bytes_filled = bytes,
    .state = TP_PROGRESS_STARTED
};
tp_producer_offer_progress(&producer, &progress);

tp_progress_handlers_t progress_handlers = {
    .on_progress = on_progress,
    .clientd = NULL
};
tp_progress_poller_t progress_poller;
tp_progress_poller_init(&progress_poller, &client, &progress_handlers);
tp_progress_poll(&progress_poller, 10);
```

If the consumer receives a per-consumer control stream in `ConsumerConfig`, use `tp_progress_poller_init_with_subscription` with the assigned subscription.

## 10. Error Semantics

- `init/close/poll` APIs return `0` on success and `-1` on error.
- Offer/claim/queue functions return `>= 0` on success (position/seq) or negative backpressure/admin codes (`TP_BACK_PRESSURED`, `TP_NOT_CONNECTED`, `TP_ADMIN_ACTION`, `TP_CLOSED`).
