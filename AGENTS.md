# AGENTS.md

This repo implements the SHM Tensor Pool wire spec and an Aeron-style C API.

## Authoritative references
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/AERON_LIKE_API_PROPOSAL.md`
- `docs/STREAM_ID_CONVENTIONS.md`

## Build and codegen
- Build with CMake: `cmake --build build`
- Run tests: `ctest --test-dir build --output-on-failure`
- SBE codegen tool: `../simple-binary-encoding`
- Aeron C client source: `../aeron/aeron-client/src/main/c`

## Implementation conventions
- Match Aeron C client style for API design, error handling, and return codes.
- Keep SBE details out of the public API; wrap with TensorPool types/constants.
- Use `goto` error unwinding in tests only when teardown is complex.
- Enforce stream IDs per `docs/STREAM_ID_CONVENTIONS.md`.
- Keep edits ASCII unless a file already uses Unicode.
- Commit changes in small, logical changesets.

## Notes
- A running Aeron Media Driver is expected at `/dev/shm/aeron-dgamroth`.
- TensorPool driver may be absent; prefer no-driver tests when validating.
