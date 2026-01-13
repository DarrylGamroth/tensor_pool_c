# TraceLink Implementation Plan (v1.0)

Authoritative reference: `docs/SHM_TraceLink_Spec_v1.0.md`.
Dependent references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/STREAM_ID_CONVENTIONS.md`

## Phase 0 - Spec alignment + decisions

- [x] Confirm TraceLinkSet schemaId=904, templateId=1, and field set per §9.
- [x] Confirm `FrameDescriptor.trace_id` is present in the wire schema (or extend it) with null sentinel `0`.
- [x] Decide node ID source (config by default, driver allocation when available) and document the default.
- [x] For driver allocation, use `ShmAttachResponse.nodeId` per `docs/SHM_Driver_Model_Spec_v1.0.md` §4.2 and treat it as stable for the lease lifetime.
- [x] Confirm trace_id propagation rules for 1→1 and N→1 stages are mapped to existing producer/consumer flows.
- [x] Review Agrona Snowflake ID generator in `../agrona` and choose parameters (nodeIdBits=10, sequenceBits=12, timestampOffsetMs=0).

## Phase 1 - SBE schema + codegen

- [x] Add TraceLinkSet SBE schema to `schemas/` (schemaId=904).
- [x] Wire C codegen for the TraceLink schema via `../simple-binary-encoding` in CMake.
- [x] Add generated headers to include paths and ensure they compile.

## Phase 2 - Trace ID generation + propagation

- [x] Implement Snowflake-style trace ID generator (based on Agrona) in `src/` with a public header.
- [x] Add configuration for node ID and clock source; handle sequence rollover per tick.
- [x] Integrate trace ID minting into producer path for root frames.
- [x] Propagate trace_id for 1→1 stages (pass-through).
- [x] Allow trace_id reset or continuity on epoch changes (document chosen default).

## Phase 3 - TraceLinkSet encode/decode

- [x] Implement encode helper for TraceLinkSet messages.
- [x] Implement decode helper with strict header validation (schemaId/templateId/version/blockLength).
- [x] Validate parents[] constraints (numInGroup ≥ 1, non-zero values, uniqueness).
- [x] Ensure `stream_id/epoch/seq/trace_id` match the associated FrameDescriptor at emission time.

## Phase 4 - Control-plane publish + poll

- [x] Add producer API to emit TraceLinkSet for N→1 stages (best-effort, non-blocking).
- [x] Add optional control poller hook for TraceLinkSet to enable capture/logging.
- [x] Ensure emission uses recommended stream allocation (control stream per `STREAM_ID_CONVENTIONS.md`).

## Phase 5 - Tests

- [x] Unit tests for trace ID generator monotonicity and uniqueness (per node).
- [x] Encode/decode tests for TraceLinkSet with validation of parent constraints.
- [x] End-to-end test: build a fake N→1 join and emit TraceLinkSet, then decode and validate.
- [x] Negative tests: schema mismatch, zero parent, duplicate parent, empty group.

## Phase 6 - Tooling and docs

- [x] Add a control-plane inspection mode to `tp_control_listen` for TraceLinkSet output (JSON and text).
- [x] Update `docs/C_CLIENT_API_USAGE.md` with tracing usage (trace_id propagation + TraceLinkSet).
- [x] Update `docs/COMPLIANCE_MATRIX_SHM_WIRE_V1_2.md` with TraceLink status.
