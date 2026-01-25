# Reference Implementation Plan (C)

This plan consolidates the conductor, driver, discovery, and supervisor work
into a single ordered set of steps. Specs are authoritative:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/SHM_TraceLink_Spec_v1.0.md`
- `docs/SHM_Join_Barrier_Spec_v1.0.md`

Related design docs:
- `docs/CLIENT_CONDUCTOR_DESIGN.md`
- `docs/AERON_LIKE_API_PROPOSAL.md`
- `docs/IMPLEMENTATION_GUIDE.md`

## Phase 0: Baseline Tracking
- [x] Ensure compliance and traceability matrices reflect missing items.
- [x] Confirm requirements-to-tests checklist coverage for new work.
- [x] Mark conductor/driver/discovery/supervisor as MISSING where applicable.

## Phase 1: Client Conductor (Aeron-style)
- [x] Implement MPSC command queue (C11 atomics) for async add operations.
- [x] Move shared control/QoS/metadata/announce subscriptions into conductor-owned state.
- [x] Add client-level handler registration and dispatch in `tp_client_do_work`.
- [x] Remove public Aeron types from user-facing headers (opaque handles).
- [x] Route per-producer/consumer polling through conductor (or deprecate).
- [x] Add unit tests for conductor lifecycle and handler dispatch.

## Phase 2: Code Organization
- [x] Migrate implementation into `src/client`, `src/common`, `src/driver`.
- [x] Split public headers to match the new layout.
- [x] Update CMake targets to build client/driver libs cleanly.

## Phase 3: C Driver (Authoritative Control Plane)
- [x] Implement attach request handling and policy checks per driver spec.
- [x] Create SHM regions (header + pools) in canonical layout.
- [x] Publish ShmPoolAnnounce and manage epochs.
- [x] Track leases and keepalive expiry; revoke with correct reason codes.
- [x] Implement GC/retention for old epochs.
- [x] Provide `tp_driver` executable with config + logging.

## Phase 4: Discovery Service (Advisory)
- [x] Implement discovery registry and stream index.
- [x] Serve DiscoveryRequest/DiscoveryResponse with filter semantics.
- [x] Respect authority rules (driver authoritative, discovery advisory).
- [x] Provide `tp_discoveryd` executable and JSON output tooling.
- [x] Allow optional embedded mode (agent can run inside driver).

## Phase 5: Supervisor / Unified Management
- [x] Implement supervisor/console agent.
- [x] Subscribe to announce/QoS/metadata/health streams.
- [x] Emit ConsumerConfig for per-consumer streams and rate limits.
- [x] Embed supervisor agent in `tp_driver`.

## Phase 6: Integration + Interop
- [ ] Add integration tests for driver + discovery + client conductor.
- [ ] Add interop tests against Julia driver/client for wire compatibility.
- [ ] Add failure-mode tests (expired lease, remap, invalid layout).

## Phase 7: Documentation & Usage
- [ ] Update `docs/C_CLIENT_API_USAGE.md` to use conductor-only APIs.
- [ ] Add examples for driver/discovery/supervisor usage.
- [ ] Keep `docs/IMPLEMENTATION_GUIDE.md` checklist in sync.

## Progress
- Phase 0: Completed
- Phase 1: Completed
- Phase 2: Completed
- Phase 3: Completed
- Phase 4: Completed
- Phase 5: Completed
- Phase 6: Not started
- Phase 7: Not started

## Checklist (Specs → Tests)
- [ ] Add integration coverage for driver server-side requirements (D-2.1, D-4.3-1, D-4.8-1, D-4.2-4/TL-6.2-1).
- [ ] Add node ID reuse cooldown policy (TraceLink §6.2) or document explicit verification.
- [ ] Expand config-matrix integration tests beyond external driver availability.
