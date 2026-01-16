# Requirements-to-Tests Checklist

This checklist is the authoritative mapping from normative requirements (MUST/SHOULD) to tests or explicit verification steps. The detailed mapping is maintained in `docs/TRACEABILITY_MATRIX_SHM_ALL.md`; any new or changed MUST/SHOULD requirement MUST be added there with a test or verification reference.

## Checklist Rules

- Every MUST/SHOULD requirement in the authoritative specs MUST have a matching entry in `docs/TRACEABILITY_MATRIX_SHM_ALL.md`.
- Each entry MUST link to at least one automated test or an explicit manual verification step.
- Any requirement lacking coverage MUST be flagged for follow-up before changes are considered complete.

## Config-Matrix Integration Tests

These tests ensure distinct spec-driven configuration permutations are exercised. They are expected to run when a TensorPool driver is available (Julia driver is acceptable).

| Case | Purpose | Driver Config | Command | Expected Result |
| --- | --- | --- | --- | --- |
| A | Control and announce stream IDs equal | `config/driver_integration_example.toml` | `TP_EXAMPLE_MAX_WAIT_MS=5000 DRIVER_CONFIG=config/driver_integration_example.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds |
| B | Control and announce stream IDs differ | `config/driver_integration_announce_separate.toml` | `TP_EXAMPLE_ANNOUNCE_STREAM_ID=1002 TP_EXAMPLE_MAX_WAIT_MS=10000 PRODUCER_FRAMES=4 STREAM_ID=10001 DRIVER_CONFIG=config/driver_integration_announce_separate.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds |
| C | Per-consumer descriptor/control streams | `config/driver_integration_example.toml` | `TP_EXAMPLE_PER_CONSUMER=1 TP_EXAMPLE_DESC_STREAM_ID=31001 TP_EXAMPLE_CTRL_STREAM_ID=32001 TP_EXAMPLE_REQUIRE_PER_CONSUMER=1 TP_EXAMPLE_ENABLE_CONSUMER_MANAGER=1 TP_EXAMPLE_WAIT_CONSUMER_MS=1000 TP_EXAMPLE_DROP_UNCONNECTED=1 TP_EXAMPLE_WAIT_CONNECTED_MS=0 TP_EXAMPLE_MAX_WAIT_MS=5000 MAX_FRAMES=1 DRIVER_CONFIG=config/driver_integration_example.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds and per-consumer subscriptions are assigned |
| D | Dynamic stream allocation | `config/driver_integration_dynamic.toml` | `TP_EXAMPLE_MAX_WAIT_MS=5000 STREAM_ID=20001 DRIVER_CONFIG=config/driver_integration_dynamic.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds on dynamically created stream |

Notes:
- Cases A-D are automated by `tools/run_driver_matrix.sh` when the driver is available.
- If the driver is not available, the cases above are manual and should be tracked as such in the traceability matrix.

## Driver Lifecycle/Behavior Integration Tests

These tests exercise runtime behavior that is not covered by the config matrix. They are expected to run when a TensorPool driver is available.

| Case | Purpose | Driver Config | Command | Expected Result |
| --- | --- | --- | --- | --- |
| E | FrameProgress on control stream | `config/driver_integration_example.toml` | `DRIVER_CONFIG=config/driver_integration_example.toml tools/run_driver_compliance.sh` | Consumer receives progress updates |
| F | Lease expiry without keepalive | `config/driver_integration_example.toml` | `DRIVER_CONFIG=config/driver_integration_example.toml tools/run_driver_compliance.sh` | Client observes lease expiry and exits cleanly |
| G | Epoch increments across driver restart | `config/driver_integration_example.toml` | `DRIVER_CONFIG=config/driver_integration_example.toml tools/run_driver_compliance.sh` | Second attach epoch > first |
| H | Hugepages requirement rejected when unavailable | `config/driver_integration_example.toml` | `DRIVER_CONFIG=config/driver_integration_example.toml tools/run_driver_compliance.sh` | Attach rejected if `HugePages_Total=0` (skipped if enabled) |

Notes:
- Cases E-H are automated by `tools/run_driver_compliance.sh`.
- Case H is skipped automatically if hugepages are enabled on the host.

## Coverage (optional)

Build with coverage enabled and run the target:

```sh
cmake -S . -B build -DTP_ENABLE_COVERAGE=ON
cmake --build build --target coverage
```

Requires `gcovr` in `PATH`.
