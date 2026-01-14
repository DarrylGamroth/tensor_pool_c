# Spec Compliance Remediation Plan (Audit 2)

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/COMPLIANCE_MATRIX_SHM_ALL.md`

## Findings to Address

### High
- Discovery responses do not reject unsupported schema versions (`schemaId`/`version` gating required).
- Discovery requests accept empty `response_channel` (must be non-empty).

### Medium
- Driver attach/detach decoders accept unknown `responseCode` values instead of failing closed.
- Driver shutdown decodes accept unknown `shutdownReason` values instead of failing closed.
- `payload_fallback_uri` is decoded but unused; producer advertises empty fallback and consumer lacks fallback mapping.

### Low
- Aeron UDP Bridge spec is not implemented (schema only).
- Supervisor/unified management (Wire spec §10.5) not implemented.

## Plan

### Phase 1: Discovery strictness (High)
- [x] Add `schemaId`/`version` gating in `tp_discovery_decode_response`.
- [x] Reject empty `response_channel` in `tp_discovery_request` (already checks NULL).
- [x] Add unit tests for unsupported discovery schema versions and empty response channel.

### Phase 2: Driver enum validation (Medium)
- [x] Fail closed on unknown `responseCode` in `tp_driver_decode_attach_response`.
- [x] Fail closed on unknown `responseCode` in `tp_driver_decode_detach_response`.
- [x] Fail closed on unknown `shutdownReason` in `tp_driver_decode_shutdown`.
- [x] Add unit tests for invalid enum values in driver decoders.

### Phase 3: Payload fallback behavior (Medium)
- [ ] Decide and document fallback behavior when `payload_fallback_uri` is supplied.
- [ ] Implement consumer fallback mapping for payload pools (if used by spec).
- [ ] Update producer to advertise configured fallback URI when applicable.
- [ ] Add tests for fallback URI handling and mapping validation.

### Phase 4: Missing optional components (Low)
- [x] Aeron UDP Bridge is external; no in-repo implementation planned.
- [x] Supervisor/Unified Management is external; no in-repo implementation planned.

## Progress Log
- 2025-01-14: Plan created.
- 2025-01-14: UDP Bridge marked external by request.
- 2025-01-14: Supervisor marked external by request.
- 2025-01-14: Phase 1 complete (discovery strictness).
- 2025-01-14: Phase 2 complete (driver enum validation).
