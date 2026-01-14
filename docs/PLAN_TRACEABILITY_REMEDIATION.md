# Traceability Remediation Plan (Spec Freeze)

This plan tracks gaps and uncertainties identified in `docs/TRACEABILITY_MATRIX_SHM_ALL.md`.

Legend:
- [ ] Not started
- [~] In progress
- [x] Done

## High Priority

- [x] W-8.3-1b / W-15.18-1: Add payload visibility hook for non-coherent DMA; add targeted tests.
- [x] W-15.21-1 / W-15.12-1: Fill out protocol state machine coverage for remap/fallback states; add tests for transitions.
- [x] D-4.7-1: Provide helper(s) for lease revoke handling (auto-reattach/backoff) or document required app behavior; add tests.

## Medium Priority

- [ ] W-15.10-1: Enforce permissions/ownership policy for SHM paths (uid/gid/mode checks) or document explicit opt-out.
- [ ] W-11-1: Expand consumer fallback modes and document behavior in API usage.
- [ ] TL-6.3-1: Add helper(s) to enforce TraceLink propagation rules for N->1 flows (auto-emit TraceLinkSet or documented hook).
- [ ] DS-3-1: Ensure discovery advisory semantics are clear in API docs and examples.

## Low Priority

- [ ] W-15.13-1: Expand test checklist coverage and add missing tests (deployment/liveness, incompat cases).

## External / Out of Scope

- [ ] W-10.5-1: Supervisor/unified management layer (external).
- [ ] D-2.1, D-4.3-1, D-4.8-1: Driver-owned policies and enforcement (external).
- [ ] DS-5.1-1, DS-6-1: Discovery provider/registry implementation (external).

## Open Questions / Uncertainties

- DMA flush hooks: decide which platforms require explicit flush instructions and how to expose that in the C API.
- TraceLink propagation: confirm whether automatic emission is required in core or remains a helper for applications.

## Current Focus Order

1. (pending selection)
