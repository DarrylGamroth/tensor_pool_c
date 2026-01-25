# SHM Traceability Matrix (Spec Freeze)

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/SHM_TraceLink_Spec_v1.0.md`
- `docs/SHM_Join_Barrier_Spec_v1.0.md`

Checklist reference:
- `docs/REQUIREMENTS_TEST_CHECKLIST.md`

Legend:
- **Compliant**: Implemented per spec.
- **Partial**: Core behavior implemented; gaps remain.
- **Missing**: Not implemented.
- **External**: Out of scope for this repo (driver/supervisor/bridge).
- **N/A**: Informative-only.

Columns:
- Implementation and test references are representative file paths.

## SHM_Tensor_Pool_Wire_Spec_v1.2

| Req ID | Spec Section | Requirement | Implementation | Tests | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| W-6-1 | 6 | `superblock_size` fixed at 64 bytes | `include/tensor_pool/common/tp_shm.h`, `src/common/tp_shm.c` | `tests/test_tp_smoke.c` | Compliant | `TP_SUPERBLOCK_SIZE_BYTES` used throughout |
| W-7.1-1 | 7.1 | Validate superblock magic, layout_version, stream_id, region_type, slot/pool geometry | `src/common/tp_shm.c` | `tests/test_tp_smoke.c`, `tests/test_tp_shm_security.c` | Compliant | Fail-closed validation |
| W-7.1-2 | 7.1 | `pool_id` semantics (header ring = 0; payload pools match announce) | `src/common/tp_shm.c`, `src/client/tp_consumer.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-7.1-3 | 7.1 | Liveness via `activity_timestamp_ns` updates + stale remap | `src/client/tp_producer.c`, `src/client/tp_consumer.c`, `src/common/tp_shm.c` | `tests/test_tp_pollers.c` | Compliant | Periodic refresh and stale detection |
| W-8.1-1 | 8.1 | Header slot size 256 bytes; offset formula | `include/tensor_pool/common/tp_shm.h`, `src/common/tp_slot.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-8.2-1 | 8.2 | SlotHeader stored as raw SBE body; `seq_commit` at byte 0 | `src/common/tp_slot.c`, `src/client/tp_consumer.c`, `src/client/tp_producer.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-8.2-2 | 8.2 | Embedded TensorHeader headerBytes length + message header validation | `src/client/tp_consumer.c`, `src/common/tp_tensor.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-8.2-3 | 8.2 | `ndims` range 1..MAX_DIMS; dims/strides non-negative; zero fill beyond `ndims` | `src/common/tp_tensor.c`, `src/client/tp_producer.c` | `tests/test_tp_smoke.c`, `tests/test_tp_rollover.c` | Compliant | |
| W-8.2-4 | 8.2 | Strides consistent with `major_order`, non-overlapping | `src/common/tp_tensor.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-8.2-5 | 8.2 | `progress_stride_bytes` matches true row/column stride | `src/common/tp_tensor.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-8.2-6 | 8.2 | `payload_offset` must be 0 | `src/client/tp_consumer.c`, `src/client/tp_producer.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-8.2-7 | 8.2 | `seq_commit` matches FrameDescriptor/Progress `seq` | `src/client/tp_consumer.c`, `src/client/tp_progress_poller.c` | `tests/test_tp_rollover.c` | Compliant | |
| W-8.3-1a | 8.3 | Seqlock commit bit semantics + stable read | `src/client/tp_consumer.c`, `src/client/tp_producer.c`, `src/common/tp_slot.c` | `tests/test_tp_rollover.c` | Compliant | Release/acquire + stable re-read |
| W-8.3-1b | 8.3 | Payload visibility/DMA flush before commit | `src/client/tp_producer.c` | `tests/test_tp_producer_claim.c` | Compliant | Optional flush hook before commit |
| W-9-1 | 9 | Payload pool `stride_bytes` >=64, multiple of 64; `pool_nslots == header_nslots` | `src/common/tp_shm.c`, `src/client/tp_consumer.c` | `tests/test_tp_smoke.c`, `tests/test_tp_shm_security.c` | Compliant | |
| W-9-2 | 9 | `headerSlotBytes` fixed at 256 and geometry cross-check | `src/common/tp_shm.c`, `src/client/tp_consumer.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-10.1-1 | 10.1 | ShmPoolAnnounce decode, layout/epoch validation, remap on epoch change | `src/client/tp_control_adapter.c`, `src/client/tp_consumer.c` | `tests/test_tp_pollers.c` | Compliant | |
| W-10.1.2-1 | 10.1.2 | ConsumerHello encode + max_rate_hz throttle | `src/client/tp_consumer_manager.c`, `src/client/tp_producer.c` | `tests/test_tp_pollers.c` | Compliant | |
| W-10.1.2-2 | 10.1.2 | ConsumerHello per-consumer request validity (channel/stream must be both set or both absent) | `src/client/tp_control.c`, `src/client/tp_consumer_registry.c` | `tests/test_tp_consumer_errors.c` | Compliant | Outbound validation enforced in `tp_consumer_send_hello` |
| W-10.1.3-1 | 10.1.3 | ConsumerConfig decode; per-consumer declines via empty channel/stream; fallback URI when `use_shm=0` | `src/client/tp_consumer_registry.c`, `src/client/tp_consumer_manager.c` | `tests/test_tp_consumer_registry.c` | Compliant | |
| W-10.2.1-1 | 10.2.1 | FrameDescriptor publish/consume; epoch/seq validation; pool/slot routing; trace_id | `src/client/tp_producer.c`, `src/client/tp_consumer.c` | `tests/test_tp_rollover.c`, `tests/test_tp_smoke.c` | Compliant | |
| W-10.2.1-2 | 10.2.1 | Timestamp semantics: SlotHeader capture time; FrameDescriptor publish time; do not assume equality | `src/client/tp_producer.c`, `src/client/tp_consumer.c` | `tests/test_tp_producer_claim.c` | Compliant | SlotHeader uses capture timestamp; descriptor defaults to null unless publish-time flag enabled |
| W-10.2.2-1 | 10.2.2 | FrameProgress publish/poll; monotonic checks | `src/client/tp_progress_poller.c` | `tests/test_tp_pollers.c`, `tests/test_tp_progress_poller_extra.c` | Compliant | Tracker sized to header ring when consumer is set |
| W-10.3-1 | 10.3 | DataSourceAnnounce/DataSourceMeta/MetaBlob encode/decode | `src/client/tp_control.c`, `src/client/tp_control_adapter.c` | `tests/test_tp_control.c`, `tests/test_tp_pollers.c` | Compliant | |
| W-10.4-1 | 10.4 | QoS message encode/decode + cadence | `src/client/tp_qos.c`, `src/client/tp_producer.c`, `src/client/tp_consumer.c` | `tests/test_tp_pollers.c` | Compliant | Cadence uses `announce_period_ns` |
| W-10.5-1 | 10.5 | Supervisor/unified management layer | `src/supervisor/tp_supervisor.c` | `tests/test_tp_supervisor.c` | Compliant | Subscribes to control/QoS/metadata/announce and emits ConsumerConfig policy decisions |
| W-11-1 | 11 | Consumer modes: shared/per-consumer descriptors and fallback | `src/client/tp_consumer_registry.c`, `src/client/tp_consumer.c` | `tests/test_tp_consumer_registry.c`, `tests/test_tp_pollers.c` | Compliant | Fallback entered on invalid announce or mapping failure when configured. |
| W-15.1-1 | 15.1 | Validation and compatibility matrix enforcement | `src/common/tp_shm.c`, `src/client/tp_consumer.c`, `src/client/tp_producer.c` | `tests/test_tp_smoke.c`, `tests/test_tp_consumer_apply.c` | Compliant | Layout version gated at attach and superblock validation matches announce |
| W-15.2-1 | 15.2 | Epoch lifecycle: drop on mismatch, remap on announce | `src/client/tp_consumer.c` | `tests/test_tp_pollers.c` | Compliant | |
| W-15.3-1 | 15.3 | Commit protocol edge cases | `src/client/tp_consumer.c` | `tests/test_tp_rollover.c` | Compliant | Drops on instability or seq mismatch |
| W-15.4-1 | 15.4 | Overwrite/drop accounting | `src/client/tp_consumer.c` | `tests/test_tp_rollover.c` | Compliant | |
| W-15.5-1 | 15.5 | `payload_slot` equals header index; pool_nslots alignment | `src/client/tp_consumer.c` | `tests/test_tp_rollover.c` | Compliant | |
| W-15.7-1 | 15.7 | Timebase/clock-domain consistency | `src/client/tp_consumer.c` | `tests/test_tp_pollers.c` | Compliant | |
| W-15.7-2 | 15.7 | Monotonic timestamp when both SlotHeader and FrameDescriptor timestamps present | `src/client/tp_producer.c` | `tests/test_tp_producer_claim.c` | Compliant | Default timestamps use monotonic clock; caller-supplied capture timestamps must remain monotonic |
| W-15.8-1 | 15.8 | Enum/type registry versioning | `src/common/tp_tensor.c` | `tests/test_tp_smoke.c` | Compliant | Unknown enums rejected |
| W-15.10-1 | 15.10 | Path containment and fail-closed validation | `src/common/tp_shm.c`, `src/common/tp_context.c`, `tools/tp_shm_create.c` | `tests/test_tp_shm_security.c` | Compliant | Permission checks enforced with opt-out in context. |
| W-15.12-1 | 15.12 | Consumer state machine/fallback | `src/client/tp_consumer.c` | `tests/test_tp_pollers.c` | Compliant | Fallback enter/exit and remap covered |
| W-15.13-1 | 15.13 | Test and validation checklist coverage | `tests/test_tp_smoke.c`, `tests/test_tp_pollers.c`, `tests/test_tp_rollover.c` | n/a | Compliant | Added fail-closed superblock tests, QoS drop counts, and epoch remap coverage. |
| W-15.14-1 | 15.14 | Liveness: ShmPoolAnnounce freshness, pid/activity checks | `src/client/tp_consumer.c`, `src/common/tp_shm.c` | `tests/test_tp_pollers.c` | Compliant | Freshness/pid/activity validated |
| W-15.15-1 | 15.15 | Supervisor/consumer coordination layer ≈ client conductor | `src/client/tp_client_conductor.c` | `tests/test_tp_client_conductor.c` | Compliant | Conductor centralizes shared polling/dispatch |
| W-15.16a-1 | 15.16a | File-backed SHM prefault/lock/fsync policy | n/a | n/a | N/A | Informative guidance |
| W-15.17-1 | 15.17 | ControlResponse error codes | `src/client/tp_control.c`, `src/client/tp_control_adapter.c` | `tests/test_tp_control.c` | Compliant | |
| W-15.18-1 | 15.18 | Normative algorithms (per role) | `src/client/tp_consumer.c`, `src/client/tp_producer.c` | `tests/test_tp_smoke.c`, `tests/test_tp_producer_claim.c` | Compliant | Commit protocol, header validation, and payload flush hook implemented |
| W-15.20-1 | 15.20 | Compatibility matrix (layout/wire) | n/a | n/a | N/A | Spec evolution guidance |
| W-15.21-1 | 15.21 | Protocol state machines | `src/client/tp_consumer.c` | `tests/test_tp_pollers.c`, `tests/test_tp_rollover.c` | Compliant | Mapping transitions validated |
| W-15.21a-1 | 15.21a | Canonical layout + path containment validation | `src/common/tp_shm.c`, `tools/tp_shm_create.c` | `tests/test_tp_shm_security.c` | Compliant | |
| W-15.22-1 | 15.22 | SHM backend validation (hugepages/stride alignment) | `src/common/tp_shm.c` | `tests/test_tp_smoke.c` | Compliant | |
| W-16-1 | 16 | Control-plane SBE schema usage | `src/client/tp_control.c`, `src/client/tp_control_adapter.c` | `tests/test_tp_control.c` | Compliant | |

## SHM_Driver_Model_Spec_v1.0

| Req ID | Spec Section | Requirement | Implementation | Tests | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| D-2.1 | 2 | Single authoritative driver per stream | `src/driver/tp_driver.c` | n/a | Partial | Driver enforces exclusive producer; no integration test yet |
| D-2.2 | 2.2/2.3 | Clients MUST NOT create/truncate/unlink SHM files | `docs/C_CLIENT_API_USAGE.md` | n/a | Compliant | Client library never creates/truncates; driver owns SHM lifecycles |
| D-3-1 | 3 | Clients treat driver URIs as authoritative | `src/client/tp_driver_client.c`, `src/client/tp_client.c`, `src/client/tp_consumer.c`, `src/client/tp_producer.c` | `tests/test_tp_smoke.c` | Compliant | Driver mode rejects manual config |
| D-4.2-1 | 4.2 | Attach request/response encode/decode, required fields validated | `src/client/tp_driver_client.c`, `src/driver/tp_driver.c` | `tests/test_tp_driver_client.c` | Partial | Driver-side attach handling needs integration tests |
| D-4.2-2 | 4.2 | `correlationId` echoed; URIs non-empty; `headerSlotBytes=256`; pool_nslots match | `src/client/tp_driver_client.c` | `tests/test_tp_driver_client.c` | Compliant | |
| D-4.2-3 | 4.2 | Node ID assignment and validation | `src/client/tp_driver_client.c`, `src/common/tp_trace.c` | `tests/test_tp_driver_client.c` | Compliant | Client honors non-null nodeId |
| D-4.2-4 | 4.2 | Driver assigns `nodeId` when absent, stable per lease | `src/driver/tp_driver.c` | n/a | Partial | No reuse cooldown; no integration test yet |
| D-4.3-1 | 4.3 | Attach request semantics (expectedLayoutVersion, publishMode, hugepages) | `src/driver/tp_driver.c` | n/a | Partial | Driver enforces semantics; no integration tests yet |
| D-4.4-1 | 4.4 | Lease keepalive send/expiry handling | `src/client/tp_driver_client.c`, `src/client/tp_client.c` | `tests/test_tp_driver_client.c` | Compliant | `tp_client_do_work` schedules keepalives |
| D-4.4a-1 | 4.4a | Schema version compatibility gating | `src/client/tp_driver_client.c` | `tests/test_tp_driver_client.c` | Compliant | |
| D-4.5-1 | 4.5 | Control-plane transport over Aeron | `src/client/tp_driver_client.c` | `tests/test_tp_driver_client.c` | Compliant | |
| D-4.6-1 | 4.6 | Response code validation | `src/client/tp_driver_client.c` | `tests/test_tp_driver_client.c` | Compliant | |
| D-4.7-1 | 4.7/4.9 | Lease lifecycle, revoke handling | `src/client/tp_consumer.c`, `src/client/tp_producer.c`, `src/client/tp_driver_client.c` | `tests/test_tp_lease_revoked.c` | Compliant | Revoke clears mappings and schedules reattach |
| D-4.8-1 | 4.8 | Lease identity and client identity uniqueness | `src/driver/tp_driver.c` | n/a | Partial | Driver enforces uniqueness; no integration tests yet |
| D-4.9-1 | 4.9 | Detach request/response encode/decode | `src/client/tp_driver_client.c` | `tests/test_tp_driver_client.c` | Compliant | |
| D-B.1-1 | Appendix B.1 | Correlation IDs unique/non-reused; seeded per process | `src/client/tp_driver_client.c`, `src/client/tp_producer.c`, `src/client/tp_consumer.c` | `tests/test_tp_driver_client.c` | Compliant | Generator seeds from urandom/time and increments |
| D-B.1-2 | Appendix B.1 | Ignore duplicate ShmAttachResponse after OK | `src/client/tp_driver_client.c` | `tests/test_tp_driver_client_live.c` | Compliant | Post-OK duplicates ignored |
| D-B.2-1 | Appendix B.2 | Auto-assign non-zero clientId before attach | `src/client/tp_driver_client.c`, `src/client/tp_producer.c`, `src/client/tp_consumer.c` | `tests/test_tp_driver_client.c` | Compliant | Attach request normalization assigns non-zero IDs |
| D-B.2-2 | Appendix B.2 | Retry on `client_id already attached` when auto-assigned | `src/client/tp_driver_client.c` | `tests/test_tp_driver_client_live.c` | Compliant | Auto-assign retries update clientId and correlationId |

## SHM_Discovery_Service_Spec_v_1.0

| Req ID | Spec Section | Requirement | Implementation | Tests | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| DS-3-1 | 3 | Discovery is advisory; attach via driver | `docs/C_CLIENT_API_USAGE.md` | n/a | Compliant | Discovery guidance clarified in API usage. |
| DS-4.2-1 | 4.2 | Gate decode by `schemaId`/`templateId` on shared streams | `src/client/tp_discovery_client.c` | `tests/test_tp_discovery_client.c` | Compliant | |
| DS-4.3-1 | 4.3 | Request must include response channel and non-zero stream ID | `src/client/tp_discovery_client.c` | `tests/test_tp_discovery_client.c` | Compliant | |
| DS-5.0-1 | 5.0 | Optional fields use nullValue or zero-length strings | `src/client/tp_discovery_client.c` | `tests/test_tp_discovery_client.c` | Compliant | Optional dataSourceId nullValue handled |
| DS-5.1-1 | 5.1 | Filter AND semantics, tag matching rules | `src/discovery/tp_discovery_service.c` | `tests/test_tp_discovery_service.c` | Compliant | Tags can be injected via provider API and matched with AND semantics. |
| DS-5.2-1 | 5.2 | Response validation: headerSlotBytes/maxDims, pool_nslots match, authority fields non-empty | `src/client/tp_discovery_client.c` | `tests/test_tp_discovery_client.c` | Compliant | |
| DS-6-1 | 6 | Registry expiry/indexing/conflict resolution | `src/discovery/tp_discovery_service.c` | `tests/test_tp_discovery_service.c` | Compliant | Expiry based on announce period and epoch monotonicity. |

## SHM_TraceLink_Spec_v1.0

| Req ID | Spec Section | Requirement | Implementation | Tests | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| TL-5-1 | 5 | TraceLink is best-effort and non-blocking | `src/common/tp_tracelink.c`, `src/client/tp_producer.c` | `tests/test_tp_tracelink.c` | Compliant | No flow-control coupling |
| TL-6.1-1 | 6.1 | 64-bit Snowflake-style trace IDs | `src/common/tp_trace.c` | `tests/test_tp_tracelink.c` | Compliant | Agrona-style generator |
| TL-6.2-1 | 6.2 | Node ID unique per deployment | `src/driver/tp_driver.c` | n/a | Partial | Unique among active leases; no reuse cooldown test |
| TL-6.3-1 | 6.3 | Propagation rules for root/derived frames | `src/client/tp_producer.c`, `src/common/tp_tracelink.c` | `tests/test_tp_tracelink.c` | Compliant | Helper enforces root/1→1/N→1 rules and flags when to emit TraceLinkSet. |
| TL-8.1-1 | 8.1 | FrameDescriptor `trace_id` field (null sentinel 0) | `src/client/tp_producer.c`, `src/client/tp_consumer.c` | `tests/test_tp_tracelink.c` | Compliant | |
| TL-9-1 | 9 | TraceLinkSet encode/decode, parent uniqueness, schema gating | `src/common/tp_tracelink.c` | `tests/test_tp_tracelink.c` | Compliant | |

## SHM_Join_Barrier_Spec_v1.0

| Req ID | Spec Section | Requirement | Implementation | Tests | Status | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| JB-5-1 | 5 | JoinBarrier hot path is allocation-free and non-blocking | `src/common/tp_join_barrier.c` | `tests/test_tp_join_barrier.c` | Compliant | |
| JB-6-1 | 6 | MergeMap rule validation | `src/common/tp_merge_map.c` | `tests/test_tp_join_barrier.c` | Compliant | |
| JB-7-1 | 7 | SequenceJoinBarrier readiness + staleness handling | `src/common/tp_join_barrier.c` | `tests/test_tp_join_barrier.c` | Compliant | |
| JB-8-1 | 8 | TimestampJoinBarrier readiness + clock domain rules | `src/common/tp_join_barrier.c` | `tests/test_tp_join_barrier.c` | Compliant | |
| JB-9-1 | 9 | LatestValueJoinBarrier semantics | `src/common/tp_join_barrier.c` | `tests/test_tp_join_barrier.c` | Compliant | |
| JB-10-1 | 10 | MergeMap control-plane request/announce | `src/common/tp_merge_map.c`, `src/client/tp_control_adapter.c` | `tests/test_tp_join_barrier.c` | Compliant | |
