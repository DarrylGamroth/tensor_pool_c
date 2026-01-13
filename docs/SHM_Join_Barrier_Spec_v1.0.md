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
rule as not ready until `out_time >= -offset_ns + lateness_ns` (clamped to 0).

#### 6.3.2 Timestamp Window Rule

```
in_time_range = [out_time - window_ns, out_time]
```

Window size MUST be a positive integer. If `out_time < window_ns`, the rule is
not ready. Readiness uses `out_time - lateness_ns` as the upper-bound check,
while the rule definition remains `in_time_range = [out_time - window_ns, out_time]`.

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
- Timestamps from `FrameDescriptor` and `TensorHeader` may reflect different
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

### 11.1 Two-Stream Aligned Join

```
A: in_seq = out_seq
B: in_seq = out_seq
```

SequenceJoinBarrier condition for `out_seq = 12034`:

```
observed[A] >= 12034
observed[B] >= 12034
```

### 11.2 Offset Compensation Join

```
A: in_seq = out_seq
B: in_seq = out_seq - 2
```

SequenceJoinBarrier condition for `out_seq = 12034`:

```
observed[A] >= 12034
observed[B] >= 12032
```

### 11.3 Sliding Window Join

```
A: in_seq_range = [out_seq - 4, out_seq]
```

SequenceJoinBarrier readiness uses `observed[A] >= out_seq`, and slot validation
determines which window members are usable.

### 11.4 Timestamp Offset Join

```
clock_domain = REALTIME_SYNCED
A: in_time = out_time
B: in_time = out_time - 5_000_000  # 5 ms offset
```

TimestampJoinBarrier condition for `out_time = 1_700_000_000_000_000_000`:

```
observed_time[A] >= 1_700_000_000_000_000_000
observed_time[B] >= 1_699_999_999_995_000_000
```

### 11.5 Latest Value (As-Of) Join

```
inputs = [A, B, C]
```

LatestValueJoinBarrier readiness for an output tick:

```
observed[A] >= min_seq_in_epoch
observed[B] >= min_seq_in_epoch
observed[C] >= min_seq_in_epoch
```

Each input uses the most recent observed frame for that stream in the current
epoch, regardless of sequence alignment.

### 11.6 Diamond Pattern Join

```
source -> branch1 -> B
source -> branch2 -> C
B + C -> D
```

MergeMap for D:

```
B: in_seq = out_seq
C: in_seq = out_seq
```

SequenceJoinBarrier for `out_seq = N` requires both branches to reach N before
attempting the join into D.

### 11.7 Stale Input Degradation (Optional Extension)

```
staleTimeoutNs = 5_000_000_000  # 5 s
inputs = [A, B]
```

If stream B has not advanced for `staleTimeoutNs`, the JoinBarrier may proceed
using A while marking B as absent for that output tick.

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

### 13.1 SequenceJoinBarrier Usage

Use SequenceJoinBarrier when:

- Streams are in the same rate domain or have been rate-normalized upstream.
- Deterministic alignment by sequence is required.
- The join stage must remain zero-alloc and low-latency.

Typical pattern:
- Use `SequenceMergeMapAnnounce` with OFFSET or WINDOW rules.
- Join on `FrameDescriptor.seq` and validate slots once.

### 13.2 TimestampJoinBarrier Usage

Use TimestampJoinBarrier when:

- Streams are naturally time-aligned but operate at different rates.
- Clock domains are disciplined (MONOTONIC local or REALTIME_SYNCED across hosts).
- A lateness budget is acceptable to tolerate jitter.

Typical pattern:
- Use `TimestampMergeMapAnnounce` with OFFSET_NS or WINDOW_NS rules.
- Configure `latenessNs` for a bounded lateness window.
- Prefer an explicit jitter-buffer stage if reordering is required.

### 13.3 LatestValueJoinBarrier Usage

Use LatestValueJoinBarrier when:

- Low latency is preferred over strict alignment.
- Stale inputs are acceptable (as-of join semantics).
- The join should proceed even when some inputs are slow, optionally with a
  staleness policy.

Typical pattern:
- Declare input streams in the active MergeMap.
- Select the most recent observed frame per stream, scoped to the current epoch.

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
