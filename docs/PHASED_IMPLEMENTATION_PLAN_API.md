# Phased Implementation Plan (Aeron-like C API)

This plan implements the Aeron-style client API. It is authoritative over prior API drafts.
Authoritative references:
- `docs/AERON_LIKE_API_PROPOSAL.md`
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.1.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`

Note: It is acceptable to replace existing functions with new ones to avoid technical debt.

## Phase 0 - Foundations and alignment

Status: in progress

- [ ] Confirm all API names, signatures, and behaviors match `docs/AERON_LIKE_API_PROPOSAL.md`.
- [ ] Align error model with Aeron (`0/-1` for init/close/poll, negative codes for offer/claim/queue).
- [ ] Add/confirm shared error helpers (`tp_errcode()/tp_errmsg()`).
- [ ] Add logging hook plumbing across all modules.

## Phase 1 - Core client/context + conductor ownership

Status: pending

- [ ] Add/extend `tp_client_context_t` with Aeron Archive-style knobs (client name, timeouts, retry attempts, error handler, delegating invoker, log handler).
- [ ] Implement `tp_client_t` lifecycle and ownership semantics (own or reuse Aeron instance).
- [ ] Implement async add wrappers for publication/subscription.
- [ ] Ensure `tp_client_do_work` drives keepalive timers and conductor work.

## Phase 2 - Driver client + attach/keepalive/detach

Status: pending

- [ ] Implement async attach/detach state machine and pollers.
- [ ] Implement keepalive scheduling and error handling (lease revoked/shutdown events).
- [ ] Add driver event poller integration with client shared subscriptions.
- [ ] Unit tests for attach/detach/keepalive and error paths (goto unwind for multi-resource teardown).

## Phase 3 - Producer API and fixed pool support

Status: pending

- [ ] Implement `tp_producer_t` lifecycle, attach paths, and publications.
- [ ] Implement `offer_frame`, `offer_progress`, and QoS publishing.
- [ ] Implement `try_claim`, `commit_claim`, `abort_claim`, `queue_claim`.
- [ ] Implement `fixed_pool_mode` semantics (claim persists across commit/queue).
- [ ] Unit tests for claim lifecycle and backpressure codes.

## Phase 4 - Consumer API and descriptor callbacks

Status: pending

- [ ] Implement consumer lifecycle and control/descriptor subscriptions.
- [ ] Implement descriptor poller with callback registration (`tp_consumer_set_descriptor_handler`).
- [ ] Implement frame read and view lifetime rules.
- [ ] Unit tests for descriptor callbacks and per-consumer stream switching.

## Phase 5 - Control/QoS/Metadata/Progress pollers

Status: pending

- [ ] Implement control poller and handlers (ConsumerHello/ConsumerConfig/DataSource*).
- [ ] Implement QoS poller + publish helpers.
- [ ] Implement metadata poller + announce/meta helpers.
- [ ] Implement progress poller for per-consumer streams.
- [ ] Unit tests for each poller and handler wiring.

## Phase 6 - Discovery client/poller

Status: pending

- [ ] Implement discovery context, request/response flow, and poller callbacks.
- [ ] Enforce schema header checks and response validation.
- [ ] Unit tests for request/response decode and poller callback delivery.

## Phase 7 - Tools and examples aligned to the new API

Status: pending

- [ ] Update `tp_example_producer.c` and `tp_example_consumer.c` to use only the new API.
- [ ] Update `tp_control_listen.c` and `tp_shm_inspect.c` as needed.
- [ ] Add a BGAPI-style example (queue-claim fixed pool).

## Phase 8 - Comprehensive tests and CI hooks

Status: pending

- [ ] Expand unit test coverage for error paths and resource cleanup.
- [ ] Add integration tests that require only the Aeron Media Driver (no TensorPool Driver).
- [ ] Add optional tests that require TensorPool Driver when available.

## Phase 9 - Documentation follow-up

Status: pending

- [ ] Create `docs/C_CLIENT_API_USAGE.md` from the implemented headers and examples.
- [ ] Ensure all docs point to the authoritative proposal/specs.
