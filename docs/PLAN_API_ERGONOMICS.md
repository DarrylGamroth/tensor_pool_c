# API Ergonomics Plan (Aeron-style)

This plan upgrades the public API to match Aeron’s async + poll model and
removes "helper" terminology. The goal is an ergonomic, user-facing API with
clear lifecycle semantics and consistent naming.

## Principles
- Public API mirrors Aeron async + poll patterns (client + archive).
- Poll functions return `0` (not complete), `1` (complete), `-1` (error).
- Blocking convenience calls are optional wrappers with `0/-1` return semantics.
- No Aeron or SBE types in public headers.
- Naming is consistent and role-symmetric (producer/consumer).
- Public headers expose handles + config only; implementation state is private.

## Phase 0: Inventory + Boundary Definition
- Audit current public headers (`include/tensor_pool/...`) and classify:
  - Public: user-facing API (client/producer/consumer/driver entrypoints).
  - Internal: conductor, pollers, registries, adapters, Aeron wrappers.
- Compare with Aeron C client + archive patterns (`../aeron/aeron-client`, `../aeron/aeron-archive`)
  and record required alignment changes.
- Define a single public include surface:
  - Keep `include/tensor_pool/tp.h` as the single umbrella, but only include public headers.
  - Move internal headers to `include/tensor_pool/internal/` (not installed).
- Add a strict list of public headers to CMake install rules to prevent accidental exposure.

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
  - `tp_client_t`, `tp_producer_t`, `tp_consumer_t`, `tp_driver_client_t` become opaque handles.
  - `tp_client_conductor_t`, `tp_aeron_client_t`, `tp_consumer_manager_t`,
    `tp_consumer_registry_t`, `tp_control_adapter_t`, `tp_*_poller_t` become internal-only.
  - Expose accessors for `tp_driver_client_t` (lease_id, client_id, role, stream_id, epoch).
  - `tp_publication_t`, `tp_subscription_t`, `tp_fragment_assembler_t` remain opaque handles only.

## Phase 2: Context + Lifecycle Alignment
- Align to Aeron core lifecycle:
  - `tp_context_init(tp_context_t **out)` and `tp_context_close(tp_context_t *ctx)`.
  - `tp_client_init(tp_client_t **out, tp_context_t *ctx)` and `tp_client_close(tp_client_t *client)`.
- Decision: `tp_context_t` is opaque (Aeron core model).
- Add `*_get_*` accessors for all context settings (mirror Aeron `get`/`set` pairs).
- Provide `tp_client_context_set_use_conductor_agent_invoker` naming to match Aeron.
- Provide `tp_client_context_set_idle_strategy`/`tp_client_idle` to align with Aeron idle handling.
- Add `tp_client_main_do_work` wrapper to match Aeron `aeron_main_do_work`.

## Phase 3: Naming & Consistency Pass
- Standardize naming across public API:
  - `*_context_*` for lifecycle config, `*_config_*` for per-attach inputs.
  - `require_*` for validation requirements, `use_*` for runtime toggles.
  - `*_offer`, `*_try_claim`, `*_commit`, `*_poll` aligned with Aeron.
- Align error messages and return codes with Aeron conventions.
- Add consistent lifecycle APIs (`*_close`) and ownership rules for all public objects.
- Add `*_constants` accessors (publication/subscription) to expose channel/stream metadata
  without leaking Aeron types.
- Expose idle strategy configuration for `tp_agent_runner` (sleep/yield/busy).
- Add `*_get_registration_id` accessors for async operations (Aeron-style).
- Standardize poll return semantics to `0/1/-1` across all pollers and async helpers.
- Add minimal client defaults API to avoid repeated channel setup (Aeron sample-style).
- Add `*_get_registration_id` for all async operations (driver attach/detach, discovery attach, etc).

## Phase 4: Public vs Private Header Refactor
- Move internal-only headers out of `include/tensor_pool`:
  - `tp_client_conductor.h`, `tp_client_conductor_agent.h`
  - `tp_aeron.h` (client wrapper)
  - `tp_consumer_manager.h`, `tp_consumer_registry.h`
  - `tp_control_adapter.h`, `tp_control_poller.h`
  - `tp_metadata_poller.h`, `tp_progress_poller.h`, `tp_qos.h`
- Update `tp.h` to only include public headers.
- Update CMake install/export lists to expose public headers only.

## Public vs Private Inventory (Aeron-style)

### Public (opaque handles + accessors)
- `tp_client_t`, `tp_producer_t`, `tp_consumer_t`, `tp_driver_client_t`
- `tp_publication_t`, `tp_subscription_t`, `tp_fragment_assembler_t`
- `tp_context_t`, `tp_client_context_t`, `tp_producer_context_t`, `tp_consumer_context_t`
- `tp_producer_config_t`, `tp_consumer_config_t`, `tp_driver_config_t`

### Private (internal-only)
- `tp_aeron_client_t` (wrap Aeron context/client)
- `tp_client_conductor_t`, pollers, registries, adapters, supervisor helpers
- Any struct exposing raw Aeron pointers or internal queues
- Direct use of `aeron_*` types in public headers

## Phase 5: Migration & Examples
- Merge `docs/HELPER_API_DRAFT.md` into this plan and remove the standalone draft.
- Update examples to use the new public API names and async + poll patterns.
- Update `docs/C_CLIENT_API_USAGE.md` to highlight the async model first.
- Update `docs/AERON_LIKE_API_PROPOSAL.md` to reflect the new public API.
- Add a migration table old→new API names (no backward compatibility).

## Phase 6: Tests + Traceability
- Add tests for new async + poll functions (success, timeout, error paths).
- Add tests for blocking convenience wrappers.
- Update requirements-to-tests checklist with new API and lifecycle coverage.
- Add integration coverage for client-conductor agent invoker vs thread.

## Phase 7: Breaking Changes
- No backwards compatibility: remove or rename old APIs directly.
- Provide a migration guide with old→new mapping.

## Open Questions
- Primary API should mirror Aeron: a single `tp_driver_attach_async` + `tp_driver_attach_poll`
  that returns `tp_driver_attach_info_t`. Producer/consumer attach wrappers are optional
  convenience built on top and are not the primary API.
- Decide whether `tp_context_t` stays opaque (Aeron core) or public struct (Aeron archive).
