# SHM Spec Compliance Matrix (Repo)

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/SHM_TraceLink_Spec_v1.0.md`
- `docs/SHM_Join_Barrier_Spec_v1.0.md`
- `docs/SHM_Aeron_UDP_Bridge_Spec_v1.0.md`

Legend:
- **Compliant**: Implemented per spec.
- **Partial**: Core behavior implemented; gaps remain.
- **Missing**: Not implemented.
- **External**: Implemented by an external driver/supervisor not present in this repo.
- **N/A**: Informative or out of scope.

## SHM_Tensor_Pool_Wire_Spec_v1.2

Detailed section-by-section coverage lives in `docs/COMPLIANCE_MATRIX_SHM_WIRE_V1_2.md`.

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| SHM region validation, headers, payload pools | Partial | Core validation in `src/tp_shm.c` and `src/tp_consumer.c`; producer-side padding/zero-fill rules and some header constraints remain unenforced. |
| Slot header / TensorHeader validation | Partial | `src/tp_slot.c` and `src/tp_tensor.c` validate on read; producer-side checks are limited. |
| FrameDescriptor/FrameProgress | Compliant | Publish/consume paths implemented with epoch/seq_commit validation and trace_id support. |
| Metadata (DataSourceAnnounce/Meta/Blob) | Compliant | Encode/decode in `src/tp_control.c` and `src/tp_control_adapter.c`. |
| QoS messages | Partial | Encode/decode implemented; periodic publish policy not enforced. |
| Supervisor/unified management | Missing | Not implemented in this repo. |
| Consumer modes and fallback | Partial | Per-consumer descriptor mode supported; fallback URI handling not implemented. |
| SHM backend validation | Compliant | URI validation, hugepages/pow2/stride checks enforced in `src/tp_shm.c`. |
| Stream mapping guidance | N/A | Informative only. |

## SHM_Driver_Model_Spec_v1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| Driver lifecycle, ownership, epoch management | External | Driver responsibilities are out of scope for this repo. |
| Attach request encode | Compliant | `tp_driver_send_attach` in `src/tp_driver_client.c`. |
| Attach response validation | Compliant | Required fields validated; optional `leaseExpiryTimestampNs` accepted; schema version and block length gated. |
| Node ID negotiation | Partial | `desiredNodeId` and `nodeId` supported; allocation is driver-owned. |
| Keepalive send / tracking | Partial | `tp_driver_keepalive` implemented; lease expiry handling respects optional expiry. |
| Detach request/response | Compliant | Encode/decode implemented; schema version/block length gated; invalid response codes rejected. |
| Lease revoked / shutdown handling | Compliant | Decode validates enums and rejects invalid values; schema version/block length gated. |
| Schema version compatibility | Compliant | `schemaId`/`templateId`/`version`/`blockLength` gated in driver client decoders. |
| Control-plane transport | Compliant | Driver control uses Aeron publication/subscription via client. |

## SHM_Discovery_Service_Spec_v_1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| DiscoveryRequest encode | Compliant | `tp_discovery_request` validates non-empty response channel and stream ID. |
| DiscoveryResponse decode | Compliant | Required fields validated; schema version/block length gated; pool `nslots` vs header `nslots` mismatch rejected. |
| Client polling / async handling | Compliant | `tp_discovery_poll` and `tp_discovery_poller` implemented. |
| Discovery provider / registry | Missing | No provider implementation in this repo. |
| Authority rules | External | Driver/registry responsibilities. |

## SHM_TraceLink_Spec_v1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| Trace ID generator | Compliant | Agrona-style generator in `src/tp_trace.c`. |
| FrameDescriptor trace_id | Compliant | `trace_id` in `tp_frame_descriptor_t` and publish paths. |
| TraceLinkSet encode/decode | Compliant | `src/tp_tracelink.c` enforces schema, uniqueness, non-zero parents. |
| Node ID allocation | External | Driver/discovery-owned; client uses `nodeId` when provided. |
| Best-effort semantics | Compliant | TraceLink emission is non-blocking. |

## SHM_Join_Barrier_Spec_v1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| Sequence/Timestamp/LatestValue JoinBarrier | Compliant | `src/tp_join_barrier.c` with MergeMap decode in `src/tp_merge_map.c`. |
| Control-plane MergeMap decode | Compliant | MergeMap registry + control poller integration. |

## SHM_Aeron_UDP_Bridge_Spec_v1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| Bridge sender/receiver | Missing | No bridge implementation in this repo. |
| Bridge schemas | Compliant | SBE schema present in `schemas/bridge-schema.xml`; no runtime use. |

## Spec Completeness Notes

- Example configuration files are now present under `docs/examples/`.
