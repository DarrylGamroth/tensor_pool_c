# Spec Compliance Remediation Plan

Authoritative references:
- `docs/COMPLIANCE_MATRIX_SHM_ALL.md`
- `docs/COMPLIANCE_MATRIX_SHM_WIRE_V1_2.md`
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/SHM_TraceLink_Spec_v1.0.md`
- `docs/SHM_Join_Barrier_Spec_v1.0.md`
- `docs/SHM_Aeron_UDP_Bridge_Spec_v1.0.md`

## Goals
- Resolve all **High** and **Medium** compliance gaps.
- Clarify or fix documentation gaps called out by specs.
- Preserve Aeron-like API behavior and error semantics.

## Findings (from latest audit)

### High
- Driver attach rejects responses without `leaseExpiryTimestampNs`, but the spec allows it to be absent.
- Lease expiry check treats a missing/zero expiry as expired.

### Medium
- Driver control-plane decodes do not validate `version`/`blockLength`.
- `ShmLeaseRevoked` accepts unknown `role`/`reason` values instead of rejecting as required.
- Discovery responses do not enforce `pool_nslots == header_nslots`.

### Low
- Spec references missing example files:
  - `docs/examples/bridge_config_example.toml`
  - `docs/examples/driver_camera_example.toml`

## Plan

### Phase 1: Driver attach/lease semantics (High)
- [x] Allow `leaseExpiryTimestampNs` to be absent in `ShmAttachResponse`.
- [x] Treat `leaseExpiryTimestampNs = null` as "unknown" rather than expired.
- [x] Adjust lease-expiry checks to only enforce expiry when an explicit timestamp is provided.
- [x] Add unit tests covering:
  - [x] attach response with `leaseExpiryTimestampNs = null` accepted
  - [x] lease-expiry behavior with null expiry does not force detach

### Phase 2: Driver control-plane strictness (Medium)
- [x] Enforce `schemaId`, `templateId`, `version`, and `blockLength` gating on:
  - [x] `ShmAttachResponse`
  - [x] `ShmDetachResponse`
  - [x] `ShmLeaseRevoked`
  - [x] `ShmDriverShutdown`
- [x] Reject unknown enum values for `role` and `reason` in `ShmLeaseRevoked`.
- [x] Add unit tests for version/block-length mismatch handling.

### Phase 3: Discovery response validation (Medium)
- [ ] Enforce `pool_nslots == header_nslots` in discovery results.
- [ ] Add unit tests for discovery response validation.

### Phase 4: Documentation gaps (Low)
- [ ] Add missing example files:
  - [ ] `docs/examples/bridge_config_example.toml`
  - [ ] `docs/examples/driver_camera_example.toml`
- [ ] Cross-link examples from the respective specs.

## Progress Log
- 2025-01-14: Plan created.
- 2025-01-14: Phase 1 in progress (driver attach/lease semantics).
- 2025-01-14: Phase 2 complete (driver control-plane strictness).
