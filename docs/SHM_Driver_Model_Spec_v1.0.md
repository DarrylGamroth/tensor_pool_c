# Shared-Memory Tensor Pool
## Driver Model Specification (v1.0)

**Abstract**  
This document defines a normative **Driver Model** for managing shared-memory tensor pools using the wire format and shared-memory layout defined in the *Shared-Memory Tensor Pool Wire Specification*. The Driver Model specifies resource ownership, attachment semantics, lifecycle management, exclusivity rules, and failure handling necessary to ensure safe and interoperable deployment across multiple processes, users, and implementations.

This document is normative for deployments that use an external SHM Driver.

**Key Words**  
The key words “MUST”, “MUST NOT”, “REQUIRED”, “SHOULD”, “SHOULD NOT”, and “MAY” are to be interpreted as described in RFC 2119.

**Normative References**  
- Shared-Memory Tensor Pool Wire Specification v1.1

**Informative Reference**  
- Stream ID guidance: `docs/STREAM_ID_CONVENTIONS.md`

---

## 1. Scope

This specification defines:

- A long-lived SHM Driver process responsible for all shared-memory resource management
- A lease-based attachment model for producers and consumers
- Exclusive producer semantics per stream
- Epoch lifecycle rules and crash recovery behavior
- Control-plane interactions required to obtain SHM mappings

This specification does **not** redefine wire formats, shared-memory layouts, commit protocols, or payload semantics. All such definitions are imported from the Wire Specification.

---

## 2. Roles

### 2.1 SHM Driver (Normative)

The SHM Driver is the authoritative entity for SHM lifecycle, epochs, and `ShmPoolAnnounce`. It owns the lifecycle of all SHM backing files, enforces filesystem and security policy, manages epochs and layout versions, authoritatively assigns SHM region URIs, enforces exclusive producer rules, and emits `ShmPoolAnnounce` messages.

The driver MAY be embedded within an application process or run as an external service. A deployment MUST ensure that only one authoritative driver instance manages a given `stream_id` at a time.

### 2.2 Producer Client (Normative)

A Producer Client attaches to a stream via the driver, writes headers and payloads into driver-owned SHM regions, publishes `FrameDescriptor` messages as defined in the Wire Specification, and MUST NOT create, truncate, or unlink SHM backing files.

### 2.3 Consumer Client (Normative)

A Consumer Client attaches to a stream via the driver, maps SHM regions using URIs provided by the driver, reads SHM and consumes descriptors per the Wire Specification, and MUST NOT create, truncate, or unlink SHM backing files.

---

## 3. SHM Ownership and Authority (Normative)

When the Driver Model is used:

1. The SHM Driver is the sole authority permitted to create, truncate, or unlink SHM backing files; select filesystem paths; initialize and validate SHM superblocks; and increment `epoch` or change `layout_version`.
2. Producer and consumer clients MUST NOT create or select SHM filesystem paths.
3. Producer and consumer clients MUST treat all SHM region URIs received from the driver as authoritative.
4. All SHM regions MUST conform to the Wire Specification.

The driver MAY update `activity_timestamp_ns` in superblocks directly or delegate that responsibility to the attached producer, but it remains responsible for ensuring liveness semantics in the Wire Specification are met.

---

## 4. Attachment Model

### 4.1 Leases (Normative)

A lease represents authorization for a client to access a specific stream in a specific role. Each lease is associated with exactly one `stream_id`, exactly one role (PRODUCER or CONSUMER), is identified by an opaque `lease_id`, and MAY have a bounded lifetime enforced by the driver. The driver MUST track active leases for liveness and cleanup.

### 4.2 Attach Protocol (Normative)

Clients attach to a stream by issuing a ShmAttachRequest to the driver and receiving a correlated ShmAttachResponse. The attach protocol MUST provide, on success, the current `epoch`, the current `layout_version`, URIs for all SHM regions required by the Wire Specification, and a valid `lease_id`.

The driver MAY create new SHM regions on demand when `publishMode=EXISTING_OR_CREATE`; otherwise, it MUST return an error if the stream does not already exist or is not provisioned for the requested role.

Clients SHOULD apply an attach timeout; if no response is received within the configured window, clients SHOULD retry with backoff.

`correlationId` is client-supplied; the driver MUST echo it unchanged in `ShmAttachResponse`.

For `code=OK`, the response MUST include: `leaseId`, `streamId`, `epoch`, `layoutVersion`, `headerNslots`, `headerSlotBytes`, `maxDims`, `headerRegionUri`, and a complete `payloadPools` group with each pool's `regionUri`, `poolId`, `poolNslots`, and `strideBytes`. `headerSlotBytes` is fixed at `256` by the Wire Specification, and `maxDims` is fixed by the schema constant (v1.1: `8`).

For `code != OK`, the response MUST include `correlationId` and `code`, and SHOULD include `errorMessage` with a diagnostic string.

Optional primitive fields in the SBE schema MUST use explicit `nullValue` sentinels. For `code=OK`, all required fields MUST be non-null; for `code != OK`, optional response fields SHOULD be set to their `nullValue`.

For optional enum fields, the `nullValue` MUST be used when the field is absent and MUST NOT match any defined enum constant. For required enum fields that are conceptually optional, this specification uses an explicit `UNSPECIFIED` value (e.g., `HugepagesPolicy`).

Variable-length `data` fields (e.g., `errorMessage`, `headerRegionUri`) MUST NOT be marked `presence="optional"` in the schema for sbe-tool compatibility. Absence is represented by a zero-length value.

If `code=OK` and any required field is set to its `nullValue`, the client MUST treat the response as a protocol error, DROP the attach, and reattach.

For `code=OK`, `headerRegionUri` and every `payloadPools.regionUri` MUST be present, non-empty, and not blank. If any required URI is absent or empty (length=0), the client MUST treat the response as a protocol error, DROP the attach, and reattach.

For `code=OK`, clients MUST reject the response if `headerSlotBytes != 256`, if `maxDims` does not match the schema constant, or if the `payloadPools` group is empty.

For `code=OK`, the driver MUST set `poolNslots` equal to `headerNslots` for each payload pool. Clients MUST treat any mismatch as a protocol error, DROP the attach, and reattach.

If `leaseExpiryTimestampNs` is present, clients MUST treat it as a hard deadline; if absent, clients MUST still send keepalives at the configured interval and treat lease validity as unknown beyond the absence of `ShmLeaseRevoked`.

All URIs returned by the driver MUST satisfy the Wire Specification URI validation rules; clients MUST validate and reject URIs that fail those rules.

If `errorMessage` is present (length > 0), it MUST be limited to 1024 bytes; drivers SHOULD truncate longer messages.

### 4.3 Attach Request Semantics (Normative)

- `expectedLayoutVersion`: If present and nonzero, the driver MUST reject the request with `code=REJECTED` if the active layout version for the stream does not match. If absent or zero, the driver uses its configured layout version and returns it in the response.
- `maxDims`: MUST be omitted or zero. The driver MUST ignore any nonzero value and MUST return the schema constant in the response.
- `publishMode`: `REQUIRE_EXISTING` means the driver MUST reject if the stream is not already provisioned. `EXISTING_OR_CREATE` allows the driver to create or initialize SHM regions on demand.
- `requireHugepages`: A `HugepagesPolicy` value. If `HUGEPAGES`, the driver MUST reject the request with `code=REJECTED` if it cannot provide hugepage-backed regions that satisfy Wire Specification validation rules. If `STANDARD`, the driver MUST reject the request with `code=REJECTED` if it cannot provide standard page-backed regions. If `UNSPECIFIED`, the driver applies its configured default policy.
- Streams with zero payload pools are invalid in v1.0; the driver MUST reject attach requests for such streams with `code=INVALID_PARAMS`.

### 4.4 Lease Keepalive (Normative)

The driver SHOULD require periodic `ShmLeaseKeepalive` messages for active leases. If `leaseExpiryTimestampNs` is provided in the attach response, the client MUST ensure keepalives arrive before that timestamp. On each valid keepalive, the driver MUST extend the lease expiry (duration is implementation-defined and MAY be documented out-of-band). If a lease expires, the driver MUST invalidate it and enforce the epoch rules in §6 and §7.

For interoperability, a deployment SHOULD configure a default keepalive interval and expiry grace. A recommended baseline is:

- Client keepalive interval: 1 second.
- Lease expiry grace: 3 consecutive missed keepalives (3 seconds).

Drivers MAY use different values but MUST make them discoverable out-of-band (configuration or operational documentation). Clients SHOULD treat a keepalive send failure as a fatal condition and reattach.

NOTE: Keepalives are one-way; clients infer lease validity from the absence of `ShmLeaseRevoked` and continued successful operations. This trades acknowledgment latency for reduced control-plane traffic.

This model mirrors Aeron’s driver liveness approach (heartbeat plus timeout rather than explicit acknowledgments).

### 4.4a Schema Version Compatibility (Normative)

Clients MUST reject messages with a schema version higher than they support. Drivers SHOULD respond using the highest schema version supported by both client and driver; if no compatible version exists, the driver MUST return `code=UNSUPPORTED`.

Clients MUST reject driver control-plane messages whose `schemaId` or `templateId` does not match the expected driver control schema.

### 4.5 Control-Plane Transport (Normative)

`ShmAttachRequest`, `ShmAttachResponse`, `ShmDetachRequest`, `ShmDetachResponse`, `ShmLeaseKeepalive`, `ShmLeaseRevoked`, `ShmDriverShutdown`, and (if implemented) `ShmDriverShutdownRequest` MUST be carried on the control-plane Aeron stream defined by the Wire Specification unless an alternative is explicitly configured and documented for the deployment.

### 4.6 Response Codes (Normative)

The driver MUST use response codes consistently:

- `OK`: The request succeeded and all required fields are present.
- `UNSUPPORTED`: The request uses a feature the driver does not implement (e.g., unsupported schema version or publish mode).
- `INVALID_PARAMS`: The request is malformed or violates parameter constraints (e.g., invalid `expectedLayoutVersion`).
- `REJECTED`: The request is valid but denied by policy or state (e.g., exclusive producer already attached, `requireHugepages=HUGEPAGES` not satisfiable, `expectedLayoutVersion` mismatch).
- `INTERNAL_ERROR`: The driver encountered an unexpected failure while processing a valid request.

### 4.7 Lease Lifecycle (Normative)

Leases follow this lifecycle:

- `ATTACHED`: Lease is issued in a successful `ShmAttachResponse`.
- `ACTIVE`: Lease remains valid while keepalives arrive before expiry (if enforced).
- `DETACHED`: Lease is invalidated by a successful `ShmDetachRequest`.
- `EXPIRED`: Lease is invalidated due to keepalive timeout or driver policy.
- `REVOKED`: Lease is invalidated by the driver for administrative or safety reasons.

Once a lease reaches `DETACHED`, `EXPIRED`, or `REVOKED`, the client MUST stop using all SHM regions from that lease and MUST reattach to continue.

### 4.7a Protocol Errors (Normative)

On any detected protocol error (e.g., required fields missing or set to null on `code=OK`, malformed responses, unknown enums where disallowed), clients MUST fail closed: drop the attach, stop using any mapped regions derived from the response, and reattach.

When a producer lease transitions to `EXPIRED` or `REVOKED`, the driver MUST increment `epoch` and MUST emit a fresh `ShmPoolAnnounce` promptly so consumers can fail closed and remap.

### 4.8 Lease Identity and Client Identity (Normative)

- `leaseId` MUST be unique per driver instance for the lifetime of the process and MUST NOT be reused after expiry or detach.
- `leaseId` scope is local to a single driver instance and MUST NOT be assumed stable across driver restarts.
- `clientId` MUST be unique per client process. If the driver observes two active leases with the same `clientId`, it MUST reject the newer attach with `code=REJECTED`.
- Clients MUST NOT issue concurrent attach requests for the same `streamId` and role. Drivers MAY reject subsequent requests with `code=REJECTED` or treat them as retries (ignoring duplicates by `correlationId`).

### 4.9 Detach Semantics (Normative)

`ShmDetachRequest` is best-effort and idempotent. If the lease is active and matches the request's `leaseId`, `streamId`, `clientId`, and `role`, the driver MUST invalidate the lease and return `code=OK`. If the lease is unknown or already invalidated, the driver SHOULD return `code=REJECTED` (or `OK` if it treats the request as idempotent success). Detaching a producer lease MUST trigger an epoch increment per §6.

The driver MUST echo `correlationId` unchanged in `ShmDetachResponse`.

For any lease invalidation event (`DETACHED`, `EXPIRED`, or `REVOKED`), the driver MUST publish a `ShmLeaseRevoked` notice on the control-plane stream. This includes consumer leases and producer leases (in addition to any `ShmPoolAnnounce` required for epoch changes).

Clients MUST handle `ShmLeaseRevoked` as follows:
- If the revoked lease matches the client's active lease, the client MUST immediately stop using mapped regions, DROP any in-flight frames, and reattach.
- If the revoked lease is a producer lease for a stream the client consumes, the client MUST wait for the epoch-bumped `ShmPoolAnnounce` before remapping and resuming.
- If the revoked lease does not match the client's lease and is not the current producer lease for a stream the client consumes, the client MAY ignore it after verifying the `leaseId`, `streamId`, and `role` do not apply.
Clients SHOULD handle revocations on a per-lease basis; revocation of one lease MUST NOT force teardown of unrelated leases.

`ShmLeaseRevoked.reason` is required; clients MUST reject messages with unknown reason values. Clients MUST also reject `ShmLeaseRevoked` messages with unknown `role` values.

The driver SHOULD emit `ShmLeaseRevoked` before the corresponding `ShmPoolAnnounce` to allow consumers to correlate the epoch change with the revocation event.

Consumers SHOULD apply a timeout (recommend 3× the announce period) when waiting for an epoch-bumped `ShmPoolAnnounce`; on timeout, consumers SHOULD unmap and enter a retry/backoff loop.

Consumers SHOULD verify that the `streamId` in `ShmLeaseRevoked` matches the `stream_id` in the subsequent `ShmPoolAnnounce`, and that the announced `epoch` is strictly greater than the previously observed value.

The driver SHOULD NOT send duplicate `ShmLeaseRevoked` messages for the same `leaseId` unless the lease has been reissued and revoked again. Clients MUST tolerate duplicate revocations idempotently.

### 4.10 Control-Plane Sequences (Informative)

Attach / keepalive / detach sequence (single stream, success path):

```
Client                        Driver
  | --- ShmAttachRequest --->   |
  | <--- ShmAttachResponse ---  |
  |                             |
  | --- ShmLeaseKeepalive --->  |
  | --- ShmLeaseKeepalive --->  |
  | --- ShmLeaseKeepalive --->  |
  |                             |
  | --- ShmDetachRequest --->   |
  | <--- ShmDetachResponse ---  |
```

Attach failure sequence (example: reject due to exclusive producer):

```
Client                        Driver
  | --- ShmAttachRequest --->   |
  | <--- ShmAttachResponse ---  |
        (code=REJECTED)
```

Lease expiry/revoke sequence (producer lease, driver forces remap):

```
Client                        Driver                      Consumers
  | --- ShmLeaseKeepalive --->   |                             |
  | (missed/expired)             |                             |
  |                              | --- ShmLeaseRevoked ----->  |
  |                              | --- ShmPoolAnnounce ----->  |
  |                              |   (epoch bumped)            |
```

Graceful driver shutdown sequence:

```
Clients                       Driver
  | <--- ShmDriverShutdown --- |
  | (invalidate leases)        |
```

### 4.11 Embedded Driver Discovery (Informative)

When the driver is embedded, deployments SHOULD still expose a well-known control-plane endpoint (channel + stream ID) so external tools (supervisors, diagnostics) can attach. If the control-plane endpoint is dynamic, deployments SHOULD publish it via service discovery or out-of-band configuration.

### 4.12 Client State Machines (Informative)

This section provides suggested state machines for implementers (e.g., a C client). These are informative and do not change normative requirements.

#### 4.12.1 Driver Client (Attach/Detach)

Suggested states:
- `Unattached`: no active lease.
- `Attaching`: attach request in flight.
- `Attached`: lease active; keepalives running.
- `Detaching`: detach request in flight.
- `Backoff`: retry delay after rejection/revocation.

Suggested transitions:
- `Unattached` → `Attaching` on attach request; send `ShmAttachRequest`.
- `Attaching` → `Attached` on `ShmAttachResponse(code=OK)`; start keepalive timer.
- `Attaching` → `Backoff` on `ShmAttachResponse(code!=OK)`; schedule retry.
- `Attaching` → `Backoff` on attach timeout (no response within a configured window); schedule retry.
- `Attached` → `Attached` on keepalive tick; send `ShmLeaseKeepalive`.
- `Attached` → `Backoff` on `ShmLeaseRevoked` for own lease; stop using SHM and reattach.
- `Attached` → `Detaching` on user close; send `ShmDetachRequest`.
- `Detaching` → `Unattached` on `ShmDetachResponse`; stop keepalive.
- Any → `Unattached` on `ShmDriverShutdown`; invalidate lease and stop.

#### 4.12.2 Producer

Suggested states:
- `Idle`: no active lease.
- `Active`: lease active; publishing allowed.
- `Paused`: lease revoked/expired; publishing stopped.

Suggested transitions:
- `Idle` → `Active` on attach success; initialize SHM mapping and publishing.
- `Active` → `Paused` on `ShmLeaseRevoked` for own lease; stop publishing and unmap.
- `Paused` → `Active` on successful reattach; remap and resume publishing.
- Any → `Idle` on shutdown or explicit detach.

#### 4.12.3 Consumer

Suggested states:
- `Unmapped`: no SHM mapping.
- `Mapped`: SHM mapped; consuming descriptors.
- `Remapping`: awaiting new `ShmPoolAnnounce` after epoch change.
- `Backoff`: retry delay after invalid mapping.

Suggested transitions:
- `Unmapped` → `Mapped` on attach success + successful SHM validation.
- `Mapped` → `Remapping` on producer `ShmLeaseRevoked` or new `ShmPoolAnnounce` with higher epoch.
- `Remapping` → `Mapped` on successful remap and validation.
- `Mapped` → `Backoff` on SHM validation failure (per Wire Spec §15.22) when no fallback is available.
- `Backoff` → `Unmapped` after retry delay; attempt reattach/remap.
- Any → `Unmapped` on `ShmDriverShutdown`; drop in-flight frames and stop.

### 4.13 Driver Termination (Normative)

The driver MAY support an administrative termination mechanism. If implemented, it SHOULD require an authorization token configured out-of-band and MUST reject unauthenticated requests.

If implemented, the driver MUST accept a `ShmDriverShutdownRequest` on the control-plane stream. The request MUST include a `token` that matches the configured shutdown token. The driver MUST reject (ignore) shutdown requests with missing or invalid tokens.

If a shutdown request is accepted, the driver SHOULD transition to `Draining`, emit a final `ShmPoolAnnounce`, and publish `ShmDriverShutdown` after the shutdown timeout expires. The `ShmDriverShutdown.reason` SHOULD reflect the request.

On graceful shutdown, the driver SHOULD publish a `ShmDriverShutdown` notice on the control-plane stream before exiting. Clients MUST treat this notice as immediate lease invalidation, stop using mapped regions, and reattach after restart.

If a shutdown notice is not observed, clients MUST still rely on lease expiry and epoch changes via `ShmPoolAnnounce` to detect driver loss and MUST fail closed on stale mappings.

---

## 5. Exclusive Producer Rule (Normative)

For a given `stream_id`, at most one producer lease MAY be active at any time. The SHM Driver MUST reject any attempt to attach a second producer to the same `stream_id`. Multiple consumers MAY attach concurrently without limit, subject to deployment policy.

---

## 6. Epoch Management (Normative)

The SHM Driver MUST increment `epoch` when a producer attaches to a stream with no existing producer lease, when a producer lease is revoked, expires, or is explicitly detached, when SHM layout parameters change, or when SHM backing files are recreated or reinitialized. Consumers MUST treat any `epoch` change as a hard remapping boundary.

---

## 7. Producer Failure and Recovery (Normative)

If a producer terminates unexpectedly, the SHM Driver SHOULD detect failure via lease keepalive expiration, process liveness detection, or stale activity timestamps. The driver MUST invalidate the producer lease and MUST increment `epoch` before granting a new producer lease for the same stream.

---

## 8. Relationship to ShmPoolAnnounce (Normative)

When the Driver Model is used, the SHM Driver MUST be the entity that emits `ShmPoolAnnounce`. ShmPoolAnnounce serves as a broadcast beacon for discovery, supervision, and liveness monitoring. Attach requests provide an on-demand mechanism to obtain the same authoritative information.

If the Wire Specification requires a `producerId`, the driver MUST populate it with the currently attached producer's `clientId` for the stream (or zero if no producer is attached).

---

## 9. Filesystem Safety and Policy (Normative)

The SHM Driver MUST enforce all filesystem validation rules defined in the Wire Specification, including base directory containment, canonical path resolution, regular-file-only backing, and hugepage enforcement. Clients MUST NOT bypass or weaken these rules.

---

## 10. Failure of the SHM Driver (Normative)

If the SHM Driver terminates, all leases are implicitly invalidated. Clients MUST treat all mapped SHM regions as stale and MUST reattach once the driver restarts. The driver MUST increment `epoch` before reissuing leases.

---

## 11. Stream ID Allocation Ranges (Normative)

To avoid manual Aeron stream ID assignment and prevent collisions across hosts, drivers MAY allocate stream IDs dynamically within configured ranges.

Rules:

- If `policies.allow_dynamic_streams=true`, the driver MUST allocate `stream_id` for new streams from `driver.stream_id_range`. If the range is empty or unset, the driver MUST reject dynamic stream creation with `code=INVALID_PARAMS` (or fail fast at startup).
- For per-consumer descriptor/control streams, the driver SHOULD allocate IDs from `driver.descriptor_stream_id_range` and `driver.control_stream_id_range` when a consumer requests per-consumer streams with `descriptor_stream_id=0` or `control_stream_id=0`. If the relevant range is unset or empty, the driver MUST decline the per-consumer stream request and fall back to shared streams (i.e., return the shared descriptor/control channel and stream IDs).
- If the relevant per-consumer range is exhausted, the driver MUST decline the per-consumer stream request by returning empty channel and null/zero stream ID in `ConsumerConfig` (see Wire Spec §10.1.3). The attach itself MAY still succeed.
- Ranges MUST NOT overlap with the driver control/announce/QoS stream IDs, any statically configured `streams.*.stream_id`, or with each other.
- The driver MUST validate ranges at startup (start ≤ end, non-overlapping). On invalid configuration, the driver MUST fail fast or reject attach requests with `code=INVALID_PARAMS`.
- Deployments SHOULD assign non-overlapping ranges per host (or per driver instance) to prevent cross-host collisions.

These rules are informational for static-only deployments; a driver MAY still operate with fully static IDs.

---

## 12. Rationale (Informative)

The Driver Model mirrors the Aeron Media Driver and Archive architecture, eliminates multi-producer contention in v1.x, centralizes filesystem and security policy, enables safe multi-user deployment, and provides a stable foundation for future extensions.

---

## 13. Relationship Between Specifications (Informative)

This Driver Model specification is normatively dependent on the Wire Specification. The Wire Specification defines encoding and layout semantics; the Driver Model defines ownership, lifecycle, and coordination semantics. Deployments that use an external SHM Driver MUST implement this document to ensure interoperability.

---

## 14. Driver Startup Behavior (Informative)

Deployments MAY configure the driver to delete and recreate existing SHM backing files at startup (for example, in controlled or single-tenant environments). When this mode is enabled, the driver MUST still enforce the epoch rules in §6 and §10 before issuing new leases.

---

## 15. Directory Layout and Namespacing (Informative)

Drivers SHOULD follow the directory layout guidance in the Wire Specification (§15.21a.3). When multiple drivers (embedded or external) can run on the same host, they SHOULD include a stable namespace and driver instance identifier in the path to avoid collisions. Embedded drivers SHOULD use the same `shm_base_dir` layout as external drivers for operational consistency.

---

## 16. Aeron Media Driver Reference (Informative)

This driver model intentionally mirrors the Aeron Media Driver/Client split. The Aeron codebase provides concrete guidance on liveness, identity, and retry behaviors that can inform SHM Driver implementations:

- Liveness and heartbeats: Aeron exposes `client_liveness_timeout` and a driver heartbeat timestamp in the CnC file (see `aeron/aeron-client/src/main/c/aeronc.h`). Clients can read the last `to_driver_heartbeat` timestamp to detect liveness.
- Driver activity checks: Aeron provides `aeron_is_driver_active(dirname, timeout_ms, ...)` to detect whether a driver is running for a directory (see `aeron/aeron-client/src/main/c/aeronc.h`), which can be used as a model for attach retry/backoff strategies.
- Client identity: Aeron clients can query their `client_id` via `aeron_client_id(...)` (see `aeron/aeron-client/src/main/c/aeronc.h`), which suggests keeping client identity stable for the lifetime of a process.
- Driver-enforced timeouts: Aeron can close a client due to driver timeouts (`aeron_is_closed(...)` comment in `aeron/aeron-client/src/main/c/aeronc.h`), which maps to lease expiry behavior in this spec.

These references are informative; this specification defines its own normative behaviors.

---

## 17. Canonical Driver Configuration (Informative)

The driver is typically configured via a TOML file. The following keys are the canonical configuration surface. Implementations MAY add additional keys, but SHOULD preserve these names and defaults for interoperability.

Drivers SHOULD also accept equivalent environment variables, following Aeron’s convention: uppercase the key and replace `.` with `_`. For example, `driver.control_stream_id` maps to `DRIVER_CONTROL_STREAM_ID`. Environment variables MUST override TOML settings when both are provided.

Required keys (unless stated otherwise):

- `driver.instance_id` (string): identifier for logging/diagnostics. Default: `"driver-01"`.
- `driver.control_channel` (string): Aeron channel for control-plane messages. Default: `"aeron:ipc?term-length=4m"`.
- `driver.control_stream_id` (uint32): control-plane stream ID. Default: `1000`.
- `shm.base_dir` (string): root directory for SHM backing files. Default: `"/dev/shm/tensorpool"`.
- `profiles.*` (table): at least one profile must be defined.
- `profiles.<name>.payload_pools` (array): must contain at least one pool entry.
- `streams.*` (table): if `policies.allow_dynamic_streams=false`, each stream MUST be explicitly defined.

Optional keys and defaults:

- `driver.aeron_dir` (string): Aeron media driver directory. Default: Aeron library default.
- `driver.announce_channel` (string): channel for `ShmPoolAnnounce`. Default: `driver.control_channel`.
- `driver.announce_stream_id` (uint32): stream ID for `ShmPoolAnnounce`. Default: `driver.control_stream_id`.
- `driver.qos_channel` (string): channel for QoS messages. Default: `"aeron:ipc?term-length=4m"`.
- `driver.qos_stream_id` (uint32): QoS stream ID. Default: `1200`.
- `driver.stream_id_range` (string or array): inclusive range for dynamically created stream IDs (e.g., `"20000-29999"`). Default: empty (disabled).
- `driver.descriptor_stream_id_range` (string or array): inclusive range for per-consumer descriptor stream IDs. Default: empty (disabled).
- `driver.control_stream_id_range` (string or array): inclusive range for per-consumer control stream IDs. Default: empty (disabled).
- `shm.require_hugepages` (bool): default policy for hugepage-backed SHM when `requireHugepages=UNSPECIFIED`. Default: `false`.
- `shm.page_size_bytes` (uint32): backing page size for validation. Default: `4096`.
- `shm.permissions_mode` (string): POSIX mode for created files. Default: `"660"`.
- `shm.allowed_base_dirs` (array of string): allowlist for URIs. Default: `[shm.base_dir]`.
- `policies.allow_dynamic_streams` (bool): allow on-demand stream creation. Default: `false`.
- `policies.default_profile` (string): profile used for dynamic streams. Default: first defined profile.
- `policies.announce_period_ms` (uint32): `ShmPoolAnnounce` cadence. Default: `1000`.
- `policies.lease_keepalive_interval_ms` (uint32): client keepalive interval. Default: `1000`.
- `policies.lease_expiry_grace_intervals` (uint32): missed keepalives before expiry. Default: `3`.
- `policies.prefault_shm` (bool): prefault/zero SHM regions on create. Default: `true`.
- `policies.mlock_shm` (bool): mlock SHM regions on create; if enabled and `mlock` fails, the driver MUST treat it as a fatal error. `mlock` is per-process; clients SHOULD mlock their own mappings when enabled. On unsupported platforms, implementations SHOULD warn and treat it as a no-op. Default: `false`.
- `policies.epoch_gc_enabled` (bool): enable epoch directory GC. Default: `true`.
- `policies.epoch_gc_keep` (uint32): number of epochs to keep (current + N-1). Default: `2`.
- `policies.epoch_gc_min_age_ns` (uint64): minimum age before deletion. Default: `3 × announce_period`.
- `policies.epoch_gc_on_startup` (bool): run GC at driver startup. Default: `false`.
- `policies.shutdown_timeout_ms` (uint32): drain period before shutdown completes. Default: `2000`.
- `policies.shutdown_token` (string): admin shutdown token. Default: empty (disabled).

Profile fields:

- `profiles.<name>.header_nslots` (uint32): power-of-two slot count. Default: `1024`.
- `profiles.<name>.payload_pools[].pool_id` (uint16): pool identifier (unique per profile).
- `profiles.<name>.payload_pools[].stride_bytes` (uint32): payload slot size in bytes (offset = `slot_index * stride_bytes`).

Stream fields:

- `streams.<name>.stream_id` (uint32): stream identifier.
- `streams.<name>.profile` (string): profile name.

See `docs/examples/driver_camera_example.toml` for a concrete example.

---

## Appendix A. Driver Control-Plane SBE Schema (Normative)

<?xml version="1.0" encoding="UTF-8"?>
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                   package="shm.tensorpool.driver"
                   id="901"
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
      <type name="varData" primitiveType="uint8" length="0"/>
    </composite>

    <enum name="HugepagesPolicy" encodingType="uint8">
      <validValue name="UNSPECIFIED">0</validValue>
      <validValue name="STANDARD">1</validValue>
      <validValue name="HUGEPAGES">2</validValue>
    </enum>

    <enum name="ResponseCode" encodingType="int32">
      <validValue name="OK">0</validValue>
      <validValue name="UNSUPPORTED">1</validValue>
      <validValue name="INVALID_PARAMS">2</validValue>
      <validValue name="REJECTED">3</validValue>
      <validValue name="INTERNAL_ERROR">4</validValue>
    </enum>

    <enum name="Role" encodingType="uint8">
      <validValue name="PRODUCER">1</validValue>
      <validValue name="CONSUMER">2</validValue>
    </enum>

    <enum name="PublishMode" encodingType="uint8">
      <validValue name="REQUIRE_EXISTING">1</validValue>
      <validValue name="EXISTING_OR_CREATE">2</validValue>
    </enum>

    <enum name="LeaseRevokeReason" encodingType="uint8">
      <validValue name="DETACHED">1</validValue>
      <validValue name="EXPIRED">2</validValue>
      <validValue name="REVOKED">3</validValue>
    </enum>

    <enum name="ShutdownReason" encodingType="uint8">
      <validValue name="NORMAL">0</validValue>
      <validValue name="ADMIN">1</validValue>
      <validValue name="ERROR">2</validValue>
    </enum>

    <type name="epoch_t"    primitiveType="uint64"/>
    <type name="version_t"  primitiveType="uint32"/>
    <type name="lease_id_t" primitiveType="uint64"/>

  </types>

  <!-- Driver control-plane messages (normative in the Driver Model specification). -->

  <sbe:message name="ShmAttachRequest" id="1">
    <field name="correlationId"        id="1" type="int64"/>
    <field name="streamId"             id="2" type="uint32"/>
    <field name="clientId"             id="3" type="uint32"/>
    <field name="role"                 id="4" type="Role"/>
    <field name="expectedLayoutVersion" id="5" type="version_t"/>
    <field name="maxDims"              id="6" type="uint8"/>
    <field name="publishMode"          id="7" type="PublishMode" presence="optional" nullValue="255"/>
    <field name="requireHugepages"     id="8" type="HugepagesPolicy"/>
  </sbe:message>

  <sbe:message name="ShmAttachResponse" id="2">
    <field name="correlationId"         id="1" type="int64"/>
    <field name="code"                  id="2" type="ResponseCode"/>
    <field name="leaseId"               id="3" type="lease_id_t" presence="optional" nullValue="18446744073709551615"/>
    <field name="leaseExpiryTimestampNs" id="4" type="uint64" presence="optional" nullValue="18446744073709551615"/>
    <field name="streamId"              id="5" type="uint32" presence="optional" nullValue="4294967295"/>
    <field name="epoch"                 id="6" type="epoch_t" presence="optional" nullValue="18446744073709551615"/>
    <field name="layoutVersion"         id="7" type="version_t" presence="optional" nullValue="4294967295"/>
    <field name="headerNslots"          id="8" type="uint32" presence="optional" nullValue="4294967295"/>
    <field name="headerSlotBytes"       id="9" type="uint16" presence="optional" nullValue="65535"/>
    <field name="maxDims"               id="10" type="uint8" presence="optional" nullValue="255"/>
    <group name="payloadPools"          id="20" dimensionType="groupSizeEncoding">
      <field name="poolId"      id="1" type="uint16"/>
      <field name="poolNslots"  id="2" type="uint32"/>
      <field name="strideBytes" id="3" type="uint32"/>
      <data  name="regionUri"   id="4" type="varAsciiEncoding"/>
    </group>
    <data  name="headerRegionUri"       id="11" type="varAsciiEncoding"/>
    <data  name="errorMessage"           id="30" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="ShmDetachRequest" id="3">
    <field name="correlationId" id="1" type="int64"/>
    <field name="leaseId"       id="2" type="lease_id_t"/>
    <field name="streamId"      id="3" type="uint32"/>
    <field name="clientId"      id="4" type="uint32"/>
    <field name="role"          id="5" type="Role"/>
  </sbe:message>

  <sbe:message name="ShmDetachResponse" id="4">
    <field name="correlationId" id="1" type="int64"/>
    <field name="code"          id="2" type="ResponseCode"/>
    <data  name="errorMessage"  id="3" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="ShmLeaseKeepalive" id="5">
    <field name="leaseId"          id="1" type="lease_id_t"/>
    <field name="streamId"         id="2" type="uint32"/>
    <field name="clientId"         id="3" type="uint32"/>
    <field name="role"             id="4" type="Role"/>
    <field name="clientTimestampNs" id="5" type="uint64"/>
  </sbe:message>

  <sbe:message name="ShmDriverShutdown" id="6">
    <field name="timestampNs" id="1" type="uint64"/>
    <field name="reason"      id="2" type="ShutdownReason"/>
    <data  name="errorMessage" id="3" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="ShmLeaseRevoked" id="7">
    <field name="timestampNs" id="1" type="uint64"/>
    <field name="leaseId"     id="2" type="lease_id_t"/>
    <field name="streamId"    id="3" type="uint32"/>
    <field name="clientId"    id="4" type="uint32"/>
    <field name="role"        id="5" type="Role"/>
    <field name="reason"      id="6" type="LeaseRevokeReason"/>
    <data  name="errorMessage" id="7" type="varAsciiEncoding"/>
  </sbe:message>

  <sbe:message name="ShmDriverShutdownRequest" id="8">
    <field name="correlationId" id="1" type="int64"/>
    <field name="reason"        id="2" type="ShutdownReason"/>
    <data  name="token"         id="3" type="varAsciiEncoding"/>
    <data  name="errorMessage"  id="4" type="varAsciiEncoding"/>
  </sbe:message>

</sbe:messageSchema>
