# Aeron-like Client API Proposal

This document proposes an Aeron-style API for TensorPool C clients. The goal is to remove direct Aeron calls from application code, provide ergonomic async add/poll patterns, and centralize control/QoS/metadata/discovery logic behind adapters and pollers.

## 1. Motivation (Current Pain Points)

Observed in `tools/tp_example_consumer.c` / `tools/tp_example_producer.c` / `tools/tp_control_listen.c`:

- Direct Aeron calls (`aeron_subscription_poll`, `aeron_fragment_assembler_create`, `aeron_subscription_close`) leak into app code.
- Control-plane handling (ConsumerHello/ConsumerConfig) is manual and duplicated.
- Per-consumer stream switching requires raw Aeron API usage.
- QoS/metadata subscriptions are ad-hoc instead of a consistent poller API.
- Discovery/driver are separate client structs without a unifying client context lifecycle.

## 2. Design Goals

- Mirror Aeron C client patterns: context, client, async add, pollers/adapters, error handling.
- Move Aeron ownership into TensorPool objects; apps only use TensorPool API.
- Make control/QoS/metadata/discovery first-class with typed handlers.
- Support per-consumer descriptor/progress streams with minimal app logic.
- Preserve wire-spec authoritative behaviors and error semantics.

## 3. Proposed Top-Level API Surface

### 3.1 Context and Client (Aeron-style lifecycle)

```c
// Context holds configuration and defaults.
typedef struct tp_client_context_stct
{
    tp_context_t base;          // existing config (aeron dir, channels, streams)
    uint64_t driver_timeout_ns;
    uint64_t keepalive_interval_ns;
    uint64_t idle_sleep_duration_ns;
    bool use_agent_invoker;
} tp_client_context_t;

int tp_client_context_init(tp_client_context_t *ctx);
void tp_client_context_set_aeron_dir(tp_client_context_t *ctx, const char *dir);
void tp_client_context_set_control_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_descriptor_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_qos_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_metadata_channel(tp_client_context_t *ctx, const char *channel, int32_t stream_id);
void tp_client_context_set_driver_timeout_ns(tp_client_context_t *ctx, uint64_t value);
void tp_client_context_set_keepalive_interval_ns(tp_client_context_t *ctx, uint64_t value);
void tp_client_context_set_idle_sleep_duration_ns(tp_client_context_t *ctx, uint64_t value);
void tp_client_context_set_use_agent_invoker(tp_client_context_t *ctx, bool value);

// Client owns Aeron instance, conductor, and shared pollers.
typedef struct tp_client_stct tp_client_t;

int tp_client_init(tp_client_t *client, const tp_client_context_t *ctx);
int tp_client_start(tp_client_t *client);
int tp_client_do_work(tp_client_t *client);   // Aeron-style if use_agent_invoker
int tp_client_close(tp_client_t *client);
```

### 3.2 Async add and poll semantics (Aeron-like)

```c
// Async add wrappers for consistency with Aeron C.
typedef struct tp_async_add_publication_stct tp_async_add_publication_t;
int tp_client_async_add_publication(tp_client_t *client, const char *channel, int32_t stream_id, tp_async_add_publication_t **out);
int tp_client_async_add_publication_poll(tp_async_add_publication_t *async, aeron_publication_t **out_pub);

typedef struct tp_async_add_subscription_stct tp_async_add_subscription_t;
int tp_client_async_add_subscription(tp_client_t *client, const char *channel, int32_t stream_id, tp_async_add_subscription_t **out);
int tp_client_async_add_subscription_poll(tp_async_add_subscription_t *async, aeron_subscription_t **out_sub);
```

Apps should not call Aeron APIs directly; they use TensorPool wrappers.

## 4. Driver-Model Client (Attach/Keepalive/Detach)

```c
typedef struct tp_driver_client_stct tp_driver_client_t; // existing

int tp_driver_client_init(tp_driver_client_t *driver, tp_client_t *client);
int tp_driver_attach_async(tp_driver_client_t *driver, const tp_driver_attach_request_t *req, tp_async_attach_t **out);
int tp_driver_attach_poll(tp_async_attach_t *async, tp_driver_attach_info_t *out);
int tp_driver_keepalive(tp_driver_client_t *driver, uint64_t timestamp_ns);
int tp_driver_detach_async(tp_driver_client_t *driver, tp_async_detach_t **out);
int tp_driver_detach_poll(tp_async_detach_t *async, tp_driver_detach_info_t *out);

// Event poller (lease revoked / shutdown / detach response)
typedef struct tp_driver_event_poller_stct tp_driver_event_poller_t;
int tp_driver_event_poller_init(tp_driver_event_poller_t *poller, tp_driver_client_t *driver, const tp_driver_event_handlers_t *handlers);
int tp_driver_event_poll(tp_driver_event_poller_t *poller, int fragment_limit);
```

Notes:
- Keepalive should reference stored lease/stream/client/role state in `tp_driver_client_t` to reduce boilerplate.
- Async API mirrors Aeron add semantics and avoids blocking.

## 5. Producer API (Aeron-like)

```c
typedef struct tp_producer_stct tp_producer_t;

typedef struct tp_producer_context_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    bool use_driver;
    tp_driver_attach_request_t driver_request;  // if use_driver
} tp_producer_context_t;

int tp_producer_context_init(tp_producer_context_t *ctx);

int tp_producer_init(tp_producer_t *producer, tp_client_t *client, const tp_producer_context_t *ctx);
int tp_producer_attach(tp_producer_t *producer, const tp_producer_config_t *direct_cfg); // direct SHM path
int tp_producer_close(tp_producer_t *producer);

int tp_producer_offer_frame(tp_producer_t *producer, const tp_frame_t *frame, tp_frame_metadata_t *meta);
int tp_producer_offer_progress(tp_producer_t *producer, const tp_frame_progress_t *progress);

// Per-consumer routing managed internally when enabled.
int tp_producer_enable_consumer_manager(tp_producer_t *producer, size_t capacity);
int tp_producer_poll_control(tp_producer_t *producer, int fragment_limit);
```

Behavior:
- Producer owns descriptor/control/qos/metadata publications; app never touches Aeron pubs.
- `tp_producer_poll_control` uses a control adapter and a consumer manager to handle ConsumerHello, per-consumer streams, and progress throttling.

## 6. Consumer API (Aeron-like)

```c
typedef struct tp_consumer_stct tp_consumer_t;

typedef struct tp_consumer_context_stct
{
    uint32_t stream_id;
    uint32_t consumer_id;
    bool use_driver;
    tp_driver_attach_request_t driver_request;  // if use_driver
    tp_consumer_hello_t hello;                  // optional per-consumer streams and hints
} tp_consumer_context_t;

int tp_consumer_context_init(tp_consumer_context_t *ctx);

int tp_consumer_init(tp_consumer_t *consumer, tp_client_t *client, const tp_consumer_context_t *ctx);
int tp_consumer_attach(tp_consumer_t *consumer, const tp_consumer_config_t *direct_cfg); // direct SHM
int tp_consumer_close(tp_consumer_t *consumer);

// Descriptor polling (FrameDescriptor) -> frame read
int tp_consumer_poll_descriptors(tp_consumer_t *consumer, int fragment_limit);

// Control poller handles ConsumerConfig and per-consumer stream switching
int tp_consumer_poll_control(tp_consumer_t *consumer, int fragment_limit);

// Frame accessors
int tp_consumer_read_frame(tp_consumer_t *consumer, uint64_t seq, uint32_t header_index, tp_frame_view_t *out);
```

Behavior:
- Consumer owns descriptor subscription(s) and switches to per-consumer streams after ConsumerConfig.
- `tp_consumer_poll_control` updates per-consumer channels and swaps subscriptions.
- FrameDescriptor handler inside consumer translates to `tp_consumer_read_frame` callback.

## 7. Control Plane API (Handlers / Pollers)

```c
// Control event handlers in Aeron-style callback form.
typedef struct tp_control_handlers_stct
{
    tp_on_consumer_hello_t on_consumer_hello;
    tp_on_consumer_config_t on_consumer_config;
    tp_on_data_source_announce_t on_data_source_announce;
    tp_on_data_source_meta_begin_t on_data_source_meta_begin;
    tp_on_data_source_meta_attr_t on_data_source_meta_attr;
    tp_on_data_source_meta_end_t on_data_source_meta_end;
    void *clientd;
} tp_control_handlers_t;

int tp_control_poller_init(tp_control_poller_t *poller, tp_client_t *client, const tp_control_handlers_t *handlers);
int tp_control_poll(tp_control_poller_t *poller, int fragment_limit);
```

These replace direct `aeron_subscription_poll` and fragmented message handling in tools/examples.

## 8. QoS API

```c
// QoS poller aggregates QosProducer/QosConsumer
int tp_qos_poller_init(tp_qos_poller_t *poller, tp_client_t *client, const tp_qos_handlers_t *handlers);
int tp_qos_poll(tp_qos_poller_t *poller, int fragment_limit);

// QoS publish helpers (producer/consumer)
int tp_qos_publish_producer(tp_producer_t *producer, uint64_t current_seq, uint32_t watermark);
int tp_qos_publish_consumer(tp_consumer_t *consumer, uint32_t consumer_id, uint64_t last_seq_seen, uint64_t drops_gap, uint64_t drops_late, uint8_t mode);
```

## 9. Metadata API

```c
// Metadata poller uses DataSourceAnnounce/DataSourceMeta handlers
int tp_metadata_poller_init(tp_metadata_poller_t *poller, tp_client_t *client, const tp_metadata_handlers_t *handlers);
int tp_metadata_poll(tp_metadata_poller_t *poller, int fragment_limit);

// Producer helpers for announce/meta
int tp_producer_send_data_source_announce(tp_producer_t *producer, const tp_data_source_announce_t *announce);
int tp_producer_send_data_source_meta(tp_producer_t *producer, const tp_data_source_meta_t *meta);
```

## 10. Discovery API

```c
typedef struct tp_discovery_client_stct tp_discovery_client_t;

int tp_discovery_client_init(tp_discovery_client_t *client, tp_client_t *base, const tp_discovery_context_t *ctx);
int tp_discovery_request(tp_discovery_client_t *client, const tp_discovery_request_t *request);
int tp_discovery_poll(tp_discovery_client_t *client, uint64_t request_id, tp_discovery_response_t *out, int64_t timeout_ns);
```

## 11. Progress API

Progress is a control-plane message. The API should expose:

- Publish helpers for producers (`tp_producer_offer_progress`).
- Consumer progress poller (`tp_progress_poller_init` + `tp_progress_poll`) for per-consumer control streams.
- Throttling policy aggregation in producer manager (already present) but accessed via `tp_producer_get_progress_policy()`.

## 12. Ergonomic Usage Examples

### 12.1 Consumer (Driver Model)

```c
tp_client_context_t ctx;
tp_client_t client;
tp_consumer_context_t consumer_ctx;
tp_consumer_t consumer;
tp_frame_descriptor_handler_t on_descriptor;
tp_discovery_client_t discovery;
tp_discovery_request_t request;
tp_discovery_response_t response;
const tp_discovery_result_t *result;

// Descriptor callback
static void on_descriptor(void *clientd, const tp_frame_descriptor_t *desc)
{
    tp_consumer_t *consumer = (tp_consumer_t *)clientd;
    tp_frame_view_t view;

    if (tp_consumer_read_frame(consumer, desc->seq, desc->header_index, &view) == 0)
    {
        // process view.data/view.data_length here
    }
}

// Init
tp_client_context_init(&ctx);
tp_client_context_set_aeron_dir(&ctx, "/dev/shm/aeron-dgamroth");
tp_client_context_set_control_channel(&ctx, "aeron:ipc", 1000);
tp_client_context_set_descriptor_channel(&ctx, "aeron:ipc", 1100);

// Start client
tp_client_init(&client, &ctx);
tp_client_start(&client);

// Discovery: find a pool and stream id
tp_discovery_request_init(&request);
request.request_id = 1;
request.client_id = 42;
request.response_channel = "aeron:ipc";
request.response_stream_id = 7001;
request.tags = (const char *const[]){"role:vision"};
request.tags_count = 1;

tp_discovery_client_init(&discovery, &client, "aeron:ipc", 7000);
tp_discovery_request(&discovery, &request);
tp_discovery_poll(&discovery, request.request_id, &response, 5 * 1000 * 1000 * 1000LL);

// Select a result (example: first match)
result = &response.results[0];

// Consumer
tp_consumer_context_init(&consumer_ctx);
consumer_ctx.stream_id = result->stream_id;
consumer_ctx.consumer_id = 42;
consumer_ctx.use_driver = true;
consumer_ctx.hello.descriptor_channel = "aeron:ipc";
consumer_ctx.hello.descriptor_stream_id = 31001; // optional per-consumer

tp_consumer_init(&consumer, &client, &consumer_ctx);
tp_consumer_set_descriptor_handler(&consumer, on_descriptor, &consumer);

// Poll loop
while (running)
{
    tp_consumer_poll_control(&consumer, 10);
    tp_consumer_poll_descriptors(&consumer, 10);
}

tp_consumer_close(&consumer);
tp_discovery_response_close(&response);
tp_discovery_client_close(&discovery);
tp_client_close(&client);
```

### 12.2 Producer (Driver Model)

```c
tp_client_context_t ctx;
tp_client_t client;
tp_producer_context_t prod_ctx;
tp_producer_t producer;

tp_client_context_init(&ctx);
tp_client_init(&client, &ctx);
tp_client_start(&client);

tp_producer_context_init(&prod_ctx);
prod_ctx.stream_id = 10000;
prod_ctx.producer_id = 7;
prod_ctx.use_driver = true;

tp_producer_init(&producer, &client, &prod_ctx);
tp_producer_enable_consumer_manager(&producer, 128);

while (running)
{
    tp_producer_poll_control(&producer, 10); // handles ConsumerHello + per-consumer streams
    tp_producer_offer_frame(&producer, &frame, &meta);
}
```

### 12.3 QoS (Producer + Consumer)

```c
// QoS handler (called from tp_qos_poll)
static void on_qos_event(void *clientd, const tp_qos_event_t *event)
{
    (void)clientd;
    // event->type distinguishes producer vs consumer QoS
    // event->producer/current_seq/watermark or event->consumer/last_seq/drops/mode
}

tp_qos_handlers_t qos_handlers =
{
    .on_qos_event = on_qos_event,
    .clientd = NULL
};

tp_qos_poller_t qos_poller;
tp_qos_poller_init(&qos_poller, &client, &qos_handlers);

while (running)
{
    // Producer publishes QoS
    tp_qos_publish_producer(&producer, current_seq, watermark);

    // Consumer publishes QoS (per-consumer stream if configured)
    tp_qos_publish_consumer(&consumer, consumer_id, last_seq_seen, drops_gap, drops_late, mode);

    // Any side that wants to observe QoS polls
    tp_qos_poll(&qos_poller, 10);
}
```

### 12.4 Metadata (Announce + Meta)

```c
static void on_metadata_event(void *clientd, const tp_metadata_event_t *event)
{
    (void)clientd;
    // event->type: announce/meta_begin/meta_attr/meta_end
}

tp_metadata_handlers_t meta_handlers =
{
    .on_metadata_event = on_metadata_event,
    .clientd = NULL
};

tp_metadata_poller_t meta_poller;
tp_metadata_poller_init(&meta_poller, &client, &meta_handlers);

// Producer announces and sends metadata
tp_data_source_announce_t announce =
{
    .data_source_id = 9,
    .producer_id = 7,
    .stream_id = 10000,
    .name = "camera-front"
};
tp_producer_send_data_source_announce(&producer, &announce);

tp_data_source_meta_t meta =
{
    .data_source_id = 9,
    .key = "format",
    .value = "rgb8"
};
tp_producer_send_data_source_meta(&producer, &meta);

// Consumers or tools observe metadata
tp_metadata_poll(&meta_poller, 10);
```

### 12.5 Progress (Per-Consumer Stream)

```c
static void on_progress(void *clientd, const tp_frame_progress_t *progress)
{
    (void)clientd;
    // update local QoS or visibility of consumer progress
}

tp_progress_handlers_t progress_handlers =
{
    .on_progress = on_progress,
    .clientd = NULL
};

tp_progress_poller_t progress_poller;
tp_progress_poller_init(&progress_poller, &client, &progress_handlers);

while (running)
{
    tp_progress_poll(&progress_poller, 10);
}
```

### 12.6 Driver Attach + Keepalive (Consumer)

```c
tp_driver_client_t driver;
tp_driver_attach_request_t attach_req;
tp_async_attach_t *attach_async = NULL;
tp_driver_attach_info_t attach_info;

tp_driver_client_init(&driver, &client);
tp_driver_attach_request_init(&attach_req);
attach_req.client_id = 42;
attach_req.stream_id = consumer_ctx.stream_id;
attach_req.role = TP_ROLE_CONSUMER;

tp_driver_attach_async(&driver, &attach_req, &attach_async);
while (tp_driver_attach_poll(attach_async, &attach_info) == 0)
{
    tp_client_do_work(&client);
}

while (running)
{
    tp_driver_keepalive(&driver, tp_clock_now_ns());
    tp_client_do_work(&client);
}
```

### 12.7 Logging (Client)

```c
static void on_log(void *clientd, int level, const char *message)
{
    (void)clientd;
    fprintf(stderr, "[tp] %d %s\n", level, message);
}

tp_client_context_t ctx;
tp_client_context_init(&ctx);
tp_client_context_set_log_handler(&ctx, on_log, NULL);
```

## 13. Migration Notes (Current -> Proposed)

- Replace direct Aeron usage in tools with pollers: `tp_control_poller`, `tp_qos_poller`, `tp_metadata_poller`.
- Replace `tp_driver_client_init` + attach with `tp_client_init` + `tp_driver_attach_async/poll`.
- Keep wire-level structs unchanged; only add ergonomic wrappers.
- Close order: close producers/consumers/pollers before `tp_client_close`.

## 14. Suggested Files / Modules

- `include/tensor_pool/tp_client.h` / `src/tp_client.c` (context, lifecycle, async add wrappers)
- `include/tensor_pool/tp_control_poller.h` / `src/tp_control_poller.c`
- `include/tensor_pool/tp_qos_poller.h` / `src/tp_qos_poller.c`
- `include/tensor_pool/tp_metadata_poller.h` / `src/tp_metadata_poller.c`
- `include/tensor_pool/tp_progress_poller.h` / `src/tp_progress_poller.c`
- `include/tensor_pool/tp_driver_client.h` updates for async attach/detach
- `include/tensor_pool/tp_discovery_client.h` remains, but init uses `tp_client_t`

## 15. Decisions and Refinements

- `tp_client_t` owns shared subscriptions (control/QoS/metadata) to avoid duplicates and centralize lifecycle.
- Producer/consumer start keepalive timers automatically when using the driver model.
- Per-consumer stream assignment is handled internally and not exposed as a public event.
- Client API hides SBE types; user-facing callbacks and structs are TensorPool abstractions.
- Consumer descriptor poller invokes registered callbacks on `FrameDescriptor` arrival; no local buffering required.

## 16. Aeron-Style API Decisions

These align with Aeron C client patterns so the API feels familiar.

- **Callback signatures**: match Aeron fragment-handler style.
  - `typedef void (*tp_frame_descriptor_handler_t)(void *clientd, const tp_frame_descriptor_t *desc);`
  - `typedef void (*tp_control_handler_t)(void *clientd, const tp_control_event_t *event);`
  - `typedef void (*tp_qos_handler_t)(void *clientd, const tp_qos_event_t *event);`
  - `typedef void (*tp_metadata_handler_t)(void *clientd, const tp_metadata_event_t *event);`
- **Error/status model**: functions return `int` (0 on success, -1 on error) and set `aeron_errcode()`/`aeron_errmsg()` analogs in TensorPool (`tp_errcode()`/`tp_errmsg()`), mirroring Aeron’s error reporting.
- **Backpressure semantics**: `tp_producer_offer_frame` returns `int64_t` like Aeron publications: `>= 0` for position, or negative codes for backpressure/admin/closed (`TP_BACK_PRESSURED`, `TP_NOT_CONNECTED`, `TP_ADMIN_ACTION`, `TP_CLOSED`), mapping to Aeron-style constants.
- **Ownership/lifetime rules**: pointers passed to callbacks are only valid for the duration of the callback; apps must copy if they need persistence, mirroring Aeron fragment handling.
- **Threading model**: single-threaded polling by default; callbacks invoked on the thread calling `tp_*_poll`. No implicit worker threads, consistent with Aeron’s poll pattern.
- **Keepalive scheduling**: keepalive work runs inside `tp_client_do_work` and/or `tp_driver_client_do_work` and uses the same idle strategy/intervals configured in the client context.
- **Fragment limits and polling**: `tp_*_poll` functions take `fragment_limit` like Aeron, and return number of fragments/events processed.
- **Poll return semantics**: `tp_*_poll` returns fragment count (`>= 0`) or `-1` on error with `tp_errcode()/tp_errmsg()` set.
