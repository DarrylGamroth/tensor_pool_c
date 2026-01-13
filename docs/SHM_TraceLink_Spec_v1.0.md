# TraceLink and Provenance Specification for AeronTensorPool (RFC Style)

## 1. Scope

This document defines TraceLink, an optional provenance and observability layer
for AeronTensorPool pipelines. It specifies trace identity, causal linkage
messages, and how TraceLink integrates with `FrameDescriptor`. It also provides
an informative SQLite persistence model. TraceLink is not part of runtime flow
control or correctness.

## 2. Key Words

The key words "MUST", "MUST NOT", "REQUIRED", "SHOULD", "SHOULD NOT", and "MAY"
are to be interpreted as described in RFC 2119.

## 3. Conformance

Unless explicitly marked as Informative, sections in this document are
Normative.

## 4. Terminology

- Trace ID: A unique identifier that groups frames belonging to the same logical
  lineage.
- Root frame: A frame that begins a new lineage (e.g., acquisition source).
- Derived frame: A frame produced from one or more parent frames.
- TraceLink: A causal edge from a derived trace to one or more parent traces.
- TraceLinkSet: A control-plane message that reports parent trace IDs for an
  output trace.

## 5. Design Constraints

- TraceLink MUST NOT affect runtime scheduling, readiness, or data correctness.
- TraceLink emission MUST be best-effort and non-blocking.
- Tracing loss MUST NOT affect pipeline correctness.

## 6. Trace Identity

### 6.1 Trace ID Format

Trace IDs MUST be 64-bit identifiers. Implementations SHOULD use a
Snowflake-style format so that IDs are sortable by embedded timestamp. The
recommended layout matches the Agrona Snowflake ID format and
`SnowflakeId.jl` (timestamp + node ID + per-tick sequence). The exact bit
layout is defined by those implementations.

### 6.2 Node ID Allocation

Each producer MUST use a node ID that is unique within the deployment. Node IDs
MAY be:

- statically configured per process/host,
- allocated by the driver (e.g., via `ShmAttachResponse.nodeId`), or
- allocated by discovery services.

If node IDs are allocated dynamically, they MUST remain stable for the lifetime
of the process to avoid collisions within the same timestamp window.
To preserve provenance clarity when using dynamic allocation, deployments
SHOULD:

- enforce a reuse cooldown for node IDs after process exit,
- record `(node_id, process_id, start_time)` in the provenance store, and
- keep node IDs stable until the process terminates.

Dynamic allocation SHOULD reuse the driver lease/keepalive model described in
`docs/SHM_Driver_Model_Spec_v1.0.md` (request/response + keepalive + revoke),
so allocation remains authoritative and robust to client failure.

### 6.3 Propagation Rules

- Root frames MUST mint a new trace ID.
- 1→1 derived frames MUST reuse the upstream trace ID.
- N→1 derived frames MUST mint a new trace ID and record parent trace IDs via
  TraceLinkSet.

## 7. TraceLink Semantics

TraceLink records causal edges from an output trace to one or more parent
traces:

```
trace_id -> parent_trace_id
```

TraceLinkSet SHOULD be emitted only when the parents group length is > 1.
It MAY be emitted with length = 1 for explicit re-rooting or retagging.
Emission is best-effort and MAY be dropped under load.

## 8. FrameDescriptor Integration

### 8.1 trace_id Field

`FrameDescriptor` is extended with an optional field:

- `trace_id : uint64`

Rules:

- The null trace ID sentinel MUST be `0` (unset).
- If tracing is enabled for a stream, producers MUST populate `trace_id` with a
  non-zero value.
- 1→1 stages MUST propagate `trace_id` unchanged.
- N→1 stages MUST mint a new `trace_id` for outputs and emit TraceLinkSet for
  parent linkage.
- On epoch change, implementations MAY either continue trace IDs or reset them;
  the chosen behavior MUST be documented per deployment.

No SHM slot layout changes are required.

### 8.2 Backward Compatibility

Consumers that do not understand `trace_id` MUST ignore it and remain fully
functional.

## 9. TraceLinkSet Message (Control Plane)

### 9.1 Definition

TraceLinkSet reports parent relationships for an output trace:

```
TraceLinkSet
  stream_id
  epoch
  seq
  trace_id
  parents[] { trace_id }
```

### 9.2 Rules

- TraceLinkSet MUST use `MessageHeader.schemaId = 904` and
  `MessageHeader.templateId = 1`.
- `stream_id`, `epoch`, and `seq` MUST match the corresponding
  `FrameDescriptor` for the output frame.
- `trace_id` MUST identify the derived output frame.
- `parents[]` MUST list all parent trace IDs for N→1 stages and MUST contain
  only non-zero values (null sentinel `0` is invalid).
- The number of parents is derived from the `parents[]` group length; the SBE
  `numInGroup` value is authoritative and MUST be ≥ 1.
- Parent trace IDs MUST be unique within a TraceLinkSet. Implementations SHOULD
  preserve parent ordering as observed by the join stage.
- TraceLinkSet SHOULD be emitted only when the parents group length is > 1.
  It MAY be emitted with length = 1 for explicit re-rooting or retagging.

## 10. Persistence Model (Informative)

### 10.1 Frames Table

```sql
CREATE TABLE frames (
  stream_id   INTEGER NOT NULL,
  epoch       INTEGER NOT NULL,
  seq         INTEGER NOT NULL,
  stage_id    INTEGER NOT NULL,
  t_ns        INTEGER NOT NULL,
  trace_id    INTEGER NOT NULL,
  status      INTEGER NOT NULL,
  PRIMARY KEY (stream_id, epoch, seq)
) WITHOUT ROWID;

CREATE INDEX frames_trace_idx ON frames(trace_id);
```

If tracing is disabled, implementations MAY omit rows for untraced frames, or
MAY persist rows with `trace_id = 0` to indicate absence.

### 10.2 Trace Links Table

```sql
CREATE TABLE trace_links (
  trace_id        INTEGER NOT NULL,
  parent_trace_id INTEGER NOT NULL,
  PRIMARY KEY (trace_id, parent_trace_id)
) WITHOUT ROWID;

CREATE INDEX trace_links_parent_idx ON trace_links(parent_trace_id);
```

### 10.3 Optional Artifact Provenance

Low-rate artifacts (e.g., calibration matrices) MAY be recorded separately
using range-based association tables.

### 10.4 Window Metadata (Informative)

For stateful integrators that defer lineage expansion, persist window bounds
per output trace in a separate table:

```sql
CREATE TABLE trace_windows (
  trace_id     INTEGER NOT NULL,
  in_stream_id INTEGER NOT NULL,
  epoch        INTEGER NOT NULL,
  seq_start    INTEGER,
  seq_end      INTEGER,
  t_start_ns   INTEGER,
  t_end_ns     INTEGER,
  PRIMARY KEY (trace_id, in_stream_id)
) WITHOUT ROWID;

CREATE INDEX trace_windows_stream_idx ON trace_windows(in_stream_id, epoch);
```

Populate either the seq bounds or time bounds; unused columns may be NULL.

## 11. Query Examples (Informative)

### 11.1 Trace All Frames in a Chain

```sql
SELECT * FROM frames WHERE trace_id = ? ORDER BY t_ns;
```

### 11.2 Immediate Parents of a Trace

```sql
SELECT parent_trace_id FROM trace_links WHERE trace_id = ?;
```

### 11.3 Downstream Effects of a Trace

```sql
SELECT trace_id FROM trace_links WHERE parent_trace_id = ?;
```

### 11.4 Recursive Lineage

SQLite recursive CTEs MAY be used to traverse multi-hop trace graphs.

## 12. Interaction with Runtime Processing

- TraceLink MUST NOT drive runtime readiness or flow control.
- TraceLink records causality after the fact and is orthogonal to data-plane
  processing.

## 12.1 Stateful Integrators (Informative)

For long-horizon or streaming integrators, per-output parent enumeration can be
too costly in the hot path. A recommended pattern is:

- Emit output frames with `trace_id` and persist lightweight window metadata
  (e.g., input stream IDs and time/seq bounds).
- Derive parent relationships offline in the database by joining input frames
  that fall within the recorded bounds.

This keeps the hot path O(1) per output frame (set `trace_id` + append a small
window record) while preserving lineage fidelity. See §14.2.3 for the reference
design.

## 13. Wire Format Changes

- `TraceLinkSet` is a new control-plane message type with `schemaId=904` and
  `templateId=1`.
- `FrameDescriptor` is extended with optional `trace_id`.
- Mixed-schema control streams MUST gate decoding on `MessageHeader.schemaId`.

No changes to SHM slot headers are required.

## 14. Reliability and Performance

- TraceLink messages MAY be batched.
- Tracing MAY be sampled or dropped under load.
- Persistence implementations SHOULD batch writes (e.g., 10–100 ms
  transactions).

Tracing loss MUST NOT affect correctness.

## 14.1 Implementation Hints (Informative)

- Use a preallocated ring buffer or queue (single-threaded or SPSC) for
  TraceLinkSet emission to avoid locks, allocations, and added latency.
- Prefer monotonic clocks for trace ID generation to keep IDs roughly ordered.
- Coalesce multiple TraceLinkSet entries into a single publish when possible.
- For stateful integrators, follow the windowed reference design in §14.2.3 to
  keep the hot path minimal.
- If using SQLite, set `PRAGMA journal_mode=WAL` and batch inserts to reduce
  contention.
- Guard mixed control-plane traffic by checking `MessageHeader.schemaId` before
  decode.

## 14.2 Reference Designs (Informative)

### 14.2.1 1→1 Stage (Filter)

- Reuse the incoming `trace_id` on the output.
- Do not emit TraceLinkSet.
- Persist the output frame row with the propagated `trace_id`.

### 14.2.2 N→1 Stage (Join)

- Mint a new `trace_id` for each output.
- Emit a TraceLinkSet with parents = input trace IDs.
- Persist `trace_links` directly from the emitted TraceLinkSet.

### 14.2.3 Stateful Integrator (Windowed Output)

- Mint a new `trace_id` per output window.
- Persist `trace_windows` with `(trace_id, in_stream_id, epoch, seq_start, seq_end)`
  or time bounds.
- Expand lineage offline by joining `trace_windows` to `frames`.

### 14.2.4 Offline Expansion Job

- Periodically materialize `trace_links` with a SQL join on `trace_windows`.
- Deduplicate `(trace_id, parent_trace_id)` before insert.
- Schedule expansion in batches to avoid long-running transactions.

## 15. Summary

TraceLink provides:

- explicit provenance tracking
- compatibility with existing correlation-id workflows
- scalable persistence for lineage queries

## 16. SBE Schema Appendix (Informative, Draft)

This appendix provides a draft SBE schema fragment for TraceLink messages. It
follows the control-plane conventions in `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.
When field ordering is ambiguous, the SBE schema appendix is authoritative.
Encoders MUST set `parents` group length (`numInGroup`) to at least 1 and MUST
NOT emit zero trace IDs in the group.

```xml
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                   package="shm.tensorpool.tracelink"
                   id="904"
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
  </types>

  <message name="TraceLinkSet" id="1">
    <field name="streamId" id="1" type="uint32"/>
    <field name="epoch" id="2" type="uint64"/>
    <field name="seq" id="3" type="uint64"/>
    <field name="traceId" id="4" type="uint64"/>
    <group name="parents" id="5" dimensionType="groupSizeEncoding">
      <field name="traceId" id="6" type="uint64"/>
    </group>
  </message>
</sbe:messageSchema>
```
