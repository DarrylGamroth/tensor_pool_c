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

## Follow-up items

### Fully implement partially implemented features

Scope
- Control-plane decoders for ConsumerHello/ConsumerConfig/DataSourceAnnounce/DataSourceMeta.
- Per-consumer descriptor/control stream negotiation and lifecycle handling.
- Progress throttling and policy hint aggregation.

Steps
1. Add control-plane decoders and adapters
   - Implement SBE decode helpers for control-plane messages (ConsumerHello/ConsumerConfig/DataSourceAnnounce/DataSourceMeta).
   - Add a control-plane poller that gates on schemaId/templateId and dispatches to handlers.
   - Provide Aeron-style handler callbacks with user clientd.
2. Implement per-consumer stream negotiation
   - Parse per-consumer stream requests from ConsumerHello.
   - Validate stream/channel request semantics (both present or both absent).
   - Assign/track per-consumer publications and subscriptions; close on stale consumer timeout.
3. Add progress throttling logic
   - Store per-consumer progress hints and derive producer aggregate policy (min interval/delta with safety floors).
   - Apply throttling to FrameProgress publishes.
4. Tests
   - Add unit tests for ConsumerHello/ConsumerConfig parsing and validation.
   - Add tests for per-consumer stream request handling and stale cleanup.
   - Add tests for progress throttling behavior (interval and delta).

Deliverables
- `src/tp_control.c` decoders + poller.
- New `tp_control_adapter` or similar to surface callbacks.
- Per-consumer stream manager module.

### Fully implement the driver client

Scope
- Complete attach response validation per Driver Spec.
- Decode and surface non-attach responses (ShmDetachResponse, ShmLeaseRevoked, ShmDriverShutdown).
- Improve lifecycle cleanup and state tracking.

Steps
1. Attach validation enhancements
   - Verify required fields (slot bytes, maxDims, payload pool count, nslots match).
   - Validate optional fields and nullValue handling.
2. Non-attach message decoding
   - Add decoders for ShmDetachResponse, ShmLeaseRevoked, ShmDriverShutdown, ShmDriverShutdownRequest responses if needed.
   - Provide a driver event poller (schemaId/templateId gating) with callbacks.
3. State tracking
   - Track active lease, expiry, and last keepalive timestamp.
   - Expose helper APIs for keepalive cadence and stale detection.
4. Tests
   - Unit tests for attach response validation (nulls, mismatches).
   - Unit tests for lease revoked/shutdown decoding.

Deliverables
- `src/tp_driver_client.c` decoding + poller.
- `include/tensor_pool/tp_driver_client.h` handler APIs.

### Fully implement the discovery client

Scope
- Decode full DiscoveryResponse results including nested groups.
- Add lifecycle cleanup helpers and query filters.

Steps
1. Response parsing improvements
   - Decode nested payloadPools and tags groups with correct position handling.
   - Validate required fields (headerSlotBytes, maxDims, payload pools).
2. Query filters
   - Add request helpers for tag filters and optional fields.
   - Add result filtering helpers (stream_id, producer_id, data_source_name).
3. Cleanup API
   - Extend cleanup helpers to free nested pools/tags per result.
4. Tests
   - Unit tests for response parsing and cleanup.
   - Unit tests for request encoding with optional fields.

Deliverables
- `src/tp_discovery_client.c` updates + cleanup helpers.
- `include/tensor_pool/tp_discovery_client.h` filter APIs.

- CLI tools for attach/keepalive, SHM inspection, and sample producer/consumer.
- Example configs and usage docs.
