# Interop Audit Note

## Summary
We marked wire-spec compliance too early. The C implementation lacked several MUST-level requirements,
so the compliance matrix overstated conformance and interop failed.

## Impact
- C producer published FrameDescriptors, but consumers that relied on ShmPoolAnnounce never mapped SHM.
- Optional-field encoding mismatches and missing progress validation created silent interoperability gaps.

## Missing Requirements (Spec v1.2)
- ShmPoolAnnounce emission in no-driver mode was missing.
- Optional `timestampNs` and `metaVersion` were encoded as 0 instead of nullValue when absent.
- FrameProgress stream_id/epoch were not surfaced or validated in poller/consumer validation.
- ConsumerHello requests with non-empty channel + stream_id=0 were not rejected.

## Fixes Applied
- Added ShmPoolAnnounce encode/publish in no-driver mode.
- Enforced nullValue encoding for optional primitives (FrameDescriptor, QosProducer watermark).
- Added stream_id/epoch to FrameProgress and validated them in poller/consumer.
- Rejected invalid ConsumerHello per-consumer stream requests.
- Updated the compliance matrix to match actual behavior.

## Prevention
- Maintain a requirements-to-tests checklist with explicit coverage for every MUST/SHOULD.
- Add/maintain a config-matrix integration test suite across all spec permutations.
- Treat any requirement without coverage as a compliance gap until verified.
