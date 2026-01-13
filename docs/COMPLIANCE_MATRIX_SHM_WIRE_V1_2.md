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
| 6. SHM Region Structure | Compliant | Superblock validation and ShmPoolAnnounce cross-checks in `src/tp_shm.c` and `src/tp_consumer.c`. |
| 7. SBE Messages Stored in SHM | Partial | Slot header decode uses raw SBE body (`src/tp_slot.c`), but producer-side enforcement of header requirements is not explicit. |
| 7.1 ShmRegionSuperblock | Compliant | Magic/layout/epoch/stream/pool/type checks in `src/tp_shm.c`, linked to ShmPoolAnnounce in `src/tp_consumer.c`. |
| 8. Header Ring | Partial | Header slots are used; no enforcement of producer-side padding/zero-fill rules. |
| 8.1 Slot Layout | Partial | Consumer validates slot header length and payload offset; no explicit enforcement of padding fields. |
| 8.2 SlotHeader and TensorHeader | Partial | Tensor validation in `src/tp_tensor.c`; producer does not validate before publish. |
| 8.3 Commit Encoding via seq_commit | Partial | Seqlock pattern used in producer/consumer; no explicit DMA flush support. |
| 9. Payload Pools | Compliant | Pool mapping enforced in attach config with stride validation in `src/tp_shm.c`. |
| 10. Aeron + SBE Messages | Compliant | Control/QoS/descriptor/progress/ShmPoolAnnounce implemented. |
| 10.1 Service Discovery and SHM Coordination | Compliant | ShmPoolAnnounce decode and consumer mapping implemented (`src/tp_consumer.c`). |
| 10.1.1 ShmPoolAnnounce | Compliant | Decode/consume path and freshness checks in `src/tp_control_adapter.c` + `src/tp_consumer.c`. |
| 10.1.2 ConsumerHello | Compliant | Encode/decode and max_rate_hz throttling in `src/tp_producer.c`. |
| 10.1.3 ConsumerConfig | Partial | Encode/decode implemented; payload_fallback_uri not used. |
| 10.2 Data Availability | Compliant | FrameDescriptor/FrameProgress with epoch mismatch checks and seq_commit validation. |
| 10.2.1 FrameDescriptor | Compliant | Published and consumed; consumer drops on epoch mismatch and seq_commit mismatch. |
| 10.2.2 FrameProgress | Compliant | Published and polled with monotonic/<= validation in `src/tp_progress_poller.c`. |
| 10.3 Per-Data-Source Metadata | Compliant | DataSourceAnnounce/Meta and meta blobs implemented. |
| 10.3.1 DataSourceAnnounce | Compliant | Encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 10.3.2 DataSourceMeta | Compliant | Encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 10.3.3 Meta blobs | Compliant | MetaBlob announce/chunk/complete encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 10.4 QoS and Health | Partial | QoS publish/poll implemented; no automatic periodic publish policy in core. |
| 10.4.1 QosConsumer | Partial | Encoding/decoding implemented; rate/interval scheduling not enforced. |
| 10.4.2 QosProducer | Partial | Encoding/decoding implemented; periodic cadence not enforced. |
| 10.5 Supervisor / Unified Management | Missing | Supervisor role not implemented. |
| 11. Consumer Modes | Partial | Mode propagated via control messages; rate-limited mode enforced for per-consumer descriptors only. |

## Sections 12–14 (Informative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 12. Bridge Service | N/A | Informative; no bridge service in this repo. |
| 13. Implementation Notes | N/A | Informative. |
| 14. Open Parameters | N/A | Informative. |

## Section 15 (Normative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 15.1 Validation and Compatibility | Partial | Superblock validation + ShmPoolAnnounce linkage implemented; compatibility matrix not enforced. |
| 15.2 Epoch Lifecycle | Compliant | ShmPoolAnnounce epoch tracking, remap, and drop behavior in `src/tp_consumer.c`. |
| 15.3 Commit Protocol Edge Cases | Partial | Seqlock stability check in `tp_consumer_read_frame`; no explicit stale seq_commit decrease handling beyond mismatch. |
| 15.4 Overwrite and Drop Accounting | Compliant | Drops tracked in `src/tp_consumer.c` with accessor in `include/tensor_pool/tp_consumer.h`. |
| 15.5 Pool Mapping Rules | Compliant | Enforced `payload_slot == header_index` and `nslots` match on attach. |
| 15.6 Sizing Guidance | N/A | Guidance only. |
| 15.7 Timebase | Compliant | ShmPoolAnnounce clock-domain and join-time rules enforced in `src/tp_consumer.c`. |
| 15.7a NUMA Policy | N/A | Deployment-driven. |
| 15.8 Enum and Type Registry | Partial | DType/major_order validated; no explicit registry versioning. |
| 15.9 Metadata Blobs | Compliant | MetaBlob announce/chunk/complete implemented. |
| 15.10 Security and Permissions | Partial | Path containment checks implemented; permissions policy not enforced in code. |
| 15.11 Stream Mapping Guidance | N/A | Guidance only. |
| 15.12 Consumer State Machine | Partial | Mapped/unmapped tracking in `src/tp_consumer.c`; fallback state not implemented. |
| 15.13 Test and Validation Checklist | Partial | Some tests added; not full checklist coverage. |
| 15.14 Deployment & Liveness | Partial | ShmPoolAnnounce freshness/join-time enforced; no periodic publish policy in core. |
| 15.15 Aeron Terminology Mapping | N/A | Informative. |
| 15.16 Reuse Aeron Primitives | Partial | Aeron usage present; no direct mapping for all suggested primitives. |
| 15.16a File-Backed SHM Regions | Partial | File-backed SHM supported with warning; no fsync/prefault/lock policy enforcement. |
| 15.17 ControlResponse Error Codes | Compliant | ControlResponse encode/decode implemented in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 15.18 Normative Algorithms | Partial | Seqlock/validation and ShmPoolAnnounce flow implemented; explicit DMA flush handling remains platform-specific. |
| 15.20 Compatibility Matrix | Partial | Layout version checks in ShmPoolAnnounce path; full matrix not enforced. |
| 15.21 Protocol State Machines | Partial | ShmPoolAnnounce-driven mapping state implemented; not all optional states (fallback) covered. |
| 15.21a Filesystem Layout and Path Containment | Compliant | Canonical layout tool + symlink-safe open/containment checks in `src/tp_shm.c`. |
| 15.22 SHM Backend Validation | Compliant | URI scheme/hugepages enforcement and stride power-of-two/64-byte multiple checks in `src/tp_shm.c`. |

## Section 16 (Normative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 16. Control-Plane SBE Schema | Compliant | Schemas and generated code used across control/QoS/descriptor/progress. |

## JoinBarrier Spec v1.0 (Normative, Optional)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 5. Requirements and Constraints | Compliant | JoinBarrier hot path is allocation-free after init in `src/tp_join_barrier.c`. |
| 6. MergeMap | Compliant | Encode/decode helpers and rule validation in `src/tp_merge_map.c`. |
| 6.4 Stability and Epoch Handling | Compliant | MergeMap registry invalidates prior epochs in `src/tp_merge_map.c`. |
| 7. SequenceJoinBarrier | Compliant | Readiness, staleness handling, and observed/processed cursor checks in `src/tp_join_barrier.c`. |
| 8. TimestampJoinBarrier | Compliant | Clock domain and monotonic out_time enforcement in `src/tp_join_barrier.c`. |
| 9. LatestValueJoinBarrier | Compliant | Selection collection + invalidation and epoch gating in `src/tp_join_barrier.c`. |
