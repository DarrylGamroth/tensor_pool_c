# Shared-Memory Tensor Pool with Aeron + SBE
## Design Specification (v1.2)

**Abstract** This specification defines a zero-copy shared-memory transport for large tensors/images coordinated via Aeron messaging and SBE encoding. Producers write tensor metadata (shape, dtype, strides) and payloads into fixed-size SHM rings with lossy-overwrite semantics; consumers receive lightweight descriptors over Aeron IPC and read SHM directly. A seqlock commit protocol prevents torn reads. The design supports multiple independent producers/consumers, optional partial-frame progress reporting, unified QoS/metadata streams, and language-neutral implementation (C/Java/Go/Rust/Julia). Operational patterns mirror Aeron (single-writer ownership, epoch-based remapping, agent lifecycle).

This document defines a complete, self-contained specification for implementing
a zero-copy shared-memory tensor/image transport coordinated via Aeron and SBE.

It is written to be directly consumable by automated code-generation tools
(e.g. Codex, other LLMs) and by human implementers.

**Key Words** The key words “MUST”, “MUST NOT”, “REQUIRED”, “SHOULD”, “SHOULD NOT”, and “MAY” are to be interpreted as described in RFC 2119.

**Document Conventions** Normative sections: 6–11, 15.1–15.22, 16. Informative sections: 1–5, 12–14. "NOTE:"/"Rationale:" text is informative. Uppercase MUST/SHOULD/MAY keywords appear only in normative sections and carry RFC 2119 force; any lowercase "must/should/may" in informative text is non-normative.
**Informative Reference** Stream ID guidance: `docs/STREAM_ID_CONVENTIONS.md`.
**Version History**
- v1.2 (2026-01-12): Remove `headerIndex` from `FrameDescriptor`/`FrameProgress` and derive slot indices from `seq`; rename `FrameProgress.frame_id` to `seq`. Clarify file-backed `mmap` guidance. Update `stride_bytes` validation to require power-of-two multiples of 64 bytes (no page-alignment requirement). Schema version fields remain unchanged.
- v1.1 (2025-12-30): Initial RFC-style specification. Adds normative algorithms, compatibility matrix, explicit field alignment (SlotHeader + TensorHeader), language-neutral requirements, and progress reporting protocol. Normative wire/SHM schemas included. Requires `announce_clock_domain` in `ShmPoolAnnounce` and forbids unsynchronized realtime timestamps.
---

## 1. Goals

- Zero-copy local data exchange for large tensors/images.
- Small descriptor and control messages transported via Aeron.
- All messages are **SBE encoded**.
- Support many independent producer and consumer services.
- Define two roles: producers (server/driver; own SHM and publish descriptors/control) and consumers (client; map SHM and subscribe). Packaging of these roles (combined vs split, akin to Aeron driver/client) is an implementation choice, not a protocol requirement.
- Lossy overwrite semantics: slow consumers may drop frames.
- Consumers must never read partially written (“torn”) data.
- Optional bridge service for remote consumers or recording.
- Unified management/observability via Aeron control-plane streams.

## 2. Non-Goals (v1)

- Exactly-once delivery.
- Per-consumer backpressure or gating (no Disruptor-style gating).
- POSIX IPC (`shm_open`) or `memfd`.
- GPU-backed shared memory (future extension).

---

## 3. High-Level Architecture

Roles: producer (server/driver) owns SHM regions and publishes descriptors/control; consumer (client) maps SHM and subscribes to descriptors/control. A deployment may package both roles together (like Aeron driver+client) or split them; packaging is an implementation detail.

Each **producer service** owns:

1. **Header Ring (SHM)**
   - Fixed-size header slots (256 bytes each).
   - Slot count is a power of two.

2. **Payload Pool(s) (SHM)**
   - Raw byte buffers.
   - Fixed stride per pool.
   - Multiple size classes allowed.
- v1.2: same slot count as header ring; `payload_slot = header_index` (header_index derived from `seq`; decoupled/free-list mapping is a v2 change).
  - Choose `stride_bytes` as a power-of-two multiple of 64 bytes (e.g., 64 B, 256 B, 1 KiB, 4 KiB, 1 MiB, 2 MiB, 4 MiB, 8 MiB).

3. **Aeron Publications**
   - `FrameDescriptor` stream (IPC).
   - Control / metadata / QoS streams (also IPC or UDP, depending on deployment).

Consumers subscribe to Aeron streams and map SHM regions when local.

---

## 4. Shared Memory Backends

All shared-memory regions are file-backed `mmap` using a URI provided in control-plane messages. The backing filesystem and page size are deployment concerns; validation rules are defined in the normative backend validation section (see 15.22).

### 4.1 Region URI Scheme *(informative)*

The region URI scheme and validation rules are defined normatively in §15.22.
Page size or hugepage alignment is a deployment consideration but is not a
protocol requirement for `stride_bytes`.

### 4.2 Behavior Overview (informative)

- Region discovery occurs via Aeron control messages.
- Unknown URI schemes are treated as unsupported; consumers may fall back to a bridged payload stream when configured.
- `stride_bytes` remains explicit and is not inferred from page size; alignment/rejection rules are normative in 15.22.

---

## 5. Control-Plane and Data-Plane Streams

Per `stream_id` (data source), define these logical streams (how you map to Aeron channels/stream IDs is an implementation detail):

- **Descriptor Stream**: producer → all consumers (`FrameDescriptor`)
- **Control Stream**: bi-directional management and discovery messages
- **QoS Stream**: producer and consumers → supervisor/console (or shared bus)
- **Metadata Stream**: producer → all (`DataSourceAnnounce`, `DataSourceMeta`, optional blobs)

A simple deployment can multiplex these message types on one Aeron channel/stream ID, but separating them improves observability.

Per-consumer descriptor streams (optional): producers MAY publish `FrameDescriptor` on per-consumer channels/stream IDs when requested. If used, the producer MUST return the assigned descriptor channel/stream in `ConsumerConfig`, and the consumer MUST subscribe to that stream instead of the shared descriptor stream. Producers MUST stop publishing and close per-consumer descriptor publications when the consumer disconnects or times out. Liveness for per-consumer streams is satisfied by either `ConsumerHello` or `QosConsumer`; producers MUST treat a consumer as stale only when neither has been received for 3–5× the configured hello/qos interval.
When per-consumer descriptor streams are used, producers MAY apply `ConsumerHello.max_rate_hz` as a per-consumer descriptor rate cap; if `max_rate_hz` is 0, descriptors are unthrottled. `max_rate_hz` MUST be ignored when only the shared descriptor stream is used.
Per-consumer control streams (optional): producers MAY publish `FrameProgress` on per-consumer control channels/stream IDs when requested. If used, the producer MUST return the assigned control channel/stream in `ConsumerConfig`, and the consumer MUST subscribe to that control stream for `FrameProgress`. Consumers MUST continue to subscribe to the shared control stream for all other control-plane messages. Producers MUST NOT publish other control-plane messages on per-consumer control streams. Producers MUST validate requested per-consumer channels/stream IDs; if unsupported or invalid, they MUST decline by returning empty channel and null/zero stream IDs in `ConsumerConfig`.

---

## 6. SHM Region Structure

All regions begin with an SBE-encoded superblock:

```
+------------------------------+
| SBE Superblock (fixed-size)  |
+------------------------------+
| Region-specific contents     |
+------------------------------+
```

Encoding is **little-endian**.

Define `superblock_size = 64` bytes (fixed); all offsets/formulas refer to this constant in v1.2. This is normative: implementations MUST treat `superblock_size` as exactly 64 bytes (do not derive from SBE blockLength metadata).

---

## 7. SBE Messages Stored in SHM

### 7.1 ShmRegionSuperblock

Stored at offset 0 of every SHM region.

**Fields**
- `magic : u64`
- `layout_version : u32`
- `epoch : u64`
- `stream_id : u32`
- `region_type : enum { HEADER_RING=1, PAYLOAD_POOL=2 }`
- `pool_id : u16`
- `nslots : u32`
- `slot_bytes : u32`
- `stride_bytes : u32`
- `pid : u64`
- `start_timestamp_ns : u64`
- `activity_timestamp_ns : u64`
- padding

**Rules**
- Written once by producer at initialization.
- Consumers MUST validate against announced parameters.
- Producer refreshes `activity_timestamp_ns` periodically (e.g., at announce cadence); stale timestamps imply producer death and force remap.
- Encoding is little-endian. Superblock layout is SBE-defined (see schema appendix) so existing SBE parsers can `wrap!` directly over the mapped memory. Current magic MUST be ASCII `TPOLSHM1` (`0x544F504C53484D31` as u64, little-endian); treat mismatches as invalid.
- Field order is cacheline-oriented: first cache line carries validation fields (`magic`, `layout_version`, `epoch`, `stream_id`, `region_type`, `pool_id`, `nslots`, `slot_bytes`, `stride_bytes`); second cache line carries liveness/identity (`pid`, `start_timestamp_ns`, `activity_timestamp_ns`). Total size is 64 bytes.
- Keep `layout_version` even though SBE `version` exists: `layout_version` governs SHM layout compatibility; SBE `version`/`schemaId` governs control-plane messages.
- Epoch wraparound (uint64) is practically unreachable; if encountered, treat as incompatible and remap with a new `layout_version`/`epoch`.
- `pool_id` meaning: for `region_type=HEADER_RING`, `pool_id` MUST be 0. For `region_type=PAYLOAD_POOL`, `pool_id` MUST match the `pool_id` advertised in `ShmPoolAnnounce`.

---

## 8. Header Ring

### 8.1 Slot Layout

Each header slot is exactly **256 bytes** and SBE encoded.

```
slot_offset(i) =
    superblock_size + i * 256
```

### 8.2 SlotHeader and TensorHeader

This header represents “almost fixed-size TensorMessage metadata” with the large `values` buffer stored out-of-line in a payload pool.

SBE definition: `SlotHeader` and `TensorHeader` messages in the schema appendix (v1.2 `MAX_DIMS=8`). The schema includes a `maxDims` constant field for codegen alignment; it is not encoded on the wire and MUST be treated as a compile-time constant. Changing `MAX_DIMS` requires a new `layout_version` and schema rebuild.

**Encoding note (normative)**
- The header slot is a raw SBE message body (no SBE message header). `seq_commit` MUST be at byte offset 0 of the slot. SBE wrap functions MUST be used without applying a message header.

**SlotHeader fields** (wire/layout order, fixed 60-byte prefix + `headerBytes`)
- `seq_commit : u64` (commit + sequence; LSB is commit bit, upper bits are `logical_sequence`)
- `values_len_bytes : u32`
- `payload_slot : u32`
- `pool_id : u16`
- `payload_offset : u32` (typically 0; reserved)
- `timestamp_ns : u64`
- `meta_version : u32` (ties frame to `DataSourceMeta`)
- `pad : bytes[26]` (reserved; producers MAY zero-fill; consumers MUST ignore)
- `headerBytes : varData` (length MUST match the embedded TensorHeader SBE message header + blockLength; v1.2 length is 192 bytes)

**TensorHeader fields** (embedded inside `headerBytes`)
- `dtype : enum Dtype` (scalar element type)
- `major_order : enum MajorOrder` (row- vs column-major)
- `ndims : u8`
- `pad_align : u8` (aligns `dims`/`strides` to 4-byte boundary)
- `progress_unit : enum ProgressUnit` (NONE, ROWS, COLUMNS)
- `progress_stride_bytes : u32` (bytes between adjacent rows/columns when `progress_unit!=NONE`; MUST be > 0 and equal to the true row/column stride)
- `dims[MAX_DIMS] : i32[]`
- `strides[MAX_DIMS] : i32[]` (bytes per dim; 0 = contiguous/infer)
- padding to 192 bytes total (including the 8-byte SBE message header)

**Slot size math (informative)**
- 256 bytes total = 60-byte fixed prefix + 4-byte varData length prefix + 192-byte embedded `TensorHeader`.

Canonical identity: `logical_sequence = seq_commit >> 1`, `FrameDescriptor.seq`, and `FrameProgress.seq` MUST be equal for the same frame; consumers MUST DROP if any differ. No separate `frame_id` field exists in the header.

**NOTE**
- Region-of-interest (ROI) offsets/boxes do not belong in this fixed header; carry ROI in `DataSourceMeta` attributes when needed.
- Interpretation: `dtype` = scalar element type; `major_order` = row/column-major; `strides` allow arbitrary layouts; 0 strides mean contiguous inferred from shape and dtype.
- `seq_commit` MUST reside entirely within a single cache line and be written with a single aligned store; assume 64-byte cache lines (common); if different, still place `seq_commit` in the first cache line and use an 8-byte aligned atomic store.
- `ndims` MUST be in the range `1..MAX_DIMS`; consumers MUST drop frames with `ndims=0` or `ndims > MAX_DIMS`.
- For v1.2, `payload_offset` MUST be 0; consumers MUST drop frames with non-zero `payload_offset`.
- Producers MUST zero-fill `dims[i]` and `strides[i]` for all `i >= ndims`; consumers MUST ignore indices `i >= ndims`.
- Strides: `0` means inferred contiguous; negative strides are **not supported in v1.2**; strides MUST describe a non-overlapping layout consistent with `major_order` (row-major implies increasing stride as dimension index decreases); reject headers whose strides would overlap or contradict `major_order`.
- Stride terms: `stride_bytes` (pool stride) is the byte distance between payload slots; `strides[]` (tensor strides) describe in-buffer layout within a single payload slot. These are independent.
- Stride diagram (informative):
```
payload slot 0 base  = payload_base + 0 * stride_bytes
payload slot 1 base  = payload_base + 1 * stride_bytes
...
payload slot N base  = payload_base + N * stride_bytes

within a slot:
element_offset = sum(dims_index[i] * strides[i])
element_addr   = slot_base + payload_offset + element_offset
```
- Progress: if `progress_unit != NONE`, `progress_stride_bytes` MUST be non-zero and equal to the true row/column stride implied by `strides` (or inferred contiguous). Consumers MUST drop frames where `progress_unit != NONE` and `progress_stride_bytes` is zero or inconsistent with the declared layout.
- Arrays alignment: `pad_align` ensures `dims` and `strides` are 4-byte aligned; consumers may rely on this for word accesses.
- Padding is reserved/opaque; producers MAY leave it zeroed or use it for implementation-specific data; consumers MUST ignore it and MUST NOT store process-specific pointers/addresses there.
- Frame identity: `logical_sequence` (derived from `seq_commit`), `FrameDescriptor.seq`, and `FrameProgress.seq` MUST match. Consumers SHOULD drop a frame if `seq_commit` and `FrameDescriptor.seq` disagree (stale slot reuse).
- Empty payloads: `values_len_bytes = 0` is valid. Producers MUST still commit the slot; consumers MUST NOT read payload bytes when `values_len_bytes = 0` (payload metadata may be ignored). `payload_slot` MUST still equal `header_index` (derived from `seq`) in v1.2.

### 8.3 Commit Encoding via `seq_commit` (Normative)

Use `seq_commit` as a seqlock derived from the logical sequence number:

```
logical_sequence = seq_commit >> 1
commit_state     = seq_commit & 1
```

- IN-PROGRESS: `(logical_sequence << 1) | 0`
- COMMITTED: `(logical_sequence << 1) | 1`

**Producer**
0. Compute `in_progress = (S << 1) | 0` and `committed = (S << 1) | 1` for logical sequence `S` (monotonic, producer-owned).
1. Store `seq_commit = in_progress` with **release** semantics.
2. Write payload bytes (DMA complete or memcpy complete).
3. Write all other header fields (full overwrite of the slot; do not rely on prior zeroing/truncate).
4. Ensure payload visibility (see coherency note below), then store `seq_commit = committed` with **release** semantics.

**Consumer**
1. Read `seq_commit` (acquire).
2. If `(seq_commit & 1) == 0` → skip/drop (not committed).
3. Read header + payload.
4. Re-read `seq_commit` (acquire).
5. Parse `headerBytes` as an embedded SBE message (expect `TensorHeader` in v1.2); drop if header length or templateId is invalid (length MUST equal the embedded message header + blockLength).
   The embedded message header MUST have the expected `schemaId` and `version`; otherwise drop.
6. Accept only if unchanged, `(seq_commit & 1) == 1`, and `(seq_commit >> 1)` equals the expected sequence (from `FrameDescriptor.seq`).

This prevents torn reads under overwrite.

**Coherency / DMA visibility**
- Producer MUST ensure payload bytes are visible to CPUs before writing the COMMITTED `seq_commit` (and before emitting a `FrameProgress` COMPLETE state). On non-coherent DMA paths, flush/invalidate as required by the platform/driver.
- Consumers MUST treat payload as valid only after the seqlock check on `seq_commit` succeeds.

---

## 9. Payload Pools

- Raw byte buffers with fixed stride.
- Slot offset:
  ```
  payload_slot_offset = superblock_size + payload_slot * stride_bytes
  ```
- Pools MAY be size-classed (e.g. 1 MiB, 16 MiB).
- Pool selection rule (v1): choose the smallest pool where `stride_bytes >= values_len_bytes`.
- If no pool fits (`values_len_bytes` larger than all `stride_bytes`), the producer MUST drop the frame (and MAY log/emit QoS) rather than blocking; future v2 may add dynamic pooling.

**Mapping rule (v1.2, simple):**
- All pools use the same `nslots` as the header ring.
- `payload_slot = header_index` (same index mapping).
- Header stores `pool_id` (size class).

(Alternate mapping with free lists can be added later as v2.)

---

## 10. Aeron + SBE Messages (Wire Protocol)

All messages below are SBE encoded and transported over Aeron.

### 10.1 Service Discovery and SHM Coordination

**Optional primitives and null values (normative)**
- All primitive fields marked `presence="optional"` use explicit null sentinels in the schema: `uint32 nullValue = 0xFFFFFFFF`, `uint64 nullValue = 0xFFFFFFFFFFFFFFFF`.
- Producers MUST encode “absent” optional primitives using the nullValue; consumers MUST interpret those null values as “not provided”.
- Variable-length `data` fields are optional by encoding an empty value (length = 0). Producers MUST use length 0 to indicate absence; consumers MUST treat length 0 as “not provided”.
- For sbe-tool compatibility, variable-length `data` fields MUST NOT be marked `presence="optional"` in the schema; absence is represented by length 0 only.
- SBE requires variable-length `data` fields to appear at the end of a message. Field IDs are assigned sequentially to fixed fields first, then sequentially to `data` fields.
- In driver-model deployments, `ShmPoolAnnounce` is emitted by the SHM Driver (authoritative) rather than the producer; see the Driver Model specification.

#### 10.1.1 ShmPoolAnnounce (producer → all)

Sent periodically (e.g. 1 Hz) and on change.

**Fields**
- `stream_id : u32`
- `producer_id : u32`
- `epoch : u64`
- `announce_timestamp_ns : u64` (producer timestamp in declared clock domain)
- `announce_clock_domain : enum ClockDomain` (MONOTONIC or REALTIME_SYNCED; v1.2 forbids unsynchronized realtime)
- `layout_version : u32`
- `header_nslots : u32`
- `header_slot_bytes : u16` (must be 256)
- repeating group `payload_pools`:
  - `pool_id : u16`
  - `region_uri : string`
  - `pool_nslots : u32`
  - `stride_bytes : u32`
- `header_region_uri : string`

**Rules**
- Consumers use this to open/mmap regions and validate superblocks.
- If `epoch` changes, consumers must remap and reset state.
- Consumers MUST treat `ShmPoolAnnounce` as soft-state and ignore stale announcements. At minimum, consumers MUST prefer the highest observed `epoch` per `stream_id` and MUST ignore any announce whose `announce_timestamp_ns` is older than a freshness window (RECOMMENDED: 3× the announce period) relative to local receipt time.
- Consumers MUST ignore announcements whose `announce_timestamp_ns` precedes the consumer's join time (to avoid Aeron log replay) **only when `announce_clock_domain=MONOTONIC`**. Define `join_time_ns` as the local monotonic time when the subscription image becomes available; compare directly only for MONOTONIC. For REALTIME_SYNCED, consumers MUST NOT apply the join-time drop rule and MUST rely on the freshness window relative to local receipt time instead.
- `announce_clock_domain=REALTIME_SYNCED` indicates that `announce_timestamp_ns` is disciplined to a shared clock (e.g., PTP/NTP within a documented error budget). Unsynchronized realtime is not permitted in v1.2.

#### 10.1.2 ConsumerHello (consumer → producer/supervisor)

Sent on startup and optionally periodically.

**Fields**
- `stream_id : u32`
- `consumer_id : u32`
- `supports_shm : bool`
- `supports_progress : bool` (can consume partial DMA progress hints)
- `mode : enum { STREAM=1, RATE_LIMITED=2 }` (RATE_LIMITED = consumer expects reduced-rate delivery via per-consumer stream or downstream rate limiter)
- `max_rate_hz : u32` (0 = unlimited; authoritative when mode=RATE_LIMITED; mainly for GUI)
- `expected_layout_version : u32`
- optional progress policy hints (producer may coarsen across consumers):
  - `progress_interval_us : u32` (minimum interval between `PROGRESS` messages; optional; if absent, producer defaults apply; **recommended default**: 250 µs)
  - `progress_bytes_delta : u32` (minimum byte delta to report; optional; if absent, producer defaults apply; **recommended default**: 65,536 bytes)
- `progress_major_delta_units : u32` (minimum major-axis delta to report; optional; **recommended default**: 0 when unknown)
- `descriptor_stream_id : u32` (preferred stream ID, or 0 to let producer choose)
- `control_stream_id : u32` (preferred stream ID, or 0 to let producer choose)
- `descriptor_channel : string` (optional; request per-consumer descriptor stream; empty string means “not requested”)
- `control_channel : string` (optional; request per-consumer control stream; empty string means “not requested”)

**Purpose**
- Advertise consumer capabilities.
- Enables management decisions (e.g., instruct remote consumers to use a bridge).
- Provides optional per-consumer progress throttling hints; producer aggregates hints (e.g., smallest intervals/deltas within producer safety floors) and retains final authority.
- Optionally request a per-consumer descriptor stream only when `descriptor_channel` is non-empty and `descriptor_stream_id` is non-zero. If either is missing, the request MUST be treated as absent; consumers MUST NOT send a non-empty channel with `descriptor_stream_id=0`, and producers MUST reject such requests.
- Optionally request a per-consumer control stream only when `control_channel` is non-empty and `control_stream_id` is non-zero. If either is missing, the request MUST be treated as absent; consumers MUST NOT send a non-empty channel with `control_stream_id=0`, and producers MUST reject such requests.
- Consumer IDs: recommended to be assigned by supervisor/authority per stream; if self-assigned, use randomized IDs and treat collisions (detected via `QosConsumer`) as a reason to reconnect with a new ID.

#### 10.1.3 ConsumerConfig (producer/supervisor → consumer) [optional]

**Fields**
- `stream_id : u32`
- `consumer_id : u32`
- `use_shm : bool`
- `mode : enum { STREAM, RATE_LIMITED }`
- `descriptor_stream_id : u32` (assigned per-consumer descriptor stream ID; 0 means not assigned)
- `control_stream_id : u32` (assigned per-consumer control stream ID; 0 means not assigned)
- `payload_fallback_uri : string` (optional; e.g., bridge channel/stream info)
  - URI SHOULD follow Aeron channel syntax when bridged over Aeron (e.g., `aeron:udp?...`) or a documented scheme such as `bridge://<id>` when using a custom bridge; undefined schemes MUST be treated as unsupported.
- `descriptor_channel : string` (optional; assigned per-consumer descriptor channel)
- `control_channel : string` (optional; assigned per-consumer control channel)

**Purpose**
- Central authority can force GUI into RATE_LIMITED.
- Can redirect non-local consumers to bridged payload.
- Can assign per-consumer descriptor streams when requested.
- Can assign per-consumer control streams when requested.

**Per-consumer stream request rules**
- Empty `descriptor_channel`/`control_channel` strings (length=0) MUST be treated as “not requested/assigned”; length=0 is the only valid absent encoding.
- If only one of channel/stream_id is provided, the request MUST be rejected (non-empty channel with stream_id=0, or non-zero stream_id with empty channel).
- If a producer declines a per-consumer stream request, it MUST return empty channel and stream ID = 0 in `ConsumerConfig`, and the consumer MUST remain on the shared stream.
- Per-consumer descriptor/control publications MUST be closed when the consumer is stale (no `QosConsumer` or `ConsumerHello` for 3–5× the configured hello/qos interval) or explicitly disconnected.

### 10.2 Data Availability

#### 10.2.1 FrameDescriptor (producer → all)

One per produced tensor/frame.

**Fields**
- `stream_id : u32`
- `epoch : u64`
- `seq : u64` (monotonic; MUST equal `logical_sequence = seq_commit >> 1` in the header)

Optional fields (may reduce SHM reads for filtering):
- `timestamp_ns : u64`
- `meta_version : u32`
- `trace_id : u64` (optional; 0 means absent; see TraceLink spec)

**Timestamp semantics (normative)**
- `SlotHeader.timestamp_ns` is source/capture time for the payload.
- `FrameDescriptor.timestamp_ns` is publish time: the producer-local timestamp taken when the descriptor is emitted (after `seq_commit` is committed).
- Producers SHOULD populate `SlotHeader.timestamp_ns` by default.
- Producers SHOULD leave `FrameDescriptor.timestamp_ns` null unless publish-time alignment/latency is required.
- If both are present, they MAY differ; consumers MUST NOT assume equality.

**Rules**
- Producers MUST publish `FrameDescriptor` only after the slot’s `seq_commit` is set to COMMITTED and payload visibility is ensured.
- Consumers MUST ignore descriptors whose `epoch` does not match mapped SHM regions.
- Consumers derive all payload location/shape/type from the SHM header slot.
- Consumers MUST compute `header_index = seq & (header_nslots - 1)` using the mapped header ring size.
- Consumer MUST drop if the header slot’s committed `logical_sequence` (derived from `seq_commit >> 1`) does not equal `FrameDescriptor.seq` for that `header_index` (stale reuse guard).
- Consumers MUST drop if `payload_slot` decoded from the header is out of range for the mapped payload pool.
- Consumers MUST drop frames where `values_len_bytes > stride_bytes`. (Future versions that permit non-zero `payload_offset` MUST additionally enforce `payload_offset + values_len_bytes <= stride_bytes`.)
- `trace_id = 0` means tracing is absent for the frame. `trace_id != 0` indicates
  TraceLink is enabled for that frame; producers SHOULD emit TraceLinkSet
  records per `docs/SHM_TraceLink_Spec_v1.0.md`. Consumers MAY ignore `trace_id`.

#### 10.2.2 FrameProgress (producer → interested consumers, optional)

Optional partial-availability hints during DMA.

**Fields**
- `stream_id : u32`
- `epoch : u64`
- `seq : u64` (MUST equal `FrameDescriptor.seq` and the header logical sequence)
- `payload_bytes_filled : u64`
- `state : enum { UNKNOWN=0, STARTED=1, PROGRESS=2, COMPLETE=3 }`

**Rules**
- Producer emits `STARTED` when acquisition for the slot begins.
- Producer emits `PROGRESS` throttled (e.g., every N rows or X µs) with updated `payload_bytes_filled`.
- Producer emits `COMPLETE` or simply the usual `FrameDescriptor` when DMA finishes; `seq_commit` semantics remain unchanged (LSB=1 = committed).
- Consumers that do not opt in ignore `FrameProgress` and rely on `FrameDescriptor`.
- Consumers that opt in and need slot access MUST compute `header_index = seq & (header_nslots - 1)` using the mapped header ring size.
- Consumers that opt in must read only the prefix `[0:payload_bytes_filled)` and may reread `payload_bytes_filled` to confirm. Consumers MUST validate `progress_unit`/`progress_stride_bytes` before treating any prefix as layout-safe; if `progress_unit != NONE` and `progress_stride_bytes` is zero or inconsistent with the declared strides, the frame MUST be dropped.
- `payload_bytes_filled` MUST be `<= values_len_bytes`.
- `payload_bytes_filled` MUST be monotonic non-decreasing within a frame; consumers MUST drop if it regresses.
- `FrameProgress.state=COMPLETE` does **not** guarantee payload visibility; consumers MUST still validate `seq_commit` before treating data as committed.
- FrameDescriptor remains the canonical “frame available” signal; consumers MUST NOT treat `FrameProgress` (including COMPLETE) as a substitute, and producers MAY omit `FrameProgress` entirely.
- Progress payload prefix safety:
  - If strides are inferred contiguous **and** `payload_offset=0`, then bytes in `[0:payload_bytes_filled)` are safe to read (subject to `seq_commit`).
  - Otherwise, treat `payload_bytes_filled` as informational only unless the embedded `TensorHeader` declares `progress_unit=ROWS` or `progress_unit=COLUMNS` and provides `progress_stride_bytes` for major-aligned interpretation.

**State machine**
- Per frame: STARTED → PROGRESS (0..N) → COMPLETE. No regression within a frame. New frame uses a new `seq` (and derived `header_index`).

**Throttling guidance**
- Emit `STARTED` once, then at most one `PROGRESS` per interval (e.g., 200–500 µs) and only when `payload_bytes_filled` advances by a threshold (e.g., ≥64 KiB or ≥N rows).
- Cap burstiness with a per-stream max rate (e.g., 1–2 kHz) or token bucket; drop intermediate updates but always send the latest state when the interval elapses.
- Use monotone triggers: combine (a) time-based tick, (b) byte/row delta threshold, (c) final `COMPLETE`. This avoids floods of tiny updates and long silences.
- Only publish when at least one subscriber has `supports_progress=true`; otherwise skip to reduce control-plane load.
- When consumers provide policy hints (`progress_interval_us`, `progress_bytes_delta`, `progress_major_delta_units`), producer applies the most aggressive common policy that stays within its safety floor: take the **smallest** interval and **smallest** deltas offered, but not below producer minima.
- If no hints are provided, fall back to producer defaults (recommend 250 µs interval, 64 KiB delta, rows hint unset).

### 10.3 Per-Data-Source Metadata

#### 10.3.1 DataSourceAnnounce (producer → all)

Periodic beacon for discovery/inventory.

**Fields**
- `stream_id : u32`
- `producer_id : u32`
- `epoch : u64`
- `meta_version : u32` (metadata correlation/version; increments when metadata changes)
- optional: human-readable name/id (bounded varAscii)
- optional: nominal dtype/shape summary
- keep this lean; richer metadata lives in `DataSourceMeta`

#### 10.3.2 DataSourceMeta (producer → all)

Sent on change and periodically (or on request). Carries structured key/format/value items so decoders can pick specific fields without decoding arbitrary blobs.

**Fields**
- `stream_id : u32`
- `meta_version : u32`
- `timestamp_ns : u64`
- repeating group `attributes`:
  - `key : varAscii` (e.g., "camera_serial", "intrinsics")
  - `format : varAscii` (e.g., "text/plain", "application/json", "application/msgpack", "sbe:<schemaId>.<templateId>")
  - `value : varData` (bytes per `format`)

**Rules**
- Each `SlotHeader.meta_version` and/or `FrameDescriptor.meta_version` references the applicable metadata version.
- Use `meta_version` bumps to add/remove `attributes`. Consumers MAY ignore unknown `key` values.

#### 10.3.3 Meta blobs (optional, if large metadata is needed)

If you must distribute large calibration tables (flat fields, masks, etc.), define chunked SBE messages:

- `MetaBlobAnnounce(stream_id, meta_version, blob_type, total_len, checksum)`
- `MetaBlobChunk(stream_id, meta_version, offset, bytes[CHUNK_MAX])`
- `MetaBlobComplete(stream_id, meta_version, checksum)`

Rules (experimental): offsets MUST be monotonically increasing, non-overlapping, and cover `[0, total_len)`; consumers discard on gap/overlap or checksum mismatch; retransmission/repair is out of scope in v1.2.

### 10.4 QoS and Health

#### 10.4.1 QosConsumer (consumer → supervisor/producer)

Sent periodically (e.g. 1 Hz).

**Fields**
- `stream_id : u32`
- `consumer_id : u32`
- `epoch : u64`
- `last_seq_seen : u64`
- `drops_gap : u64` (seq gaps)
- `drops_late : u64` (failed sentinel validation / overwritten mid-read)
- `mode : enum`
- optional latency stats (p50/p99)

#### 10.4.2 QosProducer (producer → all)

**Fields**
- `stream_id : u32`
- `producer_id : u32`
- `epoch : u64`
- `current_seq : u64`
- optional per-pool watermark/health

### 10.5 Supervisor / Unified Management (recommended)

A “supervisor/console” service subscribes to:
- `ShmPoolAnnounce`
- `DataSourceAnnounce` / `DataSourceMeta`
- `QosConsumer` / `QosProducer`
- service health announcements (optional)

It may publish:
- `ConsumerConfig`
- service commands (optional extension): `ServiceCommand(target_service_id, cmd_type, params)`

---

## 11. Consumer Modes

- **STREAM**: process all descriptors; drop if late.
- **RATE_LIMITED**: consumer requests reduced-rate delivery (e.g., per-consumer descriptor/control stream or downstream rate limiter). Producer/supervisor MAY decline; if declined, consumer remains on the shared stream and MAY drop locally. `max_rate_hz` in `ConsumerHello` is authoritative when `mode=RATE_LIMITED`.

**Implementation note (non-normative):** when enforcing `RATE_LIMITED`, consumers SHOULD base rate gating on `FrameDescriptor.timestampNs` when present (non-null). If it is null, consumers MAY fall back to the `SlotHeader.timestampNs` (from SHM) to avoid per-poll clock aliasing. If both are unavailable, use a local monotonic clock.

---

## 12. Bridge Service (Optional)

- Subscribes to descriptors + SHM.
- Republishes payload over Aeron UDP (or records).
- Provides fallback path for remote or non-SHM consumers.
- Not required for v1; specification only reserves the option for future deployment.

---

## 13. Implementation Notes (Informative; Julia-focused example)

- Specification is language-neutral and should be implementable in C/Java/Go/Rust/Julia (or any platform with Aeron + SBE support).
- SHM layout must remain language-agnostic: no runtime-specific pointers/vtables/handles; only POD data per the SBE schema.
- Use `Mmap.mmap` on `region_uri` paths.
- Decode SBE directly from mapped memory (no allocations in hot paths).
- Implement `seq_commit` loads/stores with acquire/release semantics.
- Avoid storing language-managed pointers/handles (GC/JVM/Rust lifetimes/Julia-managed) in SHM; only POD per schema.
- Use Aeron transport via Aeron.jl and SBE encoding/decoding via SBE.jl.
- Use Agent.jl for agent-style services (supervisor/console, bridge, rate limiter) to mirror Aeron/Agrona agents with structured lifecycle.

---

## 14. Open Parameters (fill these in)

- Pool size classes and `stride_bytes` (e.g., 1 MiB, 16 MiB)
- `header_nslots` and per-pool `nslots` (power-of-two)
- `layout_version` initial value
- Aeron channel/stream ID mapping conventions (examples in 15.11 are non-normative; deployments SHOULD set explicit defaults)

---

End of specification.

---

## 15. Additional Requirements and Guidance (v1.2)

### 15.1 Validation and Compatibility
- Consumers MUST validate that `layout_version`, `nslots`, `slot_bytes`, `stride_bytes`, `region_type`, and `pool_id` in `ShmRegionSuperblock` match the most recent `ShmPoolAnnounce`; mismatches MUST trigger a remap or fallback.
- Consumers MUST validate `magic` and `epoch` on every `ShmPoolAnnounce`; `pid` is informational and cannot alone detect restarts or multi-producer contention.
- Consumers MUST validate `activity_timestamp_ns` freshness: announcements older than the freshness window (RECOMMENDED: 3× announce period) MUST be ignored.
- Host endianness: implementation is little-endian only; big-endian hosts MUST reject or byte-swap consistently (out of scope in v1.2).

### 15.2 Epoch Lifecycle
- Increment `epoch` on any producer restart or layout change (superblock size change, `nslots`, pool size classes, or slot size change).
- On `epoch` change, producer SHOULD reset `seq` to 0; consumers MUST drop stale frames, unmap, and remap regions.
- Consumers treat any regression of `epoch` or `seq` as a remap requirement.

### 15.3 Commit Protocol Edge Cases
- If `seq_commit` decreases for the same slot (e.g., lower `logical_sequence`), consumers MUST treat it as stale and skip.
- If `seq_commit` changes between the two reads in the consumer flow, count as `drops_late` and skip the frame.
- Specify atomics per language: C/C++ `std::atomic<uint64_t>` with release/acquire; Java `VarHandle` setRelease/getAcquire; Julia `Base.Threads.atomic_store!`/`atomic_load!` with :release/:acquire.

### 15.4 Overwrite and Drop Accounting
- Producers MAY overwrite any slot following `header_index = seq & (nslots - 1)` with no waiting.
- Consumers SHOULD treat gaps in `seq` as `drops_gap` and `seq_commit` instability as `drops_late`.
- Documented policy: no producer backpressure in v1.2; supervisor MAY act on QoS to throttle externally.
- Optional policy: implementations MAY configure `max_outstanding_seq_gap` per consumer; if exceeded, consumer SHOULD resync (e.g., drop to latest) and report in QoS.
- Recommended `max_outstanding_seq_gap` default: 256 frames; deployments MAY tune based on buffer depth and latency tolerance.

### 15.5 Pool Mapping Rules (v1.2)
- All payload pools MUST use the same `nslots` as the header ring; differing `nslots` are invalid in v1.2.
- `payload_slot = header_index` is mandatory in v1.2; free-list or decoupled mappings are deferred to v2.

### 15.6 Sizing Guidance
- Recommended minimum `header_nslots`: `ceil(producer_rate_hz * worst_case_consumer_latency_s * safety_factor)`; start with safety factor 2–4.
- Example pool size classes: 1 MiB, 4 MiB, 16 MiB. Choose smallest `stride_bytes >= values_len_bytes`.

### 15.7 Timebase
- `timestamp_ns` SHOULD be monotonic (CLOCK_MONOTONIC) for latency calculations. If cross-host alignment is required and PTP is available, `CLOCK_REALTIME` is acceptable—document the source clock and drift budget. When possible, include both a monotonic timestamp (for latency) and a realtime/epoch timestamp (for cross-host alignment).
- Latency = `now_monotonic - timestamp_ns_monotonic`. Cross-host correlation requires externally synchronized clocks.
- When both `SlotHeader.timestamp_ns` and `FrameDescriptor.timestamp_ns` are present, each MUST be monotonic in the declared clock domain; `FrameDescriptor.timestamp_ns` is the publish-time timestamp defined in 10.2.1.

### 15.7a NUMA Policy (deployment-driven)
- Follow Aeron practice: pin critical agents (producer, supervisor) to cores on the NUMA node that owns the NIC/storage for SHM paths; allocate SHM (tmpfs/hugetlb) bound to that node using OS tooling (e.g., `numactl`).
- Do not implement NUMA policy in-protocol; placement is an ops/deployment concern. Co-locate producers and their SHM pools; prefer consumers on the same node when low latency matters.

### 15.8 Enum and Type Registry
- Define and version registries for `dtype`, `major_order`, and set `MAX_DIMS` (v1.2 fixed at 8). Changing `MAX_DIMS` requires a new `layout_version` and schema rebuild. Unknown enum values MUST cause rejection of the frame (or fallback) rather than silent misinterpretation.
- Document evolution: add new enum values with bumped `layout_version`; keep wire encoding stable; avoid reuse of retired values.
- Normative numeric values (v1.2): `Dtype.UNKNOWN=0, UINT8=1, INT8=2, UINT16=3, INT16=4, UINT32=5, INT32=6, UINT64=7, INT64=8, FLOAT32=9, FLOAT64=10, BOOLEAN=11, BYTES=13, BIT=14`; `MajorOrder.UNKNOWN=0, ROW=1, COLUMN=2`. These values MUST NOT change within v1.x.

### 15.9 Metadata Blobs
- Define `CHUNK_MAX`; checksum is OPTIONAL given Aeron reliability. Offsets MUST be monotonically increasing and non-overlapping.
- If integrity beyond transport is desired, use CRC32C over the full blob; otherwise omit checksum to reduce overhead.
- Suggested constant: `CHUNK_MAX = 64 KiB`.

### 15.10 Security and Permissions (Security Considerations)
- SHM files SHOULD be created with restrictive modes (e.g., 660) and owned by the producing service user/group; set appropriate `umask`.
- On systems with MAC (SELinux/AppArmor), label/allow rules SHOULD be set to limit writers to trusted services.

### 15.11 Stream Mapping Guidance
- Recommend fixed stream ID ranges per channel: descriptor, control, QoS, metadata. Template IDs alone MAY be used for multiplexing, but separate streams improve observability.
- Example (adjust per deployment):
  - Control: stream IDs 1000–1003 (announce/hello/config/response) on IPC.
  - Descriptor: 1100 on IPC.
  - QoS: 1200 on IPC.
  - Metadata: 1300 on IPC.
  - Bridge/fallback (if used): UDP with separate stream IDs per direction.
- Cadence guidance (SHOULD, configurable per deployment): `ShmPoolAnnounce` 1 Hz; `DataSourceAnnounce` 1 Hz; `QosProducer`/`QosConsumer` 1 Hz; `activity_timestamp` refresh at same cadence.
- Minimal deployment can multiplex all control/announce/QoS/metadata on a single IPC stream to avoid extra term buffers; the above ranges are optional for clarity/observability.

### 15.12 Consumer State Machine (suggested)
- UNMAPPED: waiting for `ShmPoolAnnounce`.
- MAPPED: SHM mapped, `epoch` matches.
- FALLBACK: SHM unsupported/unavailable → use `payload_fallback_uri` if provided.

### 15.13 Test and Validation Checklist
- mmap and superblock validation (magic, version, sizes, endianness) fails closed.
- Commit-word stability under overwrite at load: no torn reads.
- Epoch rollover/remap behavior: consumers drop and remap.
- Unlink/remap recovery: producer recreation detected via `epoch` and `magic`.
- QoS counters: `drops_gap` vs `drops_late` reported correctly.

### 15.14 Deployment & Liveness (Aeron-inspired)
- Superblock/mark fields: include `magic`, `layout_version`, `epoch`, `pid`, `start_timestamp`, `activity_timestamp`. Producers update `activity_timestamp` periodically; supervisors treat stale values as dead and require remap.
- Single-writer rule: producer is sole writer of header/pool regions; if `pid` changes or concurrent writers are detected, consumers MUST unmap and wait for a new `epoch`.
- Creation/cleanup: on producer start, create/truncate, write superblock, fsync, then publish `ShmPoolAnnounce`. On clean shutdown, optionally unlink or set a clean-close flag. Crashes are inferred by stale `activity_timestamp`.
- Permissions: create SHM files with restrictive modes (e.g., umask 007, mode 660) and trusted group ownership; align with Aeron’s driver directory practices.
- Liveness timeouts: supervisors set a timeout (e.g., 3–5× announce period) on `activity_timestamp`; on expiry, unmap and await new `epoch`.
- Recreation detection: any change to `magic`, `layout_version`, or `epoch` requires remap; treat version mismatch as incompatible.
- Optional error log: add a small fixed-size error ring (timestamp, pid, code) in SHM for postmortem without stdout scraping.

### 15.15 Aeron Terminology Mapping
- Producer service ≈ Aeron driver for this data source (owns SHM, publishes descriptors/QoS/meta over Aeron).
- Supervisor/consumer coordination layer ≈ client conductor (tracks announces, epochs, remaps, issues configs, observes QoS).
- Bridge/tap/rate limiter agents follow the Agent.jl pattern analogous to Agrona agents used by Aeron.

### 15.16 Reuse Aeron Primitives (do not reinvent)
- Keep all control/descriptor/QoS/metadata over Aeron IPC streams; avoid new SHM counter regions—use Aeron-style QoS messages and existing counter semantics for drops/health.
- If you need counters/metrics, prefer Aeron Counters (driver/client) instead of adding custom SHM counters; expose QoS via Aeron messages.
- Implement producer, supervisor, bridge/rate limiter as Aeron-style agents (Agent.jl mirrors Agrona agents) with single-writer ownership of SHM, matching Aeron driver patterns.
- Use the SBE toolchain for every control and descriptor message; multiplex by template ID when collapsing streams, or separate streams for observability—same choice pattern as Aeron archive/control.
- Liveness and lifecycle mirror Aeron driver: mark/superblock with pid/start/activity timestamps, periodic announces, and timeouts to declare death/remap.
- Optional bridge can mirror Aeron archive/tap behavior: subscribe to descriptor + SHM, then republish over UDP or record; keep it optional so the core remains SHM + Aeron IPC.
- Operationally mirror Aeron: pin agents, place SHM on the local NUMA node, and apply restrictive permissions to SHM paths and driver directories.

### 15.16a File-Backed SHM Regions (Informative)

Implementations may use regular filesystem-backed `mmap` regions instead of
`/dev/shm`, but doing so shifts availability into the page cache and can
introduce unpredictable latency from page faults or eviction. File-backed
regions also do not guarantee durability without explicit `msync`/`fsync`.
Deployments that choose file-backed mappings should treat them as a performance
trade-off (often acceptable for offline or low-rate pipelines) rather than a
replacement for the recorder.

### 15.17 ControlResponse Error Codes (usage guidance)
- `Ok`: request succeeded.
- `Unsupported`: capability not implemented (e.g., progress hints on a producer that lacks support).
- `InvalidParams`: malformed or incompatible request (bad stream_id/layout_version/mismatched nslots).
- `Rejected`: policy-based refusal (e.g., unauthorized consumer_id, oversubscription).
- `InternalError`: transient or unexpected server failure; requester MAY retry after backoff.

### 15.18 Normative Algorithms (minimal, per role)
- **Producer publish**
  1. Compute `header_index = seq & (nslots - 1)`.
  2. Compute `in_progress = (seq << 1) | 0`, `committed = (seq << 1) | 1`.
  3. Store `seq_commit = in_progress` with release.
  4. Fill payload bytes (DMA or memcpy).
  5. Fill the full header (shape/strides, pool/slot, metadata refs).
  6. Ensure payload visibility to CPUs.
  7. Store `seq_commit = committed` with release.
  8. Emit `FrameDescriptor` (and optional `FrameProgress COMPLETE`).

- **Consumer receive**
  1. On `FrameDescriptor`, validate `epoch`; derive/map `header_index`.
  2. Read `seq_commit` (acquire).
  3. If LSB is 0 → drop/skip.
  4. Read SlotHeader + `headerBytes` + payload.
5. Parse `headerBytes` as an embedded SBE message (expect `TensorHeader` in v1.2); drop if header length or templateId is invalid (length MUST equal the embedded message header + blockLength).
  6. Re-read `seq_commit` (acquire).
  7. Accept only if unchanged, LSB is 1, and `(seq_commit >> 1) == seq`; otherwise drop and count (`drops_gap` or `drops_late`).

- **Epoch remap**
  1. On `epoch` change, superblock/layout mismatch, or magic/version mismatch: unmap all regions for the stream.
  2. Reset local state.
  3. Remap on next `ShmPoolAnnounce` and resume with `seq` tracking reset.

### 15.20 Compatibility Matrix (layout/wire)
| Change | Compatible? |
| --- | --- |
| Add enum value (dtype/majorOrder) | Yes (bump `layout_version`; older readers may reject unknown values) |
| Change header slot size (256 → N) | No (requires new `layout_version` and remap) |
| Decouple `payload_slot` from `header_index` (free list) | No (v2 feature; incompatible with v1.2 readers) |
| Change `MAX_DIMS` length | No (requires new `layout_version` and schema rebuild) |

### 15.21 Protocol State Machines and Sequences (Normative)

This section defines protocol state machines for slot lifecycle and mapping/epoch handling. Any sequence diagrams or examples elsewhere are informative and do not introduce additional requirements.

#### Terminology

- **Slot**: header slot and its associated payload slot (v1.2: same index).
- **Seq commit**: per-slot commit indicator for logical completeness (`seq_commit`).
- **DROP**: frame is discarded without error or retry.

#### Slot Lifecycle State Machine (per slot index `i`)

States
- **S0: EMPTY_OR_STALE** — slot does not contain a valid committed frame (never written, overwritten, or prior epoch).
- **S1: WRITING** — producer claimed slot and is populating header/payload; `seq_commit` indicates in-progress.
- **S2: COMMITTED(seq)** — slot has a logically complete frame with sequence `seq`; `seq_commit` has LSB=1 and is stable.

Producer transitions
| From | Event | Action | To |
| ---- | ---- | ---- | ---- |
| S0, S2 | Begin write to slot `i` | Set `seq_commit` to in-progress (LSB=0) | S1 |
| S1 | Payload DMA complete and header finalized | Set `seq_commit` to committed (LSB=1) | S2 |

Consumer validation rules (given `FrameDescriptor(seq)`; let `i = seq & (nslots - 1)`)
1. **Slot selection**: compute `i` from `seq` and the mapped header ring size.
2. **Commit stability**: MUST observe a stable `seq_commit` with LSB=1 before accepting; if WRITING/unstable, DROP.
3. **Embedded header validation**: `headerBytes` MUST decode to a supported embedded header (v1.2: `TensorHeader` with the expected schemaId/version/templateId and length per §8.3); otherwise DROP.
4. **Frame identity**: `(seq_commit >> 1)` MUST equal `FrameDescriptor.seq` (same rule as §10.2.1); if not, DROP.
5. **No waiting**: MUST NOT block/spin waiting for commit; on any failure, DROP and continue.

Dropped frames are treated as overwritten or in-flight; they are not errors.

#### Mapping and Epoch State Machine (per process per mapped region)

States
- **M0: UNMAPPED** — no regions are mapped.
- **M1: MAPPED(epoch = E)** — regions mapped/validated for epoch `E`.

Mapping transitions
| From | Event | Condition | Action | To |
| ---- | ---- | ---- | ---- | ---- |
| M0 | Receive `ShmPoolAnnounce(epoch = E, paths…)` | Paths valid, superblock valid | Map regions; verify superblock matches announce; store epoch `E` | M1 |
| M1 | Receive `ShmPoolAnnounce(epoch = E2, …)` | `E2 == E` | Optionally refresh metadata | M1 |
| M1 | Receive `ShmPoolAnnounce(epoch = E2, …)` | `E2 ≠ E` | DROP in-flight frames; unmap regions | M0 |
| M1 | Slot validation indicates remap | — | DROP in-flight frames; unmap regions | M0 |

Epoch handling rules
- A consumer MUST treat any epoch mismatch as a hard boundary. All in-flight frames MUST be DROPPED.
- After detecting an epoch mismatch, the consumer MUST NOT accept subsequent frames until the regions have been remapped for the new epoch.
- While in M0: UNMAPPED, frames referencing unmapped regions MUST be DROPPED.

### 15.21a Filesystem Layout and Path Containment

This section defines a recommended filesystem layout for shared-memory regions
and a mandatory path-containment validation rule for consumers. It complements
the SHM backend validation rules in §15.22 and does not modify URI syntax or
semantics.

#### 15.21a.1 Overview (Informative)

Shared-memory regions are announced using explicit absolute filesystem paths via
control-plane messages. A canonical directory layout is defined in this
specification to improve interoperability between independent implementations,
simplify operational management, and reduce the risk of accidental or malicious
misuse on multi-user systems.

Consumers MUST NOT rely on directory scanning or filename derivation for
correctness. All required paths are conveyed explicitly via protocol messages.

#### 15.21a.2 Shared Memory Base Directory (Informative)

Implementations SHOULD expose a configuration parameter:

```
shm_base_dir
```

- Type: absolute filesystem path
- Meaning: root directory under which shared-memory regions are created and/or
  accepted by the implementation

Implementations MAY support multiple base directories (e.g.,
`allowed_base_dirs`) to enable interoperability with multiple producers or
legacy layouts.

Default values are platform- and deployment-specific and are an
implementation concern.

#### 15.21a.3 Canonical Directory Layout (Normative)

When creating shared-memory regions under `shm_base_dir`, producers MUST
organize backing files using the following canonical layout:

```
<shm_base_dir>/tensorpool-${USER}/<namespace>/<stream_id>/<epoch>/
    header.ring
    <pool_id>.pool
```

Where:

- `tensorpool-${USER}`  
  A per-user namespace directory (e.g., `tensorpool-alice`). `${USER}` MUST be
  resolved to the effective user name or an equivalent deployment-specific
  identifier. Implementations MUST sanitize this value to avoid path traversal.

- `<namespace>`  
  A stable application namespace (e.g., `default`, `lab1`, `sim`). The namespace
  MUST be configured per deployment.

- `<stream_id>`  
  The stream identifier associated with the SHM pool.

- `<epoch>`  
  The epoch value announced in `ShmPoolAnnounce`. Epoch-scoped directories
  enable atomic remapping and simplify cleanup.

- `header.ring`  
  Backing file for the header ring region.

- `<pool_id>.pool`  
  Backing files for payload pools, where `<pool_id>` matches the announced pool
  identifier.

Filenames and extensions are not protocol-significant but MUST remain stable
within an implementation for operational consistency.

#### 15.21a.4 Path Announcement Rule (Normative)

Regardless of filesystem layout:

- Producers MUST announce explicit absolute paths for all shared-memory regions.
- Consumers MUST NOT infer, derive, or synthesize filesystem paths.
- Consumers MUST NOT scan directories to discover shared-memory regions.

All required paths are conveyed exclusively via protocol messages.

#### 15.21a.5 Consumer Path Containment Validation (Normative)

Before mapping any announced shared-memory region, a consumer implementation
MUST:

1. Verify that the announced path is an absolute filesystem path.
2. Resolve the announced path to its canonical form using a platform-equivalent
  mechanism (e.g., `realpath()` on POSIX, full-path normalization on Windows).
3. Resolve each configured `allowed_base_dir` to its canonical form once (at
  startup/config-load) and use only those canonical paths for containment
  checks.
4. Verify that the canonical announced path is fully contained within one of
  the canonical `allowed_base_dirs`.
5. Perform a filesystem metadata check on the canonical announced path using
  platform-equivalent APIs and reject unless it is a regular file
  (hugetlbfs-backed regular files are acceptable); block/char devices, FIFOs,
  and sockets MUST be rejected.
6. To avoid TOCTOU/symlink swaps, consumers MUST open the file with
  no-follow/symlink-safe flags where available and MUST re-validate the opened
  file descriptor/handle (e.g., fstat on the opened handle) before mapping. If
  the platform cannot provide a safe no-follow open **and** a reliable opened-
  handle identity check (e.g., inode+device or file ID), the consumer MUST
  reject the mapping (fail closed).

If any of these checks fail, the consumer MUST reject the region and MUST NOT
map it.

This requirement applies even in controlled or trusted deployment environments.

#### 15.21a.6 Permissions and Ownership (Informative)

Implementations SHOULD ensure that directory and file permissions prevent
unintended cross-user access.

Typical recommendations include:

- Private usage:
  - Directories: `0700`
  - Files: `0600`
- Shared-group usage:
  - Directories: `2770` (setgid)
  - Files: `0660`

Exact permission models are platform- and deployment-specific.

#### 15.21a.7 Cleanup and Epoch Handling (Informative)

Using epoch-scoped directories allows:

- Immediate abandonment of stale mappings on epoch change
- Deferred or lazy cleanup of obsolete epochs
- Robust recovery after producer crashes

Implementations MAY remove epoch directories eagerly on clean shutdown or lazily
during startup or supervision. A Supervisor or Driver SHOULD periodically scan
`shm_base_dir` (or configured `allowed_base_dirs`) and unlink epoch directories
whose `activity_timestamp_ns` is stale and whose PIDs are no longer active on
the OS.

### 15.22 SHM Backend Validation (v1.2)

**Region URI scheme (normative)**

Supported scheme (only):

```
shm:file?path=<absolute_path>[|require_hugepages=true]
```

- Separator: parameters use Aeron-style `|` between entries (not `&`).
- `path` (required): absolute filesystem path to the backing file (POSIX or Windows). Examples: `/dev/shm/<name>` (tmpfs), `/dev/hugepages/<name>` (hugetlbfs), or `C:\\aeron\\<name>` on Windows. Absolute POSIX paths use a leading `/`. Non-path platform identifiers (e.g., Windows named shared memory) are out of scope for v1.2; deployments that support them MUST define equivalent containment/allowlist rules.
- `require_hugepages` (optional, default false): if true, the region MUST be backed by hugepages; mappings that do not satisfy this requirement MUST be rejected. On platforms without a reliable hugepage verification mechanism (e.g., Windows), `require_hugepages=true` is unsupported and MUST be rejected.
- v1.2 supports only `shm:file`; other schemes or additional parameters are unsupported and MUST be rejected.

ABNF (single scheme):

```
shm-uri  = "shm:file?path=" abs-path ["|" "require_hugepages=" bool]
abs-path = 1*( VCHAR except "?" and "|" and SP )
bool     = "true" / "false"
```

Only `path` and `require_hugepages` are defined; unknown parameters MUST be rejected.

**Validation rules (normative)**

- Consumers MUST reject any `region_uri` with an unknown scheme.
- For `shm:file`, if `require_hugepages=true`, consumers MUST verify that the mapped region is hugepage-backed. On platforms without a reliable verification mechanism (e.g., Windows), `require_hugepages=true` is unsupported and MUST cause the region to be rejected (no silent downgrade).
- `stride_bytes` is explicit and MUST NOT be inferred from page size. It MUST be a power-of-two multiple of 64 bytes.
- Page size or hugepage alignment MAY be chosen by deployments for performance
  reasons, but it is not a validation requirement in v1.2.
- For `shm:file`, parameters are separated by `|`; unknown parameters MUST be rejected.
- Regions that violate these requirements MUST be rejected. On rejection, consumers MAY fall back to a configured `payload_fallback_uri`; otherwise they MUST fail the data source with a clear diagnostic. Rejected regions MUST NOT be partially consumed.

---

## 16. Control-Plane SBE Schema (Draft)

Reference schema patterned after Aeron archive control style; adjust IDs and fields as needed. Keep `schemaId` and `version` aligned with `layout_version`/doc version.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                   package="shm.tensorpool.control"
                   id="900"
                   version="1"
                   semanticVersion="1.1"
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

    <composite name="varAsciiEncoding">
      <type name="length"  primitiveType="uint32" maxValue="1073741824"/>
      <type name="varData" primitiveType="uint8" length="0" characterEncoding="US-ASCII"/>
    </composite>

    <composite name="varDataEncoding">
      <type name="length"  primitiveType="uint32" maxValue="1073741824"/>
      <type name="varData" primitiveType="uint8" length="0"/>
    </composite>

    <!-- Constant used for array sizing in generated code -->
    <type name="MaxDimsConst" primitiveType="uint8" presence="constant">8</type>

    <!-- Fixed-length arrays for TensorHeader (length matches MaxDimsConst) -->
    <type name="DimsArray"     primitiveType="int32" length="8"/>
    <type name="StridesArray"  primitiveType="int32" length="8"/>

    <enum name="Bool" encodingType="uint8">
      <validValue name="FALSE">0</validValue>
      <validValue name="TRUE">1</validValue>
    </enum>

    <enum name="Mode" encodingType="uint8">
      <validValue name="STREAM">1</validValue>
      <validValue name="RATE_LIMITED">2</validValue>
    </enum>

    <enum name="FrameProgressState" encodingType="uint8">
      <validValue name="UNKNOWN">0</validValue>
      <validValue name="STARTED">1</validValue>
      <validValue name="PROGRESS">2</validValue>
      <validValue name="COMPLETE">3</validValue>
    </enum>

    <enum name="ProgressUnit" encodingType="uint8">
      <validValue name="NONE">0</validValue>
      <validValue name="ROWS">1</validValue>
      <validValue name="COLUMNS">2</validValue>
    </enum>

    <enum name="ResponseCode" encodingType="int32">
      <validValue name="OK">0</validValue>
      <validValue name="UNSUPPORTED">1</validValue>
      <validValue name="INVALID_PARAMS">2</validValue>
      <validValue name="REJECTED">3</validValue>
      <validValue name="INTERNAL_ERROR">4</validValue>
    </enum>

    <enum name="ClockDomain" encodingType="uint8">
      <validValue name="MONOTONIC">1</validValue>
      <validValue name="REALTIME_SYNCED">2</validValue>
    </enum>

    <enum name="RegionType" encodingType="int16">
      <validValue name="HEADER_RING">1</validValue>
      <validValue name="PAYLOAD_POOL">2</validValue>
    </enum>

    <enum name="Dtype" encodingType="int16">
      <validValue name="UNKNOWN">0</validValue>
      <validValue name="UINT8">1</validValue>
      <validValue name="INT8">2</validValue>
      <validValue name="UINT16">3</validValue>
      <validValue name="INT16">4</validValue>
      <validValue name="UINT32">5</validValue>
      <validValue name="INT32">6</validValue>
      <validValue name="UINT64">7</validValue>
      <validValue name="INT64">8</validValue>
      <validValue name="FLOAT32">9</validValue>
      <validValue name="FLOAT64">10</validValue>
      <validValue name="BOOLEAN">11</validValue>
      <validValue name="BYTES">13</validValue>
      <validValue name="BIT">14</validValue>
    </enum>

    <enum name="MajorOrder" encodingType="int16">
      <validValue name="UNKNOWN">0</validValue>
      <validValue name="ROW">1</validValue>
      <validValue name="COLUMN">2</validValue>
    </enum>

    <type name="epoch_t"   primitiveType="uint64"/>
    <type name="seq_t"     primitiveType="uint64"/>
    <type name="version_t" primitiveType="uint32"/>
  </types>

  <sbe:message name="ShmPoolAnnounce" id="1">
    <field name="streamId"        id="1" type="uint32"/>
    <field name="producerId"      id="2" type="uint32"/>
    <field name="epoch"           id="3" type="epoch_t"/>
    <field name="announceTimestampNs" id="4" type="uint64"/>
    <field name="announceClockDomain" id="5" type="ClockDomain"/>
    <field name="layoutVersion"   id="6" type="version_t"/>
    <field name="headerNslots"    id="7" type="uint32"/>
    <field name="headerSlotBytes" id="8" type="uint16"/>
    <group name="payloadPools"    id="9" dimensionType="groupSizeEncoding">
      <field name="poolId"      id="1" type="uint16"/>
      <field name="poolNslots"  id="2" type="uint32"/>
      <field name="strideBytes" id="3" type="uint32"/>
      <data  name="regionUri"   id="4" type="varAsciiEncoding"/>
    </group>
    <data  name="headerRegionUri" id="10" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="ConsumerHello" id="2">
    <field name="streamId"              id="1" type="uint32"/>
    <field name="consumerId"            id="2" type="uint32"/>
    <field name="supportsShm"           id="3" type="Bool"/>
    <field name="supportsProgress"      id="4" type="Bool"/>
    <field name="mode"                  id="5" type="Mode"/>
    <field name="maxRateHz"             id="6" type="uint32"/>
    <field name="expectedLayoutVersion" id="7" type="version_t"/>
    <field name="progressIntervalUs"    id="8" type="uint32" presence="optional" nullValue="4294967295"/>
    <field name="progressBytesDelta"    id="9" type="uint32" presence="optional" nullValue="4294967295"/>
    <field name="progressMajorDeltaUnits" id="10" type="uint32" presence="optional" nullValue="4294967295"/>
    <field name="descriptorStreamId"    id="11" type="uint32"/>
    <field name="controlStreamId"       id="12" type="uint32"/>
    <data  name="descriptorChannel"     id="13" type="varAsciiEncoding"/>
    <data  name="controlChannel"        id="14" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="ConsumerConfig" id="3">
    <field name="streamId"           id="1" type="uint32"/>
    <field name="consumerId"         id="2" type="uint32"/>
    <field name="useShm"             id="3" type="Bool"/>
    <field name="mode"               id="4" type="Mode"/>
    <field name="descriptorStreamId" id="6" type="uint32"/>
    <field name="controlStreamId"    id="7" type="uint32"/>
    <data  name="payloadFallbackUri" id="8" type="varAsciiEncoding"/>
    <data  name="descriptorChannel"  id="9" type="varAsciiEncoding"/>
    <data  name="controlChannel"     id="10" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="FrameDescriptor" id="4">
    <field name="streamId"    id="1" type="uint32"/>
    <field name="epoch"       id="2" type="epoch_t"/>
    <field name="seq"         id="3" type="seq_t"/>
    <field name="timestampNs" id="4" type="uint64" presence="optional" nullValue="18446744073709551615"/>
    <field name="metaVersion" id="5" type="version_t" presence="optional" nullValue="4294967295"/>
    <field name="traceId"     id="6" type="uint64" presence="optional" nullValue="0"/>
  </sbe:message>

  <sbe:message name="FrameProgress" id="11">
    <field name="streamId"          id="1" type="uint32"/>
    <field name="epoch"             id="2" type="epoch_t"/>
    <field name="seq"               id="3" type="seq_t"/>
    <field name="payloadBytesFilled" id="4" type="uint64"/>
    <field name="state"             id="5" type="FrameProgressState"/>
  </sbe:message>

  <sbe:message name="QosConsumer" id="5">
    <field name="streamId"    id="1" type="uint32"/>
    <field name="consumerId"  id="2" type="uint32"/>
    <field name="epoch"       id="3" type="epoch_t"/>
    <field name="lastSeqSeen" id="4" type="seq_t"/>
    <field name="dropsGap"    id="5" type="uint64"/>
    <field name="dropsLate"   id="6" type="uint64"/>
    <field name="mode"        id="7" type="Mode"/>
  </sbe:message>

  <sbe:message name="QosProducer" id="6">
    <field name="streamId"   id="1" type="uint32"/>
    <field name="producerId" id="2" type="uint32"/>
    <field name="epoch"      id="3" type="epoch_t"/>
    <field name="currentSeq" id="4" type="seq_t"/>
    <field name="watermark"  id="5" type="uint32" presence="optional" nullValue="4294967295"/>
  </sbe:message>

  <!-- SHM-only composites wrapped without SBE headers -->
  <sbe:message name="ShmRegionSuperblock" id="50" blockLength="64">
    <field name="magic"              id="1" type="uint64"/>
    <field name="layoutVersion"      id="2" type="uint32"/>
    <field name="epoch"              id="3" type="epoch_t"/>
    <field name="streamId"           id="4" type="uint32"/>
    <field name="regionType"         id="5" type="RegionType"/>
    <field name="poolId"             id="6" type="uint16"/>
    <field name="nslots"             id="7" type="uint32"/>
    <field name="slotBytes"          id="8" type="uint32"/>
    <field name="strideBytes"        id="9" type="uint32"/>
    <field name="pid"                id="10" type="uint64"/>
    <field name="startTimestampNs"   id="11" type="uint64"/>
    <field name="activityTimestampNs" id="12" type="uint64"/>
  </sbe:message>

  <sbe:message name="SlotHeader" id="51" blockLength="60">
    <field name="seqCommit"      id="1" type="uint64"/>
    <field name="valuesLenBytes" id="2" type="uint32"/>
    <field name="payloadSlot"    id="3" type="uint32"/>
    <field name="poolId"         id="4" type="uint16"/>
    <field name="payloadOffset"  id="5" type="uint32"/>
    <field name="timestampNs"    id="6" type="uint64"/>
    <field name="metaVersion"    id="7" type="version_t"/>
    <field name="pad"            id="8" type="uint8" length="26"/>
    <data  name="headerBytes"    id="9" type="varDataEncoding"/>
  </sbe:message>

  <sbe:message name="TensorHeader" id="52" blockLength="184">
    <field name="dtype"           id="1" type="Dtype"/>
    <field name="majorOrder"      id="2" type="MajorOrder"/>
    <field name="ndims"           id="3" type="uint8"/>
    <field name="maxDims"         id="4" type="MaxDimsConst"/>
    <field name="padAlign"        id="5" type="uint8"/>
    <field name="progressUnit"    id="6" type="ProgressUnit"/>
    <field name="progressStrideBytes"  id="7" type="uint32"/>
    <field name="dims"            id="8" type="DimsArray"/>
    <field name="strides"         id="9" type="StridesArray"/>
    <field name="pad"             id="10" type="uint8" length="109"/>
  </sbe:message>

  <sbe:message name="DataSourceAnnounce" id="7">
    <field name="streamId"    id="1" type="uint32"/>
    <field name="producerId"  id="2" type="uint32"/>
    <field name="epoch"       id="3" type="epoch_t"/>
    <field name="metaVersion" id="4" type="version_t"/>
    <data  name="name"        id="5" type="varAsciiEncoding"/>
    <data  name="summary"     id="6" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="DataSourceMeta" id="8">
    <field name="streamId"    id="1" type="uint32"/>
    <field name="metaVersion" id="2" type="version_t"/>
    <field name="timestampNs" id="3" type="uint64"/>
    <group name="attributes"  id="4" dimensionType="groupSizeEncoding">
      <data  name="key"    id="1" type="varAsciiEncoding"/>
      <data  name="format" id="2" type="varAsciiEncoding"/>
      <data  name="value"  id="3" type="varDataEncoding"/>
    </group>
  </sbe:message>

  <sbe:message name="ControlResponse" id="9">
    <field name="correlationId" id="1" type="int64"/>
    <field name="code"          id="2" type="ResponseCode"/>
    <data  name="errorMessage"  id="3" type="varAsciiEncoding"/>
  </sbe:message>

</sbe:messageSchema>
```
