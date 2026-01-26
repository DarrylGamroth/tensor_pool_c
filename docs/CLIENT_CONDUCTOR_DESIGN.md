# Client Conductor Design (Draft)

This document defines the Aeron-style client conductor for TensorPool C. It is
the single-writer owner of client state and centralizes control/QoS/metadata/
descriptor handling so applications do not call Aeron APIs directly.

## Status

Active design reference for conductor behavior. Update when conductor implementation or specs change.

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md` (15.15, 10.x control plane)
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/AERON_LIKE_API_PROPOSAL.md`

## Goals
- Mirror Aeron C client patterns: context, async add, poll/do_work, error style.
- Provide a single-writer conductor that owns client state and Aeron resources.
- Eliminate Aeron calls from application code (use TensorPool APIs only).
- Centralize driver attach/keepalive/revoke, discovery updates, SHM remap logic.
- Provide uniform callback dispatch for FrameDescriptor/QoS/metadata/control.

## Assumptions
- Breaking API changes are acceptable during development.
- Follow Aeron conductor/agent patterns wherever possible.
- The conductor is an Aeron-style agent; avoid pthread-specific helpers.

## Non-Goals
- Implementing the driver, discovery provider, or supervisor services.
- Implementing the UDP bridge (external by policy).

## Responsibilities
- Own shared subscriptions/publications:
  - Control, descriptor, QoS, metadata (shared or per-consumer where configured).
- Own driver-client state:
  - Attach request/response, keepalive schedule, revoke handling.
- Own discovery-client state:
  - Discovery request/response, registry cache, authority fields.
- Own SHM mapping lifecycle:
  - Apply ShmPoolAnnounce, validate superblocks, remap on epoch changes.
- Dispatch callbacks:
  - FrameDescriptor, FrameProgress, QoS, metadata, driver events, discovery.

## Threading / Ownership Model
- Single-writer: all conductor state is mutated only by `tp_client_do_work`.
- Application threads MAY call APIs that enqueue requests; the conductor drains
  them in `do_work` (Aeron-style command queue).
- Optional agent-invoker mode: `tp_client_do_work` drives Aeron and conductor
  together for single-threaded applications.
- For dedicated loops, use the Aeron-style agent runner (`tp_client_conductor_agent_*`).
- No blocking inside `do_work`; it returns work count as in Aeron.

## Command Queue (Aeron-style)
The conductor owns a lock-free ring buffer for commands. Application threads
enqueue requests (e.g., async add publication/subscription) and receive a handle
with a correlation ID. The conductor drains the ring in `tp_client_do_work`,
executes the command, and updates the handle status. Callers poll their handle
until completion, matching Aeron’s async add API.

Reference implementation (Aeron C client):
- `../aeron/aeron-client/src/main/c/aeron_context.c` (command queue init)
- `../aeron/aeron-client/src/main/c/aeron_client_conductor.h` (command queue field + handlers)
- `../aeron/aeron-client/src/main/c/aeron_client_conductor.c` (do_work + command dispatch)

Proposed command types:
- `TP_CMD_ADD_PUBLICATION(channel, stream_id, correlation_id)`
- `TP_CMD_ADD_SUBSCRIPTION(channel, stream_id, on_available, on_unavailable, correlation_id)`
- `TP_CMD_CLOSE_RESOURCE(resource_id)` (optional for future)

Proposed queue API (internal):
- `tp_cmd_queue_init(tp_cmd_queue_t *q, size_t capacity)`
- `tp_cmd_queue_offer(tp_cmd_queue_t *q, const tp_cmd_t *cmd)` → 0 or -1 (backpressure)
- `tp_cmd_queue_poll(tp_cmd_queue_t *q, tp_cmd_t *out)` → 1 if command returned

Proposed async handle state:
- `PENDING` → `COMPLETE` (success with pointer) or `ERROR` (tp_err set)

### Minimal MPSC Queue (C11 atomics)
We will implement a small in-repo MPSC ring modeled after Aeron’s
`aeron_mpsc_concurrent_array_queue`:

- Fixed power-of-two capacity.
- Producers claim a slot via atomic `tail`; consumer reads via atomic `head`.
- Slot publication uses a sequence value or a per-slot flag to avoid ABA.
- Enqueue returns `-1` when the queue is full (caller retries).
- Dequeue returns `0` when empty, `1` when a command is returned.

Proposed internal API:
- `tp_mpsc_queue_init(tp_mpsc_queue_t *q, size_t capacity)`
- `tp_mpsc_queue_close(tp_mpsc_queue_t *q)`
- `tp_mpsc_queue_offer(tp_mpsc_queue_t *q, const tp_cmd_t *cmd)`
- `tp_mpsc_queue_poll(tp_mpsc_queue_t *q, tp_cmd_t *out)`

Implementation notes:
- Use C11 atomics (`stdatomic.h`) for head/tail and per-slot sequence.
- Keep the structure in `src/internal/tp_mpsc_queue.[ch]` (not public).
- Follow Aeron’s memory-ordering approach (release on publish, acquire on consume).

## State Machine (Driver Mode)
```
INIT -> CONNECTING -> ATTACHED -> MAPPED -> ACTIVE
  \-> ERROR
ACTIVE -> REVOKED -> CONNECTING
ACTIVE -> SHUTDOWN
```
Transitions:
- CONNECTING: async add control pub/sub, send attach.
- ATTACHED: attach response OK; store lease, epoch, URIs.
- MAPPED: SHM mapping validated and active.
- ACTIVE: descriptor/QoS/metadata polling enabled; callbacks may fire.
- REVOKED: lease revoked; unmap, clear state, schedule reattach.

## API Surface (Draft)
Additions to `tp_client_t`:
- `tp_client_do_work(tp_client_t *client)` drives:
  - driver keepalive, control polling, descriptor/QoS/metadata polling,
    discovery polling, SHM remap work.
- `tp_client_set_handlers(...)` for global event handlers (optional).
- `tp_client_request_attach(...)` for explicit attach (driver mode).

Per-role APIs should delegate to the conductor:
- `tp_producer_init` and `tp_consumer_init` register with the conductor.
- `tp_producer_poll_control` and `tp_consumer_poll_control` become thin
  wrappers that call conductor work (or are removed in favor of `do_work`).

## Callback Model
Handlers should mirror Aeron style (function pointer + clientd):
- `on_driver_attached`, `on_lease_revoked`
- `on_shm_pool_announce` (raw announce + validated mapping result)
- `on_descriptor`, `on_progress`, `on_qos`, `on_metadata`
- `on_discovery_update` (optional)

Callbacks MUST be invoked from the conductor thread (`do_work`).

## Error Handling
- Follow Aeron style: return 0 on success, -1 on error with `tp_err` set.
- Any fatal protocol violation triggers `tp_err` and transitions to ERROR.
- Non-fatal input errors are logged and dropped (per spec).

## Config and Defaults
- Use `tp_context_t` for channels/stream IDs and timeouts.
- Defaults follow `docs/STREAM_ID_CONVENTIONS.md` and the spec.
- Respect driver/discovery authority fields; client does not override them.

## Testing Expectations
- Unit tests for conductor state transitions and event dispatch.
- Integration tests with a driver (Julia or C) for attach/keepalive/revoke.
- Interop tests to validate shared control streams and SHM remap behavior.

## Open Questions
- Should conductor own per-producer/per-consumer QoS cadence timers, or should
  producer/consumer keep their own? (Prefer conductor for shared scheduling.)
- Should `tp_client_do_work` include idle strategy (sleep/yield) or leave that
  to the caller? (Prefer Aeron-style: return work count; caller idles.)

## API Delta vs `docs/AERON_LIKE_API_PROPOSAL.md`
- **Conductor scope**: current `tp_client_conductor` only wraps Aeron init and
  async add; it does NOT own control/QoS/metadata/descriptor polling or
  driver/discovery orchestration as proposed.
- **Shared subscriptions**: `tp_client_start` creates control/announce/QoS/
  metadata subscriptions, but there is no conductor-level dispatch or unified
  handler registration.
- **Aeron leakage**: public structs expose Aeron types (publications,
  subscriptions, fragment assemblers). Proposal expects these to be hidden
  behind TensorPool types.
- **Unified callbacks**: no client-level handler registration for descriptor/
  progress/QoS/metadata/discovery events; callbacks are currently per-producer/
  per-consumer.
- **Producer/consumer polling**: apps still call `tp_producer_poll_control` and
  `tp_consumer_poll_descriptors/control`; proposal calls for central
  `tp_client_do_work` to drive control-plane processing.

## Conductor Implementation Checklist
- [ ] Move control/QoS/metadata/descriptor subscriptions into conductor-owned
      state; remove public Aeron members from `tp_client_t` where possible.
- [ ] Implement an Aeron-style command queue (lock-free ring) for async add
      requests; `tp_client_do_work` drains the queue and executes commands.
- [ ] Replace GCC `__atomic` usage with C11 atomics for portability in
      `tp_seqlock`, driver client ID/correlation counters, and producer fences.
- [ ] Add client-level handler registration for descriptor/progress/QoS/metadata/
      discovery and driver events.
- [ ] Route existing per-producer/consumer callbacks through conductor or
      deprecate them in favor of client-level handlers.
- [ ] Ensure `tp_client_do_work` drives control-plane polling and dispatch.
- [ ] Replace public Aeron types in headers with opaque TensorPool handles.
- [ ] Update `tp_producer_poll_control`/`tp_consumer_poll_*` to thin wrappers
      (or remove them if `tp_client_do_work` fully owns polling).
- [ ] Add unit tests for conductor lifecycle and handler dispatch.
- [ ] Add integration tests with driver (Julia or C) validating attach/keepalive,
      remap, and per-consumer stream switching.
