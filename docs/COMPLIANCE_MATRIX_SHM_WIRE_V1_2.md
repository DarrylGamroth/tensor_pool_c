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
| 7. SBE Messages Stored in SHM | Compliant | Slot header uses raw SBE body with `seq_commit` at offset 0; producer writes fixed header length and consumer validates embedded header length/template. |
| 7.1 ShmRegionSuperblock | Compliant | Magic/layout/epoch/stream/pool/type checks in `src/tp_shm.c`, linked to ShmPoolAnnounce in `src/tp_consumer.c`. |
| 8. Header Ring | Compliant | Producer overwrites full slot, zeros padding, and writes fixed header bytes length. |
| 8.1 Slot Layout | Compliant | Payload offset enforced to 0 and header length validated; producer writes `payload_slot` and `pool_id` explicitly. |
| 8.2 SlotHeader and TensorHeader | Compliant | Producer validates tensor headers before publish; consumer decodes and validates on read. |
| 8.3 Commit Encoding via seq_commit | Compliant | Seqlock pattern with optional payload flush hook before commit. |
| 9. Payload Pools | Compliant | Pool mapping enforced in attach config with stride validation (64-byte multiple) in `src/tp_shm.c`. |
| 10. Aeron + SBE Messages | Compliant | Control/QoS/descriptor/progress/ShmPoolAnnounce implemented (driver emits in driver mode; producer emits in no-driver mode). |
| 10.1 Service Discovery and SHM Coordination | Compliant | ShmPoolAnnounce decode and consumer mapping implemented (`src/tp_consumer.c`). |
| 10.1.1 ShmPoolAnnounce | Compliant | Decode/consume path and freshness checks in `src/tp_control_adapter.c` + `src/tp_consumer.c`; producer emits in no-driver mode via `src/tp_control.c` + `src/tp_producer.c`. |
| 10.1.2 ConsumerHello | Partial | Encode/decode implemented; producer rejects invalid per-consumer channel/stream requests, but consumer does not validate outbound requests. |
| 10.1.3 ConsumerConfig | Compliant | Encode/decode implemented; per-consumer declines return empty channel/stream ID; consumers honor `use_shm` and expose `payload_fallback_uri`. |
| 10.2 Data Availability | Compliant | FrameDescriptor/FrameProgress implemented with nullValue optional handling and stream_id/epoch validation. |
| 10.2.1 FrameDescriptor | Compliant | Published and consumed with trace_id support; optional timestamp/meta use nullValue when omitted. |
| 10.2.2 FrameProgress | Partial | Published with stream_id/epoch; monotonic tracking is fixed-capacity (64) and can miss regressions under high concurrency. |
| 10.3 Per-Data-Source Metadata | Compliant | DataSourceAnnounce/Meta and meta blobs implemented. |
| 10.3.1 DataSourceAnnounce | Compliant | Encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 10.3.2 DataSourceMeta | Compliant | Encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 10.3.3 Meta blobs | Compliant | MetaBlob announce/chunk/complete encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 10.4 QoS and Health | Compliant | QoS publish/poll implemented with cadence via `announce_period_ns`, watermark uses nullValue when absent. |
| 10.4.1 QosConsumer | Compliant | Encode/decode plus cadence in consumer poll loop. |
| 10.4.2 QosProducer | Compliant | Encode/decode plus cadence implemented; `watermark` uses nullValue when absent. |
| 10.5 Supervisor / Unified Management | External | Supervisor role is out of scope for this repo. |
| 11. Consumer Modes | Compliant | Rate-limited mode honored for per-consumer descriptors; shared stream fallback allowed when declined. |

## Sections 12–14 (Informative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 12. Bridge Service | N/A | Informative; no bridge service in this repo. |
| 13. Implementation Notes | N/A | Informative. |
| 14. Open Parameters | N/A | Informative. |

## Section 15 (Normative)

| Section | Status | Evidence / Notes |
| --- | --- | --- |
| 15.1 Validation and Compatibility | Compliant | Layout version gated at attach; superblock fields validated against ShmPoolAnnounce. |
| 15.2 Epoch Lifecycle | Compliant | ShmPoolAnnounce epoch tracking, remap, and drop behavior in `src/tp_consumer.c`. |
| 15.3 Commit Protocol Edge Cases | Compliant | Seqlock stability and seq mismatch handled as drops. |
| 15.4 Overwrite and Drop Accounting | Compliant | Drops tracked in `src/tp_consumer.c` with accessor in `include/tensor_pool/tp_consumer.h`. |
| 15.5 Pool Mapping Rules | Compliant | Enforced `payload_slot == header_index` and `nslots` match on attach. |
| 15.6 Sizing Guidance | N/A | Guidance only. |
| 15.7 Timebase | Compliant | ShmPoolAnnounce clock-domain and join-time rules enforced in `src/tp_consumer.c`. |
| 15.7a NUMA Policy | N/A | Deployment-driven. |
| 15.8 Enum and Type Registry | Compliant | Enum values validated against schema; `TP_LAYOUT_VERSION` and `TP_MAX_DIMS` pin registry versioning. |
| 15.9 Metadata Blobs | Compliant | MetaBlob announce/chunk/complete implemented. |
| 15.10 Security and Permissions | Compliant | Path containment plus permission checks enforced with opt-out in context. |
| 15.11 Stream Mapping Guidance | N/A | Guidance only. |
| 15.12 Consumer State Machine | Compliant | Mapped/unmapped tracking with fallback entry/exit in `src/tp_consumer.c`. |
| 15.13 Test and Validation Checklist | Compliant | Fail-closed superblock validation, QoS drop counts, and epoch remap coverage added in tests. |
| 15.14 Deployment & Liveness | Compliant | ShmPoolAnnounce freshness/join-time enforced; activity/pid liveness checks unmap stale regions. |
| 15.15 Aeron Terminology Mapping | N/A | Informative. |
| 15.16 Reuse Aeron Primitives | Compliant | Control/descriptor/QoS/metadata all use Aeron; no custom SHM counters added; optional bridge/supervisor remain external. |
| 15.16a File-Backed SHM Regions | N/A | Informative guidance. |
| 15.17 ControlResponse Error Codes | Compliant | ControlResponse encode/decode implemented in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| 15.18 Normative Algorithms | Compliant | Producer commit protocol and consumer validation follow spec; payload flush hook covers non-coherent DMA. |
| 15.20 Compatibility Matrix | N/A | Spec evolution guidance. |
| 15.21 Protocol State Machines | Compliant | Mapping transitions and fallback recovery exercised in tests. |
| 15.21a Filesystem Layout and Path Containment | Compliant | Canonical layout tool defaulted; noncanonical path creation gated; symlink-safe open/containment checks in `src/tp_shm.c`. |
| 15.22 SHM Backend Validation | Compliant | URI scheme/hugepages enforcement and 64-byte stride alignment checks in `src/tp_shm.c`. |

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
