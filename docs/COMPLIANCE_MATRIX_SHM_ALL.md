# SHM Spec Compliance Matrix (Repo)

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/SHM_TraceLink_Spec_v1.0.md`
- `docs/SHM_Join_Barrier_Spec_v1.0.md`

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
| SHM region validation, headers, payload pools | Compliant | Superblock validation, padding zeroing, and payload flush hook implemented in `src/common/tp_shm.c`/`src/client/tp_producer.c`. |
| Slot header / TensorHeader validation | Compliant | Producer validates tensor headers before publish; consumer validates on read. |
| FrameDescriptor/FrameProgress | Compliant | Publish/consume paths implemented with epoch/seq_commit validation and trace_id support. |
| Metadata (DataSourceAnnounce/Meta/Blob) | Compliant | Encode/decode in `src/client/tp_control.c` and `src/client/tp_control_adapter.c`. |
| QoS messages | Compliant | Encode/decode and cadence in `src/client/tp_producer.c` and `src/client/tp_consumer.c`. |
| Client conductor (Aeron-style) | Missing | Conductor does not yet centralize control/QoS/metadata/descriptor polling or handler dispatch. |
| Supervisor/unified management | Missing | Not implemented in this repo. |
| Consumer modes and fallback | Compliant | Per-consumer descriptor/control mode supported; fallback entered on `use_shm=0` or invalid SHM announces when `payload_fallback_uri` is set. |
| SHM backend validation | Compliant | URI validation, hugepages/stride checks, and permissions policy enforced in `src/common/tp_shm.c`. |
| Stream mapping guidance | N/A | Informative only. |

## SHM_Driver_Model_Spec_v1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| Driver lifecycle, ownership, epoch management | Missing | Driver responsibilities not yet implemented in this repo. |
| Attach request encode | Compliant | `tp_driver_send_attach` in `src/client/tp_driver_client.c`. |
| Attach response validation | Compliant | Required fields validated; optional `leaseExpiryTimestampNs` accepted; schema version and block length gated. |
| Node ID negotiation | Compliant | `desiredNodeId` sent; `nodeId` accepted from driver; allocation remains driver-owned. |
| Keepalive send / tracking | Compliant | `tp_driver_keepalive` plus scheduling in `tp_client_do_work`. |
| Detach request/response | Compliant | Encode/decode implemented; schema version/block length gated; invalid response codes rejected. |
| Lease revoked / shutdown handling | Compliant | Decode validates enums; revoke clears mappings and schedules reattach. |
| Schema version compatibility | Compliant | `schemaId`/`templateId`/`version`/`blockLength` gated in driver client decoders. |
| Control-plane transport | Compliant | Driver control uses Aeron publication/subscription via client. |

## SHM_Discovery_Service_Spec_v_1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| DiscoveryRequest encode | Compliant | `tp_discovery_request` validates non-empty response channel and stream ID. |
| DiscoveryResponse decode | Compliant | Required fields validated; schema version/block length gated; pool `nslots` vs header `nslots` mismatch rejected. |
| Client polling / async handling | Compliant | `tp_discovery_poll` and `tp_discovery_poller` implemented. |
| Discovery provider / registry | Missing | No provider implementation in this repo. |
| Authority rules | Missing | Driver/registry responsibilities not yet implemented. |

## SHM_TraceLink_Spec_v1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| Trace ID generator | Compliant | Agrona-style generator in `src/common/tp_trace.c`. |
| FrameDescriptor trace_id | Compliant | `trace_id` in `tp_frame_descriptor_t` and publish paths. |
| TraceLinkSet encode/decode | Compliant | `src/common/tp_tracelink.c` enforces schema, uniqueness, non-zero parents. |
| TraceLink propagation helpers | Compliant | `tp_tracelink_resolve_trace_id` enforces root/1→1/N→1 rules. |
| Node ID allocation | External | Driver/discovery-owned; client uses `nodeId` when provided. |
| Best-effort semantics | Compliant | TraceLink emission is non-blocking. |

## SHM_Join_Barrier_Spec_v1.0

| Area | Status | Evidence / Notes |
| --- | --- | --- |
| Sequence/Timestamp/LatestValue JoinBarrier | Compliant | `src/common/tp_join_barrier.c` with MergeMap decode in `src/common/tp_merge_map.c`. |
| Control-plane MergeMap decode | Compliant | MergeMap registry + control poller integration. |


## Spec Completeness Notes

- Example configuration files are now present under `docs/examples/`.
