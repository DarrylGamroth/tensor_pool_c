#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TP_FUZZ_BUILD_DIR:-$ROOT_DIR/build-fuzz-smoke}"
CORPUS_DIR="${TP_FUZZ_CORPUS_DIR:-$ROOT_DIR/fuzz/corpus}"
RUNS="${TP_FUZZ_RUNS:-2000}"

if ! command -v clang >/dev/null 2>&1; then
    echo "clang not found; install clang with libFuzzer support."
    exit 1
fi

cmake_args=(
    -DTP_ENABLE_FUZZ=ON
    -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_C_COMPILER=clang
)
if [[ -n "${TP_AERON_ROOT:-}" ]]; then
    cmake_args+=("-DAERON_ROOT=${TP_AERON_ROOT}")
fi
if [[ -n "${SBE_TOOL_JAR:-}" ]]; then
    cmake_args+=("-DSBE_TOOL_JAR=${SBE_TOOL_JAR}")
fi
if [[ -n "${SBE_TOOL_CLASSPATH:-}" ]]; then
    cmake_args+=("-DSBE_TOOL_CLASSPATH=${SBE_TOOL_CLASSPATH}")
fi
if [[ -n "${TP_USE_SYSTEM_AERON:-}" ]]; then
    cmake_args+=("-DTP_USE_SYSTEM_AERON=${TP_USE_SYSTEM_AERON}")
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${cmake_args[@]}"
cmake --build "$BUILD_DIR"

"$BUILD_DIR"/tp_fuzz_seed_gen
if [[ -x "$BUILD_DIR/tp_fuzz_seed_gen_discovery" ]]; then
    "$BUILD_DIR"/tp_fuzz_seed_gen_discovery
fi

TMP_CORPUS_DIR="$BUILD_DIR/corpus-smoke"
rm -rf "$TMP_CORPUS_DIR"
mkdir -p "$TMP_CORPUS_DIR"

fuzzers=(
    tp_fuzz_control_decode
    tp_fuzz_control_poller
    tp_fuzz_discovery_decode
    tp_fuzz_merge_map_decode
    tp_fuzz_metadata_poller
    tp_fuzz_producer_claim
    tp_fuzz_progress_validate
    tp_fuzz_qos_poller
    tp_fuzz_shm_superblock
    tp_fuzz_slot_tensor_decode
    tp_fuzz_tensor_header
    tp_fuzz_tracelink_decode
    tp_fuzz_slot_decode
    tp_fuzz_shm_uri
)

for fuzzer in "${fuzzers[@]}"; do
    if [[ ! -x "$BUILD_DIR/$fuzzer" ]]; then
        echo "Missing fuzzer binary: $fuzzer"
        exit 1
    fi
    if [[ ! -d "$CORPUS_DIR/$fuzzer" ]]; then
        echo "Missing corpus directory: $CORPUS_DIR/$fuzzer"
        exit 1
    fi
    mkdir -p "$TMP_CORPUS_DIR/$fuzzer"
    cp -a "$CORPUS_DIR/$fuzzer/." "$TMP_CORPUS_DIR/$fuzzer/"
    "$BUILD_DIR/$fuzzer" -runs="$RUNS" "$TMP_CORPUS_DIR/$fuzzer"
done

rm -rf "$TMP_CORPUS_DIR"

if [[ "${TP_FUZZ_CLEAN:-}" == "1" ]]; then
    rm -rf "$BUILD_DIR"
fi
