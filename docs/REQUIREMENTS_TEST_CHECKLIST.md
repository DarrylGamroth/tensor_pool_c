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
| B | Control and announce stream IDs differ | `config/driver_integration_announce_separate.toml` | `TP_EXAMPLE_ANNOUNCE_STREAM_ID=1002 TP_EXAMPLE_MAX_WAIT_MS=5000 STREAM_ID=10001 DRIVER_CONFIG=config/driver_integration_announce_separate.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds |
| C | Per-consumer descriptor/control streams | `config/driver_integration_example.toml` | `TP_EXAMPLE_PER_CONSUMER=1 TP_EXAMPLE_DESC_STREAM_ID=31001 TP_EXAMPLE_CTRL_STREAM_ID=32001 TP_EXAMPLE_REQUIRE_PER_CONSUMER=1 TP_EXAMPLE_ENABLE_CONSUMER_MANAGER=1 TP_EXAMPLE_WAIT_CONSUMER_MS=1000 TP_EXAMPLE_DROP_UNCONNECTED=1 TP_EXAMPLE_WAIT_CONNECTED_MS=0 TP_EXAMPLE_MAX_WAIT_MS=5000 MAX_FRAMES=1 DRIVER_CONFIG=config/driver_integration_example.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds and per-consumer subscriptions are assigned |
| D | Dynamic stream allocation | `config/driver_integration_dynamic.toml` | `TP_EXAMPLE_MAX_WAIT_MS=5000 STREAM_ID=20001 DRIVER_CONFIG=config/driver_integration_dynamic.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds on dynamically created stream |

Notes:
- Cases C/D are required by the spec but depend on test harness support; keep them flagged until covered.
- If the driver is not available, the cases above are manual and should be tracked as such in the traceability matrix.
