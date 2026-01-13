#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${AERON_DIR:-}" ]]; then
  if [[ -d "/dev/shm/aeron-dgamroth" ]]; then
    AERON_DIR="/dev/shm/aeron-dgamroth"
  else
    AERON_DIR="/dev/shm/aeron"
  fi
fi

CONTROL_CHANNEL="aeron:ipc"
STREAM_ID=10000
CLIENT_ID=42
POOL_ID=1
POOL_STRIDE="${POOL_STRIDE:-$(getconf PAGESIZE 2>/dev/null || echo 4096)}"
HEADER_NSLOTS=32
EPOCH=1
LAYOUT_VERSION=1
MAX_FRAMES=16

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"

PRODUCER_BIN="${BUILD_DIR}/tp_example_producer_nodriver"
CONSUMER_BIN="${BUILD_DIR}/tp_example_consumer_nodriver"
SHM_CREATE_BIN="${BUILD_DIR}/tp_shm_create"

if [[ ! -x "${PRODUCER_BIN}" || ! -x "${CONSUMER_BIN}" || ! -x "${SHM_CREATE_BIN}" ]]; then
  echo "Missing binaries. Build first: cmake --build ${BUILD_DIR}" >&2
  exit 1
fi

tmpdir="$(mktemp -d)"
cleanup() {
  rm -rf "${tmpdir}"
}
trap cleanup EXIT

header_path="${tmpdir}/tp_header.bin"
pool_path="${tmpdir}/tp_pool.bin"
header_uri="shm:file?path=${header_path}"
pool_uri="shm:file?path=${pool_path}"

"${SHM_CREATE_BIN}" --noncanonical "${header_path}" header "${STREAM_ID}" "${EPOCH}" 0 "${HEADER_NSLOTS}" 0 "${LAYOUT_VERSION}"
"${SHM_CREATE_BIN}" --noncanonical "${pool_path}" pool "${STREAM_ID}" "${EPOCH}" "${POOL_ID}" "${HEADER_NSLOTS}" "${POOL_STRIDE}" "${LAYOUT_VERSION}"

set +e
"${CONSUMER_BIN}" "${AERON_DIR}" "${CONTROL_CHANNEL}" "${STREAM_ID}" "${CLIENT_ID}" "${MAX_FRAMES}" \
  "${header_uri}" "${pool_uri}" "${POOL_ID}" "${POOL_STRIDE}" "${HEADER_NSLOTS}" "${EPOCH}" "${LAYOUT_VERSION}" &
consumer_pid=$!

sleep 0.2
if ! kill -0 "${consumer_pid}" 2>/dev/null; then
  wait "${consumer_pid}"
  echo "Consumer failed to start" >&2
  exit 1
fi

"${PRODUCER_BIN}" "${AERON_DIR}" "${CONTROL_CHANNEL}" "${STREAM_ID}" "${CLIENT_ID}" \
  "${header_uri}" "${pool_uri}" "${POOL_ID}" "${POOL_STRIDE}" "${HEADER_NSLOTS}" "${EPOCH}" "${LAYOUT_VERSION}" "${MAX_FRAMES}"
producer_status=$?

wait "${consumer_pid}"
consumer_status=$?
set -e

if [[ ${producer_status} -ne 0 ]]; then
  echo "Producer failed with status ${producer_status}" >&2
  exit 1
fi

if [[ ${consumer_status} -ne 0 ]]; then
  echo "Consumer failed with status ${consumer_status}" >&2
  exit 1
fi

echo "No-driver example exchange succeeded."
