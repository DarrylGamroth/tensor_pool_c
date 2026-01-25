# Coverage Improvement Plan

Goal: raise overall coverage above 80% and increase coverage of spec-critical logic (SHM, control plane, progress, discovery, driver client). This plan assumes no TensorPool driver in CI; driver-integration tests remain optional and gated.

## Success Criteria
- Overall line coverage >= 80% (CI target).
- Each spec-critical module >= 70% line coverage.
- Every MUST/SHOULD requirement in the specs has a mapped test or an explicit manual verification note.

## Phase 0: Baseline and Targeting
- [x] Generate a per-file coverage report (gcovr) and save a snapshot under `docs/coverage/` for baseline.
- [x] Identify bottom 10 files by line coverage and tag each with a test strategy (unit, integration, fuzz seed).
- [x] Update the requirements-to-tests checklist with current gaps.

Baseline (gcovr, excluding tests/tools/fuzz/Aeron/build artifacts):
- lines: 56.7% (3829 / 6759)
- functions: 71.2% (270 / 379)
- branches: 44.8% (1983 / 4427)

Current (gcovr, excluding tests/tools/fuzz/Aeron/build artifacts):
- lines: 65.9% (6893 / 10453)
- functions: 85.1% (497 / 584)
- branches: 53.6% (3549 / 6627)

Bottom 10 files by line coverage (baseline):
- `include/tensor_pool/tp_error.h` (0.0%) -> unit (exercise error setters via API failures)
- `src/common/tp_aeron.c` (0.0%) -> unit (wrap Aeron errors + directory resolution)
- `src/client/tp_control_poller.c` (0.0%) -> unit/fuzz (direct handler tests)
- `src/client/tp_driver_client.c` (28.1%) -> unit (driver attach/reject paths)
- `src/client/tp_discovery_client.c` (38.6%) -> unit (error paths + invalid responses)
- `src/common/tp_merge_map.c` (42.3%) -> unit (merge rules + map apply)
- `src/client/tp_client.c` (42.9%) -> unit (context defaults, subscription errors)
- `src/common/tp_tracelink.c` (51.8%) -> unit (trace link edge cases)
- `src/common/tp_log.c` (54.2%) -> unit (log routing, levels)
- `src/client/tp_client_conductor.c` (56.0%) -> unit (init/start/stop error handling)

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
- [x] Add a control/metadata/QoS listener test to verify JSON output paths (stderr). (JSON escaping covered via `tp_control_listen` unit test)

## Phase 3: Driver Integration (Optional / External)
- [ ] Add a driver-backed integration test gated by `TP_ENABLE_DRIVER_TESTS=ON`.
- [ ] Use `scripts/run_driver.jl` when available; skip with clear message if absent.
- [ ] Validate attach/keepalive/lease revoke flows end-to-end.

## Phase 4: Fuzz Coverage Boost
- [x] Add targeted fuzz seeds for edge-case SBE messages and invalid var-data lengths.
- [x] Add at least one new fuzz target that exercises URI parsing + SHM validation together.

## Phase 5: CI Enforcement
- [x] Add CI coverage thresholds (overall) with progressive enforcement.
- [x] Document how to reproduce coverage locally in `README.md`.

## Phase 6: Coverage Uplift (Target >= 70% Lines, >= 50% Branches)
- [x] Add unit tests for `tp_aeron.c` (directory resolution, error propagation, invalid inputs).
- [x] Add unit tests for `tp_control_poller.c` using direct handler entrypoint (valid/invalid headers + template paths).
- [x] Expand driver client unit tests to cover attach/keepalive/lease expiry and rejection branches.
- [x] Expand discovery client tests for malformed channel strings and missing fields.
- [x] Add tests for `tp_client.c` error paths (invalid channels/stream IDs, async add failures).
- [x] Add trace/log tests for `tp_log.c` and `tp_error.h` to cover formatting and error propagation.
- [x] Add merge map tests to exercise all rule types and edge cases (empty, conflicting, out-of-order).

## Phase 7: Coverage Targets (>= 70% Lines, >= 55% Branches)
- [x] Add consumer/producer error-path tests for descriptor parsing, progress validation, and claim/offer failure branches.
- [x] Add QoS/metadata poller tests for invalid messages, empty fragments, and channel mismatch handling.
- [x] Expand SHM/URI permission-path tests to hit negative cases (uid/gid/mode mismatches, canonical path rejection).
- [x] Add trace + join-barrier edge tests for stale inputs, missing clocks, and latest ordering paths.

## Phase 8: Coverage Uplift (Producer/Consumer + Driver Client)
- [x] Add a no-driver SHM roundtrip test that publishes a frame into the ring and validates `tp_consumer_read_frame` + progress validation.
- [x] Add an embedded-driver test that exercises driver attach/detach async polling with real Aeron publications.

## Phase 9: Coverage Uplift (Discovery Client)
- [x] Add an embedded-driver test that exercises discovery request encoding, response polling, and poller callbacks end-to-end.

## Phase 10: Coverage Finishers
- [x] Add client context setter coverage and additional conductor error-path tests.
- [x] Add tracelink send + claim helper tests with embedded Aeron control publication.
- [x] Add progress poller misc coverage and control poller error-path tests.
- [x] Add extra tensor header validation cases (including log path).
