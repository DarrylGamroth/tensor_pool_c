# JoinBarrier Implementation Plan (v1.0)

Authoritative reference: `docs/SHM_Join_Barrier_Spec_v1.0.md`.
Dependent references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/STREAM_ID_CONVENTIONS.md`

## Phase 0 - Spec alignment and schema decisions

Status: completed

- [x] Confirm required message schema IDs and versions for MergeMap control-plane messages (schemaId=903 per spec §10.1).
- [x] Adopt Appendix A schema verbatim in `schemas/` (schemaId=903, version=1).
- [x] Use the existing control-plane stream for MergeMap announce/request (no new stream IDs required).

## Phase 1 - SBE schema + codegen

Status: completed

- [x] Add MergeMap SBE schema to `schemas/` (new file or extend existing schemas) with schemaId=903.
- [x] Wire C codegen for the new schema via `../simple-binary-encoding` in CMake.
- [x] Add generated headers to include paths and ensure they are in sync with the spec.

## Phase 2 - Control-plane encode/decode adapters

Status: completed

- [x] Implement encode helpers for:
  - `SequenceMergeMapAnnounce`
  - `SequenceMergeMapRequest`
  - `TimestampMergeMapAnnounce`
  - `TimestampMergeMapRequest`
- [x] Implement decode adapters with strict header validation (schemaId/templateId/version/blockLength).
- [x] Validate rule invariants (OFFSET vs WINDOW, OFFSET_NS vs WINDOW_NS, positive window sizes).
- [x] Enforce `(out_stream_id, epoch)` scoping and reject invalid epochs (rule-level validation only).

## Phase 3 - MergeMap store and lifecycle

Status: completed

- [x] Add a MergeMap registry keyed by `(out_stream_id, epoch, type)`.
- [x] Invalidate cached MergeMaps on epoch change or new announce.
- [x] Track re-announce time and optional staleTimeoutNs.
- [x] Add logging for invalid or rejected MergeMaps.

## Phase 4 - SequenceJoinBarrier

Status: completed

- [x] Implement readiness checks per §7:
  - observed/processed cursors vs required in_seq
  - handle OFFSET/WINDOW rules
  - block when MergeMap missing
- [x] Add optional staleness policy handling (`staleTimeoutNs`).
- [x] Ensure no waiting on SHM commit stability or overwrite prevention.

## Phase 5 - TimestampJoinBarrier

Status: completed

- [x] Implement readiness checks per §8:
  - clock domain consistency
  - lateness_ns handling
  - OFFSET_NS/WINDOW_NS rules
- [x] Enforce timestamp monotonicity assumptions and reject mixed clock domains.

## Phase 6 - LatestValueJoinBarrier

Status: completed

- [x] Implement best-effort behavior per §9:
  - use most recent observed per input
  - respect epoch scoping
  - allow stale data with staleness policy

## Phase 7 - API surface (Aeron-style)

Status: completed

- [x] Add public API for:
  - MergeMap announce/request
  - JoinBarrier creation and readiness evaluation
  - Cursor updates (observed/processed) per stream
- [x] Keep SBE out of public API; use TensorPool types and enums.

## Phase 8 - Tests and tooling

Status: completed

- [x] Unit tests for MergeMap validation (OFFSET/WINDOW, OFFSET_NS/WINDOW_NS).
- [x] JoinBarrier readiness tests for sequence/timestamp/latest-value paths.
- [x] Late-join tests for MergeMap request/announce flow.
- [x] Negative tests for schema mismatch and invalid epochs.
- [x] Optional: CLI tool to inspect MergeMap announces on control stream.

## Phase 9 - Documentation updates

Status: completed

- [x] Update `docs/COMPLIANCE_MATRIX_SHM_WIRE_V1_2.md` with JoinBarrier status.
- [x] Add usage examples to `docs/C_CLIENT_API_USAGE.md` if exposed to end users.
