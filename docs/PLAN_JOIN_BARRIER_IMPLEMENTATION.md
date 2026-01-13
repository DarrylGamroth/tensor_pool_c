# JoinBarrier Implementation Plan (v1.0)

Authoritative reference: `docs/SHM_Join_Barrier_Spec_v1.0.md`.
Dependent references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/STREAM_ID_CONVENTIONS.md`

## Phase 0 - Spec alignment and schema decisions

Status: pending

- [ ] Confirm required message schema IDs and versions for MergeMap control-plane messages (schemaId=903 per spec §10.1).
- [ ] Decide whether to adopt the Appendix A schema verbatim or publish a finalized schema in `schemas/` (Appendix is informative draft).
- [ ] Define stream IDs/channels for MergeMap announce/request in `docs/STREAM_ID_CONVENTIONS.md` if needed.

## Phase 1 - SBE schema + codegen

Status: pending

- [ ] Add MergeMap SBE schema to `schemas/` (new file or extend existing schemas) with schemaId=903.
- [ ] Wire C codegen for the new schema via `../simple-binary-encoding` in CMake.
- [ ] Add generated headers to include paths and ensure they are in sync with the spec.

## Phase 2 - Control-plane encode/decode adapters

Status: pending

- [ ] Implement encode helpers for:
  - `SequenceMergeMapAnnounce`
  - `SequenceMergeMapRequest`
  - `TimestampMergeMapAnnounce`
  - `TimestampMergeMapRequest`
- [ ] Implement decode adapters with strict header validation (schemaId/templateId/version/blockLength).
- [ ] Validate rule invariants (OFFSET vs WINDOW, OFFSET_NS vs WINDOW_NS, positive window sizes).
- [ ] Enforce `(out_stream_id, epoch)` scoping and reject invalid epochs.

## Phase 3 - MergeMap store and lifecycle

Status: pending

- [ ] Add a MergeMap registry keyed by `(out_stream_id, epoch, type)`.
- [ ] Invalidate cached MergeMaps on epoch change or new announce.
- [ ] Track re-announce time and optional staleTimeoutNs.
- [ ] Add logging for invalid or rejected MergeMaps.

## Phase 4 - SequenceJoinBarrier

Status: pending

- [ ] Implement readiness checks per §7:
  - observed/processed cursors vs required in_seq
  - handle OFFSET/WINDOW rules
  - block when MergeMap missing
- [ ] Add optional staleness policy handling (`staleTimeoutNs`).
- [ ] Ensure no waiting on SHM commit stability or overwrite prevention.

## Phase 5 - TimestampJoinBarrier

Status: pending

- [ ] Implement readiness checks per §8:
  - clock domain consistency
  - lateness_ns handling
  - OFFSET_NS/WINDOW_NS rules
- [ ] Enforce timestamp monotonicity assumptions and reject mixed clock domains.

## Phase 6 - LatestValueJoinBarrier

Status: pending

- [ ] Implement best-effort behavior per §9:
  - use most recent observed per input
  - respect epoch scoping
  - allow stale data with staleness policy

## Phase 7 - API surface (Aeron-style)

Status: pending

- [ ] Add public API for:
  - MergeMap announce/request
  - JoinBarrier creation and readiness evaluation
  - Cursor updates (observed/processed) per stream
- [ ] Keep SBE out of public API; use TensorPool types and enums.

## Phase 8 - Tests and tooling

Status: pending

- [ ] Unit tests for MergeMap validation (OFFSET/WINDOW, OFFSET_NS/WINDOW_NS).
- [ ] JoinBarrier readiness tests for sequence/timestamp/latest-value paths.
- [ ] Late-join tests for MergeMap request/announce flow.
- [ ] Negative tests for schema mismatch and invalid epochs.
- [ ] Optional: CLI tool to inspect MergeMap announces on control stream.

## Phase 9 - Documentation updates

Status: pending

- [ ] Update `docs/COMPLIANCE_MATRIX_SHM_WIRE_V1_2.md` with JoinBarrier status.
- [ ] Add usage examples to `docs/C_CLIENT_API_USAGE.md` if exposed to end users.
