# Stream ID Conventions

This document provides **recommended** stream ID conventions for AeronTensorPool deployments. The driver and bridge specs remain authoritative for required stream IDs; these are suggestions to reduce collisions and make deployments easier to reason about.

---

## 1. Goals

- Keep control-plane streams stable and well-known.
- Avoid collisions between payload, metadata, discovery, and bridge traffic.
- Reserve ranges for dynamic allocation (per-consumer streams, dynamic streams).

---

## 2. Suggested Stream ID Map

### Pool Streams (per tensor pool)

- Primary pool `stream_id`: `10000`
- Additional pool streams: `10000-10999` (recommended static range)

### Driver Control Plane (local IPC)

- `driver.control_stream_id`: `1000`
- `driver.announce_stream_id`: `1001` (may equal control stream if desired)
- `driver.qos_stream_id`: `1200`

### Producer/Consumer (local IPC)

- Shared descriptor stream: `1100`
- Shared control stream: `1000` (same as driver control in simple IPC deployments)
- Shared QoS stream: `1200`
- Shared metadata stream: `1300`

### Discovery (local IPC or UDP)

- Request stream: `7000`
- Response stream: `7004`
- Announce subscription stream: `7001`
- Metadata subscription stream: `7002`
- Driver control endpoint stream (for discovery metadata): `7003`

### Bridge (UDP)

- Payload chunks: `3000`
- Bridge control (announce/QoS/progress): `3001`
- Bridge metadata forwarding: `3002`

---

## 3. Recommended Ranges

### Static pool stream IDs

- `pool.stream_id` range: `10000-10999`

### Dynamic stream IDs (driver)

- `driver.stream_id_range`: `20000-29999`

### Per-consumer descriptor/control streams (driver)

- `driver.descriptor_stream_id_range`: `31000-31999`
- `driver.control_stream_id_range`: `32000-32999`

### Bridge destination payload streams

- `bridge.dest_stream_id_range`: `50000-59999`

---

## 4. Notes

- These values are suggestions only; deployments may choose different allocations.
- Ensure ranges do not overlap with statically configured stream IDs.
- For UDP deployments across hosts, use distinct stream ID ranges for each direction to avoid feedback loops.
- Consider documenting your chosen stream ID plan in your operational playbook.
