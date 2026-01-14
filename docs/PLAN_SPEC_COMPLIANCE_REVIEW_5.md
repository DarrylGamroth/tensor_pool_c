# Spec Compliance Remediation Plan (Review 5)

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/AERON_LIKE_API_PROPOSAL.md`
- `docs/STREAM_ID_CONVENTIONS.md`

Scope: Address review findings related to path containment, single-writer PID validation, per-consumer stream decline behavior, pool selection rules, and canonical layout tooling. Spec language is authoritative for all decisions.

## Decisions from spec (authoritative)
- **Path containment (15.21a.5):** If no `allowed_base_dirs` are configured, mappings MUST be rejected (fail closed). Containment checks are mandatory for every mapping.
- **Pool selection rule (15.3):** Producer MUST choose the smallest pool with `stride_bytes >= values_len_bytes`; if none fit, drop the frame.

## Phases (track progress)

### Phase 1: Path containment fail-closed (15.21a.5)
- [x] Update `tp_shm_map` to reject mappings when `allowed_paths.canonical_length == 0`.
- [x] Ensure `tp_context_finalize_allowed_paths` returns error if `paths` were supplied but canonicalization fails.
- [x] Add/extend negative tests in `tests/test_tp_shm_security.c` to cover empty allowlist.
- [x] Update any docs/comments that mention `allowed_paths` to clarify that it is required for SHM mapping.

### Phase 2: Single-writer PID validation (15.22 + 15.21a)
- [x] Capture `pid` from each superblock at map time (header + pools).
- [x] During liveness checks, compare current `pid` to the stored value; on mismatch, unmap and require remap.
- [x] Add unit test to simulate `pid` changes and validate unmap behavior.

### Phase 3: Per-consumer stream decline behavior (10.1.3)
- [x] Change per-consumer channel validation to decline invalid/unsupported requests by returning empty channel and stream ID 0 in `ConsumerConfig`.
- [x] Add unit tests to confirm invalid channel requests do not error out but decline correctly.

### Phase 4: Pool selection enforcement (15.3)
- [x] Enforce smallest-fit pool selection inside `tp_producer_offer_frame`.
- [x] If no pool fits, return error and drop (do not block).
- [x] Add tests for best-fit selection and no-fit drop behavior.

### Phase 5: Canonical layout tooling (15.21a.3)
- [x] Make `tp_shm_create` default to canonical layout only.
- [x] Gate noncanonical path creation behind an explicit `--allow-noncompliant` flag with a warning.
- [x] Add a short doc note that noncanonical output is test-only.

### Phase 6: Compliance update
- [x] Update compliance matrix and relevant plan tracking docs after fixes land.
- [x] Re-run tests (`ctest --test-dir build --output-on-failure`).
