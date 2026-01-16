#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNNER="$ROOT_DIR/tools/run_driver_examples.sh"

if [[ ! -x "$RUNNER" ]]; then
  echo "Missing runner: $RUNNER" >&2
  exit 1
fi

run_case() {
  local label="$1"
  shift
  echo "==> $label"
  if [[ $# -eq 0 ]]; then
    "$RUNNER"
  else
    env "$@" "$RUNNER"
  fi
}

run_case "case A: control == announce (1001)" \
  TP_EXAMPLE_MAX_WAIT_MS=5000 \
  DRIVER_CONFIG="$ROOT_DIR/config/driver_integration_example.toml"

run_case "case B: control != announce (1002)" \
  TP_EXAMPLE_ANNOUNCE_STREAM_ID=1002 \
  TP_EXAMPLE_MAX_WAIT_MS=10000 \
  PRODUCER_FRAMES=4 \
  STREAM_ID=10001 \
  DRIVER_CONFIG="$ROOT_DIR/config/driver_integration_announce_separate.toml"

run_case "case C: per-consumer descriptor/control streams" \
  TP_EXAMPLE_PER_CONSUMER=1 \
  TP_EXAMPLE_DESC_STREAM_ID=31001 \
  TP_EXAMPLE_CTRL_STREAM_ID=32001 \
  TP_EXAMPLE_REQUIRE_PER_CONSUMER=1 \
  TP_EXAMPLE_ENABLE_CONSUMER_MANAGER=1 \
  TP_EXAMPLE_WAIT_CONSUMER_MS=1000 \
  TP_EXAMPLE_DROP_UNCONNECTED=1 \
  TP_EXAMPLE_WAIT_CONNECTED_MS=0 \
  TP_EXAMPLE_MAX_WAIT_MS=5000 \
  MAX_FRAMES=1 \
  DRIVER_CONFIG="$ROOT_DIR/config/driver_integration_example.toml"

run_case "case D: dynamic stream allocation" \
  TP_EXAMPLE_MAX_WAIT_MS=5000 \
  STREAM_ID=20001 \
  DRIVER_CONFIG="$ROOT_DIR/config/driver_integration_dynamic.toml"
