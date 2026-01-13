# TraceLink Implementation Plan (v1.0)

Authoritative reference: `docs/SHM_TraceLink_Spec_v1.0.md`.
Dependent references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/STREAM_ID_CONVENTIONS.md`

## Phase 0 - Spec alignment + decisions

- [ ] Confirm TraceLinkSet schemaId=904, templateId=1, and field set per §9.
- [ ] Confirm `FrameDescriptor.trace_id` is present in the wire schema (or extend it) with null sentinel `0`.
- [ ] Decide node ID source (config vs driver allocation) and document the default.
- [ ] Confirm trace_id propagation rules for 1→1 and N→1 stages are mapped to existing producer/consumer flows.
- [ ] Review Agrona Snowflake ID generator in `../agrona` and choose parameters (epoch, node ID bit width, sequence width).

## Phase 1 - SBE schema + codegen

- [ ] Add TraceLinkSet SBE schema to `schemas/` (schemaId=904).
- [ ] Wire C codegen for the TraceLink schema via `../simple-binary-encoding` in CMake.
- [ ] Add generated headers to include paths and ensure they compile.

## Phase 2 - Trace ID generation + propagation

- [ ] Implement Snowflake-style trace ID generator (based on Agrona) in `src/` with a public header.
- [ ] Add configuration for node ID and clock source; handle sequence rollover per tick.
- [ ] Integrate trace ID minting into producer path for root frames.
- [ ] Propagate trace_id for 1→1 stages (pass-through).
- [ ] Allow trace_id reset or continuity on epoch changes (document chosen default).

## Phase 3 - TraceLinkSet encode/decode

- [ ] Implement encode helper for TraceLinkSet messages.
- [ ] Implement decode helper with strict header validation (schemaId/templateId/version/blockLength).
- [ ] Validate parents[] constraints (numInGroup ≥ 1, non-zero values, uniqueness).
- [ ] Ensure `stream_id/epoch/seq/trace_id` match the associated FrameDescriptor at emission time.

## Phase 4 - Control-plane publish + poll

- [ ] Add producer API to emit TraceLinkSet for N→1 stages (best-effort, non-blocking).
- [ ] Add optional control poller hook for TraceLinkSet to enable capture/logging.
- [ ] Ensure emission uses recommended stream allocation (control stream per `STREAM_ID_CONVENTIONS.md`).

## Phase 5 - Tests

- [ ] Unit tests for trace ID generator monotonicity and uniqueness (per node).
- [ ] Encode/decode tests for TraceLinkSet with validation of parent constraints.
- [ ] End-to-end test: build a fake N→1 join and emit TraceLinkSet, then decode and validate.
- [ ] Negative tests: schema mismatch, zero parent, duplicate parent, empty group.

## Phase 6 - Tooling and docs

- [ ] Add a control-plane inspection mode to `tp_control_listen` for TraceLinkSet output (JSON and text).
- [ ] Update `docs/C_CLIENT_API_USAGE.md` with tracing usage (trace_id propagation + TraceLinkSet).
- [ ] Update `docs/COMPLIANCE_MATRIX_SHM_WIRE_V1_2.md` with TraceLink status.
