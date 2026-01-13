# Spec Compliance Gaps (v1.2) — Ranked Plan

Authoritative reference: `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.

Legend:
- **Severity**: Critical / High / Medium / Low
- **Status**: Pending / In Progress / Done

## Critical

- [ ] **ShmPoolAnnounce handling (consumer path)** — Status: Pending  
  **Spec:** §10.1.1, §15.18, §15.21  
  **Gap:** No decoding/handling of `ShmPoolAnnounce`; no epoch/freshness gating or state machine.  
  **Impact:** Consumers cannot remap or validate regions; core protocol state is missing.  
  **Targets:** `src/tp_control_adapter.c`, `src/tp_control_poller.c`, `src/tp_consumer.c`, add new state storage.

- [ ] **Epoch mismatch drop on FrameDescriptor** — Status: Pending  
  **Spec:** §10.2.1  
  **Gap:** Descriptor handler does not drop mismatched epochs.  
  **Impact:** Consumers may accept frames from stale mappings.  
  **Targets:** `src/tp_consumer.c`.

- [ ] **Strict symlink-safe open across full path** — Status: Pending  
  **Spec:** §15.21a.5 step 6  
  **Gap:** `O_NOFOLLOW` only protects final component; intermediate symlinks remain possible.  
  **Impact:** TOCTOU hardening incomplete.  
  **Targets:** `src/tp_shm.c` (add `openat2` with `RESOLVE_NO_SYMLINKS`, or component-by-component `openat` fallback).

## High

- [ ] **Stride alignment and power-of-two validation** — Status: Pending  
  **Spec:** §15.22  
  **Gap:** `stride_bytes` power-of-two and page-size alignment are not enforced.  
  **Impact:** Invalid regions may be mapped.  
  **Targets:** `src/tp_shm.c`.

- [ ] **FrameProgress validation rules** — Status: Pending  
  **Spec:** §10.2.2  
  **Gap:** No monotonic or bounds checks for `payload_bytes_filled`.  
  **Impact:** Consumers may accept invalid progress updates.  
  **Targets:** `src/tp_progress_poller.c` or consumer-side validation.

- [ ] **Per-consumer descriptor rate cap (`max_rate_hz`)** — Status: Pending  
  **Spec:** §5  
  **Gap:** `max_rate_hz` stored but not enforced.  
  **Impact:** Producer may overwhelm consumers requesting throttling.  
  **Targets:** `src/tp_consumer_manager.c`.

## Medium

- [ ] **Metadata blob support** — Status: Pending  
  **Spec:** §10.3.3, §15.9  
  **Gap:** No chunked blob handling beyond `DataSourceMeta`.  
  **Impact:** Large metadata unsupported.  
  **Targets:** new blob encoder/decoder, control adapter/poller.

- [ ] **ShmPoolAnnounce freshness rules** — Status: Pending  
  **Spec:** §10.1.1, §15.2, §15.18  
  **Gap:** No announce freshness window or join-time logic.  
  **Impact:** Stale announces may be accepted.  
  **Targets:** consumer mapping state, announce tracking.

- [ ] **Superblock validation vs announce** — Status: Pending  
  **Spec:** §15.1, §15.5  
  **Gap:** No cross-validation between superblock fields and `ShmPoolAnnounce`.  
  **Impact:** Mismatched layouts could be consumed.  
  **Targets:** mapping/attach flow.

## Low

- [ ] **QoS/metadata cadence enforcement** — Status: Pending  
  **Spec:** §10.4, §15.14  
  **Gap:** Periodic publishing cadence not enforced; only APIs provided.  
  **Impact:** Liveness/telemetry may be absent unless user drives it.  
  **Targets:** client conductor or producer helper utilities.

- [ ] **Consumer state machine instrumentation** — Status: Pending  
  **Spec:** §15.12  
  **Gap:** No explicit M0/M1 state machine tracking.  
  **Impact:** Harder to reason about mapping behavior; not strictly required if logic is implemented.  
  **Targets:** consumer mapping subsystem.

