# API Convenience Wrapper Plan (Aeron-Style)

This plan documents the Aeron patterns that motivate higher-level TensorPool helpers and proposes a minimal, Aeron‑aligned convenience layer. The low‑level APIs remain supported; these helpers reduce boilerplate in user code and make wrapper bindings (Julia, Python) simpler.

## Step 1: Aeron C Patterns (Audit)

### Aeron Core Client
- **Context-based configuration**: Aeron uses `aeron_context_t` with `aeron_context_init` plus a set of `*_set_*` functions. Defaults are assigned inside the context initializer.
  - Reference: `../aeron/aeron-client/src/main/c/aeron_context.c`, `../aeron/aeron-client/src/main/c/aeronc.h`.
- **Async add + poll**: Aeron C exposes `aeron_async_add_publication` / `aeron_async_add_subscription` and corresponding `*_poll` to complete; callers typically loop and call `aeron_main_do_work` (or their invoker) between polls.
  - Reference: `../aeron/aeron-client/src/main/c/aeron_client.c`, `../aeron/aeron-client/src/main/c/aeronc.h`.
- **Callbacks carry client state**: Aeron fragment handlers take a `void *clientd` which is always user‑owned and passed back on callback invocation.
- **Opaque handles**: Application code uses opaque types and accessor functions rather than touching internal fields.

### Aeron Archive
- **Context + connect**: `aeron_archive_context_t` mirrors Aeron core with setters for channels and timeouts.
- **Blocking wrapper built on async**: `aeron_archive_connect` loops on `aeron_archive_async_connect_poll` until completion, then returns a connected handle.
  - Reference: `../aeron/aeron-archive/src/main/c/client/aeron_archive_client.c`, `../aeron/aeron-archive/src/main/c/client/aeron_archive_async_connect.c`, `../aeron/aeron-archive/src/main/c/client/aeron_archive.h`.
- **Async handle lifecycle**: `aeron_archive_async_connect_poll` frees the async handle on completion or error (caller just polls). This is a strong cue to make TensorPool convenience helpers manage async handle lifetimes cleanly.

### Implications for TensorPool
- Use **context presets + setters** to avoid direct struct mutation.
- Use **async + poll** as the primary API with optional blocking wrappers (as Aeron Archive does).
- Provide **self‑state convenience** wrappers to avoid repeating `(void *)consumer` or `(void *)producer` in handlers.

## Step 2: Proposed Convenience Layer (Design)

### A. Driver Attach Request Builder
Goal: avoid raw `tp_driver_attach_request_t` field assignment in user code.

Proposed API:
```c
int tp_driver_attach_request_init(tp_driver_attach_request_t *req, uint32_t stream_id, uint8_t role);
void tp_driver_attach_request_set_client_id(tp_driver_attach_request_t *req, uint32_t client_id);
void tp_driver_attach_request_set_publish_mode(tp_driver_attach_request_t *req, tp_publish_mode_t mode);
void tp_driver_attach_request_set_expected_layout_version(tp_driver_attach_request_t *req, uint32_t version);
void tp_driver_attach_request_set_hugepages(tp_driver_attach_request_t *req, tp_hugepages_policy_t policy);
void tp_driver_attach_request_set_desired_node_id(tp_driver_attach_request_t *req, uint32_t node_id);
```

Defaults applied by `tp_driver_attach_request_init`:
- `correlation_id = 0` (driver/client allocates)
- `client_id = 0` (auto‑assign)
- `expected_layout_version = TP_LAYOUT_VERSION`
- `publish_mode = TP_PUBLISH_MODE_EXISTING_OR_CREATE`
- `require_hugepages = TP_HUGEPAGES_UNSPECIFIED`
- `desired_node_id = TP_NULL_U32`
- `stream_id` and `role` are required inputs

### B. Driver Attach Convenience Wrapper
Goal: reduce boilerplate for common attach paths.

Proposed API:
```c
int tp_driver_attach_async_simple(
    tp_driver_client_t *driver,
    uint32_t stream_id,
    uint8_t role,
    tp_async_attach_t **out);
```

This wrapper would:
- initialize a `tp_driver_attach_request_t` with defaults,
- set `stream_id` and `role`,
- call `tp_driver_attach_async`.

### C. Consumer/Producer Context Presets
Goal: reduce manual context struct configuration.

Proposed API:
```c
int tp_consumer_context_init_default(tp_consumer_context_t *ctx, uint32_t stream_id, uint32_t consumer_id, bool use_driver);
int tp_producer_context_init_default(tp_producer_context_t *ctx, uint32_t stream_id, uint32_t producer_id, bool use_driver);
```

Defaults applied:
- `use_driver` set from parameter
- `use_conductor_polling = false` (explicit opt‑in)
- `driver_request` initialized using the builder (stream_id/role + defaults)
- other fields left as standard defaults (no forced per‑consumer channels)

### D. Simple Init Helpers
Goal: a minimal “Aeron‑like” path for common use cases.

Proposed API:
```c
int tp_consumer_init_simple(
    tp_consumer_t **out,
    tp_client_t *client,
    uint32_t stream_id,
    uint32_t consumer_id,
    bool use_driver);

int tp_producer_init_simple(
    tp_producer_t **out,
    tp_client_t *client,
    uint32_t stream_id,
    uint32_t producer_id,
    bool use_driver);
```

Internally uses `*_context_init_default` and then `tp_consumer_init` / `tp_producer_init`.

### E. Handler Convenience (Self‑State)
Goal: reduce `clientd` boilerplate without changing callback style.

Proposed API:
```c
void tp_consumer_set_descriptor_handler_self(tp_consumer_t *consumer, tp_frame_descriptor_handler_t handler);
void tp_consumer_set_progress_handler_self(tp_consumer_t *consumer, tp_frame_progress_handler_t handler);
```

These wrappers simply pass `consumer` as the `clientd` argument.

## Documentation Updates
- Update `docs/C_CLIENT_API_USAGE.md` to show the new convenience APIs first (with explicit defaults), then show the low‑level forms.
- Ensure examples use `*_init_default` and `*_attach_async_simple` where appropriate.
- Keep explicit “advanced/low‑level” sections for full control.

## Non‑Goals
- Removing the low‑level APIs (we keep them for power users and tests).
- Changing async poll semantics (remain `0/1/-1`).
- Hiding `void *clientd` from the callback model (we add helpers instead).

## Implementation Notes (Completed)

Implemented convenience helpers:
- `tp_driver_attach_request_init` + setters (`tp_driver_attach_request_set_*`) and `tp_driver_attach_async_simple`.
- `tp_consumer_context_init_default`, `tp_consumer_init_simple`.
- `tp_producer_context_init_default`, `tp_producer_init_simple`.
- `tp_consumer_set_descriptor_handler_self`, `tp_consumer_set_progress_handler_self`.

Follow-up:
- Update `docs/C_CLIENT_API_USAGE.md` to showcase the new helpers first. (done)
