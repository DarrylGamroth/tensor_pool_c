# Client Conductor Design (Draft)

This document defines the Aeron-style client conductor for TensorPool C. It is
the single-writer owner of client state and centralizes control/QoS/metadata/
descriptor handling so applications do not call Aeron APIs directly.

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
- No blocking inside `do_work`; it returns work count as in Aeron.

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
- Use `tp_client_context_t` for channels/stream IDs and timeouts.
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
