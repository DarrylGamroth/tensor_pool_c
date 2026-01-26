# TP Driver Usage

This document describes how the C driver (`tp_driver`) operates and how to run it.
The authoritative behavior is defined by:
- `docs/SHM_Driver_Model_Spec_v1.0.md`
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/STREAM_ID_CONVENTIONS.md`

## Overview
- The driver is the authoritative control plane for SHM lifecycle, epochs, and `ShmPoolAnnounce`.
- The supervisor policy engine is embedded in `tp_driver` and enabled via `[supervisor]` config.
- Clients MUST attach via the driver when using the driver model.
- Producers and consumers MUST NOT create, truncate, or unlink SHM files directly in driver mode.

## Running the Driver

```
./build/tp_driver config/driver_integration_example.toml
```

`tp_driver` runs the driver loop via the TensorPool agent runner (Aeron-style).

Optional logging:
```
TP_LOG_LEVEL=4 ./build/tp_driver config/driver_integration_example.toml
```

Log levels are integers (0=ERROR .. 4=TRACE).

To override the Aeron directory for embedded Media Driver or shared Aeron clients:

```
export AERON_DIR=/dev/shm/aeron-dgamroth
```

## Embedding the Driver (Library API)

Use the C driver API when you want to embed the driver into another process:

```c
tp_driver_config_t config;
tp_driver_t driver;

tp_driver_config_init(&config);
tp_driver_config_load(&config, "config/driver_integration_example.toml");

tp_driver_init(&driver, &config);
tp_driver_start(&driver);

while (running)
{
    tp_driver_do_work(&driver);
}

tp_driver_close(&driver);
```

To run the driver in an agent loop (Aeron-style), use `tp_driver_agent`:

```c
tp_driver_agent_t agent;
tp_driver_agent_init(&agent, &driver, 1000000ULL);
tp_driver_agent_start(&agent);
// ... later ...
tp_driver_agent_stop(&agent);
tp_driver_agent_close(&agent);
```

## Config File

The driver reads TOML configuration (see `config/driver_integration_example.toml`).
Key sections:

### [driver]
- `control_channel` + `control_stream_id`: control plane for attach/keepalive/detach.
- `announce_channel` + `announce_stream_id`: `ShmPoolAnnounce` broadcasts.
- `qos_channel` + `qos_stream_id`: QoS stream (reserved; not used by driver).
- `stream_id_range`: range for dynamic stream allocation.
- `descriptor_stream_id_range` / `control_stream_id_range`: per-consumer stream ranges.

### [shm]
- `base_dir`: base path for canonical layout.
- `namespace`: per-deployment namespace.
- `require_hugepages`: default hugepages policy when `requireHugepages=UNSPECIFIED`.
- `page_size_bytes`: expected backing page size for validation.
- `permissions_mode`: POSIX mode for created files (octal string, e.g. "660").
- `allowed_base_dirs`: allowlist for SHM URIs (defaults to `base_dir`).

### [policies]
- `allow_dynamic_streams`: allow dynamic stream creation.
- `default_profile`: profile used for dynamic streams.
- `announce_period_ms`: `ShmPoolAnnounce` cadence.
- `lease_keepalive_interval_ms`: keepalive cadence.
- `lease_expiry_grace_intervals`: missed keepalives before expiry.
- `node_id_reuse_cooldown_ms`: cooldown before reusing a released `nodeId`.
- `prefault_shm`: prefault/zero SHM regions on create.
- `mlock_shm`: lock SHM pages in RAM (fatal on failure if enabled).
- `epoch_gc_enabled`: enable old epoch cleanup.
- `epoch_gc_keep`: number of epochs to keep (current + N-1).
- `epoch_gc_min_age_ns`: minimum age before deletion.
- `epoch_gc_on_startup`: run GC at startup.

### [profiles.<name>]
- `header_nslots`: power-of-two slot count for header ring.
- `payload_pools`: list of `{ pool_id, stride_bytes }`.
- `stride_bytes` MUST be a multiple of 64 bytes (per wire spec).

### [streams.<name>]
- `stream_id`: static stream ID.
- `profile`: profile name.

### [supervisor]
- `control_channel` / `control_stream_id`: control-plane stream to receive `ConsumerHello` and send `ConsumerConfig`.
- `announce_channel` / `announce_stream_id`: `ShmPoolAnnounce` subscription (defaults to `[driver]` values).
- `metadata_channel` / `metadata_stream_id`: `DataSourceAnnounce`/`DataSourceMeta` subscription (defaults to `[driver]` values).
- `qos_channel` / `qos_stream_id`: `QosConsumer`/`QosProducer` subscription (defaults to `[driver]` values).
- `consumer_capacity`: registry capacity for tracking consumers.
- `consumer_stale_ms`: stale timeout for consumer registry sweeps.
- `per_consumer_enabled`: enable per-consumer stream assignment.
- `per_consumer_descriptor_channel` / `per_consumer_descriptor_base` / `per_consumer_descriptor_range`: assignment policy.
- `per_consumer_control_channel` / `per_consumer_control_base` / `per_consumer_control_range`: assignment policy.
- `force_no_shm`: force `ConsumerConfig.use_shm=0`.
- `force_mode`: override consumer mode (0 = no override).
- `payload_fallback_uri`: optional fallback URI for non-SHM consumers.

## Discovery Service (Optional)

The discovery service can run as a standalone daemon or embedded service:

```
./build/tp_discoveryd config/discovery_integration_example.toml
```

Query discovery results with:

```
./build/tp_discovery_query /dev/shm/aeron-dgamroth "aeron:ipc?term-length=4m" 9000
```

Embedding:

```c
tp_discovery_service_config_t config;
tp_discovery_service_t service;

tp_discovery_service_config_init(&config);
tp_discovery_service_config_load(&config, "config/discovery_integration_example.toml");
tp_discovery_service_init(&service, &config);
tp_discovery_service_start(&service);

while (running)
{
    tp_discovery_service_do_work(&service);
}
```

## Canonical SHM Layout

The driver MUST create backing files using the canonical layout:

```
<shm_base_dir>/tensorpool-${USER}/<namespace>/<stream_id>/<epoch>/
    header.ring
    <pool_id>.pool
```

Clients map SHM via URIs returned in `ShmAttachResponse` and `ShmPoolAnnounce`:

```
shm:file?path=/dev/shm/tensorpool-USER/default/10000/123456/header.ring|require_hugepages=false
```

## Attach / Lease / Epoch Behavior

- `ShmAttachRequest` is accepted or rejected based on policy, layout version, and exclusivity rules.
- The driver MUST enforce exclusive producer leases per `stream_id`.
- The driver MUST create SHM regions on demand when `publishMode=EXISTING_OR_CREATE`.
- On producer attach, detach, revoke, or expiry the driver MUST increment `epoch`.
- Lease revocations are reported with `ShmLeaseRevoked` before any epoch bump announce.
- The driver assigns `nodeId` per lease when `desiredNodeId` is not provided. Node IDs are unique among active leases and obey reuse cooldown.

## Per-Consumer Streams (Supervisor)

When `per_consumer_enabled=true`, the supervisor assigns unique descriptor/control streams to each consumer via `ConsumerConfig`.
Set the base/range in `[supervisor]` and ensure they do not overlap with shared streams.

Example (per-consumer enable):

```
[supervisor]
per_consumer_enabled = true
per_consumer_descriptor_base = 31000
per_consumer_descriptor_range = 100
per_consumer_control_base = 32000
per_consumer_control_range = 100
```

Consumers remain subscribed to the shared control stream for non-FrameProgress control-plane messages; per-consumer control streams carry FrameProgress only.

## Dynamic Streams

Allow dynamic stream creation by setting:

```
[policies]
allow_dynamic_streams = true
```

Clients then use `publishMode=EXISTING_OR_CREATE` in `ShmAttachRequest` to create new streams within `stream_id_range`.

## Example: Driver Mode Exchange

Run the driver, then a consumer and producer:

```
./build/tp_driver config/driver_integration_example.toml
./build/tp_example_consumer_driver /dev/shm/aeron-dgamroth "aeron:ipc?term-length=4m" 10000 2 1
./build/tp_example_producer_driver /dev/shm/aeron-dgamroth "aeron:ipc?term-length=4m" 10000 1 16
```

The consumer should log frames with `seq` values 0..15.

## Tools

- `tp_control_listen`: Inspect control/metadata/qos streams with text or JSON output.
- `tp_descriptor_listen`: Inspect descriptor stream traffic.
- `tp_shm_inspect`: Inspect SHM superblocks and headers for a given region.
- `tp_shm_create`: Create a canonical SHM layout for no-driver testing.
