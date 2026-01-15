# Implementation Comparison Matrix

| Spec/Feature Area | AeronTensorPool.jl (Julia) | tensor_pool_c (C) | Notes |
| --- | --- | --- | --- |
| Control/QoS/metadata streams | Full | External | C implements message/poller support; supervisor/policy layer is external. |
| FrameProgress | Full | Full | C now validates progress against layout via consumer-side validation. |
