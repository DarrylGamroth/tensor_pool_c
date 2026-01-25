# Implementation Comparison Matrix

| Spec/Feature Area | AeronTensorPool.jl (Julia) | tensor_pool_c (C) | Notes |
| --- | --- | --- | --- |
| Control/QoS/metadata streams | Full | Partial | C implements supervisor subscriptions and ConsumerConfig policy; advanced policy layers still minimal. |
| FrameProgress | Full | Full | C now validates progress against layout via consumer-side validation. |
