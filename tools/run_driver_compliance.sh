#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
AERON_TENSORPOOL_DIR="${AERON_TENSORPOOL_DIR:-$ROOT_DIR/../AeronTensorPool.jl}"
RUNNER="$ROOT_DIR/tools/run_driver_examples.sh"

if [[ -z "${AERON_DIR:-}" ]]; then
  if [[ -d "/dev/shm/aeron-dgamroth" ]]; then
    AERON_DIR="/dev/shm/aeron-dgamroth"
  else
    AERON_DIR="/dev/shm/aeron"
  fi
fi

CONTROL_CHANNEL="${CONTROL_CHANNEL:-aeron:ipc?term-length=4m}"
STREAM_ID="${STREAM_ID:-}"
if [[ -z "${PRODUCER_ID:-}" ]]; then
  PRODUCER_ID=$(( (RANDOM << 16) | RANDOM ))
fi
if [[ -z "${CONSUMER_ID:-}" ]]; then
  CONSUMER_ID=$(( (RANDOM << 16) | RANDOM ))
fi
if [[ "${PRODUCER_ID}" -eq 0 ]]; then
  PRODUCER_ID=1
fi
if [[ "${CONSUMER_ID}" -eq 0 ]]; then
  CONSUMER_ID=1
fi
if [[ "${CONSUMER_ID}" -eq "${PRODUCER_ID}" ]]; then
  CONSUMER_ID=$(( (CONSUMER_ID + 1) & 0xffffffff ))
fi
READY_CONSUMER_ID_BASE="${READY_CONSUMER_ID_BASE:-$(( (CONSUMER_ID + 1000) & 0xffffffff ))}"

CONFIG_DEFAULT="$ROOT_DIR/config/driver_integration_example.toml"
CONFIG_FALLBACK="$AERON_TENSORPOOL_DIR/config/driver_integration_example.toml"
DRIVER_CONFIG="${DRIVER_CONFIG:-$CONFIG_DEFAULT}"

if [[ ! -f "$DRIVER_CONFIG" ]]; then
  if [[ -f "$CONFIG_FALLBACK" ]]; then
    DRIVER_CONFIG="$CONFIG_FALLBACK"
  else
    echo "Driver config not found: $DRIVER_CONFIG" >&2
    exit 1
  fi
fi

if [[ -z "$STREAM_ID" ]]; then
  STREAM_ID="$(awk '
    $0 ~ /^\[streams\./ { in_stream = 1; next }
    in_stream && $0 ~ /^[[:space:]]*stream_id[[:space:]]*=/ {
      sub(/#.*/, "", $0);
      gsub(/[^0-9]/, "", $0);
      if (length($0) > 0) { print $0; exit }
    }
    $0 ~ /^\[/ { in_stream = 0 }
  ' "$DRIVER_CONFIG")"
fi
STREAM_ID="${STREAM_ID:-10000}"

PRODUCER_BIN="$BUILD_DIR/tp_example_producer_driver"
CONSUMER_BIN="$BUILD_DIR/tp_example_consumer_driver"

if [[ ! -x "$PRODUCER_BIN" || ! -x "$CONSUMER_BIN" ]]; then
  echo "Missing binaries. Build first: cmake --build $BUILD_DIR" >&2
  exit 1
fi

if [[ ! -d "$AERON_TENSORPOOL_DIR" ]]; then
  echo "AeronTensorPool.jl not found at $AERON_TENSORPOOL_DIR" >&2
  exit 1
fi

if ! command -v julia >/dev/null 2>&1; then
  echo "julia not found in PATH" >&2
  exit 1
fi

launch_media_driver="${LAUNCH_MEDIA_DRIVER:-}"
if [[ -z "$launch_media_driver" ]]; then
  if [[ -f "$AERON_DIR/CnC.dat" || -f "$AERON_DIR/cnc.dat" ]]; then
    launch_media_driver="false"
  else
    launch_media_driver="true"
  fi
fi

start_driver() {
  AERON_DIR="$AERON_DIR" \
  TP_CONTROL_CHANNEL="$CONTROL_CHANNEL" \
  TP_CONTROL_STREAM_ID="1000" \
  LAUNCH_MEDIA_DRIVER="$launch_media_driver" \
    julia --project="$AERON_TENSORPOOL_DIR" \
    "$AERON_TENSORPOOL_DIR/scripts/run_driver.jl" "$DRIVER_CONFIG" &
  driver_pid=$!
}

stop_driver() {
  if [[ -n "${driver_pid:-}" ]]; then
    kill "$driver_pid" 2>/dev/null || true
    wait "$driver_pid" 2>/dev/null || true
    driver_pid=""
  fi
}

wait_ready() {
  local ready_timeout="${READY_TIMEOUT_S:-30}"
  local ready_sleep="${READY_SLEEP_S:-0.5}"
  local ready=false
  local attempt=0

  while (( attempt < ready_timeout )); do
    local ready_id=$(( (READY_CONSUMER_ID_BASE + attempt) & 0xffffffff ))
    if [[ $ready_id -eq 0 ]]; then
      ready_id=1
    fi
    set +e
    "$CONSUMER_BIN" "$AERON_DIR" "$CONTROL_CHANNEL" "$STREAM_ID" "$ready_id" 0
    local status=$?
    set -e
    if [[ $status -eq 0 ]]; then
      ready=true
      break
    fi
    sleep "$ready_sleep"
    attempt=$(( attempt + 1 ))
  done

  if [[ $ready != true ]]; then
    echo "Driver did not become ready within ${ready_timeout}s" >&2
    return 1
  fi
  return 0
}

run_case() {
  local label="$1"
  shift
  echo "==> $label"
  "$@"
}

run_lease_expiry_case() {
  start_driver
  wait_ready
  if ! TP_EXAMPLE_EXPECT_LEASE_EXPIRE=1 \
       TP_EXAMPLE_KEEPALIVE_INTERVAL_MS=0 \
       TP_EXAMPLE_MAX_WAIT_MS=8000 \
       "$CONSUMER_BIN" "$AERON_DIR" "$CONTROL_CHANNEL" "$STREAM_ID" "$CONSUMER_ID" 1; then
    echo "Lease expiry test failed" >&2
    stop_driver
    return 1
  fi
  stop_driver
  return 0
}

run_epoch_restart_case() {
  local out1
  local out2
  local epoch1
  local epoch2

  start_driver
  wait_ready
  out1=$(TP_EXAMPLE_PRINT_ATTACH=1 \
    "$PRODUCER_BIN" "$AERON_DIR" "$CONTROL_CHANNEL" "$STREAM_ID" "$PRODUCER_ID" 1 2>&1)
  epoch1=$(echo "$out1" | awk -F"[ =]" '/Attach info/ {for (i=1; i<=NF; i++) if ($i=="epoch") {print $(i+1); exit}}')
  stop_driver

  if [[ -z "$epoch1" ]]; then
    echo "Failed to parse epoch from first attach" >&2
    return 1
  fi

  start_driver
  wait_ready
  out2=$(TP_EXAMPLE_PRINT_ATTACH=1 \
    "$PRODUCER_BIN" "$AERON_DIR" "$CONTROL_CHANNEL" "$STREAM_ID" "$PRODUCER_ID" 1 2>&1)
  epoch2=$(echo "$out2" | awk -F"[ =]" '/Attach info/ {for (i=1; i<=NF; i++) if ($i=="epoch") {print $(i+1); exit}}')
  stop_driver

  if [[ -z "$epoch2" ]]; then
    echo "Failed to parse epoch from second attach" >&2
    return 1
  fi

  if [[ "$epoch2" -le "$epoch1" ]]; then
    echo "Epoch did not advance (epoch1=$epoch1 epoch2=$epoch2)" >&2
    return 1
  fi

  return 0
}

run_hugepages_case() {
  local total
  local status

  total=$(awk '/^HugePages_Total:/ {print $2}' /proc/meminfo)
  if [[ -n "$total" && "$total" -gt 0 ]]; then
    echo "Hugepages enabled (HugePages_Total=$total). Skipping negative-case test."
    return 0
  fi

  start_driver
  wait_ready
  set +e
  TP_EXAMPLE_REQUIRE_HUGEPAGES=1 \
    "$CONSUMER_BIN" "$AERON_DIR" "$CONTROL_CHANNEL" "$STREAM_ID" "$CONSUMER_ID" 1
  status=$?
  set -e
  stop_driver

  if [[ $status -eq 0 ]]; then
    echo "Hugepages requirement unexpectedly succeeded" >&2
    return 1
  fi

  return 0
}

cleanup() {
  stop_driver
}
trap cleanup EXIT

run_case "case E: progress stream" env \
  TP_EXAMPLE_PUBLISH_PROGRESS=1 \
  TP_EXAMPLE_REQUIRE_PROGRESS=1 \
  TP_EXAMPLE_MAX_WAIT_MS=5000 \
  MAX_FRAMES=4 \
  DRIVER_CONFIG="$DRIVER_CONFIG" \
  "$RUNNER"

run_case "case F: lease expiry without keepalive" run_lease_expiry_case

run_case "case G: epoch increases across driver restart" run_epoch_restart_case

run_case "case H: hugepages requirement rejected when unavailable" run_hugepages_case
