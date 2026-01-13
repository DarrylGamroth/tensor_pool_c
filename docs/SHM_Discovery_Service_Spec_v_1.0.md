# SHM Discovery Service Specification (v1.0)

## Abstract
This document defines a **Discovery Service** for the Shared-Memory Tensor Pool ecosystem. The Discovery Service provides a read-only, advisory inventory of available streams across one or more SHM Drivers. It enables clients, tools, and supervisors to locate streams, inspect metadata, and identify the authoritative Driver endpoint responsible for attachment.

Discovery **does not grant attachment authority**, manage epochs, or allocate shared memory. The SHM Driver remains the sole authority for lifecycle, attach, and epoch management.

This specification defines a **single Discovery API** that may be implemented either:
- Embedded within an SHM Driver (single-host default), or
- As a standalone Registry service aggregating multiple Drivers (multi-host/fleet deployments).

**Key Words**  
The key words “MUST”, “MUST NOT”, “REQUIRED”, “SHOULD”, “SHOULD NOT”, and “MAY” are to be interpreted as described in RFC 2119.

---

## 1. Scope

This specification defines:
- Discovery request/response messages
- Authority identification for multi-host discovery
- Registry behavior and expiry rules
- Client usage rules and constraints

This specification does **not** redefine:
- SHM layout
- Attach, lease, or epoch semantics
- Wire-format data-plane messages

---

## 2. Roles

### 2.1 SHM Driver (Authoritative)

The SHM Driver:
- Owns SHM lifecycle and epochs
- Emits `ShmPoolAnnounce` and metadata messages
- Processes `ShmAttach*` requests
- MAY implement the Discovery API directly

### 2.2 Discovery Provider (Advisory)

A Discovery Provider:
- Implements the Discovery API defined in this document
- Maintains a cache/index derived from announcements
- Answers discovery queries

A Discovery Provider MAY be:
- Embedded in an SHM Driver
- A standalone Registry service subscribing to multiple Drivers

### 2.3 Client

A Client:
- Sends Discovery requests to locate streams
- MUST still attach via the authoritative SHM Driver
- MUST validate epochs, layout versions, and SHM superblocks

---

## 3. Authority Model (Normative)

1. Discovery responses are **informational snapshots**.
2. Discovery responses MUST NOT be treated as authoritative.
3. Clients MUST NOT attach, map, or consume SHM solely based on Discovery responses.
4. All attachments MUST be validated via the authoritative SHM Driver.

---

## 4. Transport and Endpoint Model

### 4.1 Control Plane Transport

Discovery messages are SBE-encoded and transported over Aeron using the standard message header.

### 4.2 Endpoint Configuration

Deployments define:

```toml
# Single-host default
discovery.channel   = "aeron:ipc?term-length=1m"
discovery.stream_id = 1000

# Multi-host / fleet
discovery.channel   = "aeron:udp?endpoint=registry.example.org:40123"
discovery.stream_id = 1000
```

When Discovery is embedded in the Driver, `discovery.*` MAY default to the driver control endpoint.

If discovery shares a channel or stream with other control-plane traffic, implementations MUST gate decoding on the SBE message header `schemaId` (and `templateId`) to avoid mixed-schema collisions.

### 4.3 Response Channels (Normative)

- Clients MUST provide a response channel and stream ID in each request.
- Discovery Providers MUST publish responses to the provided response endpoint.
- Providers MUST NOT respond on the request channel unless the client explicitly requests it.

---

## 5. Discovery Messages

### 5.0 Encoding and Optional Fields (Normative)

- Discovery messages use the same SBE message header as the wire specification.
- Optional primitive fields MUST use explicit nullValue sentinels as defined in the SBE schema.
- Variable-length `data` fields are optional by encoding length = 0. Producers MUST use length 0 to indicate absence; consumers MUST treat length 0 as “not provided”.
- SBE requires variable-length `data` fields to appear at the end of a message.

### 5.1 DiscoveryRequest

**Direction:** Client → Discovery Provider

**Fields**
- `request_id : u64` — client-generated correlation ID
- `client_id : u32`
- `response_channel : string` (required, non-empty)
- `response_stream_id : u32` (required, non-zero)

**Optional Filters (AND semantics)**
- `stream_id : u32`
- `producer_id : u32`
- `data_source_id : u64`
- `data_source_name : string`
- repeating group `tags`:
  - `tag : string`

**Rules**
- If no filters are supplied, all known streams MAY be returned (subject to limits).
- If multiple filters are provided, they MUST be treated as AND.
- Requests with empty `response_channel` or `response_stream_id=0` MUST be rejected. Providers MUST drop such requests silently and MUST NOT emit any response when `response_channel` is empty.
- `response_stream_id` is required on the wire and MUST be non-zero; providers MUST reject any request whose encoded value is 0. If `response_channel` is non-empty, providers SHOULD return `status=ERROR` to the response endpoint; if `response_channel` is empty, providers MUST drop the request without responding. Clients MUST NOT send zero.
- `data_source_name` matching is case-sensitive.
- Providers MUST return `data_source_name` exactly as announced (byte-for-byte); clients MUST compare names byte-for-byte.
- `data_source_name` SHOULD be at most 256 bytes.

**Tag matching rules**
- Tag matching is case-sensitive.
- If multiple tags are provided, all requested tags MUST be present on a stream result.
- Duplicate tags in a request MUST be treated as a single tag for matching purposes.

**Client identity**
- `client_id` is an opaque identifier provided by the client. Providers MAY use it for logging, rate limiting, or access control, but MUST NOT treat it as an authentication token.

**Response endpoint validation**
- `response_stream_id` MUST be non-zero; providers MUST reject requests with zero as invalid.

---

### 5.2 DiscoveryResponse

**Direction:** Discovery Provider → Client

**Fields**
- `request_id : u64` (echoed)
- `status : enum { OK=1, NOT_FOUND=2, ERROR=3 }`
- `error_message : string` (optional)

**Repeating Group: results**

Each result describes one stream:

- `stream_id : u32`
- `producer_id : u32`
- `epoch : u64`
- `layout_version : u32`

#### SHM Layout (Informational)
- `header_region_uri : string`
- `header_nslots : u32`
- `header_slot_bytes : u16` (must be `256` in v1.x)
- `max_dims : u8` (must equal the schema constant; v1.x fixed at `8`)
- repeating `payload_pools`:
  - `pool_id : u16`
  - `region_uri : string`
  - `pool_nslots : u32`
  - `stride_bytes : u32`

#### Metadata (Optional)
- `data_source_id : u64`
- `data_source_name : string`
- repeating `tags`:
  - `tag : string`
  - Providers MUST return the full tag set for the stream (not a filtered subset), so clients can cache and display complete metadata. Tags MUST preserve original case; ordering is unspecified.

#### Authority Identification (Normative)
- `driver_instance_id : string`
- `driver_control_channel : string`
- `driver_control_stream_id : u32`

**Rules**
- The authority fields identify the Driver responsible for attachment.
- Clients MUST use these fields when initiating attach operations.
- The authority fields refer to the Driver control endpoint used for `ShmAttach*` and lease messages. Per-consumer descriptor/control streams (if used) are separate and MUST NOT be inferred from discovery responses.
- `driver_control_channel` MUST be non-empty; results with an empty channel MUST be treated as invalid by clients.
- `driver_control_stream_id` is required on the wire and MUST be non-zero; providers MUST NOT emit results where the value is 0, and clients MUST treat zero as invalid and ignore any such result.
- Providers SHOULD cap responses (RECOMMENDED: max 1,000 results) and MAY return `status=ERROR` with an `error_message` if limits are exceeded.
- `error_message` SHOULD be limited to 1024 bytes for UDP MTU safety.
- Responses MUST echo `request_id`. Clients MUST drop responses with unknown `request_id`.
- If `pool_nslots` is present in results, providers MUST ensure it matches `header_nslots` for v1.0; clients MUST treat any mismatch as a protocol error and reattach.
- `status=OK` MUST be used when the request is processed successfully, even if there are zero matching results. `status=NOT_FOUND` MUST be used only to indicate that no visible matches exist (after applying ACLs/filters). `status=ERROR` is reserved for internal failures or malformed requests.
- When `status=ERROR` or `status=NOT_FOUND`, the `results` group MUST be empty (`numInGroup = 0`). Clients MUST ignore any results if a non-zero count is received with these statuses.

---

## 6. Registry State and Expiry

Discovery Providers maintain soft-state derived from announcements.

### 6.1 Indexing

Entries are keyed by:
```
(driver_instance_id, stream_id)
```

### 6.2 Expiry Rules

- Entries MUST expire if no `ShmPoolAnnounce` is observed within a timeout window.
- RECOMMENDED expiry: `3 × announce_period_ms`
  - This matches the wire-spec freshness window for `ShmPoolAnnounce` (3× announce period).

Expired entries MUST NOT be returned in Discovery responses.

### 6.3 Conflict Resolution

- If multiple announces are observed for the same `(driver_instance_id, stream_id)` with different `epoch`, the newest `epoch` MUST replace prior entries.
- Providers SHOULD drop entries that regress `epoch` unless a driver restart is known (e.g., driver endpoint changes or `driver_instance_id` changes).

### 6.4 Source Inputs (Normative)

Discovery Providers MUST derive their index from:
- `ShmPoolAnnounce` for SHM layout and epochs
- `DataSourceAnnounce` / `DataSourceMeta` for metadata (if present)
- Driver identity and control endpoint (out-of-band or via driver config)

Providers MAY also index:
- QoS messages for diagnostics
- Health checks from embedded driver endpoints

---

## 7. Client Behavior (Normative)

Clients MUST:

1. Treat Discovery responses as advisory snapshots
2. Select a stream and authority
3. Attach via `ShmAttachRequest` to the indicated Driver
4. Validate:
   - epoch
   - layout_version
   - SHM superblocks
5. Subscribe to `ShmPoolAnnounce` for liveness

Clients MUST NOT:
- Map SHM regions based solely on Discovery responses
- Assume Discovery results imply attach permission

Clients SHOULD:
- De-duplicate responses by `(driver_instance_id, stream_id, epoch)` and discard stale epochs.
- Apply a local freshness window: if the provider returns stale epochs or layouts (e.g., by mismatch against `ShmPoolAnnounce`), the client SHOULD discard and re-query.

---

## 8. Multi-Host and Fleet Discovery

In multi-host deployments:

- A standalone Registry service MAY aggregate announcements from multiple Drivers
- Stream identity is `(driver_instance_id, stream_id)`
- Registries MAY apply policy filters, ACLs, or visibility rules

Registries MUST NOT proxy or modify attach semantics.

---

## 9. Relationship to Bridging

Discovery MAY advertise streams that are not locally attachable.

Such streams MAY include metadata indicating:
- local SHM attach supported
- remote consumption requires a bridge

The Discovery protocol itself remains transport-agnostic.

---

## 10. Compatibility and Versioning

- Discovery schema versioning is independent of SHM layout versioning
- Clients MUST reject Discovery messages with unsupported schema versions.
- Clients MUST use the SBE message header (`schemaId`, `version`) to determine discovery schema compatibility.
- Providers SHOULD include a `discovery_schema_version` constant in their SBE schema (for codegen/diagnostics), but clients MUST rely on the message header for version checks.
- For v1.0, the discovery schema uses `schemaId=910`, `version=1`, and template IDs: `DiscoveryRequest=1`, `DiscoveryResponse=2`. Implementations MUST reject mismatched `schemaId` or unsupported `version`.

---

## 11. Rationale (Informative)

This design:
- Preserves a single protocol surface
- Enables both single-host simplicity and fleet-scale discovery
- Mirrors Aeron Driver vs Archive vs tooling separation
- Prevents authority confusion or TOCTOU errors

---

## 12. Security and Policy (Normative)

- Providers MUST treat discovery as advisory and MUST NOT grant access implicitly.
- Providers MAY enforce ACLs and visibility filters based on client identity.
- If access control is used, providers MUST return `status=NOT_FOUND` for unauthorized streams to avoid oracle leakage, or `status=ERROR` if policy requires explicit denial.

---

## 13. Operational Guidance (Informative)

- Embedded discovery SHOULD reuse the driver control channel for simplicity.
- Standalone registries SHOULD be replicated for availability.
- Providers SHOULD log discovery queries at a sampled rate to avoid PII leaks.

---

## 14. Example Flows (Informative)

### 14.1 Single-Host Discovery (Embedded)

```
Client -> DiscoveryRequest (ipc)
Driver -> DiscoveryResponse (ipc)
Client -> ShmAttachRequest (driver control)
```

### 14.2 Fleet Discovery (Standalone Registry)

```
Client -> DiscoveryRequest (udp registry)
Registry -> DiscoveryResponse (udp response channel)
Client -> ShmAttachRequest (driver control endpoint from response)
```

---

## 15. Discovery Schema (SBE Appendix)

The following schema excerpt is normative for v1.0 of the Discovery API.

 **Normative encoding constraints (schema-level notes)**
- `responseStreamId` MUST be present and non-zero. If zero, the request MUST be rejected (see §5.1).
 - `responseChannel` MUST be non-empty. If length=0, providers MUST drop the request without responding.
- `driverControlStreamId` MUST be present and non-zero. Results with zero MUST be omitted or treated as invalid by clients.
 - When `status=ERROR` or `status=NOT_FOUND`, `results.numInGroup` MUST be 0; clients MUST ignore any results if a non-zero count is received.
 - `data_source_name` SHOULD be at most 256 bytes.

```xml
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                   package="shm.tensorpool.discovery"
                   id="910"
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

    <composite name="varAsciiEncoding">
      <type name="length"  primitiveType="uint32" maxValue="1073741824"/>
      <type name="varData" primitiveType="uint8" length="0" characterEncoding="US-ASCII"/>
    </composite>

    <enum name="DiscoveryStatus" encodingType="uint8">
      <validValue name="OK">1</validValue>
      <validValue name="NOT_FOUND">2</validValue>
      <validValue name="ERROR">3</validValue>
    </enum>
  </types>

  <sbe:message name="DiscoveryRequest" id="1">
    <field name="requestId"        id="1" type="uint64"/>
    <field name="clientId"         id="2" type="uint32"/>
    <field name="responseStreamId" id="3" type="uint32"/>
    <field name="streamId"         id="4" type="uint32" presence="optional" nullValue="4294967295"/>
    <field name="producerId"       id="5" type="uint32" presence="optional" nullValue="4294967295"/>
    <field name="dataSourceId"     id="6" type="uint64" presence="optional" nullValue="18446744073709551615"/>
    <group name="tags" id="7" dimensionType="groupSizeEncoding">
      <field name="tag" id="1" type="varAsciiEncoding"/>
    </group>
    <!-- responseChannel length=0 is invalid; providers MUST drop such requests without responding -->
    <data  name="responseChannel"  id="8" type="varAsciiEncoding"/>
    <data  name="dataSourceName"   id="9" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="DiscoveryResponse" id="2">
    <field name="requestId" id="1" type="uint64"/>
    <field name="status"    id="2" type="DiscoveryStatus"/>
    <!-- When status=ERROR or NOT_FOUND, results.numInGroup MUST be 0 -->
    <group name="results" id="3" dimensionType="groupSizeEncoding">
      <field name="streamId"         id="1" type="uint32"/>
      <field name="producerId"       id="2" type="uint32"/>
      <field name="epoch"            id="3" type="uint64"/>
      <field name="layoutVersion"    id="4" type="uint32"/>
      <field name="headerNslots"     id="5" type="uint32"/>
      <field name="headerSlotBytes"  id="6" type="uint16"/>
      <field name="maxDims"          id="7" type="uint8"/>
      <field name="dataSourceId"     id="8" type="uint64" presence="optional" nullValue="18446744073709551615"/>
      <!-- driverControlStreamId MUST be non-zero; zero is invalid -->
      <field name="driverControlStreamId" id="9" type="uint32"/>
      <group name="payloadPools" id="10" dimensionType="groupSizeEncoding">
        <field name="poolId"      id="1" type="uint16"/>
        <field name="poolNslots"  id="2" type="uint32"/>
        <field name="strideBytes" id="3" type="uint32"/>
        <data  name="regionUri"   id="4" type="varAsciiEncoding"/>
      </group>
      <group name="tags" id="11" dimensionType="groupSizeEncoding">
        <field name="tag" id="1" type="varAsciiEncoding"/>
      </group>
      <data  name="headerRegionUri"  id="12" type="varAsciiEncoding"/>
      <data  name="dataSourceName"   id="13" type="varAsciiEncoding"/>
      <data  name="driverInstanceId" id="14" type="varAsciiEncoding"/>
      <data  name="driverControlChannel" id="15" type="varAsciiEncoding"/>
    </group>
    <data name="errorMessage" id="4" type="varAsciiEncoding"/>
  </sbe:message>
</sbe:messageSchema>
```

---

End of Specification
