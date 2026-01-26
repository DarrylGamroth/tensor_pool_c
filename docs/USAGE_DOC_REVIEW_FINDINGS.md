# Usage Doc Review Findings

This document tracks findings from the C client usage documentation review and their completion status.

## Findings

| ID | Finding | Status | Resolution |
| --- | --- | --- | --- |
| UD-1 | Trace example referenced internal fields on opaque handles (`producer.driver_attach`, `producer.stream_id/epoch`). | Done | Use driver attach info in trace examples and prefer `tp_producer_send_tracelink_set_ex`. |
| UD-2 | Driver attach example omitted `tp_driver_client_init` and attach-info cleanup. | Done | Added `tp_driver_client_init`/`tp_driver_client_close` and `tp_driver_attach_info_close`. |
| UD-3 | Driver attach helper snippet used undefined `producer_id`. | Done | Added explicit `producer_id` example variable. |
| UD-4 | No-driver SHM example omitted pool configuration. | Done | Added `tp_payload_pool_config_t` example and `pool_count` derivation. |
| UD-5 | Progress section used internal poller API. | Done | Replaced with `tp_consumer_set_progress_handler` + `tp_consumer_poll_progress`. |
| UD-6 | MergeMap section used internal control poller API. | Done | Replaced with `tp_client_set_control_handlers`. |
| UD-7 | Cleanup/ownership guidance missing for public handles. | Done | Added cleanup section covering close/free responsibilities. |
| UD-8 | Discovery example did not free response resources. | Done | Added `tp_discovery_response_close`. |

## Status

All listed findings are addressed in `docs/C_CLIENT_API_USAGE.md` and `docs/DRIVER_USAGE.md`.
