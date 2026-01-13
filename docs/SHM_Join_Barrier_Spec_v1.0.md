# MergeMap and JoinBarrier Specification for AeronTensorPool (RFC Style)

## 1. Scope

This document defines the MergeMap and JoinBarrier control-plane semantics for
multi-input synchronization in AeronTensorPool. It is an optional feature layer
built on top of the AeronTensorPool wire protocol, and it depends on the
control-plane conventions and SBE framing defined in
`docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`. Implementations MAY adopt this feature
independently without modifying the base wire spec.

## 2. Key Words

The key words "MUST", "MUST NOT", "REQUIRED", "SHOULD", "SHOULD NOT", and "MAY"
are to be interpreted as described in RFC 2119.

## 3. Conformance

Unless explicitly marked as Informative, sections in this document are
Normative.

## 4. Terminology

- Output stream: The stream produced by a merge/join stage.
- Input stream: A stream required to produce a given output.
- Output sequence (out_seq): The logical sequence number for the output stream.
- Input sequence (in_seq): The logical sequence number required from an input.
- Observed cursor: Highest sequence observed via FrameDescriptor for a stream.
- Processed cursor: Highest sequence processed by a downstream stage.
- MergeMap: A control-plane mapping that defines required input sequences or
  input timestamps for a given output.
- JoinBarrier: A readiness gate that uses MergeMap and stream state to
  authorize an attempt to process a given output. When used without a qualifier,
  JoinBarrier refers to SequenceJoinBarrier, TimestampJoinBarrier, or
  LatestValueJoinBarrier, or all when the statement applies to all.
- SequenceJoinBarrier: A JoinBarrier that gates on sequence numbers (out_seq).
- TimestampJoinBarrier: A JoinBarrier that gates on timestamps (out_time).
- LatestValueJoinBarrier: A JoinBarrier that uses the most recent observed input
  from each stream without requiring aligned sequences or timestamps.
- Observed time: Latest timestamp observed via FrameDescriptor or SlotHeader.
- Processed time: Latest timestamp processed by the join stage for a stream,
  using the rule's declared timestamp source (FrameDescriptor or SlotHeader).
  When multiple outputs are emitted per input frame, processed_time SHOULD
  advance only when the input frame is actually consumed by the join stage.
- Timestamp source: The field used to read timestamps (FrameDescriptor or
  SlotHeader).
- MergeMap authority: The producer or supervisor responsible for publishing
  MergeMap rules for a given output stream.

## 5. Requirements and Constraints

- The steady-state hot path MUST remain type-stable and zero-allocation after
  initialization.
- JoinBarrier MUST NOT wait on SHM slot commit or overwrite prevention.
- JoinBarrier MUST only gate attempts to process; slot validation remains
  single-attempt and drop-on-failure per AeronTensorPool semantics.
- MergeMap MUST be treated as configuration (low-rate, control-plane data).

## 6. MergeMap

### 6.1 Overview

MergeMap defines how output sequences or output timestamps map to required
input sequences or input timestamps for a specific `(out_stream_id, epoch)`
pair. Output and input sequences are `u64`.

### 6.2 Rule Types (Sequence)

Each MergeMap entry consists of one or more rules. Each rule MUST identify an
input stream and a rule type.

For each rule, exactly one parameter MUST be present:
- For `OFFSET`, `offset` MUST be non-null and `windowSize` MUST be null.
- For `WINDOW`, `windowSize` MUST be non-null and `offset` MUST be null.
Rules violating these constraints MUST be rejected, and the MergeMap MUST be
treated as invalid until replaced.

#### 6.2.1 Sequence Offset Rule

```
in_seq = out_seq + offset
```

Use cases include aligned joins and fixed-latency compensation.
The offset MUST be encoded as a signed 32-bit integer. If `out_seq + offset`
would be negative for a given `out_seq`, SequenceJoinBarrier MUST treat the rule as not
ready until `out_seq >= -offset`.

#### 6.2.2 Window Rule

```
in_seq_range = [out_seq - N + 1, out_seq]
```

The SequenceJoinBarrier readiness check uses the upper bound of the range, and actual
slot validation determines the usability of individual elements in the range.
Window size MUST be a positive integer.
If `out_seq + 1 < N`, SequenceJoinBarrier MUST treat the rule as not ready.
SequenceJoinBarrier readiness uses only the upper bound; the join stage MAY
attempt to process all frames in the window but MUST tolerate missing or
overwritten frames. Implementations MAY add an optional lower-bound availability
check (e.g., based on header ring size and observed cursor), but MUST NOT wait
for commit stability.

### 6.3 Rule Types (Timestamp)

Timestamp MergeMap rules map an output timestamp `out_time` (nanoseconds) to
required input timestamps `in_time`. All timestamps MUST use a declared clock
domain (see `ClockDomain` in `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`).

For each timestamp rule, exactly one parameter MUST be present:
- For `OFFSET_NS`, `offsetNs` MUST be non-null and `windowNs` MUST be null.
- For `WINDOW_NS`, `windowNs` MUST be non-null and `offsetNs` MUST be null.
Rules violating these constraints MUST be rejected, and the MergeMap MUST be
treated as invalid until replaced.

#### 6.3.1 Timestamp Offset Rule

```
in_time = out_time + offset_ns
```

The offset MUST be encoded as a signed 64-bit integer. If `out_time + offset_ns`
would be negative for a given `out_time`, TimestampJoinBarrier MUST treat the
rule as not ready until `out_time >= -offset_ns` (clamped to 0). Lateness is
applied only in the readiness inequality in §8.1.

#### 6.3.2 Timestamp Window Rule

```
in_time_range = [out_time - window_ns, out_time]
```

Window size MUST be a positive integer. If `out_time < window_ns`, the rule is
not ready. Readiness uses `out_time - lateness_ns` as the upper-bound check,
while the rule definition remains `in_time_range = [out_time - window_ns, out_time]`.
If `latenessNs` is zero, readiness still requires `observed_time` to reach
`out_time`; deployments that expect lagged inputs SHOULD size `latenessNs` to
cover the desired window.

### 6.4 Stability and Epoch Handling

- MergeMap MUST be scoped to `(out_stream_id, epoch)`.
- MergeMap changes MUST invalidate SequenceJoinBarrier and TimestampJoinBarrier
  readiness for the associated output stream until the new MergeMap is applied.
- On epoch change, JoinBarrier MUST require a fresh MergeMap before resuming
  processing for that output stream.

## 7. SequenceJoinBarrier

### 7.1 Readiness Condition

For a given `out_seq`, SequenceJoinBarrier readiness MUST be computed as follows:

```
FOR EACH rule r:
  observed_cursor[r.stream_id] >= required_in_seq(r, out_seq)
AND
  (if configured) processed_cursor[r.stream_id] >= required_in_seq(r, out_seq)
```

If all conditions are satisfied, SequenceJoinBarrier MAY allow the stage to attempt
processing of `out_seq`.

### 7.2 Commit and Overwrite Semantics

- SequenceJoinBarrier MUST NOT wait for SHM slot commit stability.
- SequenceJoinBarrier MUST NOT attempt to prevent overwrite in the SHM data plane.
- Slot validation MUST remain single-attempt with drop-on-failure semantics.

### 7.3 JoinBarrier State for Late Join

- If no MergeMap is available for `(out_stream_id, epoch)`, SequenceJoinBarrier MUST
  remain blocked for that output stream.
- Upon receiving a `SequenceMergeMapAnnounce` that matches `(out_stream_id, epoch)`,
  SequenceJoinBarrier MAY resume readiness evaluation for subsequent `out_seq` values.

### 7.4 Staleness Policy (Optional Extension)

JoinBarriers MAY support a staleness policy that allows progress when a required
input stream stops advancing. If `staleTimeoutNs` is configured in the active
MergeMap, a required input is considered stale when the local monotonic time
since its last observed frame exceeds `staleTimeoutNs`.

- When a required input is stale, the JoinBarrier MAY treat it as absent for the
  current output tick and proceed if all non-stale inputs are ready.
- The join stage MUST surface which inputs were absent (implementation-defined
  mechanism).
- If `staleTimeoutNs` is absent, the JoinBarrier MUST continue to block on
  missing inputs (default deterministic behavior).

## 8. TimestampJoinBarrier

TimestampJoinBarrier gates on timestamps instead of sequences. It is optional
and requires timestamp fields to be populated and monotonic for participating
streams.

### 8.1 Readiness Condition

For a given output timestamp `out_time`, TimestampJoinBarrier readiness MUST be
computed as follows:

```
clockDomain is consistent across all rules
out_time uses TimestampMergeMapAnnounce.clockDomain
observed_time and processed_time use TimestampMergeMapAnnounce.clockDomain
FOR EACH rule r:
  observed_time[r.stream_id] >= required_in_time(r, out_time) - lateness_ns
AND
  (if configured) processed_time[r.stream_id] >= required_in_time(r, out_time) - lateness_ns
```

If all conditions are satisfied, TimestampJoinBarrier MAY allow the stage to
attempt processing of the corresponding output.
`required_in_time(r, out_time)` is defined by the rule itself; lateness is
applied only in the readiness inequality above.

### 8.2 Timestamp Sources and Ordering

- The timestamp source MUST be declared per rule and MUST be either
  `FrameDescriptor.timestamp_ns` or `SlotHeader.timestamp_ns`.
- Informative guidance: use `SlotHeader.timestamp_ns` for source/capture-time
  alignment (sensor time), and `FrameDescriptor.timestamp_ns` for
  availability/ingest-time alignment (pipeline time).
- When `TimestampSource=FRAME_DESCRIPTOR`, the FrameDescriptor timestamp MUST
  be present (non-null) and monotonic for the stream.
- When `TimestampSource=SLOT_HEADER`, the slot header timestamp MUST be present
  (non-null) and monotonic for the stream.
- All participating rules MUST use the same `clockDomain`, or the join MUST be
  rejected.
- If the output timestamp or any observed/processed timestamps are in a
  different clock domain than `TimestampMergeMapAnnounce.clockDomain`, the join
  MUST be rejected.
- Lateness MUST be configured via `TimestampMergeMapAnnounce`. If absent,
  lateness is zero (strict readiness).
- The `lateness_ns` term in readiness is the map-wide `latenessNs` from the
  active `TimestampMergeMapAnnounce`.
- When using WINDOW_NS rules, lateness is applied to the upper bound of the
  window (i.e., the required in_time for readiness is `out_time - lateness_ns`).
- If `latenessNs` is zero, WINDOW_NS rules still require `observed_time` to
  reach `out_time`; deployments that expect lagged inputs SHOULD set
  `latenessNs` large enough to cover the desired window.
- For streams participating in TimestampJoinBarrier, timestamps MUST be
  monotonic non-decreasing within a stream; otherwise the stream MUST be
  rejected for timestamp-based joins.
- If a required timestamp is absent (null), the rule MUST be treated as not
  ready.
- The output `out_time` MUST be monotonic non-decreasing for a given output
  stream and MUST use the declared clock domain.

### 8.3 Commit and Overwrite Semantics

- TimestampJoinBarrier MUST NOT wait for SHM slot commit stability.
- TimestampJoinBarrier MUST NOT attempt to prevent overwrite in the SHM data
  plane.
- Slot validation MUST remain single-attempt with drop-on-failure semantics.

### 8.4 Pitfalls and Constraints (Informative)

- TimestampJoinBarrier relies on clock alignment; for cross-host joins, use
  `ClockDomain=REALTIME_SYNCED` and document the drift budget.
- Non-monotonic timestamps within a stream can stall readiness or mis-order
  joins; such streams should be rejected for timestamp joins.
- If timestamps are missing (null) for a stream, the join will remain not ready.
- Timestamps from `FrameDescriptor` and `SlotHeader` may reflect different
  capture points; pick one source per rule to avoid skew.
- Timestamp-based joins may require buffering/reordering to tolerate jitter,
  which can conflict with zero-allocation hot paths.

### 8.5 Lessons from Timestamp Mixers (Informative)

Systems like GStreamer align streams by timestamp against a shared clock and
use explicit latency budgets. Useful takeaways for this spec:

- Treat lateness as a first-class, configurable budget (`latenessNs`).
- Make drop vs stall policy explicit when data arrives beyond the lateness
  window.
- Use epoch changes as hard discontinuities similar to timestamp segment resets.
- Consider a dedicated jitter-buffer stage rather than hiding buffering inside
  the join.

## 9. LatestValueJoinBarrier

LatestValueJoinBarrier is an optional best-effort join that uses the most
recent observed input per stream (as-of join). It is intended for low-latency
pipelines that tolerate stale inputs.

### 9.1 Readiness Condition

For a given output tick (sequence or timestamp), LatestValueJoinBarrier MUST be
considered ready when each required input stream has at least one observed
frame in the current epoch. Required input streams MUST be derived from the
active MergeMap; rule parameters are ignored. It MUST NOT require alignment on
sequences or timestamps.

If a previously selected latest frame for any input stream is overwritten or
fails slot validation, that input MUST be treated as absent for the current
output tick.
If any required input is absent after validation, readiness MUST be considered
false and the join MUST be retried on a subsequent tick.

LatestValueJoinBarrier MUST NOT use frames from prior epochs.

Define `min_seq_in_epoch` as the first observed `FrameDescriptor.seq` for a
stream after adopting the current epoch for that stream. Implementations MAY
use this as a readiness threshold to ensure at least one in-epoch observation
per input.

### 9.2 Selection Rule

- For each input stream, LatestValueJoinBarrier MUST select the most recent
  observed frame (by sequence or timestamp in that stream).
- Implementations MAY choose sequence or timestamp ordering per stream, but
  MUST be consistent within a join stage.
- If timestamp ordering is used, the timestamp source for each stream MUST use
  the declared clock domain from the active MergeMap and all inputs for the
  stage MUST share a single clock domain to make "most recent" unambiguous.
- If the selected frame is overwritten or fails slot validation, the input is
  treated as absent for that output tick.

### 9.3 Semantics and Constraints

- LatestValueJoinBarrier MUST be treated as best-effort and MAY use stale data.
- It MUST NOT wait for SHM slot commit stability or prevent overwrite.
- It SHOULD be used only when the application tolerates temporal misalignment.

## 10. Wire Format

### 10.1 Sequence MergeMap Announce Message

A new control-plane message is REQUIRED to convey MergeMap rules. The message
MUST be SBE-encoded and carried on the control stream (shared or per-consumer)
per the control-plane conventions in `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.
Recommended control stream ID ranges are described in
`docs/STREAM_ID_CONVENTIONS.md`.

The message MUST contain:

- outStreamId
- epoch (u64)
- staleTimeoutNs (optional; map-wide, see §7.4)
- rules[] (repeating group; rule count is `numInGroup`)

Each rule MUST encode:

- inputStreamId
- ruleType
- parameters (offset, windowSize)

Implementations SHOULD encode `rules` as an SBE repeating group with fixed
fields and use explicit null sentinels for unused parameters (consistent with
the SBE presence rules in `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`).
Template IDs and field IDs MUST be allocated in the control-plane schema
appendix and remain consistent with the control-plane `schemaId` and `version`.
MergeMap authorities SHOULD re-announce MergeMap rules at a low cadence as
soft-state, and MUST respond to explicit `SequenceMergeMapRequest` messages.
Implementations MUST set `MessageHeader.schemaId = 903` for MergeMap messages.

### 10.2 FrameDescriptor

FrameDescriptor remains the availability signal. No changes are REQUIRED for
SHM slot headers.

### 10.3 Sequence MergeMap Request (Late Join)

To support late joiners without requiring a central cache, an explicit
request/response path is REQUIRED.

- A consumer that needs MergeMap MUST send a `SequenceMergeMapRequest` on the control
  stream.
- The MergeMap authority (producer or supervisor that owns the output stream)
  MUST respond with a `SequenceMergeMapAnnounce` for the requested `(out_stream_id,
  epoch)`.
- The driver MUST NOT cache or proxy MergeMap state.

The `SequenceMergeMapRequest` message MUST contain:

- outStreamId
- epoch (u64)

Implementations MAY extend the request with additional fields (e.g., a list of
stream IDs or a capability flag), but the minimal form above is REQUIRED for
interoperability.

### 10.4 Timestamp MergeMap Announce Message

A timestamp MergeMap announcement is REQUIRED to convey timestamp-based rules.
The message MUST be SBE-encoded and carried on the control stream per the
control-plane conventions in `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.
When field ordering is ambiguous, the SBE schema appendix is authoritative.

The message MUST contain:

- outStreamId
- epoch (u64)
- staleTimeoutNs (optional; map-wide, see §7.4)
- clockDomain
- latenessNs (optional; map-wide, see §8.2)
- rules[] (repeating group; rule count is `numInGroup`)

Each rule MUST encode:

- inputStreamId
- ruleType
- timestampSource
- parameters (offsetNs, windowNs)

If `latenessNs` is absent, it defaults to zero.

Timestamp MergeMap authorities SHOULD re-announce rules at a low cadence as
soft-state, and MUST respond to explicit `TimestampMergeMapRequest` messages.

### 10.5 Timestamp MergeMap Request (Late Join)

The timestamp request message MUST contain:

- outStreamId
- epoch (u64)

## 11. Examples (Informative)

Worked examples are provided in Appendix A.

## 12. Feature Comparison Matrix (Informative)

| Feature | SequenceJoinBarrier (AeronTensorPool) | Disruptor SequenceBarrier |
|---|---|---|
| Sequence spaces | Multiple input streams | Single sequence space |
| Availability signal | FrameDescriptor observation | Contiguous publish cursor |
| Overwrite handling | SHM overwrite allowed | Gating prevents overwrite |
| Commit waiting | Does not wait for commit | May wait for publish/availability |
| Dependency mapping | Explicit MergeMap rules | Implicit single-sequence graph |
| Window joins | Supported via Window Rule | Not natively supported |
| Deterministic offsets | Supported via Offset Rule | Supported via sequencing only |
| Timestamp joins | Supported via TimestampJoinBarrier (optional) | Not supported |
| As-of joins | Supported via LatestValueJoinBarrier (optional) | Not supported |
| Epoch-scoped config | REQUIRED | Not applicable |
| Hot-path allocs | Zero-alloc after init | Depends on implementation |

## 13. Usage Guidance (Informative)

Usage guidance and worked examples are provided in Appendix A.

## 14. Interoperability Requirements

- Mixed schema traffic MUST be guarded by `MessageHeader.schemaId` before decode.
- Embedded TensorHeader decode MUST use `TensorHeaderMsg.wrap!` on the read path.
- TimestampJoinBarrier MUST use the same clock domain across all participating
  streams; otherwise it MUST reject the join.
- Codecs SHOULD be regenerated after spec changes to avoid schema mismatches.

## 15. SBE Schema Appendix (Informative, Draft)

This appendix provides a draft SBE schema fragment for MergeMap messages. It is
intended to be implemented independently from the base wire spec while
following its control-plane conventions and message header.
Rules must follow the parameter pairing constraints in the normative text:
OFFSET requires a non-null offset and null windowSize; WINDOW requires a
non-null windowSize and null offset. Timestamp rules follow the same pattern
for offsetNs and windowNs.

```xml
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                   package="shm.tensorpool.merge"
                   id="903"
                   version="1"
                   semanticVersion="1.0"
                   byteOrder="littleEndian">
  <types>
    <composite name="messageHeader">
      <type name="blockLength" primitiveType="uint16"/>
      <type name="templateId"  primitiveType="uint16"/>
      <type name="schemaId"    primitiveType="uint16"/>
      <type name="version"     primitiveType="uint16"/>
    </composite>

    <composite name="groupSizeEncoding">
      <type name="blockLength" primitiveType="uint16"/>
      <type name="numInGroup"  primitiveType="uint16"/>
    </composite>

    <enum name="ClockDomain" encodingType="uint8">
      <validValue name="MONOTONIC">1</validValue>
      <validValue name="REALTIME_SYNCED">2</validValue>
    </enum>

    <enum name="TimestampSource" encodingType="uint8">
      <validValue name="FRAME_DESCRIPTOR">1</validValue>
      <validValue name="SLOT_HEADER">2</validValue>
    </enum>

    <enum name="MergeRuleType" encodingType="uint8">
      <validValue name="OFFSET">0</validValue>
      <validValue name="WINDOW">1</validValue>
    </enum>

    <enum name="MergeTimeRuleType" encodingType="uint8">
      <validValue name="OFFSET_NS">0</validValue>
      <validValue name="WINDOW_NS">1</validValue>
    </enum>
  </types>

  <message name="SequenceMergeMapAnnounce" id="1">
    <field name="outStreamId" id="1" type="uint32"/>
    <field name="epoch" id="2" type="uint64"/>
    <field name="staleTimeoutNs" id="3" type="uint64" presence="optional" nullValue="18446744073709551615"/>
    <group name="rules" id="4" dimensionType="groupSizeEncoding">
      <field name="inputStreamId" id="5" type="uint32"/>
      <field name="ruleType" id="6" type="MergeRuleType"/>
      <field name="offset" id="7" type="int32" presence="optional" nullValue="-2147483648"/>
      <field name="windowSize" id="8" type="uint32" presence="optional" nullValue="4294967295"/>
    </group>
  </message>

  <message name="SequenceMergeMapRequest" id="2">
    <field name="outStreamId" id="1" type="uint32"/>
    <field name="epoch" id="2" type="uint64"/>
  </message>

  <message name="TimestampMergeMapAnnounce" id="3">
    <field name="outStreamId" id="1" type="uint32"/>
    <field name="epoch" id="2" type="uint64"/>
    <field name="staleTimeoutNs" id="3" type="uint64" presence="optional" nullValue="18446744073709551615"/>
    <field name="clockDomain" id="4" type="ClockDomain"/>
    <field name="latenessNs" id="5" type="uint64" presence="optional" nullValue="18446744073709551615"/>
    <group name="rules" id="6" dimensionType="groupSizeEncoding">
      <field name="inputStreamId" id="7" type="uint32"/>
      <field name="ruleType" id="8" type="MergeTimeRuleType"/>
      <field name="timestampSource" id="9" type="TimestampSource"/>
      <field name="offsetNs" id="10" type="int64" presence="optional" nullValue="-9223372036854775808"/>
      <field name="windowNs" id="11" type="uint64" presence="optional" nullValue="18446744073709551615"/>
    </group>
  </message>

  <message name="TimestampMergeMapRequest" id="4">
    <field name="outStreamId" id="1" type="uint32"/>
    <field name="epoch" id="2" type="uint64"/>
  </message>
</sbe:messageSchema>
```

## Appendix A. Usage Examples (Informative)

This appendix provides practical, non-normative examples for using the
SequenceJoinBarrier, TimestampJoinBarrier, and LatestValueJoinBarrier.

### A.1 Picking a JoinBarrier

- **SequenceJoinBarrier**: best when streams are in the same rate domain or
  have been rate-normalized upstream; deterministic alignment by `seq`.
- **TimestampJoinBarrier**: best when streams are time-aligned but at different
  rates; alignment by `timestamp_ns` with a configurable lateness budget.
- **LatestValueJoinBarrier**: best when low latency matters more than strict
  alignment; uses most recent value per stream.

### A.2 TimestampJoinBarrier `out_time` Sources

TimestampJoinBarrier uses an `out_time` value to decide readiness. Common
sources:

- **Pipeline clock (recommended)**: a monotonic clock in the join agent. You
  compute `out_time` on each output tick.
- **Input-driven**: set `out_time` to the latest eligible input frame timestamp
  (useful for "follow the data" joins).
- **External reference**: for REALTIME_SYNCED, drive `out_time` from a
  PTP/NTP-disciplined clock when wall-clock alignment is required.

### A.3 SequenceJoinBarrier Examples

#### A.3.1 Lockstep Cameras

Scenario: Two cameras publish at the same rate. You want output only when both
reach the same sequence so the join is deterministic.

Approach: Use SequenceJoinBarrier with identical sequence rules.

```
camA: in_seq = out_seq
camB: in_seq = out_seq
```

Result: Output advances only when both streams reach the same `seq`.

#### A.3.2 Fixed Offset Compensation

Scenario: Stream B lags by two frames due to a known processing stage.

Approach: Use an OFFSET rule to compensate the fixed latency.

```
A: in_seq = out_seq
B: in_seq = out_seq - 2
```

Result: Output advances with A while still aligning B to its delayed sequence.

#### A.3.3 Sliding Window Join

Scenario: Stream A is bursty; you can accept any of the last 5 frames while
still gating on `out_seq`.

Approach: Use a WINDOW rule to allow a bounded lookback.

```
A: in_seq_range = [out_seq - 4, out_seq]
```

Result: The join picks the newest valid frame within the window; stale/overwritten
slots simply fail validation.

#### A.3.4 Diamond Pattern (Sequence-Preserving Branches)

Scenario: A single source fans out to two branches and is rejoined downstream.

```
source -> branch1 -> B
source -> branch2 -> C
B + C -> D
```

Approach: Use SequenceJoinBarrier at D when both branches preserve sequence
numbers.

Result: D advances only when both branches reach the same `seq`, avoiding skew.

### A.4 TimestampJoinBarrier Examples

#### A.4.1 Pipeline Clock (Monotonic Tick)

Scenario: You need fixed-rate output regardless of input cadence.

Approach: Use a pipeline clock; set `out_time` on each tick.

```
out_time = pipeline_clock_now()
A: in_time = out_time
B: in_time = out_time - 2_000_000  # 2 ms offset
```

Result: Output cadence is stable; inputs are selected relative to each tick.

#### A.4.2 Input-Driven (Camera + IMU)

Scenario: Two streams at different rates: `cam` at ~30 Hz and `imu` at ~200 Hz.
You want each output aligned to the most recent camera frame, with the nearest
IMU sample inside a short window.

Approach: When a new camera frame arrives, set `out_time = cam.timestamp_ns`.
Use a WINDOW rule for IMU so the join is ready as long as IMU has a sample
within the last 10 ms.

Rules:
```
cam: OFFSET_NS = 0
imu: WINDOW_NS = 10_000_000
latenessNs = 10_000_000
```

Readiness:

```
observed_time[cam] >= out_time
observed_time[imu] >= out_time - lateness_ns
```

Result: The join selects the newest IMU sample within the window and emits
output at camera time.

#### A.4.3 Sensor Fusion (Camera + Lidar + IMU)

Scenario: Fuse camera + lidar + IMU with different rates.

Approach: Drive `out_time` from the camera and use short windows for IMU/lidar.

Example:
```
out_time = cam.timestamp_ns
cam: OFFSET_NS = 0
lidar: WINDOW_NS = 50_000_000   # 50 ms
imu:   WINDOW_NS = 10_000_000   # 10 ms
latenessNs = 50_000_000
```

Result: Camera anchors the join; lidar/IMU samples are chosen from bounded
windows around the camera time.

#### A.4.4 Event-Time Window Join (Flink/Beam-style)

Scenario: Join two streams by event time with bounded lateness.

Approach: Use a watermark or pipeline clock as `out_time` and set `latenessNs`
to allow late arrivals.

Example:
```
out_time = watermark_time()
latenessNs = 5_000_000_000  # 5 s
A: WINDOW_NS = 2_000_000_000
B: WINDOW_NS = 2_000_000_000
```

Result: At each watermark tick, accept frames in the last 2 s while tolerating
up to 5 s of lateness.

#### A.4.5 Audio/Video Sync (External Reference)

Scenario: Multi-host audio + video with REALTIME_SYNCED timestamps.

Approach: Drive `out_time` from a PTP/NTP-disciplined clock to align streams
to wall-clock time.

```
out_time = ptp_now()
video: in_time = out_time
audio: in_time = out_time - 20_000_000  # audio pipeline latency
```

Result: The join is wall-clock anchored; video and audio are selected relative
to the external reference time.


#### A.4.6 Batch-to-Stream Reconciliation

Scenario: Join periodic batch results with a high-rate stream.

Approach: Use TimestampJoinBarrier; apply OFFSET_NS for batch lag.

Example:
```
out_time = pipeline_clock_now()
stream: OFFSET_NS = 0
batch:  OFFSET_NS = -30_000_000_000  # 30 s batch delay
```

Result: Batch results are aligned to the streaming timeline with a fixed delay.

### A.5 LatestValueJoinBarrier Examples

#### A.5.1 Fast Data + Slow Configuration

Scenario: The config stream updates at ~0.1 Hz while data is 1 kHz. Each output
should use the most recent config without blocking.

Approach: Use LatestValueJoinBarrier with a staleness policy.

```
inputs = [data, config]
staleTimeoutNs = 60_000_000_000  # 60 s
```

Result: Outputs use the latest config; if it is stale, it is marked absent.

#### A.5.2 Telemetry Overlay (Health + Metrics)

Scenario: Attach health/heartbeat state to high-rate metrics.

Approach: Use LatestValueJoinBarrier and treat health as optional.

```
inputs = [metrics, health]
staleTimeoutNs = 5_000_000_000  # 5 s
```

Result: Metrics continue even if health stalls, with explicit absent markers.

#### A.5.3 Most-Recent Buffer Join

Scenario: Use the latest buffer from multiple sensors without strict
synchronization. This matches many AO workflows that prefer "freshest
available" data.

Approach: Use LatestValueJoinBarrier and select the most recent per stream.

Result: Low-latency outputs with best-effort alignment.

### A.6 Multi-Rate Join Patterns

- **High-rate -> low-rate alignment**: use TimestampJoinBarrier with a small
  WINDOW_NS for the high-rate stream; drive `out_time` from the low-rate stream.
- **Jittery sources**: set `latenessNs` and consider a jitter-buffer stage if
  reordering is required.
- **Rate normalization**: insert a rate limiter upstream, then use
  SequenceJoinBarrier for deterministic joins.

### A.7 Practical Tips

- Keep all TimestampJoinBarrier streams in the same clock domain.
- Use `staleTimeoutNs` when a dead input should not block outputs forever.
- Prefer explicit lateness budgets over indefinite waits to bound jitter.
- For cross-host joins, require REALTIME_SYNCED and document the clock error
  budget of the deployment.
