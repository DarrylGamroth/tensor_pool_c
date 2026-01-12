# Phased Implementation Plan

This plan implements the v1.1 wire spec with Aeron C client and SBE codecs, using CMake. It follows Aeron-style API patterns and emphasizes correctness, testability, and observability.

## Phase 0 - Spec alignment and API sketch

Status: Complete

- Resolve any spec ambiguities (e.g., seqlock encoding) and record the authoritative behavior.
- Define public C API surfaces mirroring Aeron (context structs, error codes, pollers, async command/response patterns).

## Phase 1 - Build and codegen scaffolding

Status: Complete

- CMake targets: `tensor_pool` library, `tensor_pool_tests` test runner, `tensor_pool_tools` examples/CLI.
- SBE code generation via `../simple-binary-encoding` into build output.
- Aeron C client integration via `../aeron/aeron-client/src/main/c`.

## Phase 2 - Core wire + SHM utilities

Status: Complete

- SHM URI parsing and validation (`shm:file?path=...|require_hugepages=...`).
- SHM mapping, superblock validation, epoch/layout_version checks.
- SlotHeader + TensorHeader parsing/validation; stride rules; progress validation.
- Seqlock helpers with acquire/release semantics.
- Aeron-style error handling and logging hooks.

## Phase 3 - Aeron control-plane plumbing

Status: Complete

- Publication/subscription wrappers for descriptor/control/qos/metadata streams.
- FragmentAssembler-based pollers.
- SchemaId/templateId gating for mixed streams.

## Phase 4 - Producer API

Status: Complete

- Attach and SHM mapping via driver-model or direct mode.
- Payload write path (seqlock in-progress -> payload write -> header write -> commit).
- FrameDescriptor publish, optional FrameProgress and QoS.
- Pool selection and drop behavior per spec.

## Phase 5 - Consumer API

Status: Complete

- Attach and SHM mapping via driver-model or direct mode.
- Descriptor polling, seqlock validation, payload accessors.
- Optional per-consumer descriptor/control streams.
- Drop logic, QoS accounting, and liveness checks.

## Phase 6 - Driver model support

Status: Complete

- ShmAttachRequest/Response and lease lifecycle.
- Keepalive/detach/revoke handling.
- Driver-side validation of URIs and pool parameters.

## Phase 7 - Discovery support (optional)

Status: Complete

- Discovery request/response codecs and client helper.
- Registry/driver discovery adapter.

## Phase 8 - Logging and diagnostics

Status: Complete

- Aeron-style logging API with levels and optional callbacks.
- Structured debug output for attaches, mapping, drops, and validation failures.

## Phase 9 - Comprehensive tests

Status: Complete

- Unit tests: URI parsing, superblock validation, seqlock, tensor header validation, strides/progress rules.
- Integration tests with Aeron Media Driver at `/dev/shm/aeron-dgamroth`.
- Producer/consumer interop tests across shared and per-consumer streams.

## Phase 10 - Tools and examples

Status: Complete

- CLI tools for attach/keepalive, SHM inspection, and sample producer/consumer.
- Example configs and usage docs.
