# Spec Compliance Remediation Plan (Audit 4)

Authoritative reference:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`

## Spec Requirements (Authoritative)

- **Per-consumer control streams**: If assigned, consumers MUST subscribe to the per-consumer control stream for `FrameProgress`, while keeping the shared control stream for all other control-plane messages; producers MUST close per-consumer publications when a consumer is stale (no `ConsumerHello` or `QosConsumer` for 3–5× interval). (Section 10.1.3)
- **Slot header and embedded TensorHeader**: Producers MUST zero-fill `dims[i]` and `strides[i]` for all `i >= ndims`; `ndims` MUST be in `1..MAX_DIMS`; negative strides are not supported; `progress_stride_bytes` must be consistent with the declared layout when progress is enabled. Consumers MUST drop frames when the embedded header is invalid. (Section 8.2, 10.2.1)
- **Epoch lifecycle**: On epoch regression or mismatch, consumers MUST drop and remap/unmap regions; stale epochs must not be accepted. (Section 15.2)
- **QoS liveness**: Liveness for per-consumer streams is satisfied by either `ConsumerHello` or `QosConsumer`; staleness requires absence of both for 3–5× interval. (Section 10.1.3)
- **Metadata blob ordering**: Blob chunk offsets MUST be monotonically increasing, non-overlapping, and cover the full range. (Section 10.3.3)
- **Liveness timestamps**: Superblock `activity_timestamp_ns` is used for liveness; implementations should refresh and check freshness (stale indicates dead). (Section 15.14)

## Findings to Address

### High
- Per-consumer control stream is assigned but not polled for `FrameProgress` (consumer only polls shared control stream).
- Producer does not enforce embedded TensorHeader invariants (ndims range, zero-fill for i>=ndims, negative strides, progress stride consistency).

### Medium
- Consumer registry staleness only considers `ConsumerHello`, not `QosConsumer`.
- Epoch regression is ignored rather than forcing unmap/remap.
- Metadata blob ordering/coverage validation missing.
- Superblock `activity_timestamp_ns` not refreshed or checked for staleness.

### Low
- Consumer state machine lacks a fallback state; SHM disabled only unmaps without fallback behavior.

## Plan

### Phase 1: Per-consumer control stream compliance (High)
- [x] Poll per-consumer control subscription for `FrameProgress` while keeping shared control subscription for other control-plane messages.
- [x] Add unit/integration tests to verify progress is received on per-consumer control streams.

### Phase 2: Producer TensorHeader enforcement (High)
- [x] Validate `tp_tensor_header_t` in producer publish/claim paths (ndims range, no negative strides, progress stride consistency).
- [x] Zero-fill dims/strides beyond `ndims` before encoding.
- [x] Add tests for producer-side rejection of invalid TensorHeader values.

### Phase 3: QoS-driven liveness updates (Medium)
- [x] Update consumer registry liveness on `QosConsumer` events.
- [x] Ensure per-consumer publications are closed only after absence of both hello and QoS for the stale window.
- [x] Add tests to confirm QoS keeps a consumer alive.

### Phase 4: Epoch regression handling (Medium)
- [x] On epoch regression in ShmPoolAnnounce, force unmap/remap and drop stale frames.
- [x] Add tests for epoch regression handling.

### Phase 5: Metadata blob ordering enforcement (Medium)
- [x] Track blob chunk offset monotonicity and coverage per blob; reject gaps/overlaps.
- [x] Add tests for ordering violations.

### Phase 6: Activity timestamp liveness (Medium)
- [ ] Update superblock `activity_timestamp_ns` on producer cadence.
- [ ] Validate freshness on consumer side; drop/remap when stale.
- [ ] Add tests for stale activity timestamps (including negative case when missing driver).

### Phase 7: Consumer fallback state (Low)
- [ ] Add explicit FALLBACK state in consumer state machine.
- [ ] Ensure unmap on `use_shm=false` transitions into FALLBACK with fallback URI retained.
- [ ] Add tests for state transitions.

## Progress Log
- 2025-01-14: Plan created.
- 2025-01-14: Phase 1 complete (per-consumer control stream progress polling + tests).
- 2025-01-14: Phase 2 complete (producer TensorHeader validation + zero-fill + tests).
- 2025-01-14: Phase 3 complete (QoS consumer liveness updates + tests).
- 2025-01-14: Phase 4 complete (epoch mismatch unmap + tests).
- 2025-01-14: Phase 5 complete (metadata blob ordering enforcement + tests).
