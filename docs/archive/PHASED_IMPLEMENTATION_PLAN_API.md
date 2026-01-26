# Phased Implementation Plan (Aeron-like C API)

This plan implements the Aeron-style client API. It is authoritative over prior API drafts.
Authoritative references:
- `docs/AERON_LIKE_API_PROPOSAL.md`
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`

Note: It is acceptable to replace existing functions with new ones to avoid technical debt.

## Phase 0 - Foundations and alignment

Status: completed

- [x] Confirm all API names, signatures, and behaviors match `docs/AERON_LIKE_API_PROPOSAL.md`.
- [x] Align error model with Aeron (`0/-1` for init/close/poll, negative codes for offer/claim/queue).
- [x] Add/confirm shared error helpers (`tp_errcode()/tp_errmsg()`).
- [x] Add logging hook plumbing across all modules.

## Phase 1 - Core client/context + conductor ownership

Status: completed

- [x] Add/extend `tp_context_t` with Aeron Archive-style knobs (client name, timeouts, retry attempts, error handler, delegating invoker, log handler).
- [x] Implement `tp_client_t` lifecycle and ownership semantics (own or reuse Aeron instance).
- [x] Implement async add wrappers for publication/subscription.
- [x] Ensure `tp_client_do_work` drives keepalive timers and conductor work.

## Phase 2 - Driver client + attach/keepalive/detach

Status: completed

- [x] Implement async attach/detach state machine and pollers.
- [x] Implement keepalive scheduling and error handling (lease revoked/shutdown events).
- [x] Add driver event poller integration with client shared subscriptions.
- [x] Unit tests for attach/detach/keepalive and error paths (goto unwind for multi-resource teardown).

## Phase 3 - Producer API and fixed pool support

Status: completed

- [x] Implement `tp_producer_t` lifecycle, attach paths, and publications.
- [x] Implement `offer_frame`, `offer_progress`, and QoS publishing.
- [x] Implement `try_claim`, `commit_claim`, `abort_claim`, `queue_claim`.
- [x] Implement `fixed_pool_mode` semantics (claim persists across commit/queue).
- [x] Unit tests for claim lifecycle and backpressure codes.

## Phase 4 - Consumer API and descriptor callbacks

Status: completed

- [x] Implement consumer lifecycle and control/descriptor subscriptions.
- [x] Implement descriptor poller with callback registration (`tp_consumer_set_descriptor_handler`).
- [x] Implement frame read and view lifetime rules.
- [x] Unit tests for descriptor callbacks and per-consumer stream switching.

## Phase 5 - Control/QoS/Metadata/Progress pollers

Status: completed

- [x] Implement control poller and handlers (ConsumerHello/ConsumerConfig/DataSource*).
- [x] Implement QoS poller + publish helpers.
- [x] Implement metadata poller + announce/meta helpers.
- [x] Implement progress poller for per-consumer streams.
- [x] Unit tests for each poller and handler wiring.

## Phase 6 - Discovery client/poller

Status: completed

- [x] Implement discovery context, request/response flow, and poller callbacks.
- [x] Enforce schema header checks and response validation.
- [x] Unit tests for request/response decode and poller callback delivery.

## Phase 7 - Tools and examples aligned to the new API

Status: completed

- [x] Update `tp_example_producer_driver.c`, `tp_example_producer_nodriver.c`, `tp_example_consumer_driver.c`, and `tp_example_consumer_nodriver.c` to use only the new API.
- [x] Update `tp_control_listen.c` and `tp_shm_inspect.c` as needed.
- [x] Add a BGAPI-style example (queue-claim fixed pool).

## Phase 8 - Comprehensive tests and CI hooks

Status: completed

- [x] Expand unit test coverage for error paths and resource cleanup.
- [x] Add integration tests that require only the Aeron Media Driver (no TensorPool Driver).
- [x] Add optional tests that require TensorPool Driver when available.

## Phase 9 - Documentation follow-up

Status: completed

- [x] Create `docs/C_CLIENT_API_USAGE.md` from the implemented headers and examples.
- [x] Ensure all docs point to the authoritative proposal/specs.
