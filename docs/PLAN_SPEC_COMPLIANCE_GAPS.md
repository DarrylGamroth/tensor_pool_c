# Spec Compliance Gaps (v1.2) — Ranked Plan (Full List)

Authoritative reference: `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md`.

Legend:
- **Severity**: Critical / High / Medium / Low
- **Status**: Pending / In Progress / Done

## Remaining (Ranked)

1. [x] **QoS/metadata cadence enforcement** — Severity: Low — Status: Done  
   **Spec:** §10.4, §15.14  
   **Gap:** Periodic publish cadence not enforced (API only).  
   **Impact:** Liveness telemetry optional.

2. [x] **Producer padding/zero-fill guidance** — Severity: Low — Status: Done  
   **Spec:** §8.2  
   **Gap:** Producer does not explicitly zero reserved fields.  
   **Impact:** Cosmetic; consumers ignore.

3. [ ] **Bridge service compliance** — Severity: Low — Status: Pending  
   **Spec:** §12  
   **Gap:** Bridge not implemented in this repo.  
   **Impact:** Out of scope unless bridge is required.

## Completed (All Higher-Severity Items)

All Critical/High/Medium items are complete as of this revision. See git history for individual changesets.
