# Wire Spec v1.2 Implementation Plan

Authoritative reference: `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.

## Key Behavior Change

- `header_index` is derived from `seq` using power-of-two masking: `header_index = (uint32_t)(seq & (header_nslots - 1))`.

## Progress

- [x] 1) Schemas + Codegen
  - Updated `schemas/wire-schema.xml`:
    - Removed `headerIndex` from `FrameDescriptor`.
    - Renamed `FrameProgress.frame_id` → `seq` and removed `headerIndex`.
    - Added `traceId` to `FrameDescriptor` per v1.2.
  - Regenerated C code via `../simple-binary-encoding`.
- [x] 2) Public Types + Adapters
  - `include/tensor_pool/tp_producer.h`, `include/tensor_pool/tp_consumer.h`:
    - Removed `header_index` from descriptor/progress view types where exposed.
    - Added `trace_id` to descriptor metadata flow.
- [x] 3) Producer Path
  - `src/tp_producer.c`:
    - Removed `header_index` params from descriptor/progress publish functions.
    - Derive `header_index = seq & (header_nslots - 1)`.
    - Encode `traceId` in `FrameDescriptor`.
  - `include/tensor_pool/tp_producer.h`:
    - Updated public metadata to carry `trace_id`.
- [x] 4) Consumer Path
  - `src/tp_consumer.c`:
    - Descriptor handler now emits `trace_id`.
    - `tp_consumer_read_frame` derives `header_index` from `seq`.
  - `include/tensor_pool/tp_consumer.h`:
    - `tp_frame_descriptor_t` matches v1.2 (no header index).
- [x] 5) Progress / Pollers
  - `src/tp_progress_poller.c`, `tools/tp_control_listen.c`:
    - Updated for `FrameProgress.seq` only.
- [x] 6) Tests + Examples
  - `tests/test_tp_pollers.c`, `tests/test_tp_rollover.c`:
    - Updated to new descriptor/progress schema.
  - `tools/tp_example_*`:
    - Updated `tp_consumer_read_frame` usage.
- [x] 7) Validation
  - Existing slot header length enforcement remains unchanged (v1.2 compatible).
