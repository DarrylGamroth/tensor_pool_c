#ifndef TENSOR_POOL_TP_AERON_CLIENT_H
#define TENSOR_POOL_TP_AERON_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_aeron_client_stct
{
    void *context;
    void *aeron;
}
tp_aeron_client_t;

#ifdef __cplusplus
}
#endif

#endif
