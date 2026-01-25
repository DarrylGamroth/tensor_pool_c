# Deployment Guide (Defaults)

This guide provides **recommended defaults** for the open parameters listed in
`docs/SHM_Tensor_Pool_Wire_Spec_v1.2.md` §14. These are deployment defaults
only; adjust per workload and hardware.

## Recommended Defaults for §14

| Parameter | Default | Notes |
| --- | --- | --- |
| Pool size classes / `stride_bytes` | `1 MiB`, `4 MiB`, `16 MiB` | Power-of-two multiples of 64 bytes; pick the smallest pool that fits the payload. |
| `header_nslots` / pool `nslots` | `1024` | Power-of-two; choose `ceil(rate_hz * worst_case_latency_s * safety_factor)` with safety factor 2-4. |
| `layout_version` | `1` | Must match `TP_LAYOUT_VERSION` in `include/tensor_pool/common/tp_types.h`. |
| Aeron channel + stream IDs | Use `docs/STREAM_ID_CONVENTIONS.md` | Default IPC: control `1000`, descriptor `1100`, QoS `1200`, metadata `1300`, pool stream `10000`. |

## Example Defaults (Single-Host IPC)

```ini
pool.stream_id=10000
control.channel=aeron:ipc
control.stream_id=1000
descriptor.channel=aeron:ipc
descriptor.stream_id=1100
qos.channel=aeron:ipc
qos.stream_id=1200
metadata.channel=aeron:ipc
metadata.stream_id=1300
header_nslots=1024
stride_bytes=1048576,4194304,16777216
layout_version=1
```

## Notes

- Increase `header_nslots` when consumer latency or producer rate increases.
- Keep `header_nslots` and each pool `nslots` identical (v1.2 requirement).
- If you use hugepages, ensure the backing path is on hugetlbfs and set
  `require_hugepages=true` in SHM URIs.
