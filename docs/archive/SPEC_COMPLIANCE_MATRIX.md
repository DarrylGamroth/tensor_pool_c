# Spec Compliance Matrix

Status key:
- Implemented: functionality present in code.
- Partial: implemented in part (e.g., encoder only).
- Not implemented: no support yet.

| Feature | Spec section | Status | Notes / refs |
| --- | --- | --- | --- |
| SHM URI parsing (`shm:file?path=...|require_hugepages=...`) | 4.1, 15.22 | Implemented | Validates scheme, required path, rejects unknown params, absolute path enforced. `src/tp_uri.c`, `src/tp_shm.c` |
| Path containment allowlist | 15.21a, 15.22 | Implemented | realpath + allowlist prefix check. `src/tp_shm.c` |
| Superblock validation (magic/layout/epoch/slot bytes) | 7 | Implemented | Validates magic, layout version, epoch, stream id, region type, slot bytes, stride. `src/tp_shm.c` |
| Header slot layout / SlotHeader decode | 8 | Implemented | Parses SlotHeader and headerBytes length. `src/tp_slot.c` |
| TensorHeader decode + validation | 8.2 | Implemented | Dtype/major order/dims/strides/progress validation. `src/tp_tensor.c` |
| Seqlock commit protocol (writer/reader) | 8.3 | Implemented | Acquire/release, LSB commit bit, seq checks. `include/tensor_pool/tp_seqlock.h`, `src/tp_producer.c`, `src/tp_consumer.c` |
| Payload pool mapping/stride rules | 9 | Implemented | Pool stride check + slot mapping enforced. `src/tp_producer.c`, `src/tp_consumer.c` |
| FrameDescriptor publish | 10.2.1 | Implemented | SBE encode + offer. `src/tp_producer.c` |
| FrameProgress publish | 10.2.2 | Implemented | `tp_producer_publish_progress`. `src/tp_producer.c` |
| QoS Producer/Consumer | 10.3 | Implemented | Encoding helpers. `src/tp_qos.c` |
| Control-plane ConsumerHello | 10.1.2 | Implemented | Encoder + decoder + poller. `src/tp_control.c`, `src/tp_control_adapter.c` |
| Control-plane ConsumerConfig | 10.1.3 | Implemented | Encoder + decoder + poller. `src/tp_control.c`, `src/tp_control_adapter.c` |
| DataSourceAnnounce / DataSourceMeta | 10.4 | Implemented | Encoder + decoder + poller. `src/tp_control.c`, `src/tp_control_adapter.c` |
| Per-consumer descriptor/control streams | 5 | Implemented | Request/assign + lifecycle + per-consumer publish helpers. `src/tp_consumer_manager.c`, `src/tp_consumer_registry.c` |
| Progress throttling / policy hints | 10.1.2 | Implemented | Aggregation + emit helper logic. `src/tp_consumer_registry.c`, `src/tp_consumer_manager.c` |
| Driver attach/keepalive/detach (client) | Driver Spec 4 | Implemented | Attach validation, events, keepalive/detach. `src/tp_driver_client.c` |
| Driver-side behavior (server) | Driver Spec 3–7 | Not implemented | Client-only in this repo. |
| Discovery client | Discovery Spec 5 | Implemented | Request/response decode + tags + validation + cleanup. `src/tp_discovery_client.c` |
| Aeron-style error handling | General | Implemented | Uses Aeron error stack via `tp_error.h`. |
| Logging API | General | Implemented | `tp_log_t` with levels + callback. `src/tp_log.c` |
| Examples (driver model) | Docs Phase 10 | Implemented | `tools/tp_example_producer_driver.c`, `tools/tp_example_consumer_driver.c` |
| Tests (core validation) | Docs Phase 9 | Implemented | URI, seqlock, superblock, tensor header. `tests/test_tp_smoke.c` |
