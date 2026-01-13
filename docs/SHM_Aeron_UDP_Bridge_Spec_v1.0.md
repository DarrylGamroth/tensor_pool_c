# SHM Tensor Pool Aeron-UDP Bridge
## Specification (v1.0)

**Abstract**  
This document defines a bridge protocol for transporting SHM Tensor Pool frames over `aeron:udp` between hosts. The bridge reads frames from a local SHM pool and re-materializes them into a remote host's SHM pool, then publishes standard `FrameDescriptor` messages locally. This enables remote consumers to use the same wire specification without attaching to a remote driver.

**Key Words**  
The key words “MUST”, “MUST NOT”, “REQUIRED”, “SHOULD”, “SHOULD NOT”, and “MAY” are to be interpreted as described in RFC 2119.

**Normative References**
- SHM Tensor Pool Wire Specification v1.2 (`docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`)
- SHM Driver Model Specification v1.0 (`docs/SHM_Driver_Model_Spec_v1.0.md`)

---

## 1. Scope

This specification defines:

- UDP bridge streams for transporting frame headers and payload bytes.
- Re-materialization behavior into a remote SHM pool.
- Required constraints for chunking, ordering, and loss handling.

This specification does **not** change the SHM layout or local wire protocol; it defines a transport bridge between hosts.

---

## 2. Roles

### 2.1 Bridge Sender (Normative)

The Bridge Sender reads frames from a local SHM pool as a consumer and publishes bridge payload chunks over `aeron:udp`.

### 2.2 Bridge Receiver (Normative)

The Bridge Receiver subscribes to bridge payload chunks, reconstructs frames, writes them into local SHM pools as a producer, and publishes local `FrameDescriptor` messages for local consumers.

### 2.3 Bidirectional Bridge Instances (Informative)

A single bridge instance MAY host mappings in both directions (A→B and B→A). In this case, each mapping is independent; stream IDs MUST be distinct, and deployments SHOULD avoid creating feedback loops (e.g., bridging a stream back to its origin with the same IDs). If the same UDP channels are reused for both directions, stream IDs MUST disambiguate all payload/control/metadata traffic.

**Note:** Do not map `1000→2000` and `2000→1000` on the same bridge control channel unless payload/control/metadata stream IDs are carefully segregated to prevent feedback loops.

---

## 3. Transport Model

- The bridge uses Aeron UDP channels (e.g., `aeron:udp?endpoint=...`).
- Multicast is supported and MAY be used for one-to-many fan-out (use a multicast endpoint address in the UDP channel).
- When using multicast, each receiver independently reconstructs frames into its local SHM pool; receivers do not coordinate and must apply the same out-of-order/duplicate handling rules. Divergent drop behavior across receivers is expected.
- The bridge does not expose the SHM Driver over the network; each host runs its own driver with local SHM pools.
- The bridge is lossy: if any chunk is missing, the frame is dropped.

---

## 4. Streams and IDs

For each bridged `stream_id`, the bridge uses:

- **Bridge Payload Stream**: `BridgeFrameChunk` messages over UDP.
- **Local Descriptor Stream** (receiver side): standard `FrameDescriptor` on IPC or local channel.

Stream ID assignment is deployment-specific. A common pattern is to reserve a UDP stream ID range for bridge payloads, distinct from local descriptor/control streams.

Bridge payload streams SHOULD use a reserved stream ID range (e.g., 50000-59999) to avoid collisions with local control/descriptor streams.

**Alignment with driver allocation ranges (Normative)**
- If the bridge publishes into driver-owned SHM on the destination host, it MUST attach as the exclusive producer for the destination stream and obey the driver’s stream ID allocation rules (see Driver Spec §11). If a producer is already attached for the destination stream, the bridge MUST fail or remap to a different destination.
- If `dest_stream_id` is omitted or set to `0`, the bridge MUST allocate from a configured bridge range (e.g., `bridge.dest_stream_id_range`) and MUST ensure it does not overlap driver control/announce/QoS stream IDs, any statically configured stream IDs, or other bridge ranges on the destination host.
- The bridge MUST NOT allocate per-consumer streams; it publishes only to the configured `dest_stream_id` (and optional metadata/control stream IDs per mapping).

---

## 5. Bridge Frame Chunk Message (Normative)

The bridge transports frames as a sequence of chunks. Each chunk carries a small header plus a byte slice of the frame payload. The first chunk includes the serialized `SlotHeader` + embedded TensorHeader so the receiver can reconstruct metadata without access to remote SHM.

### 5.1 Message Fields

`BridgeFrameChunk` fields:

- `streamId : u32`
- `epoch : u64` (source epoch)
- `seq : u64` (frame identity; equals `seq_commit >> 1` in the header)
- `chunkIndex : u32` (0-based)
- `chunkCount : u32`
- `chunkOffset : u32` (offset into payload)
- `chunkLength : u32`
- `payloadLength : u32` (total payload bytes)
- `headerIncluded : Bool` (TRUE only for `chunkIndex==0`)
- `headerBytes[256]` (present only when `headerIncluded=TRUE`)
- `payloadBytes` (varData)

### 5.2 Chunking Rules

- `chunkCount` MUST be >= 1.
- `chunkCount` MUST NOT exceed 65535.
- `chunkIndex` MUST be < `chunkCount`.
- For `chunkIndex==0`, `headerIncluded` MUST be TRUE and `headerBytes` MUST contain the full 256-byte SlotHeader block (60-byte fixed prefix + 4-byte varData length + 192-byte TensorHeader).
- For `chunkIndex==0`, `chunkOffset` MUST be 0.
- For `chunkIndex>0`, `headerIncluded` MUST be FALSE.
- `chunkOffset` and `chunkLength` MUST describe a non-overlapping slice of the payload.
- The sum of all `chunkLength` values MUST equal `payloadLength`.
- `payloadLength` MUST NOT exceed the largest supported local `stride_bytes`; receivers MUST drop frames that violate this limit.
- Receivers MUST drop all frame chunks until a `ShmPoolAnnounce` has been received for the mapping.
- Chunks SHOULD be sized to fit within the configured MTU for the UDP channel to avoid fragmentation.
- Implementations SHOULD size chunks to allow Aeron `try_claim` usage (single buffer write) and avoid extra copies.
- A safe default is `chunk_bytes = MTU - 128` to account for Aeron and IP/UDP overhead.
- When `headerIncluded=TRUE`, `headerBytes` length MUST be 256. When `headerIncluded=FALSE`, `headerBytes` length MUST be 0.
- Receivers MUST drop chunks where `headerIncluded=TRUE` but `headerBytes.length != 256`, or `headerIncluded=FALSE` and `headerBytes.length > 0`.
- `payloadBytes` length MUST equal `chunkLength`.
- Chunks MAY arrive out-of-order; receivers MUST assemble by `chunkOffset` and `chunkLength`.
- Senders SHOULD publish chunks in order but MUST NOT require in-order delivery.
- Duplicate chunks MAY be ignored if identical; overlapping or conflicting chunks for the same offset MUST cause the frame to be dropped.
- Two chunks are identical if `chunkOffset`, `chunkLength`, and `payloadBytes` content match exactly.
- Receivers MUST drop chunks where `chunkLength` exceeds the configured `bridge.chunk_bytes`, MTU-derived bound, or `bridge.max_chunk_bytes`.
- Decoders MUST NOT allocate buffers larger than configured `bridge.max_chunk_bytes` for `payloadBytes`; oversized varData fields MUST be rejected.

### 5.3 Loss Handling

If any chunk is missing or inconsistent, the receiver MUST drop the frame and MUST NOT publish a `FrameDescriptor` for it.

### 5.3a Frame Assembly Timeout (Normative)

Receivers MUST apply a per-stream frame assembly timeout (RECOMMENDED: 100-500 ms). Incomplete frames exceeding this timeout MUST be dropped, and any in-flight frame state MUST be discarded (including associated buffer credits).

The timeout SHOULD be configurable via `bridge.assembly_timeout_ms`.

### 5.4 Integrity (Informative)

The bridge assumes Aeron UDP reliability. If additional integrity is required, deployments MAY add an out-of-band CRC32C policy or a future schema version with checksums; this v1.0 spec does not require per-chunk or per-frame CRCs.

---

## 6. Receiver Re-materialization (Normative)

Upon receiving all chunks for a frame:

1. Validate `streamId`, `epoch`, `seq`, and chunk consistency.
2. Drop frames until at least one `ShmPoolAnnounce` has been received for the mapping; receivers MUST NOT accept payloads without a source pool announce.
3. Drop frames if the chunk `epoch` does not match the most recent forwarded `ShmPoolAnnounce` epoch for the mapping; receivers MUST NOT write into a mapping with mismatched epoch/layout.
4. Validate the header: `values_len_bytes` MUST equal `payloadLength`; `ndims`, `dtype`, and `dims` MUST be within local limits; malformed headers MUST be dropped. Header length requirements in §5.2 are mandatory.
   The embedded header MUST decode to a supported type (v1.0: `TensorHeader` with the expected schemaId/version/templateId and length); otherwise drop. `payload_offset` MUST be 0 in v1.2.
5. Validate `headerBytes.pool_id` against the source pool range from the most recent forwarded `ShmPoolAnnounce` for the mapping; invalid pool IDs MUST be dropped.
6. Validate `payloadLength` against the source pool stride (from the forwarded announce); if `payloadLength` exceeds the source `stride_bytes`, the frame MUST be dropped.
7. Select the local payload pool and slot using configured mapping rules (e.g., smallest stride >= `payloadLength`). If no local pool can fit the payload, or if `payloadLength` exceeds the largest local `stride_bytes`, the receiver MUST drop the frame.
8. Write payload bytes into the selected local SHM payload pool.
9. Write the `SlotHeader` (with embedded TensorHeader) into the local header ring (with logical sequence preserved), but override `pool_id` and `payload_slot` to match the local mapping. The receiver MUST ignore source `pool_id` and `payload_slot` values.
10. Commit via the standard `seq_commit` protocol.
11. Publish a local `FrameDescriptor` on the receiver's descriptor stream only after `seq_commit` is COMMITTED and payload visibility is ensured.

The receiver MUST treat `seq` as the canonical frame identity and MUST ensure it matches `seq_commit >> 1`.

---

## 7. Descriptor Semantics (Normative)

The bridge receiver publishes a standard `FrameDescriptor` for the re-materialized frame. The derived header index and payload slot refer to the receiver's local SHM pools. The receiver MUST publish `FrameDescriptor` on its standard local IPC descriptor channel and stream for `dest_stream_id` (per the wire specification), unless explicitly overridden by deployment configuration.

Bridge senders MUST NOT publish local `FrameDescriptor` messages over UDP; only `BridgeFrameChunk` messages are carried over the bridge transport.

---

## 7.1 Metadata Forwarding (Normative)

Bridge instances MUST support forwarding `DataSourceAnnounce` and `DataSourceMeta` from the source stream to the receiver host. When `bridge.forward_metadata=true`, the sender MUST forward metadata over `bridge.metadata_channel`/`bridge.metadata_stream_id`, and the receiver MUST publish the forwarded metadata on the destination host's standard local IPC metadata channel/stream. The forwarded `stream_id` MUST be rewritten to `metadata_stream_id` for the mapping (defaulting to `dest_stream_id` if unset) and MUST preserve `meta_version`. If the metadata channel/stream is not configured, the bridge MUST disable metadata forwarding for that mapping (and SHOULD fail fast if `bridge.forward_metadata=true`). When `bridge.forward_metadata=false`, metadata MAY be omitted and bridged consumers will lack metadata.

## 7.2 Source Pool Announce Forwarding (Normative)

Bridge instances MUST forward `ShmPoolAnnounce` for each mapped source stream to the receiver host on the bridge control channel. The receiver MUST use the most recent forwarded announce to validate pool IDs and epochs and MUST NOT republish the source `ShmPoolAnnounce` to local consumers. Metadata forwarding does not use the bridge control channel. When enabled, QoS and FrameProgress MUST be carried on the bridge control channel.

---

## 8. Liveness and Epochs

- The bridge MUST treat an epoch change on the source stream as a remap boundary.
- If `epoch` changes, the receiver MUST drop any in-flight bridge frames and wait for new frames with the new epoch.
- Once a receiver observes a higher `epoch` for a stream, it MUST drop any subsequent chunks that carry an older `epoch`.
- When the receiver first observes a source epoch for a stream, it MUST initialize its local SHM pool epoch at 1 (or increment from any prior local epoch). The receiver's local epoch is independent of the source epoch but MUST be incremented on receiver restart.

---

## 9. Control and QoS (Normative)

Bridge instances MAY forward or translate `QosProducer`/`QosConsumer` messages; when `bridge.forward_qos=true`, they SHOULD do so on the bridge control channel using the per-mapping `source_control_stream_id` and `dest_control_stream_id`. A minimal bridge only handles payload and local descriptor publication.

Bridge instances MAY forward `FrameProgress`; when `bridge.forward_progress=true`, they MUST:
1. Subscribe to the source host's local control stream at `source_control_stream_id` (per mapping).
2. Forward `FrameProgress` messages over `bridge.control_channel` with the following rewrites:
   - `FrameProgress.streamId` MUST be set to `source_stream_id` (sender) and rewritten to `dest_stream_id` (receiver).
  - `FrameProgress.epoch`, `seq`, `payloadBytesFilled`, and `state` MUST be preserved as received from the source.
3. Receiver MUST republish forwarded `FrameProgress` on the destination host's local IPC control stream at `dest_control_stream_id` (per mapping).

Receivers derive the local header index from `seq` before republishing. If the mapping cannot be determined, the receiver MUST drop the forwarded progress for that frame.

Progress forwarding is sender-side (as observed from the source stream) and independent of whether the receiver has finished re-materialization. Receivers SHOULD drop forwarded progress that refers to unknown or expired assembly state.

If `bridge.forward_progress=true`, both `source_control_stream_id` and `dest_control_stream_id` MUST be nonzero for each mapping. If either is unset, the mapping is invalid and the bridge MUST drop forwarded progress for that mapping (and SHOULD fail fast at startup if possible).

Consumers MUST still treat `FrameDescriptor` as the canonical availability signal.

---

## 10. Bridge Configuration (Informative)

The bridge is a separate application from the driver and SHOULD be configured independently. The following keys define a minimal configuration surface; implementations MAY add additional keys.

Required keys:

- `bridge.instance_id` (string): identifier for logging/diagnostics.
- `bridge.payload_channel` (string): Aeron UDP channel for `BridgeFrameChunk`.
- `bridge.payload_stream_id` (uint32): stream ID for `BridgeFrameChunk`.
- `bridge.control_channel` (string): Aeron channel for control messages (forwarded `ShmPoolAnnounce`, and when enabled, `Qos*` and `FrameProgress`).
- `bridge.control_stream_id` (uint32): stream ID for bridge control messages.
- `mappings` (array): one or more stream mappings.

Optional keys and defaults:

- `bridge.mtu_bytes` (uint32): MTU used to size chunks. Default: Aeron channel MTU.
- `bridge.chunk_bytes` (uint32): payload bytes per chunk. If set, the effective chunk size is `min(bridge.chunk_bytes, bridge.mtu_bytes - 128)`; if unset, the default is `bridge.mtu_bytes - 128`.
- `bridge.max_chunk_bytes` (uint32): hard cap for chunk length. Default: `65535`.
- `bridge.max_payload_bytes` (uint32): hard cap for total payload length. Default: `1073741824`.
- `bridge.dest_stream_id_range` (string or array): inclusive range for dynamically allocated destination stream IDs when `dest_stream_id=0`. Ranges MUST NOT overlap metadata/control/QoS stream IDs or other bridge ranges. Default: empty (disabled).
- `bridge.forward_metadata` (bool): forward `DataSourceAnnounce`/`DataSourceMeta`. Default: `true`.
- `bridge.metadata_channel` (string): Aeron UDP channel used to forward metadata (distinct from `bridge.control_channel`). Default: empty (disabled unless set).
- `bridge.metadata_stream_id` (uint32): stream ID for forwarded metadata over `bridge.metadata_channel`. Default: deployment-specific.
- `bridge.source_metadata_stream_id` (uint32): source metadata stream ID to subscribe on the sender host. Default: deployment-specific.
- `bridge.forward_qos` (bool): forward QoS messages. Default: `false`.
- `bridge.forward_progress` (bool): forward `FrameProgress` messages. Default: `false`. Progress forwarding increases control-plane traffic; enable only when remote consumers require partial-availability hints.
- QoS forwarding uses `source_control_stream_id` and `dest_control_stream_id` (per mapping); there is no global QoS stream.
- `bridge.assembly_timeout_ms` (uint32): per-stream frame assembly timeout. Default: `250`.

The bridge control channel is used for forwarding `ShmPoolAnnounce` and, when enabled, QoS and `FrameProgress` messages. Metadata forwarding uses `bridge.metadata_channel` for transport and the destination host's local IPC metadata stream for publication.

Each `mappings` entry:

- `source_stream_id` (uint32): stream ID consumed from local SHM.
- `dest_stream_id` (uint32): stream ID produced on the destination host.
- `profile` (string): destination profile name or pool mapping policy.
- `metadata_stream_id` (uint32, optional): stream ID for forwarded metadata on the destination host. Default: `dest_stream_id`.
- `source_control_stream_id` (uint32, optional): source control stream ID to subscribe for progress/QoS forwarding. Default: `0` (disabled).
- `dest_control_stream_id` (uint32, optional): destination control stream ID to publish forwarded progress/QoS. Default: `0` (disabled).

The bridge control channel carries `ShmPoolAnnounce`, `QosProducer`, `QosConsumer`, and `FrameProgress` messages from the main wire schema (id=900) alongside bridge-specific messages (schema id=902).

Example config: `docs/examples/bridge_config_example.toml`.

---

## 11. Bridge SBE Schema (Normative)

```
<?xml version="1.0" encoding="UTF-8"?>
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                   package="shm.tensorpool.bridge"
                   id="902"
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

    <composite name="varDataEncoding">
      <type name="length"  primitiveType="uint32" maxValue="1073741824"/>
      <type name="varData" primitiveType="uint8" length="0"/>
    </composite>

    <composite name="varDataEncoding256">
      <type name="length"  primitiveType="uint32" maxValue="256"/>
      <type name="varData" primitiveType="uint8" length="0"/>
    </composite>

    <enum name="Bool" encodingType="uint8">
      <validValue name="FALSE">0</validValue>
      <validValue name="TRUE">1</validValue>
    </enum>
  </types>

  <sbe:message name="BridgeFrameChunk" id="1">
    <field name="streamId"       id="1" type="uint32"/>
    <field name="epoch"          id="2" type="uint64"/>
    <field name="seq"            id="3" type="uint64"/>
    <field name="chunkIndex"     id="4" type="uint32" maxValue="65535"/>
    <field name="chunkCount"     id="5" type="uint32" maxValue="65535"/>
    <field name="chunkOffset"    id="6" type="uint32"/>
    <field name="chunkLength"    id="7" type="uint32" maxValue="65535"/>
    <field name="payloadLength"  id="8" type="uint32" maxValue="1073741824"/>
    <field name="headerIncluded" id="9" type="Bool"/>
    <data  name="headerBytes"    id="10" type="varDataEncoding256"/>
    <data  name="payloadBytes"   id="11" type="varDataEncoding"/>
  </sbe:message>

</sbe:messageSchema>
```
