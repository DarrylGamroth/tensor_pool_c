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
- [x] Enumerate all **External** items in `docs/COMPLIANCE_MATRIX_SHM_ALL.md`.
- [x] Expand `docs/REQUIREMENTS_TEST_CHECKLIST.md` with MUST/SHOULD for driver, discovery,
      supervisor, and node-id allocation.
- [x] Add traceability rows for newly planned components in
      `docs/TRACEABILITY_MATRIX_SHM_ALL.md`.

## Phase 1: Client Conductor (Aeron-style)
- [x] Implement a single-writer conductor that owns client state.
- [x] Centralize control/QoS/metadata/descriptor subscriptions and publications.
- [x] Implement driver attach/keepalive/revoke state machine.
- [x] Apply epoch/remap logic and SHM mapping decisions.
- [x] Dispatch callbacks for FrameDescriptor, QoS, metadata, discovery updates.
- [x] Provide `tp_client_do_work()`/agent-loop integration for polling.

## Phase 2: Common Infrastructure (Shared by Driver/Discovery/Supervisor)
- [x] Define config schema (TOML) for driver + discovery + supervisor.
- [x] Add shared logging/metrics infrastructure (Aeron-style levels).
- [x] Add Aeron context helpers (channel config, timeouts, idle strategies).
- [x] Add shared SHM helpers (canonical directory layout, file creation, mmap/hugepages).

## Phase 3: C TensorPool Driver (Authoritative Control Plane)
- [x] Implement attach request handling and policy checks per driver spec.
- [x] Allocate and publish epochs; create header/pool files in canonical layout.
- [x] Implement lease management, keepalive tracking, and revoke reasons.
- [x] Enforce publishMode, expectedLayoutVersion, hugepages requirements.
- [x] Implement GC/retention policy for epochs (keep N, prune old).
- [x] Provide driver control + announce publications (stream IDs per conventions).
- [x] Implement node-id allocation (authoritative per spec).
- [x] Provide `tp_driver` executable with lifecycle control and logging.

## Phase 4: Discovery Service / Registry (Advisory)
- [x] Implement discovery registry state model (streams, pools, QoS metadata).
- [x] Encode/decode DiscoveryRequest/DiscoveryResponse; enforce validity rules.
- [x] Support authority rules: driver is authoritative, discovery is advisory.
- [x] Provide `tp_discoveryd` executable with config + logging.
- [x] Add client-facing utilities to query discovery and print JSON.

## Phase 5: Supervisor / Unified Management (Policy Layer)
- [x] Implement supervisor policy engine (admission control, quotas, priorities).
- [x] Integrate with driver (or provide hooks) for authoritative decisions.
- [x] Add monitoring/metrics exports for operator visibility.
- [x] Integrate supervisor into driver binary.

## Phase 6: Integration Tests & Interop
- [ ] Add integration tests that bring up C driver + discovery + clients.
- [ ] Add interop tests against Julia driver to ensure full wire compatibility.
- [ ] Add failure-mode tests: timeouts, revoked leases, invalid SHM layout.

## Phase 7: CI / Packaging
- [ ] Add CI job to build and smoke-test driver/discovery binaries.
- [ ] Provide local scripts to run driver/discovery with Aeron Media Driver.
- [ ] Document operational workflows in `docs/DEPLOYMENT_GUIDE.md`.

## Progress
- Phase 0: Completed
- Phase 1: Completed
- Phase 2: Completed
- Phase 3: Completed (node-id reuse cooldown missing)
- Phase 4: Completed
- Phase 5: Completed
- Phase 6: Not started
- Phase 7: Not started
