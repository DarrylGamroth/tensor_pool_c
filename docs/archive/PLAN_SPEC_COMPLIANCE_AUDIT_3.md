# Spec Compliance Remediation Plan (Audit 3)

Authoritative reference:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`

## Findings to Address

### Medium
- ConsumerConfig stream assignment rules are not enforced on the consumer side (channel/stream_id mismatch should be rejected).
- Producer/consumer manager has no policy hook to force `use_shm=false` for fallback/bridge scenarios.
- `payload_fallback_uri` is accepted without scheme validation; undefined schemes must be treated as unsupported.

### Low
- ConsumerConfig decode does not gate `version`/`blockLength` (control-plane schema versioning is normative).

## Plan

### Phase 1: ConsumerConfig validation (Medium)
- [x] Validate ConsumerConfig per spec on receive:
  - [x] Reject (fail closed) when only one of channel/stream_id is provided.
  - [x] Accept empty channel + stream_id=0 as "not assigned".
- [x] Add unit tests for invalid ConsumerConfig combinations.

### Phase 2: Fallback policy hooks (Medium)
- [x] Add a producer/consumer‑manager policy hook to force `use_shm=false` and attach a fallback URI.
- [x] Document the policy behavior in `docs/C_CLIENT_API_USAGE.md`.
- [x] Add unit tests to verify forced fallback is reflected in ConsumerConfig.

### Phase 3: Fallback URI scheme validation (Medium)
- [x] Validate `payload_fallback_uri` scheme on the consumer side (accept `aeron:` or `bridge://`, reject others).
- [x] Add unit tests for unsupported schemes.

### Phase 4: ConsumerConfig schema gating (Low)
- [x] Gate `schemaId`/`version`/`blockLength` in `tp_control_decode_consumer_config`.
- [x] Add unit tests for unsupported schema versions and block length mismatch.

## Progress Log
- 2025-01-14: Plan created.
- 2025-01-14: Phase 1 in progress (ConsumerConfig validation).
- 2025-01-14: Phase 1 complete (ConsumerConfig validation).
- 2025-01-14: Phase 2 complete (fallback policy hooks).
- 2025-01-14: Phase 3 complete (fallback URI scheme validation).
- 2025-01-14: Phase 4 complete (ConsumerConfig schema gating).
