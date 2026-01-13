# SHM Wire Spec v1.2 Compliance Matrix

Authoritative reference: `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.

Legend:
- **Compliant**: Implemented per spec.
- **Partial**: Some requirements implemented; gaps remain.
- **Missing**: Not implemented.
- **N/A**: Informative or out of scope.
- **External**: Implemented by an external driver/supervisor not present in this repo.

## Sections 1–5 (Informative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 1. Goals | N/A | Informative. |
| 2. Non-Goals | N/A | Informative. |
| 3. High-Level Architecture | N/A | Informative. |
| 4. Shared Memory Backends | N/A | Informative. |
| 4.1 Region URI Scheme | N/A | Informative; enforcement handled in §15.22. |
| 4.2 Behavior Overview | N/A | Informative. |
| 5. Control-Plane and Data-Plane Streams | N/A | Informative; stream defaults documented in code and docs. |

## Sections 6–11 (Normative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 6. SHM Region Structure | Partial | Superblock validation exists (`src/tp_shm.c`) but lacks full §15.22 stride/power-of-two/page-size checks and ShmPoolAnnounce cross-validation. |
| 7. SBE Messages Stored in SHM | Partial | Slot header decode uses raw SBE body (`src/tp_slot.c`), but producer-side enforcement of header requirements is not explicit. |
| 7.1 ShmRegionSuperblock | Partial | Magic/layout/epoch/stream/pool/type checks in `src/tp_shm.c`; no linkage to ShmPoolAnnounce freshness. |
| 8. Header Ring | Partial | Header slots are used; no enforcement of producer-side padding/zero-fill rules. |
| 8.1 Slot Layout | Partial | Consumer validates slot header length and payload offset; no explicit enforcement of padding fields. |
| 8.2 SlotHeader and TensorHeader | Partial | Tensor validation in `src/tp_tensor.c`; producer does not validate before publish. |
| 8.3 Commit Encoding via seq_commit | Partial | Seqlock pattern used in producer/consumer; no explicit DMA flush support. |
| 9. Payload Pools | Partial | Pool mapping enforced in attach config; does not enforce stride_bytes page size constraints. |
| 10. Aeron + SBE Messages | Partial | Implemented for control/QoS/descriptor/progress, but missing ShmPoolAnnounce handling. |
| 10.1 Service Discovery and SHM Coordination | Missing | No ShmPoolAnnounce publish/consume path. |
| 10.1.1 ShmPoolAnnounce | Missing | SBE schema only; no emit/receive logic. |
| 10.1.2 ConsumerHello | Partial | Encode/decode implemented; per-consumer stream validation present, but no enforcement of max_rate_hz throttling. |
| 10.1.3 ConsumerConfig | Partial | Encode/decode implemented; per-consumer streams supported; payload_fallback_uri not used. |
| 10.2 Data Availability | Partial | FrameDescriptor/FrameProgress implemented; epoch mismatch checks missing. |
| 10.2.1 FrameDescriptor | Partial | Published and consumed; consumer does not drop on epoch mismatch. |
| 10.2.2 FrameProgress | Partial | Published and polled; no monotonic/<=validation for payload_bytes_filled. |
| 10.3 Per-Data-Source Metadata | Partial | DataSourceAnnounce/Meta implemented; large blob support missing. |
| 10.3.1 DataSourceAnnounce | Compliant | Encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 10.3.2 DataSourceMeta | Compliant | Encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 10.3.3 Meta blobs | Missing | Chunked blob support not implemented. |
| 10.4 QoS and Health | Partial | QoS publish/poll implemented; no automatic periodic publish policy in core. |
| 10.4.1 QosConsumer | Partial | Encoding/decoding implemented; rate/interval scheduling not enforced. |
| 10.4.2 QosProducer | Partial | Encoding/decoding implemented; periodic cadence not enforced. |
| 10.5 Supervisor / Unified Management | Missing | Supervisor role not implemented. |
| 11. Consumer Modes | Partial | Mode propagated via control messages but no behavioral enforcement. |

## Sections 12–14 (Informative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 12. Bridge Service | N/A | Informative; no bridge service in this repo. |
| 13. Implementation Notes | N/A | Informative. |
| 14. Open Parameters | N/A | Informative. |

## Section 15 (Normative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 15.1 Validation and Compatibility | Partial | Superblock validation exists; no ShmPoolAnnounce linkage. |
| 15.2 Epoch Lifecycle | Partial | Lease revocation handled; no ShmPoolAnnounce remap flow. |
| 15.3 Commit Protocol Edge Cases | Partial | Seqlock stability check in `tp_consumer_read_frame`; no explicit stale seq_commit decrease handling beyond mismatch. |
| 15.4 Overwrite and Drop Accounting | Missing | No drop accounting exported. |
| 15.5 Pool Mapping Rules | Compliant | Enforced `payload_slot == header_index` and `nslots` match on attach. |
| 15.6 Sizing Guidance | N/A | Guidance only. |
| 15.7 Timebase | Missing | No explicit timebase/clock-domain validation. |
| 15.7a NUMA Policy | N/A | Deployment-driven. |
| 15.8 Enum and Type Registry | Partial | DType/major_order validated; no explicit registry versioning. |
| 15.9 Metadata Blobs | Missing | Chunked blob handling not implemented. |
| 15.10 Security and Permissions | Partial | Path containment checks implemented; permissions policy not enforced in code. |
| 15.11 Stream Mapping Guidance | N/A | Guidance only. |
| 15.12 Consumer State Machine | Missing | No explicit consumer state machine implementation. |
| 15.13 Test and Validation Checklist | Partial | Some tests added; not full checklist coverage. |
| 15.14 Deployment & Liveness | Partial | Driver keepalive exists; no ShmPoolAnnounce freshness logic. |
| 15.15 Aeron Terminology Mapping | N/A | Informative. |
| 15.16 Reuse Aeron Primitives | Partial | Aeron usage present; no direct mapping for all suggested primitives. |
| 15.16a File-Backed SHM Regions | Partial | File-backed SHM supported; no fsync/prefault/lock policy enforcement. |
| 15.17 ControlResponse Error Codes | Partial | Driver responses decoded; no full ControlResponse API in wire control path. |
| 15.18 Normative Algorithms | Partial | Seqlock/validation implemented; ShmPoolAnnounce-driven algorithms missing. |
| 15.20 Compatibility Matrix | Missing | Not enforced at runtime. |
| 15.21 Protocol State Machines | Missing | No ShmPoolAnnounce state machine handling. |
| 15.21a Filesystem Layout and Path Containment | Partial | Containment checks implemented; symlink-safe open needs openat2 or equivalent. Canonical layout enforced only in tooling. |
| 15.22 SHM Backend Validation | Partial | URI scheme/hugepages enforced; stride_bytes power-of-two/page-size checks missing. |

## Section 16 (Normative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 16. Control-Plane SBE Schema | Compliant | Schemas and generated code used across control/QoS/descriptor/progress. |

