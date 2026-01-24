# C External Components Implementation Plan

This plan covers the components currently marked **External** in the compliance/traceability
matrices and implements them in C. The SHM specs are authoritative:
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/STREAM_ID_CONVENTIONS.md`

Out of scope:
- UDP bridge service (spec removed / external by policy).

## Phase 0: Requirements Baseline
- [ ] Enumerate all **External** items in `docs/COMPLIANCE_MATRIX_SHM_ALL.md`.
- [ ] Expand `docs/REQUIREMENTS_TEST_CHECKLIST.md` with MUST/SHOULD for driver, discovery,
      supervisor, and node-id allocation.
- [ ] Add traceability rows for newly planned components in
      `docs/TRACEABILITY_MATRIX_SHM_ALL.md`.

## Phase 1: Client Conductor (Aeron-style)
- [ ] Implement a single-writer conductor that owns client state.
- [ ] Centralize control/QoS/metadata/descriptor subscriptions and publications.
- [ ] Implement driver attach/keepalive/revoke state machine.
- [ ] Apply epoch/remap logic and SHM mapping decisions.
- [ ] Dispatch callbacks for FrameDescriptor, QoS, metadata, discovery updates.
- [ ] Provide `tp_client_do_work()`/agent-loop integration for polling.

## Phase 2: Common Infrastructure (Shared by Driver/Discovery/Supervisor)
- [ ] Define config schema (TOML) for driver + discovery + supervisor.
- [ ] Add shared logging/metrics infrastructure (Aeron-style levels).
- [ ] Add Aeron context helpers (channel config, timeouts, idle strategies).
- [ ] Add shared SHM helpers (canonical directory layout, file creation, mmap/hugepages).

## Phase 3: C TensorPool Driver (Authoritative Control Plane)
- [ ] Implement attach request handling and policy checks per driver spec.
- [ ] Allocate and publish epochs; create header/pool files in canonical layout.
- [ ] Implement lease management, keepalive tracking, and revoke reasons.
- [ ] Enforce publishMode, expectedLayoutVersion, hugepages requirements.
- [ ] Implement GC/retention policy for epochs (keep N, prune old).
- [ ] Provide driver control + announce publications (stream IDs per conventions).
- [ ] Implement node-id allocation (authoritative per spec).
- [ ] Provide `tp_driver` executable with lifecycle control and logging.

## Phase 4: Discovery Service / Registry (Advisory)
- [ ] Implement discovery registry state model (streams, pools, QoS metadata).
- [ ] Encode/decode DiscoveryRequest/DiscoveryResponse; enforce validity rules.
- [ ] Support authority rules: driver is authoritative, discovery is advisory.
- [ ] Provide `tp_discoveryd` executable with config + logging.
- [ ] Add client-facing utilities to query discovery and print JSON.

## Phase 5: Supervisor / Unified Management (Policy Layer)
- [ ] Implement supervisor policy engine (admission control, quotas, priorities).
- [ ] Integrate with driver (or provide hooks) for authoritative decisions.
- [ ] Add monitoring/metrics exports for operator visibility.
- [ ] Provide `tp_supervisord` executable or integrate into driver binary.

## Phase 6: Integration Tests & Interop
- [ ] Add integration tests that bring up C driver + discovery + clients.
- [ ] Add interop tests against Julia driver to ensure full wire compatibility.
- [ ] Add failure-mode tests: timeouts, revoked leases, invalid SHM layout.

## Phase 7: CI / Packaging
- [ ] Add CI job to build and smoke-test driver/discovery binaries.
- [ ] Provide local scripts to run driver/discovery with Aeron Media Driver.
- [ ] Document operational workflows in `docs/DEPLOYMENT_GUIDE.md`.

## Progress
- Phase 0: Not started
- Phase 1: Not started
- Phase 2: Not started
- Phase 3: Not started
- Phase 4: Not started
- Phase 5: Not started
- Phase 6: Not started
- Phase 7: Not started
