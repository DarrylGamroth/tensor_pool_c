#include "tp_aeron_wrap.h"

#include <stdlib.h>
#include <string.h>

int tp_publication_wrap(tp_publication_t **out, aeron_publication_t *pub)
{
    if (NULL == out || NULL == pub)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_publication_wrap: invalid input");
        return -1;
    }

    tp_publication_t *wrapper = (tp_publication_t *)calloc(1, sizeof(*wrapper));
    if (NULL == wrapper)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_publication_wrap: alloc failed");
        return -1;
    }

    wrapper->aeron = pub;
    *out = wrapper;
    return 0;
}

int tp_subscription_wrap(tp_subscription_t **out, aeron_subscription_t *sub)
{
    if (NULL == out || NULL == sub)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_subscription_wrap: invalid input");
        return -1;
    }

    tp_subscription_t *wrapper = (tp_subscription_t *)calloc(1, sizeof(*wrapper));
    if (NULL == wrapper)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_subscription_wrap: alloc failed");
        return -1;
    }

    wrapper->aeron = sub;
    *out = wrapper;
    return 0;
}

int tp_fragment_assembler_wrap(tp_fragment_assembler_t **out, aeron_fragment_assembler_t *assembler)
{
    if (NULL == out || NULL == assembler)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_fragment_assembler_wrap: invalid input");
        return -1;
    }

    tp_fragment_assembler_t *wrapper = (tp_fragment_assembler_t *)calloc(1, sizeof(*wrapper));
    if (NULL == wrapper)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_fragment_assembler_wrap: alloc failed");
        return -1;
    }

    wrapper->aeron = assembler;
    *out = wrapper;
    return 0;
}

int tp_fragment_assembler_create(tp_fragment_assembler_t **out, aeron_fragment_handler_t handler, void *clientd)
{
    aeron_fragment_assembler_t *assembler = NULL;

    if (NULL == out)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_fragment_assembler_create: invalid input");
        return -1;
    }

    if (aeron_fragment_assembler_create(&assembler, handler, clientd) < 0)
    {
        return -1;
    }

    if (tp_fragment_assembler_wrap(out, assembler) < 0)
    {
        aeron_fragment_assembler_delete(assembler);
        return -1;
    }

    return 0;
}

void tp_publication_close(tp_publication_t **pub)
{
    if (NULL == pub || NULL == *pub)
    {
        return;
    }

    if (NULL != (*pub)->aeron)
    {
        aeron_publication_close((*pub)->aeron, NULL, NULL);
        (*pub)->aeron = NULL;
    }

    free(*pub);
    *pub = NULL;
}

void tp_subscription_close(tp_subscription_t **sub)
{
    if (NULL == sub || NULL == *sub)
    {
        return;
    }

    if (NULL != (*sub)->aeron)
    {
        aeron_subscription_close((*sub)->aeron, NULL, NULL);
        (*sub)->aeron = NULL;
    }

    free(*sub);
    *sub = NULL;
}

void tp_fragment_assembler_close(tp_fragment_assembler_t **assembler)
{
    if (NULL == assembler || NULL == *assembler)
    {
        return;
    }

    if (NULL != (*assembler)->aeron)
    {
        aeron_fragment_assembler_delete((*assembler)->aeron);
        (*assembler)->aeron = NULL;
    }

    free(*assembler);
    *assembler = NULL;
}
