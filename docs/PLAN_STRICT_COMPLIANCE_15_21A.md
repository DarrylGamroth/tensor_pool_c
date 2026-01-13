# Strict Compliance Plan: SHM Path Layout + Containment (15.21a)

Authoritative reference: `docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md` §15.21a.

## Progress

- [ ] 1) TOCTOU-safe mapping in `tp_shm_map` (MUST)
  - Add `tp_shm_open_canonical()` helper to:
    - Resolve `realpath()` of announced path.
    - Check containment against canonical allowed base dirs.
    - Open with `O_NOFOLLOW` when available.
    - Re-validate opened FD identity (device+inode) and type (regular file).
    - Fail closed if safe open/identity checks are not possible.
  - Replace current `tp_shm_validate_path()` + `open()` flow.

- [ ] 2) Canonicalize and cache allowed base dirs (MUST)
  - Extend `tp_context_t` to store canonicalized `allowed_paths`.
  - Add `tp_context_finalize_allowed_paths()` and call during client init.
  - Use canonical list in `tp_shm_map` (no per-map base `realpath()` calls).

- [ ] 3) Absolute path validation (MUST)
  - Keep strict absolute-path check in `tp_shm_uri_parse()`; ensure it remains a hard error.

- [ ] 4) Driver canonical layout generation (MUST for producer-side)
  - Add driver config: `shm_base_dir`, `namespace`.
  - Implement canonical layout construction:
    ```
    <shm_base_dir>/tensorpool-${USER}/<namespace>/<stream_id>/<epoch>/
        header.ring
        <pool_id>.pool
    ```
  - Sanitize `${USER}` to prevent traversal.
  - Create directories/files with appropriate permissions.

- [ ] 5) Tooling alignment (SHOULD, REQUIRED if used for provisioning)
  - Update `tp_shm_create` to optionally construct canonical paths from
    `--shm-base-dir`, `--namespace`, `--stream-id`, `--epoch`, `--pool-id`.
  - Keep explicit path mode behind a `--noncanonical` flag.

- [ ] 6) Tests (MUST)
  - Add `tests/test_tp_shm_security.c`:
    - Containment accept/reject.
    - Symlink swap rejection.
    - Non-regular file rejection.
    - Non-absolute path rejection.
    - `O_NOFOLLOW` behavior (best effort).
  - Extend `tests/test_tp_smoke.c` as needed.

- [ ] 7) Documentation updates (MUST)
  - Document driver config keys and canonical layout in
    `docs/SHM_Driver_Model_Spec_v1.0.md` (and any other relevant docs).

