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
- Make internal structs opaque (Aeron model):
  - `tp_driver_client_t` becomes opaque; expose accessors for lease_id, client_id, role, stream_id.
  - `tp_aeron_client_t` becomes internal; public API should not expose Aeron context pointers.
  - `tp_publication_t`, `tp_subscription_t`, `tp_fragment_assembler_t` remain opaque handles only.

## Phase 2: Naming & Consistency Pass
- Standardize naming across public API:
  - `*_context_*` for lifecycle config, `*_config_*` for per-attach inputs.
  - `require_*` for validation requirements, `use_*` for runtime toggles.
  - `*_offer`, `*_try_claim`, `*_commit`, `*_poll` aligned with Aeron.
- Align error messages and return codes with Aeron conventions.
- Add consistent lifecycle APIs (`*_close`) and ownership rules for all public objects.
- Add `*_constants` accessors (publication/subscription) to expose channel/stream metadata
  without leaking Aeron types.
- Expose idle strategy configuration for `tp_agent_runner` (sleep/yield/busy).

## Public vs Private Inventory (Aeron-style)

### Public (opaque handles + accessors)
- `tp_client_t`, `tp_producer_t`, `tp_consumer_t`, `tp_driver_client_t`
- `tp_publication_t`, `tp_subscription_t`, `tp_fragment_assembler_t`
- `tp_client_context_t`, `tp_producer_context_t`, `tp_consumer_context_t`
- `tp_producer_config_t`, `tp_consumer_config_t`

### Private (internal-only)
- `tp_aeron_client_t` (wrap Aeron context/client)
- Any struct exposing raw Aeron pointers or internal queues
- Direct use of `aeron_*` types in public headers

## Phase 3: Migration & Examples
- Merge `docs/HELPER_API_DRAFT.md` into this plan and remove the standalone draft.
- Update examples to use the new public API names and async + poll patterns.
- Update `docs/C_CLIENT_API_USAGE.md` to highlight the async model first.
- Update `docs/AERON_LIKE_API_PROPOSAL.md` to reflect the new public API.

## Phase 4: Tests
- Add tests for new async + poll functions (success, timeout, error paths).
- Add tests for blocking convenience wrappers.

## Phase 5: Breaking Changes
- No backwards compatibility: remove or rename old APIs directly.
- Provide a migration guide with old→new mapping.

## Open Questions
- Primary API should mirror Aeron: a single `tp_driver_attach_async` + `tp_driver_attach_poll`
  that returns `tp_driver_attach_info_t`. Producer/consumer attach wrappers are optional
  convenience built on top and are not the primary API.
