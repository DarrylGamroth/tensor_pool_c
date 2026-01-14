# Traceability Remediation Plan (Spec Freeze)

This plan tracks gaps and uncertainties identified in `docs/TRACEABILITY_MATRIX_SHM_ALL.md`.

Legend:
- [ ] Not started
- [~] In progress
- [x] Done

## High Priority

- [ ] W-15.20-1: Implement layout/wire compatibility matrix gating in attach/consume paths (`src/tp_shm.c`, `src/tp_consumer.c`); add tests.
- [ ] W-8.3-1 / W-15.3-1: Add explicit `seq_commit` regression handling and platform hooks for DMA flush; add tests for stale/stomped slots.
- [ ] D-4.4-1 / D-4.7-1: Add driver-client keepalive scheduler, attach retry/backoff, and lease expiry handling; add tests.
- [ ] W-15.21-1 / W-15.12-1: Fill out protocol state machine coverage for remap/fallback states; add tests for transitions.

## Medium Priority

- [ ] W-10.4-1: Enforce QoS publish cadence (producer/consumer) per spec; add cadence tests.
- [ ] W-15.14-1: Enforce ShmPoolAnnounce cadence policy and liveness timeouts in core; add tests.
- [ ] W-15.10-1: Enforce permissions/ownership policy for SHM paths (uid/gid/mode checks) or document explicit opt-out.
- [ ] W-15.8-1: Implement enum/type registry version checks and compatibility gating.
- [ ] W-11-1: Expand consumer fallback modes and document behavior in API usage.
- [ ] W-15.16a-1: Implement file-backed SHM policy (prefault/lock/fsync) or explicitly mark unsupported in code paths.
- [ ] TL-6.3-1: Add helper(s) to enforce TraceLink propagation rules for N->1 flows (auto-emit TraceLinkSet or documented hook).
- [ ] DS-3-1: Ensure discovery advisory semantics are clear in API docs and examples.

## Low Priority

- [ ] W-15.13-1: Expand test checklist coverage and add missing tests (deployment/liveness, incompat cases).

## External / Out of Scope

- [ ] W-10.5-1: Supervisor/unified management layer (external).
- [ ] D-2.1, D-4.3-1, D-4.8-1: Driver-owned policies and enforcement (external).
- [ ] DS-5.1-1, DS-6-1: Discovery provider/registry implementation (external).
- [ ] BR-1: UDP bridge implementation (external).

## Open Questions / Uncertainties

- Compatibility matrix details: confirm how to apply layout/wire compatibility rules in client attach paths.
- DMA flush hooks: decide which platforms require explicit flush instructions and how to expose that in the C API.
- TraceLink propagation: confirm whether automatic emission is required in core or remains a helper for applications.
