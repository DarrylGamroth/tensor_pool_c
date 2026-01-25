# Supervisor Usage

This document describes the standalone supervisor (`tp_supervisord`) that handles
`ConsumerHello` and emits `ConsumerConfig` per the wire spec.

Authoritative references:
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`
- `docs/STREAM_ID_CONVENTIONS.md`

## Running the Supervisor

```
./build/tp_supervisord -c config/supervisor_example.toml
```

Optional logging:
```
TP_LOG_LEVEL=4 ./build/tp_supervisord -c config/supervisor_example.toml
```

Optional stats logging (milliseconds):
```
TP_SUPERVISOR_STATS_MS=2000 ./build/tp_supervisord -c config/supervisor_example.toml
```

## Config

See `config/supervisor_example.toml`. Key fields:

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
