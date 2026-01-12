# Implementation Guide (Wire v1.1 + Driver v1.0)

This guide maps the wire-level spec and driver model spec into concrete implementation steps.
It is implementation-oriented and references the specs for normative rules.

References:
- Wire spec: `docs/SHM_Tensor_Pool_Wire_Spec_v1.1.md`
- Driver model: `docs/SHM_Driver_Model_Spec_v1.0.md`

Note: Initial driver and client implementations are in Julia. A C client is planned next, so API and protocol decisions should remain C-friendly. The driver can remain Julia-only.

Code organization: separate modules for shared (wire + shm utilities), client (attach/keepalive/detach proxies and pollers), and driver (SHM lifecycle, leases, announces).

## 1. Scope and Roles

Wire-level roles:
- Producer: owns SHM regions and publishes descriptors/progress/QoS.
- Consumer: maps SHM regions and reads frames based on descriptors.
- Supervisor: aggregates QoS, issues ConsumerConfig.
- Bridge/RateLimiter: optional scaffolds.

Driver-model roles:
- SHM Driver: authoritative owner of SHM lifecycle, epochs, and URIs.
- Producer Client: attaches to driver and writes into driver-owned SHM.
- Consumer Client: attaches to driver and maps driver-owned SHM.

The wire spec is always in force. The driver model is optional and used when the deployment chooses a shared SHM driver.

## 2. Wire Spec Implementation Summary

SHM layout:
- Superblock size: 64 bytes, little-endian only (Wire 6-8).
- Header slots: 256 bytes per slot.
- Payload pools: fixed stride, same nslots as header ring.

Commit protocol:
- commit_word = (seq << 1) | 0 for WRITING.
- commit_word = (seq << 1) | 1 for COMMITTED.
- Use acquire/release semantics and seqlock read rules.

Producer flow (Wire 15.19):
1) Write commit_word WRITING.
2) Write payload bytes and ensure visibility.
3) Write header fields (except commit_word).
4) Write commit_word COMMITTED (release).
5) Publish FrameDescriptor; optionally FrameProgress.

Producer startup:
- Aeron `try_claim`/`offer` returns NOT_CONNECTED when no subscribers are present; this is expected and retryable as consumers come and go.
- `publication_is_connected`/`channel_status` are observability hints only; do not gate correctness on them.

Consumer flow (Wire 15.19):
1) Read commit_word (acquire); drop if LSB=0.
2) Read header and payload.
3) Re-read commit_word (acquire); drop if changed or LSB=0.
4) Accept only if (commit_word >> 1) == descriptor.seq.

Implementation notes:
- Control/QoS/metadata channels can carry mixed message families; guard on `MessageHeader.schemaId` (or `DriverMessageHeader.schemaId`) before decoding.
- Regenerate codecs after schema changes with `julia --project -e 'using Pkg; Pkg.build(\"AeronTensorPool\")'` to avoid schema/version mismatches.
- Embedded TensorHeader decode should use `TensorHeaderMsg.wrap!` (headerBytes already includes its MessageHeader). Use `wrap_and_apply_header!` only on the write path.

Control plane:
- ShmPoolAnnounce informs mmap and validation.
- ConsumerHello advertises capabilities (progress hints).
- ConsumerConfig provides mode and fallback.
- QosProducer/QosConsumer report drops and liveness.

Per-consumer streams (optional):
- Consumers may request per-consumer descriptor/control streams in ConsumerHello.
- Producers may assign them via ConsumerConfig; if declined, consumers remain on shared streams.
- Drivers may allocate per-consumer stream IDs when ConsumerHello supplies a channel with stream_id=0; the driver returns ConsumerConfig and producers should honor it.
- Per-consumer descriptor streams may be rate-limited with max_rate_hz; shared descriptor streams are never gated.
- Producers MUST close per-consumer publications when the consumer is stale (no ConsumerHello/QoS for 3–5× cadence).

## 3. Driver Model Integration Summary

Driver responsibilities (Driver 3-4):
- Create, initialize, and own SHM backing files.
- Maintain epochs and layout_version.
- Enforce exclusive producer per stream.
- Publish ShmPoolAnnounce.
 - Apply configured profiles/policies (see Driver Spec §16 and `docs/examples/driver_camera_example.toml`).

Attach protocol (Driver 4.2-4.4):
- Client sends ShmAttachRequest.
- Driver replies ShmAttachResponse with URIs, epoch, layout_version, lease_id.
- Clients validate required fields and fail closed on protocol errors.

Lease lifecycle (Driver 4.7-4.9):
- Keepalives extend lease; expiry or revoke forces detach.
- Driver publishes ShmLeaseRevoked and bumps epoch on producer lease loss.
- Clients stop using mappings and reattach on revoke/expiry.

## 4. Deployment Modes

Driver-first model (recommended):
- SHM Driver owns allocation and announces URIs.
- Producer/consumer attach with leases and must not create SHM files.
- Clients do not use TOML or environment variables; they configure only connection info via direct API parameters (Aeron dir/URI, control stream, client_id, role).
  - Driver MAY accept environment overrides per the driver spec; this is driver-only, not client.
- Client responses should be polled (Aeron-style), not callback-based.
- Client API should mirror Aeron/Aeron Archive patterns (proxy/adapter style) for attach/keepalive/detach.
  - Suggested types (task-based): `AttachRequestProxy`, `KeepaliveProxy`, `DetachRequestProxy`, `DriverResponseAdapter`.

## 5. Path Layout and Containment (Wire 15.21a)

Consumers MUST map only the paths announced by the producer/driver.
Implement path containment checks before mmap:
- Require absolute paths.
- Canonicalize to realpath.
- Ensure path is within allowed_base_dirs.
- Reject non-regular files and unknown schemes.

Default layout (informative):
- `<shm_base_dir>/<namespace>/<producer_instance_id>/epoch-<E>/`
- `header.ring` and `payload-<pool_id>.pool`

## 6. Aeron Usage

Control plane:
- Use try_claim to write small messages and commit.
- Prefer separate stream IDs for control/descriptor/qos/metadata.

Subscriptions:
- Use FragmentAssembler per subscription.
- Decode SBE messages in-place and reuse decoders.

### QoS Monitoring (Optional)

Use the QoS monitor to subscribe to the QoS stream and maintain last-seen snapshots.

```julia
using AeronTensorPool
using Aeron

Aeron.Context() do ctx
    Aeron.Client(ctx) do client
        consumer_cfg = load_consumer_config("config/defaults.toml")
        monitor = QosMonitor(consumer_cfg; client = client)

        poll_qos!(monitor)

        prod = producer_qos(monitor, UInt32(1))
        cons = consumer_qos(monitor, UInt32(1))
        prod === nothing || @info "QosProducer" current_seq=prod.current_seq
        cons === nothing || @info "QosConsumer" drops_late=cons.drops_late

        close(monitor)
    end
end
```

## 7. Timers and Work Loops

Use polled timers to manage periodic work:
- Announce cadence (1 Hz recommended).
- QoS cadence (1 Hz recommended).
- Driver keepalive cadence (default 1 Hz).

Fetch the clock once per duty cycle and pass now_ns through the work loop.

## 8. Type Stability and Allocation-Free Hot Paths

After initialization:
- Preallocate buffers for SBE messages and SHM reads.
- Avoid VarData/VarAscii parsing in hot loops.
- Reuse decoders for SHM headers and descriptors.
- Keep hot-path functions concrete and avoid dynamic dispatch.

## 9. Tooling and Tests

Tooling:
- CLI utilities for attach/detach/keepalive and control messages.
- Scripts to run producer/consumer/supervisor/driver.
- Example driver config: `docs/examples/driver_camera_example.toml` and `docs/EXAMPLE_Camera_Pipeline.md`.
  - `scripts/tp_tool.jl` supports driver attach/detach/keepalive, URI validation, and superblock/header reads.
  - `scripts/run_all_driver.sh` launches driver + supervisor + producer + consumer.
  - `scripts/run_driver_smoke.jl` runs a minimal driver attach/keepalive/detach flow.

Testing:
- Unit tests for SHM validation, seqlock behavior, URI parsing.
- Integration tests with embedded media driver.
- Allocation tests under load for producer and consumer loops.

## 10. References for Implementers

Specs:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.1.md`
- `docs/SHM_Driver_Model_Spec_v1.0.md`

Project docs:
- `docs/OPERATIONAL_PLAYBOOK.md`
- `docs/INTEGRATION_EXAMPLES.md`
- `docs/USE_CASES.md`
