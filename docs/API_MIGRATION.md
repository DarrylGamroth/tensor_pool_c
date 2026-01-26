# API Migration Table

This table lists public API renames introduced by the Aeron-style ergonomics pass.
There is no backward compatibility; callers must update to the new names.

| Old API | New API | Notes |
| --- | --- | --- |
| `tp_driver_attach_info_to_producer_config` | `tp_driver_attach_producer_config` | Produces `tp_producer_config_t` from driver attach info. |
| `tp_driver_attach_info_to_consumer_config` | `tp_driver_attach_consumer_config` | Produces `tp_consumer_config_t` from driver attach info. |
| `tp_driver_detach_if_active` | `tp_driver_detach_active` | Detach helper (no implicit behavior change). |

For blocking workflows, prefer the async + poll equivalents and use the blocking wrappers only as convenience helpers.
