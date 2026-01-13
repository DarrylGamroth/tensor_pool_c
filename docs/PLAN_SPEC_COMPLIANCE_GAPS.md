# Spec Compliance Gaps (v1.2) — Ranked Plan (Full List)

Authoritative reference: `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.

Legend:
- **Severity**: Critical / High / Medium / Low
- **Status**: Pending / In Progress / Done

## Critical

- [x] **ShmPoolAnnounce handling (consumer path)** — Status: Done  
  **Spec:** §10.1.1, §15.18, §15.21  
  **Gap:** No decoding/handling of `ShmPoolAnnounce`; no epoch/freshness gating or state machine.  
  **Impact:** Core protocol state missing.

- [x] **Epoch mismatch drop on FrameDescriptor** — Status: Done  
  **Spec:** §10.2.1  
  **Gap:** Descriptor handler does not drop mismatched epochs.  
  **Impact:** Consumers may accept stale frames.

- [x] **Strict symlink-safe open across full path** — Status: Done  
  **Spec:** §15.21a.5 step 6  
  **Gap:** `O_NOFOLLOW` only protects final component; intermediate symlinks remain possible.  
  **Impact:** TOCTOU hardening incomplete.

## High

- [x] **Stride alignment and power-of-two validation** — Status: Done  
  **Spec:** §15.22  
  **Gap:** `stride_bytes` power-of-two and page/hugepage alignment not enforced.  
  **Impact:** Invalid regions may be mapped.

- [x] **FrameProgress validation rules** — Status: Done  
  **Spec:** §10.2.2  
  **Gap:** No monotonic or bounds checks for `payload_bytes_filled`.  
  **Impact:** Consumers may accept invalid progress updates.

- [x] **Per-consumer descriptor rate cap (`max_rate_hz`)** — Status: Done  
  **Spec:** §5  
  **Gap:** `max_rate_hz` stored but not enforced.  
  **Impact:** Consumers can be overwhelmed.

- [x] **Superblock validation vs ShmPoolAnnounce** — Status: Done  
  **Spec:** §15.1, §15.5  
  **Gap:** No cross-validation between superblock and announce fields.  
  **Impact:** Mismatched layouts may be consumed.

- [x] **FrameDescriptor publish ordering / epoch checks** — Status: Done  
  **Spec:** §10.2.1, §8.3  
  **Gap:** No explicit enforcement that descriptors are published only after commit + visibility.  
  **Impact:** Consumers may read uncommitted payloads on weakly-ordered systems.

## Medium

- [x] **ShmPoolAnnounce freshness rules** — Status: Done  
  **Spec:** §10.1.1, §15.2, §15.18  
  **Gap:** No freshness window or join-time logic.  
  **Impact:** Stale announces may be accepted.

- [x] **Metadata blob support** — Status: Done  
  **Spec:** §10.3.3, §15.9  
  **Gap:** No chunked blob handling beyond `DataSourceMeta`.  
  **Impact:** Large metadata unsupported.

- [x] **Consumer state machine implementation** — Status: Done  
  **Spec:** §15.12, §15.21  
  **Gap:** No explicit M0/M1 mapping state tracking.  
  **Impact:** Harder to enforce remap boundaries.

- [x] **Drop accounting** — Status: Done  
  **Spec:** §15.4  
  **Gap:** No drop metrics or accounting path.  
  **Impact:** QoS observability incomplete.

- [x] **Timebase / clock-domain handling** — Status: Done  
  **Spec:** §15.7, §10.1.1  
  **Gap:** No validation of announce clock domain and join-time logic.  
  **Impact:** Stale data acceptance under replay.

- [x] **Enum registry versioning / unknown enum handling** — Status: Done  
  **Spec:** §15.8  
  **Gap:** Enums validated, but no explicit registry/versioning guard.  
  **Impact:** Future compatibility risk.

- [x] **File-backed SHM region guidance** — Status: Done  
  **Spec:** §15.16a  
  **Gap:** No fsync/prefault/mlock policy enforcement.  
  **Impact:** Durability/perf assumptions may break.

- [x] **ControlResponse error codes (control-plane)** — Status: Done  
  **Spec:** §15.17  
  **Gap:** No full ControlResponse error surface on wire control path.  
  **Impact:** Limited error reporting.

- [x] **Compatibility matrix enforcement** — Status: Done  
  **Spec:** §15.20  
  **Gap:** No runtime enforcement against compatibility matrix.  
  **Impact:** Incompatible versions may be accepted.

## Low

- [ ] **QoS/metadata cadence enforcement** — Status: Pending  
  **Spec:** §10.4, §15.14  
  **Gap:** Periodic publish cadence not enforced (API only).  
  **Impact:** Liveness telemetry optional.

- [ ] **Producer padding/zero-fill guidance** — Status: Pending  
  **Spec:** §8.2  
  **Gap:** Producer does not explicitly zero reserved fields.  
  **Impact:** Cosmetic; consumers ignore.

- [ ] **Bridge service compliance** — Status: Pending  
  **Spec:** §12  
  **Gap:** Bridge not implemented in this repo.  
  **Impact:** Out of scope unless bridge is required.
