# Coverage Improvement Plan

Goal: raise overall coverage above 80% and increase coverage of spec-critical logic (SHM, control plane, progress, discovery, driver client). This plan assumes no TensorPool driver in CI; driver-integration tests remain optional and gated.

## Success Criteria
- Overall line coverage >= 80% (CI target).
- Each spec-critical module >= 70% line coverage.
- Every MUST/SHOULD requirement in the specs has a mapped test or an explicit manual verification note.

## Phase 0: Baseline and Targeting
- [ ] Generate a per-file coverage report (gcovr) and save a snapshot under `docs/coverage/` for baseline. (blocked: gcovr missing locally; use CI or install)
- [ ] Identify bottom 10 files by line coverage and tag each with a test strategy (unit, integration, fuzz seed). (blocked on baseline)
- [ ] Update the requirements-to-tests checklist with current gaps. (blocked on baseline)

## Phase 1: Core Unit Tests (No Driver)
- [x] `tp_control_adapter.c`: cover all decode/validate branches, including length, schema/version, and invalid var-data cases.
- [x] `tp_discovery_client.c`: cover invalid response filtering (missing control channel/stream). (already covered in tests)
- [x] `tp_tensor.c`: cover dtype/stride/shape validation edge cases (zero dims, invalid strides, bad header version).
- [x] `tp_progress_poller.c` + `tp_consumer.c`: cover progress validation, monotonic checks, and seqlock handling.
- [x] `tp_shm.c` + `tp_uri.c`: cover canonical URI parsing, permissions checks, and hugepage negative cases.
- [x] `tp_tracelink.c`: cover parent caps, invalid linkages, and trace id generation boundaries.

## Phase 2: Integration Tests (Media Driver Only)
- [x] Add a no-driver producer/consumer integration test that exchanges >= 16 frames and validates payload integrity. (covered by `tp_test_rollover`)
- [x] Add rollover stress test (header ring wrap) with pacing to avoid false drops. (covered by `tp_test_rollover`)
- [ ] Add a control/metadata/QoS listener test to verify JSON output paths (stderr).

## Phase 3: Driver Integration (Optional / External)
- [ ] Add a driver-backed integration test gated by `TP_ENABLE_DRIVER_TESTS=ON`.
- [ ] Use `scripts/run_driver.jl` when available; skip with clear message if absent.
- [ ] Validate attach/keepalive/lease revoke flows end-to-end.

## Phase 4: Fuzz Coverage Boost
- [ ] Add targeted fuzz seeds for edge-case SBE messages and invalid var-data lengths.
- [ ] Add at least one new fuzz target that exercises URI parsing + SHM validation together.

## Phase 5: CI Enforcement
- [ ] Add CI coverage thresholds (overall and per-module) with progressive enforcement.
- [ ] Document how to reproduce coverage locally in `README.md`.
