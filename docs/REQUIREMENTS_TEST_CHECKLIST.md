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
| A | Control and announce stream IDs equal | `config/driver_integration_example.toml` | `DRIVER_CONFIG=config/driver_integration_example.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds |
| B | Control and announce stream IDs differ | `config/driver_integration_announce_separate.toml` | `TP_EXAMPLE_ANNOUNCE_STREAM_ID=1002 STREAM_ID=10001 DRIVER_CONFIG=config/driver_integration_announce_separate.toml tools/run_driver_examples.sh` | Producer/consumer exchange succeeds |
| C | Per-consumer descriptor/control streams | TBD | Manual: request per-consumer streams and verify consumer switches subscriptions | Verified when example support exists |
| D | Dynamic stream allocation | TBD | Manual: enable `allow_dynamic_streams` and validate stream assignment | Verified when driver config is available |

Notes:
- Cases C/D are required by the spec but depend on test harness support; keep them flagged until covered.
- If the driver is not available, the cases above are manual and should be tracked as such in the traceability matrix.
