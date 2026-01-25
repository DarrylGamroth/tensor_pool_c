# Helper API Draft (Promotion Candidates)

This document proposes ergonomic helper APIs that wrap existing internal
primitives. These helpers are intended to be promoted to public API after
review, while the existing low-level functions remain internal.

Goals:
- Reduce boilerplate in examples and applications.
- Align with Aeron sample patterns (simple init + poll loop).
- Keep strict spec behavior unchanged; helpers are thin wrappers.

## 1. Driver Attach → Config Helpers

### 1.1 Producer Config Helper
```c
int tp_driver_attach_info_to_producer_config(
    const tp_driver_attach_info_t *info,
    uint32_t producer_id,
    tp_payload_pool_config_t *pools,
    size_t pool_capacity,
    tp_producer_config_t *out_cfg,
    size_t *out_pool_count);
```
Behavior:
- Copies stream_id/epoch/layout/header URIs into `out_cfg`.
- Copies pool geometry from `info->pools` into `pools`.
- Sets `out_cfg->pools` to `pools` and `out_cfg->pool_count` to copied count.
- Returns -1 if `pool_capacity < info->pool_count`, or if inputs are invalid.

### 1.2 Consumer Config Helper
```c
int tp_driver_attach_info_to_consumer_config(
    const tp_driver_attach_info_t *info,
    tp_consumer_pool_config_t *pools,
    size_t pool_capacity,
    tp_consumer_config_t *out_cfg,
    size_t *out_pool_count);
```
Behavior mirrors producer helper, with consumer config fields.

### 1.3 Detach Helper
```c
int tp_driver_detach_if_active(tp_driver_client_t *client);
```
Behavior:
- If `client->active_lease_id != 0` and `client->publication != NULL`, sends detach.
- Otherwise returns 0.

## 2. Driver Attach Convenience

### 2.1 Producer Attach Helper
```c
int tp_producer_attach_driver(
    tp_producer_t *producer,
    tp_driver_client_t *driver,
    const tp_driver_attach_request_t *request,
    tp_payload_pool_config_t *pools,
    size_t pool_capacity,
    tp_driver_attach_info_t *attach_info,
    int64_t timeout_ns);
```
Behavior:
- Calls `tp_driver_attach`.
- Validates response code.
- Populates `producer` via `tp_driver_attach_info_to_producer_config`.
- Calls `tp_producer_attach`.

### 2.2 Consumer Attach Helper
```c
int tp_consumer_attach_driver(
    tp_consumer_t *consumer,
    tp_driver_client_t *driver,
    const tp_driver_attach_request_t *request,
    tp_consumer_pool_config_t *pools,
    size_t pool_capacity,
    tp_driver_attach_info_t *attach_info,
    int64_t timeout_ns);
```
Behavior mirrors producer helper.

## 3. Example/Utility Helpers (Optional Promotion)

### 3.1 Client Context Defaults
```c
int tp_client_context_set_default_channels(
    tp_client_context_t *ctx,
    const char *channel,
    int32_t announce_stream_id);
```
Behavior:
- Sets control/announce/descriptor/qos/metadata channels and standard stream IDs
  per `docs/STREAM_ID_CONVENTIONS.md`.

### 3.2 Wait Helpers
```c
int tp_publication_wait_connected(tp_client_t *client, tp_publication_t *pub, int64_t timeout_ns);
int tp_subscription_wait_connected(tp_client_t *client, tp_subscription_t *sub, int64_t timeout_ns);
```
Behavior:
- Polls `tp_client_do_work` until connected or timeout.

## 4. API Placement

Proposed public headers:
- `include/tensor_pool/client/tp_driver_helpers.h` (driver attach helpers).
- `include/tensor_pool/client/tp_client_helpers.h` (context defaults + wait helpers).

Existing low-level APIs remain available internally under `src/...`.

## 5. Migration Notes

- Examples should be updated to use helpers to reduce boilerplate.
- Error handling remains `0` on success, `-1` on failure with `tp_err` set.
