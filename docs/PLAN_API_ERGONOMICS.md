# API Ergonomics Plan (Aeron-style)

This plan upgrades the public API to match Aeron’s async + poll model and
removes "helper" terminology. The goal is an ergonomic, user-facing API with
clear lifecycle semantics and consistent naming.

## Principles
- Public API uses Aeron-style async add + poll patterns.
- Poll functions return `0` (not complete), `1` (complete), `-1` (error).
- Blocking convenience calls are optional wrappers with `0/-1` return semantics.
- No Aeron types in public headers.
- Naming is consistent and role-symmetric (producer/consumer).

## Phase 1: API Surface Redesign
- Replace "helper" names with public API names:
  - `tp_driver_attach_info_to_producer_config` → `tp_driver_attach_producer_config`.
  - `tp_driver_attach_info_to_consumer_config` → `tp_driver_attach_consumer_config`.
  - `tp_driver_detach_if_active` → `tp_driver_detach_active`.
- Add explicit async + poll variants for driver attach:
  - `tp_producer_attach_driver_async`, `tp_producer_attach_driver_poll`.
  - `tp_consumer_attach_driver_async`, `tp_consumer_attach_driver_poll`.
- Keep blocking `tp_producer_attach_driver` and `tp_consumer_attach_driver` as optional wrappers.

## Phase 2: Naming & Consistency Pass
- Standardize naming across public API:
  - `*_context_*` for lifecycle config, `*_config_*` for per-attach inputs.
  - `require_*` for validation requirements, `use_*` for runtime toggles.
  - `*_offer`, `*_try_claim`, `*_commit`, `*_poll` aligned with Aeron.
- Align error messages and return codes with Aeron conventions.

## Phase 3: Migration & Examples
- Update examples to use the new public API names and async + poll patterns.
- Update `docs/C_CLIENT_API_USAGE.md` to highlight the async model first.
- Update `docs/AERON_LIKE_API_PROPOSAL.md` and `docs/HELPER_API_DRAFT.md` (rename to `API_DRAFT.md`).

## Phase 4: Tests
- Add tests for new async + poll functions (success, timeout, error paths).
- Add tests for blocking convenience wrappers.

## Phase 5: Breaking Changes
- No backwards compatibility: remove or rename old APIs directly.
- Provide a migration guide with old→new mapping.

## Open Questions
- Do we want a single `tp_driver_attach_async` + `tp_driver_attach_poll` that returns
  attach info only, and then separate config/attach helpers? Or keep producer/consumer
  attach wrappers as the primary user-facing API?
