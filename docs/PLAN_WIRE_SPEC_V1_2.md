# Wire Spec v1.2 Implementation Plan

Authoritative reference: `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.

## Key Behavior Change

- `header_index` is derived from `seq` using power-of-two masking: `header_index = (uint32_t)(seq & (header_nslots - 1))`.

## Step-by-step Plan

1) Schemas + Codegen
- Update SBE schemas in `schemas/`:
  - Remove `headerIndex` from `FrameDescriptor`.
  - Rename `FrameProgress.frame_id` → `seq` and remove `headerIndex`.
- Regenerate C code via `../simple-binary-encoding`.

2) Public Types + Adapters
- `include/tensor_pool/tp_types.h`, `include/tensor_pool/tp_producer.h`, `include/tensor_pool/tp_consumer.h`:
  - Remove `header_index` from descriptor/progress view types where exposed.
- `include/tensor_pool/tp_control_adapter.h`, `src/tp_control_adapter.c`:
  - Decode helpers updated to accept `seq` only and compute header index via mask.

3) Producer Path
- `src/tp_producer.c`:
  - Remove `header_index` params from descriptor/progress publish functions.
  - Derive `header_index = seq & (header_nslots - 1)`.
  - Ensure `FrameDescriptor.seq` matches `SlotHeader.seq_commit >> 1`.
- `include/tensor_pool/tp_producer.h`:
  - Update public prototypes to match.

4) Consumer Path
- `src/tp_consumer.c`:
  - In descriptor handler: compute header index from `seq` and `header_nslots`.
  - Update `tp_consumer_read_frame` call sites accordingly.
- `include/tensor_pool/tp_consumer.h`:
  - Update `tp_frame_descriptor_t` layout to match (no header index).

5) Progress / Pollers
- `src/tp_progress_poller.c`, `include/tensor_pool/tp_progress_poller.h`:
  - Update progress view structs and callbacks for `seq` only.
- `src/tp_control_poller.c`, `src/tp_control.c`:
  - Update handler wiring for new progress shape.

6) Tests + Examples
- `tests/test_tp_pollers.c`, `tests/test_tp_rollover.c`, `tests/test_tp_producer_claim.c`:
  - Remove `headerIndex` usage in encoded messages.
  - Use masked `seq` to locate header slots.
- `tools/tp_example_*` and `tools/run_nodriver_examples.sh`:
  - Align with new descriptor/progress schema.

7) Validation
- `src/tp_consumer.c`, `src/tp_slot.c`:
  - Enforce `headerBytes` length == 192 bytes per v1.2.
  - Keep slot header decoding as raw SBE body (no message header).
