# Implementation Guide (Reference C Implementation)

This document is the implementation guide for a fully spec-compliant, Aeron-style
C reference implementation of the SHM Tensor Pool system. It is the baseline to
wrap for other languages (Julia, Python, etc).

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/SHM_TraceLink_Spec_v1.0.md`
- `docs/SHM_Join_Barrier_Spec_v1.0.md`
- `docs/STREAM_ID_CONVENTIONS.md`
- `docs/AERON_LIKE_API_PROPOSAL.md`
- `docs/CLIENT_CONDUCTOR_DESIGN.md`
- `docs/DRIVER_USAGE.md`

Target style references:
- Aeron C client: `../aeron/aeron-client/src/main/c`
- Aeron C driver: `../aeron/aeron-driver/src/main/c`

## 1. Scope and Intent
- This implementation MUST be fully compliant with the authoritative specs.
- It MUST follow Aeron C client/driver patterns for ownership, error handling,
  async operations, and agent-style polling.
- It SHOULD be the canonical reference for other language bindings.

## 2. Core Principles (Aeron-style)
- Single-writer ownership for conductor and SHM write paths.
- Command queue for async operations; `do_work` drives state and dispatch.
- No Aeron types in public API; wrap with TensorPool handles/types.
- Error semantics: return 0 on success, -1 on failure; `tp_err` set on error.
- C11 atomics for portability (`stdatomic.h`).

## 3. Client Conductor (Required)
The conductor is the single-writer owner of client state and shared Aeron
resources. It MUST:
- Own shared control/announce/QoS/metadata/descriptor subscriptions.
- Own driver attach/keepalive/revoke state and discovery polling.
- Dispatch all registered callbacks from `tp_client_do_work` only.
- Drain a lock-free MPSC command queue for async add operations.

Command queue MUST follow Aeron’s model:
- Fixed-capacity ring with backpressure on full.
- Enqueue from application threads; dequeue in `tp_client_do_work`.
- Async handles polled by callers (Aeron-style).

## 4. Driver Model Client
Driver model behaviors MUST follow `docs/SHM_Driver_Model_Spec_v1.0.md`:
- Attach request/response encode/decode with schema gating.
- Keepalive scheduling and lease expiry handling in `tp_client_do_work`.
- Client ID auto-assignment when 0 is provided.
- Reattach logic on revoke and attach failures.

## 5. SHM Layout and Mapping
SHM layout MUST match `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`:
- Canonical directory layout per 15.21a.3.
- Superblock validation: magic, layout_version, stream_id, pool geometry.
- Header ring slot size fixed at 256 bytes; `seq_commit` at byte 0.
- Payload pools enforce 64-byte stride alignment; `pool_nslots == header_nslots`.
- All SHM mappings MUST be validated; mismatches MUST fail closed.

## 6. Producer Implementation
Producer MUST:
- Follow seqlock commit semantics (write in-progress, payload, header, flush,
  commit).
- Encode FrameDescriptor and FrameProgress via SBE (no hand-rolled frames).
- Emit QoS/metadata at configured cadence.
- Support `try_claim` + `commit` + `queue_claim` for DMA workflows.
- Respect fixed announced buffer pools (no add/remove during acquisition).

## 7. Consumer Implementation
Consumer MUST:
- Validate `seq_commit` with stable re-read and committed bit.
- Validate embedded TensorHeader message header (schema ID/version/block length).
- Enforce stride/shape rules, payload_len bounds, and pool routing.
- Track drops and report QoS according to spec.
- Support per-consumer descriptor/control streams and fallback mode.

## 8. Discovery Client
Discovery client MUST:
- Encode DiscoveryRequest with non-empty response channel and stream ID.
- Decode responses with schema gating and required field validation.
- Treat discovery as advisory; driver remains authoritative for attach.

## 9. Supervisor / Unified Management (Optional, External)
Supervisor/console is recommended but external. When implemented, it SHOULD:
- Subscribe to ShmPoolAnnounce, QoS, metadata, and health streams.
- Emit ConsumerConfig to control per-consumer streams and rate limits.

## 10. TraceLink
- Trace IDs MUST be 64-bit Snowflake-style IDs.
- FrameDescriptor.trace_id MUST carry the resolved trace ID.
- TraceLinkSet emission MUST be non-blocking and best-effort.

## 11. JoinBarrier
JoinBarrier MUST be allocation-free in hot paths and follow merge-map rules.
It SHOULD be integrated into control-plane polling for updates.

## 12. Build and Codegen
- Use CMake for builds.
- Generate SBE code from `./schemas` using `sbe-tool` (Maven artifacts).
- Do not check in generated code; generate during build.

## 13. Code Organization
The codebase SHOULD be organized by role:
- `src/client/`: client API, conductor, producer/consumer, discovery client.
- `src/driver/`: driver lifecycle, attach/lease/epoch management, SHM creation/GC.
- `src/common/`: shared utilities (logging, SHM helpers, SBE glue, atomics).

Public headers SHOULD follow the same split under `include/tensor_pool/` as the
code is moved.
The current layout follows this split under:
- `include/tensor_pool/client/` for client-facing APIs.
- `include/tensor_pool/common/` for shared utilities.

## 14. Testing and Compliance
- Every MUST/SHOULD requirement MUST map to a test or explicit verification.
- Maintain `docs/TRACEABILITY_MATRIX_SHM_ALL.md` and
  `docs/REQUIREMENTS_TEST_CHECKLIST.md`.
- Add config-matrix integration tests for driver-mode permutations.
- Add fuzzing for control-plane decoders (libFuzzer) where applicable.

## 15. Operational Defaults
- A running Aeron Media Driver is expected at `/dev/shm/aeron-dgamroth`.
- Stream IDs MUST follow `docs/STREAM_ID_CONVENTIONS.md`.
- Logging should be configurable; stderr default is acceptable for tools.

## 16. Implementation Checklist (Status)
Status keywords: DONE / PARTIAL / MISSING / EXTERNAL.

### Client Conductor (Aeron-style)
- DONE: Single-writer conductor owns shared control/announce/QoS/metadata/descriptor streams.
- DONE: MPSC command queue for async add operations.
- DONE: Client-level handler registration and dispatch from `tp_client_do_work`.
- DONE: Remove public Aeron types from user-facing headers.

### Driver Model (Client-side)
- DONE: Attach/keepalive/detach encode/decode and lease handling.
- DONE: Client ID auto-assign when 0 provided; retry on collision.
- DONE: Schema/version/block-length gating for driver messages.

### SHM Layout / Mapping
- DONE: Superblock validation, canonical layout enforcement, path containment checks.
- DONE: Header ring layout, seqlock commit protocol, payload pool mapping rules.
- DONE: TensorHeader validation and stride rules (64-byte aligned).

### Producer
- DONE: Offer/try_claim/commit/queue_claim APIs and DMA flush hook.
- DONE: FrameDescriptor/FrameProgress encode; QoS + metadata cadence.
- DONE: TraceLink helpers with non-blocking emission.

### Consumer
- DONE: Descriptor/progress polling with committed seqlock validation.
- DONE: Per-consumer stream support and fallback behavior.
- DONE: Drop accounting and QoS emission.

### Discovery
- DONE: DiscoveryRequest encode + response decode + poller API.
- DONE: Discovery provider/registry service (`tp_discoveryd`, `tp_discovery_service`).

### Supervisor
- DONE: Supervisor service and policy handler embedded in `tp_driver` (`tp_supervisor`).
  - SHOULD be a standalone agent/executable; MAY run embedded in the driver.

### Driver (Server-side)
- DONE: Authoritative attach/keepalive/detach and policy checks.
- DONE: SHM creation, epoch management, and ShmPoolAnnounce emission.
- DONE: Lease revocation and GC/retention policy.
- DONE: `tp_driver` executable with TOML config + logging.
- PARTIAL: Node ID allocation (auto-assigns per lease; no reuse cooldown).

### Supervisor / Unified Management
- MISSING: Supervisor/console policy layer (recommended by spec).
  - SHOULD be a standalone agent/executable; MAY run embedded in the driver.

### TraceLink
- DONE: Snowflake trace IDs and TraceLinkSet encode/decode.
- PARTIAL: Node ID allocation (driver assigns per lease; no reuse cooldown).

### JoinBarrier
- DONE: MergeMap decode + join barrier modes (sequence/timestamp/latest).

### Build / Codegen
- DONE: CMake build and SBE codegen via Maven toolchain.

### Tests / Compliance
- DONE: Wire-spec unit tests and compliance mappings.
- PARTIAL: Driver integration tests (lifecycle not yet covered end-to-end).

### Code Organization
- DONE: Split implementation into `src/client`, `src/driver`, `src/common`.
