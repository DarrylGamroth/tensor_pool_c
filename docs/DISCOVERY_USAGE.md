# Discovery Service Usage

This document describes the standalone discovery service (`tp_discoveryd`).
Authoritative references:
- `docs/SHM_Discovery_Service_Spec_v_1.0.md`
- `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`

## Overview
- Discovery is advisory and MUST NOT be treated as attach authority.
- The driver remains authoritative for `ShmAttach*` and epochs.
- The discovery service indexes `ShmPoolAnnounce` and `DataSourceAnnounce`.
- Tags are optional and may be supplied by the provider (see `tp_discovery_service_set_tags`).

## Running the Service

```
./build/tp_discoveryd config/discovery_example.toml
```

Optional logging:
```
TP_LOG_LEVEL=4 ./build/tp_discoveryd config/discovery_example.toml
```

## Config

See `config/discovery_example.toml`. Key fields:

### [discovery]
- `request_channel` / `request_stream_id`: endpoint for `DiscoveryRequest`.
- `announce_channel` / `announce_stream_id`: `ShmPoolAnnounce` source.
- `metadata_channel` / `metadata_stream_id`: `DataSourceAnnounce` source.
- `driver_instance_id`: authority identifier for responses.
- `driver_control_channel` / `driver_control_stream_id`: driver attach endpoint.
- `announce_period_ms`: used for expiry (3× announce period).
- `max_results`: cap on response size (default 1000).

### [driver]
- `aeron_dir`: Aeron directory (optional).

## Query Tool (JSON)

```
./build/tp_discovery_query /dev/shm/aeron-dgamroth "aeron:ipc?term-length=4m" 9000 \
  "aeron:ipc?term-length=4m" 9001 10000
```

The query tool emits a JSON response and exits after the response arrives.

## Tags

If your deployment maintains tags outside the core wire messages, call
`tp_discovery_service_set_tags` to update the registry entry for a stream.
These tags are returned in DiscoveryResponse and participate in filter matching.
