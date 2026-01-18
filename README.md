# Tensor Pool C

[![CI](https://github.com/DarrylGamroth/tensor_pool_c/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/DarrylGamroth/tensor_pool_c/actions/workflows/ci.yml)
[![Codecov](https://codecov.io/gh/DarrylGamroth/tensor_pool_c/branch/main/graph/badge.svg)](https://codecov.io/gh/DarrylGamroth/tensor_pool_c)

An Aeron-style C client for the SHM Tensor Pool wire specs. The implementation follows the
specs in `docs/` and keeps SBE details out of the public API.

## Build

Requirements:
- CMake 3.24+
- A C compiler
- Java 17+ (for SBE codegen)
- Aeron source checkout (default: `../aeron`)
- sbe-tool jar (set `SBE_TOOL_JAR` or let CMake fetch it)

Example:
```
cmake -S . -B build \
  -DAERON_ROOT=../aeron \
  -DSBE_TOOL_JAR=/path/to/sbe-tool-<ver>.jar
cmake --build build
```

## Tests

```
ctest --test-dir build --output-on-failure
```

Some integration paths expect a running Aeron Media Driver (see CI for example setup).

## Fuzz smoke

```
SBE_TOOL_JAR=/path/to/sbe-tool-<ver>.jar \
TP_AERON_ROOT=../aeron \
TP_FUZZ_RUNS=2000 \
TP_FUZZ_CLEAN=1 \
tools/run_fuzz_smoke.sh
```

## Docs

- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/AERON_LIKE_API_PROPOSAL.md`
- `docs/STREAM_ID_CONVENTIONS.md`
- `docs/C_CLIENT_API_USAGE.md`
