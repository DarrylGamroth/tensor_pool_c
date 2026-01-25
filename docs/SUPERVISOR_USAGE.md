# Supervisor Usage

This document describes the supervisor logic that is embedded in `tp_driver`.
It handles `ConsumerHello` and emits `ConsumerConfig` per the wire spec.

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/STREAM_ID_CONVENTIONS.md`

## Running the Supervisor

The supervisor runs inside `tp_driver` when `[supervisor]` is present in the driver
config. See the example in `config/driver_integration_example.toml`.

## Config

See the `[supervisor]` section in your driver config. Key fields:

### [supervisor]
- `control_channel` / `control_stream_id`: control-plane stream to receive `ConsumerHello` and send `ConsumerConfig`.
- `announce_channel` / `announce_stream_id`: `ShmPoolAnnounce` subscription.
- `metadata_channel` / `metadata_stream_id`: `DataSourceAnnounce`/`DataSourceMeta` subscription.
- `qos_channel` / `qos_stream_id`: `QosConsumer`/`QosProducer` subscription.
- `consumer_capacity`: registry capacity for tracking consumers.
- `consumer_stale_ms`: stale timeout for consumer registry sweeps.
- `per_consumer_enabled`: enable per-consumer stream assignment.
- `per_consumer_descriptor_channel` / `per_consumer_descriptor_base` / `per_consumer_descriptor_range`: assignment policy.
- `per_consumer_control_channel` / `per_consumer_control_base` / `per_consumer_control_range`: assignment policy.
- `force_no_shm`: force `ConsumerConfig.use_shm=0`.
- `force_mode`: override consumer mode (0 = no override).
- `payload_fallback_uri`: optional fallback URI for non-SHM consumers.
