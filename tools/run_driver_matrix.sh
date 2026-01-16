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
  DRIVER_CONFIG="$ROOT_DIR/config/driver_integration_example.toml"

run_case "case B: control != announce (1002)" \
  TP_EXAMPLE_ANNOUNCE_STREAM_ID=1002 \
  STREAM_ID=10001 \
  DRIVER_CONFIG="$ROOT_DIR/config/driver_integration_announce_separate.toml"
