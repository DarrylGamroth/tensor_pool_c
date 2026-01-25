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
const char *allowed_paths[] = { "/dev/shm", "/tmp" };
tp_context_set_allowed_paths(&ctx.base, allowed_paths, 2);

tp_client_init(&client, &ctx);
tp_client_start(&client);
```

Shared-memory mappings require an allowlist of base directories (`allowed_paths`) to satisfy the containment rules in the wire spec. If no allowlist is configured, SHM mappings are rejected.

By default, SHM mappings enforce restrictive permissions (no group/other write). Use `tp_context_set_shm_permissions` to relax or customize the checks:

```c
tp_context_set_shm_permissions(&ctx.base, false, TP_NULL_U32, TP_NULL_U32, 0);
```

Noncanonical SHM paths from `tools/tp_shm_create --noncanonical` are test-only and require `--allow-noncompliant`; production deployments should use the canonical directory layout.

Call `tp_client_do_work(&client)` in your poll loop to drive keepalives, conductor command processing, and any registered pollers.

### 1.1 Conductor Execution Model (Agent Invoker)

TensorPool mirrors Aeron’s invoker pattern. Choose one of two modes:

- Default (threaded conductor): do nothing special; Aeron runs its own conductor thread after `tp_client_start`.
- Agent invoker (single-threaded): set `tp_client_context_set_use_agent_invoker(&ctx, true)` and call `tp_client_do_work` frequently to drive the Aeron conductor in your loop.

Example (agent invoker + shared pollers):

```c
tp_client_context_t ctx;
tp_client_t client;
tp_control_handlers_t control_handlers = {0};
tp_qos_handlers_t qos_handlers = {0};
tp_metadata_handlers_t metadata_handlers = {0};

tp_client_context_init(&ctx);
tp_client_context_set_use_agent_invoker(&ctx, true);

tp_client_init(&client, &ctx);
tp_client_start(&client);

tp_client_set_control_handlers(&client, &control_handlers, 10);
tp_client_set_qos_handlers(&client, &qos_handlers, 10);
tp_client_set_metadata_handlers(&client, &metadata_handlers, 10);

while (running)
{
    tp_client_do_work(&client);
}
```

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

Discovery results are advisory; clients must still attach via the driver and handle rejections or stream reassignments.

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
    tp_client_do_work(&client);
    tp_consumer_poll_descriptors(&consumer, 10);
}
```

Notes:
- `tp_consumer_init` auto-attaches when `use_driver` is true.
- `tp_consumer_attach` (direct SHM) sends `ConsumerHello` on success.
- On `ShmLeaseRevoked`, `tp_consumer_reattach_due` can be used to retry attach with backoff.
- Invalid `ShmPoolAnnounce` or SHM mapping failures transition to fallback when `payload_fallback_uri` is configured.
- In driver model, clients must not create/truncate/unlink SHM files; the driver owns SHM lifecycles.
- Consumers MUST remain subscribed to the shared control stream for non-FrameProgress control-plane messages; per-consumer control streams carry FrameProgress only.

```c
if (tp_consumer_reattach_due(&consumer, (uint64_t)tp_clock_now_ns()))
{
    tp_consumer_attach(&consumer, NULL);
}
```

## 4. Producer: Driver Model

```c
tp_producer_context_t prod_ctx;
tp_producer_t producer;

tp_producer_context_init(&prod_ctx);
prod_ctx.stream_id = 10000;
prod_ctx.producer_id = 7;
prod_ctx.use_driver = true;
prod_ctx.fixed_pool_mode = true;
tp_producer_context_set_payload_flush(&prod_ctx, flush_fn, flush_clientd); // optional DMA visibility hook
// For non-coherent DMA, provide flush_fn to make payload visible before commit.

tp_producer_init(&producer, &client, &prod_ctx);
tp_producer_enable_consumer_manager(&producer, 128);

while (running)
{
    tp_client_do_work(&client);
    (void)tp_producer_offer_frame(&producer, &frame, &meta);
}
```

Lease revoked handling (driver model):
```c
if (tp_producer_reattach_due(&producer, (uint64_t)tp_clock_now_ns()))
{
    tp_producer_attach(&producer, NULL);
}
```

Notes:
- `tp_frame_metadata_t.timestamp_ns` is capture time for `SlotHeader.timestamp_ns`. If it is `0` or `TP_NULL_U64`, the producer fills `SlotHeader.timestamp_ns` with `tp_clock_now_ns()`.
- `FrameDescriptor.timestamp_ns` is null by default. Enable publish-time timestamps with `tp_producer_context_set_publish_descriptor_timestamp(&prod_ctx, true)` when needed.

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

## 7. Trace IDs + TraceLinkSet

```c
tp_trace_id_generator_t trace_gen;
tp_frame_t frame;
uint64_t node_id = (producer.driver_attach.node_id != TP_NULL_U32)
    ? producer.driver_attach.node_id
    : 42;

tp_trace_id_generator_init_default(&trace_gen, node_id);
tp_producer_set_trace_id_generator(&producer, &trace_gen);

memset(&frame, 0, sizeof(frame));
// Populate frame.tensor/frame.payload/frame.payload_len before publishing.
// pool_id is ignored by tp_producer_offer_frame (smallest-fit pool selection is automatic).
frame.trace_id = 0;
tp_frame_metadata_t meta = { .timestamp_ns = 0, .meta_version = 0 };
tp_producer_offer_frame(&producer, &frame, &meta);
// If frame.trace_id is 0 and a generator is set, a trace_id is minted and written into FrameDescriptor.trace_id.
// For try-claim paths, set claim.trace_id before tp_producer_commit_claim (0 uses the generator when configured).
```

Try-claim example:

```c
tp_buffer_claim_t claim;

if (tp_producer_try_claim(&producer, payload_len, &claim) >= 0)
{
    // Fill claim.payload/claim.tensor as needed.
    claim.trace_id = 0; // or set a specific trace id
    tp_producer_commit_claim(&producer, &claim, &meta);
}
```

N→1 stages mint a new trace id and emit TraceLinkSet:

```c
uint64_t parent_ids[2] = { left_trace_id, right_trace_id };
uint64_t out_trace_id = 0;
int emit_tracelink = 0;
if (tp_tracelink_resolve_trace_id(&trace_gen, parent_ids, 2, &out_trace_id, &emit_tracelink) < 0)
{
    // handle error
}
frame.trace_id = out_trace_id;
tp_frame_metadata_t out_meta = { .timestamp_ns = 0, .meta_version = 0 };
int64_t seq = tp_producer_offer_frame(&producer, &frame, &out_meta);
// frame.trace_id is encoded into FrameDescriptor.trace_id for the output frame.

Note: for N→1 stages, set `frame.trace_id` explicitly so you can emit TraceLinkSet for the derived output.

tp_tracelink_set_t link = {
    .stream_id = producer.stream_id,
    .epoch = producer.epoch,
    .seq = (uint64_t)seq,
    .trace_id = out_trace_id,
    .parents = parent_ids,
    .parent_count = 2
};
if (emit_tracelink)
{
    tp_producer_send_tracelink_set(&producer, &link);
}
```

Convenience form (fills stream/epoch automatically):

```c
tp_producer_send_tracelink_set_ex(&producer, (uint64_t)seq, out_trace_id, parent_ids, 2);
```

Claim helper:

```c
tp_tracelink_set_t set;

if (tp_tracelink_set_from_claim(&producer, &claim, parent_ids, 2, &set) == 0)
{
    tp_producer_send_tracelink_set(&producer, &set);
}
```

TraceLinkSet validation defaults to checking the most recently published descriptor for the slot. You can override or disable validation:

```c
static int tracelink_validator(const tp_tracelink_set_t *set, void *clientd)
{
    (void)clientd;
    // Return 0 to accept, <0 to reject.
    return 0;
}

tp_producer_set_tracelink_validator(&producer, tracelink_validator, NULL);
```

Control-plane listeners can subscribe to TraceLinkSet events:

```c
static void on_tracelink(const tp_tracelink_set_t *set, void *clientd)
{
    (void)clientd;
    // set->trace_id, set->parents, set->parent_count
}

tp_control_handlers_t handlers = { .on_tracelink_set = on_tracelink, .clientd = NULL };
tp_client_set_control_handlers(&client, &handlers, 10);
tp_client_do_work(&client);
```

Note: trace ID continuity across epoch changes is a deployment choice. The client API does not reset IDs automatically; mint new trace IDs when desired.

## 8. QoS

```c
tp_qos_publish_consumer(&consumer, consumer_id, last_seq_seen, drops_gap, drops_late, TP_MODE_STREAM);

tp_qos_handlers_t qos_handlers = {
    .on_qos_event = on_qos_event,
    .clientd = NULL
};
tp_client_set_qos_handlers(&client, &qos_handlers, 10);
tp_client_do_work(&client);
```

## 9. Metadata

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
tp_client_set_metadata_handlers(&client, &meta_handlers, 10);
tp_client_do_work(&client);

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
    .stream_id = stream_id,
    .epoch = epoch,
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

When a `ConsumerConfig` sets `use_shm=0`, the consumer disables SHM mapping and exposes the configured fallback URI. Use this to switch to an external payload path (e.g., a bridge):

```c
if (!tp_consumer_uses_shm(consumer))
{
    const char *fallback = tp_consumer_payload_fallback_uri(consumer);
    // Connect to fallback transport using fallback (may be empty if not provided).
}
```

Producers can force fallback for specific deployments by configuring the consumer manager before handling hellos:

```c
tp_consumer_manager_set_payload_fallback_uri(&manager, "aeron:udp?endpoint=10.0.0.2:40125");
tp_consumer_manager_set_force_no_shm(&manager, true);
```

## 10. MergeMap and JoinBarrier

MergeMap announcements are control-plane messages. Apply them to a JoinBarrier and use
JoinBarrier readiness to gate processing attempts. You can either decode/apply manually or let
the control poller auto-apply to the JoinBarrier.

```c
tp_join_barrier_t barrier;
tp_join_barrier_init(&barrier, TP_JOIN_BARRIER_SEQUENCE, 16);
tp_join_barrier_set_allow_stale(&barrier, true);
tp_join_barrier_set_require_processed(&barrier, false);

// Update cursors from FrameDescriptor callbacks.
tp_join_barrier_update_observed_seq(&barrier, input_stream_id, desc->seq, tp_clock_now_ns());
// If require_processed is true, update processed cursor too.
tp_join_barrier_update_processed_seq(&barrier, input_stream_id, processed_seq, tp_clock_now_ns());

if (tp_join_barrier_is_ready_sequence(&barrier, out_seq, tp_clock_now_ns()) == 1)
{
    // Proceed to attempt processing for out_seq.
}

// Optional: report stale inputs when staleness policy is enabled.
uint32_t stale_inputs[16];
size_t stale_count = 0;
tp_join_barrier_collect_stale_inputs(&barrier, tp_clock_now_ns(), stale_inputs, 16, &stale_count);
```

Timestamp-based joins use `tp_join_barrier_apply_timestamp_map`,
`tp_join_barrier_update_observed_time`, and `tp_join_barrier_is_ready_timestamp` with the declared
clock domain and timestamp source. Output timestamps must be monotonic on every probe:

```c
tp_join_barrier_t ts_barrier;
tp_join_barrier_init(&ts_barrier, TP_JOIN_BARRIER_TIMESTAMP, 8);
tp_join_barrier_apply_timestamp_map(&ts_barrier, &ts_map);
tp_join_barrier_update_observed_time(&ts_barrier, input_stream_id, desc->timestamp_ns,
    TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR, TP_CLOCK_DOMAIN_MONOTONIC, tp_clock_now_ns());
if (tp_join_barrier_is_ready_timestamp(&ts_barrier, out_time_ns, TP_CLOCK_DOMAIN_MONOTONIC, tp_clock_now_ns()) == 1)
{
    // Proceed to attempt processing for out_time_ns.
}
```

WINDOW_NS rules require `latenessNs` to permit lagged inputs. If `latenessNs` is zero, readiness
still requires `observed_time >= out_time`. Set it on the active timestamp map before applying:

```c
ts_map.lateness_ns = 10 * 1000 * 1000; // 10 ms
tp_join_barrier_apply_timestamp_map(&ts_barrier, &ts_map);
```

For LatestValueJoinBarrier, set ordering explicitly. Timestamp ordering requires an active
Timestamp MergeMap so the clock domain and timestamp source are defined:

```c
tp_join_barrier_set_latest_ordering(&latest_barrier, TP_LATEST_ORDERING_TIMESTAMP);
tp_join_barrier_apply_latest_value_timestamp_map(&latest_barrier, &ts_map);
```

When using LatestValueJoinBarrier, invalidate inputs whose selected frame fails validation:

```c
if (tp_consumer_read_frame(consumer, desc->seq, &view) < 0)
{
    tp_join_barrier_invalidate_latest(&latest_barrier, desc->stream_id);
    return;
}
```

To retrieve the selected latest inputs:

```c
tp_latest_selection_t latest[16];
size_t latest_count = 0;
if (tp_join_barrier_collect_latest(&latest_barrier, latest, 16, &latest_count) == 0)
{
    // latest[i].seq/latest[i].timestamp_ns identify the chosen frames.
}
```

You can also wire merge-map application into the control poller:

```c
tp_control_handlers_t handlers = {
    .sequence_join_barrier = &barrier,
    .latest_join_barrier = NULL,
    .timestamp_join_barrier = NULL,
    .clientd = NULL
};
tp_control_poller_t control_poller;
tp_control_poller_init(&control_poller, &client, &handlers);
```

## 11. Error Semantics

- `init/close/poll` APIs return `0` on success and `-1` on error.
- Offer/claim/queue functions return `>= 0` on success (position/seq) or negative backpressure/admin codes (`TP_BACK_PRESSURED`, `TP_NOT_CONNECTED`, `TP_ADMIN_ACTION`, `TP_CLOSED`).

## 12. Tools

- `tp_control_listen`: Inspect control/metadata/qos streams with text or JSON output. Use `--raw` / `--raw-out` to dump hex fragments for offline decoding.
- `tp_descriptor_listen`: Inspect descriptor stream traffic (FrameDescriptor) with JSON or raw output. Useful for verifying producer publish behavior without SHM mapping.
- `tp_shm_inspect`: Inspect SHM superblocks and headers for a given region.

Example (descriptor stream):

```sh
./build/tp_descriptor_listen --json --raw /dev/shm/aeron-dgamroth "aeron:ipc?term-length=4m" 1100
```

Example (control/metadata/qos):

```sh
./build/tp_control_listen --json --raw-out /tmp/tp_control.hex /dev/shm/aeron-dgamroth "aeron:ipc?term-length=4m" 1000
```
