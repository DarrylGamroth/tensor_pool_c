#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
AERON_TENSORPOOL_DIR="${AERON_TENSORPOOL_DIR:-$ROOT_DIR/../AeronTensorPool.jl}"

if [[ -z "${AERON_DIR:-}" ]]; then
  if [[ -d "/dev/shm/aeron-dgamroth" ]]; then
    AERON_DIR="/dev/shm/aeron-dgamroth"
  else
    AERON_DIR="/dev/shm/aeron"
  fi
fi

CONTROL_CHANNEL="${CONTROL_CHANNEL:-aeron:ipc?term-length=4m}"
STREAM_ID="${STREAM_ID:-10000}"
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
MAX_FRAMES="${MAX_FRAMES:-1}"

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

cleanup() {
  if [[ -n "${driver_pid:-}" ]]; then
    kill "$driver_pid" 2>/dev/null || true
    wait "$driver_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

AERON_DIR="$AERON_DIR" \
TP_CONTROL_CHANNEL="$CONTROL_CHANNEL" \
TP_CONTROL_STREAM_ID="1000" \
LAUNCH_MEDIA_DRIVER="$launch_media_driver" \
  julia --project="$AERON_TENSORPOOL_DIR" \
  "$AERON_TENSORPOOL_DIR/scripts/run_driver.jl" "$DRIVER_CONFIG" &

driver_pid=$!

READY_TIMEOUT_S="${READY_TIMEOUT_S:-30}"
READY_SLEEP_S="${READY_SLEEP_S:-0.5}"
ready=false
SECONDS=0
while (( SECONDS < READY_TIMEOUT_S )); do
  set +e
  ready_id=$(( (READY_CONSUMER_ID_BASE + SECONDS) & 0xffffffff ))
  if [[ $ready_id -eq 0 ]]; then
    ready_id=1
  fi
  "$CONSUMER_BIN" "$AERON_DIR" "$CONTROL_CHANNEL" "$STREAM_ID" "$ready_id" 0
  status=$?
  set -e
  if [[ $status -eq 0 ]]; then
    ready=true
    break
  fi
  sleep "$READY_SLEEP_S"
done

if [[ $ready != true ]]; then
  echo "Driver did not become ready within ${READY_TIMEOUT_S}s" >&2
  exit 1
fi

set +e
"$CONSUMER_BIN" "$AERON_DIR" "$CONTROL_CHANNEL" "$STREAM_ID" "$CONSUMER_ID" "$MAX_FRAMES" &
consumer_pid=$!

sleep 0.2
if ! kill -0 "$consumer_pid" 2>/dev/null; then
  wait "$consumer_pid"
  echo "Consumer failed to start" >&2
  exit 1
fi

"$PRODUCER_BIN" "$AERON_DIR" "$CONTROL_CHANNEL" "$STREAM_ID" "$PRODUCER_ID"
producer_status=$?

wait "$consumer_pid"
consumer_status=$?
set -e

if [[ $producer_status -ne 0 ]]; then
  echo "Producer failed with status $producer_status" >&2
  exit 1
fi

if [[ $consumer_status -ne 0 ]]; then
  echo "Consumer failed with status $consumer_status" >&2
  exit 1
fi

echo "Driver example exchange succeeded."
