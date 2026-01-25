#include "tensor_pool/internal/tp_client_conductor.h"
#include "tensor_pool/internal/tp_context.h"

#include "tensor_pool/tp_client.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/tp_error.h"
#include "tp_aeron_wrap.h"
#include "tp_mpsc_queue.h"

typedef enum tp_client_cmd_type_enum
{
    TP_CLIENT_CMD_ADD_PUBLICATION = 1,
    TP_CLIENT_CMD_ADD_SUBSCRIPTION = 2
}
tp_client_cmd_type_t;

typedef struct tp_client_cmd_stct
{
    tp_client_cmd_type_t type;
    void *async;
}
tp_client_cmd_t;

typedef struct tp_client_poller_entry_stct
{
    tp_client_poller_t poller;
    void *clientd;
    int fragment_limit;
    struct tp_client_poller_entry_stct *next;
}
tp_client_poller_entry_t;

typedef struct tp_client_conductor_state_stct
{
    tp_mpsc_queue_t command_queue;
    tp_async_add_publication_t *pending_publications;
    tp_async_add_subscription_t *pending_subscriptions;
    tp_subscription_t *control_subscription;
    tp_subscription_t *announce_subscription;
    tp_subscription_t *qos_subscription;
    tp_subscription_t *metadata_subscription;
    tp_subscription_t *descriptor_subscription;
    tp_client_poller_entry_t *pollers;
    int do_work_depth;
}
tp_client_conductor_state_t;

enum { TP_CLIENT_COMMAND_QUEUE_CAPACITY = 1024 };

static char *tp_client_strdup(const char *value)
{
    size_t len;
    char *copy;

    if (NULL == value)
    {
        return NULL;
    }

    len = strlen(value);
    copy = (char *)malloc(len + 1);
    if (NULL == copy)
    {
        return NULL;
    }
    memcpy(copy, value, len + 1);
    return copy;
}

static int tp_client_conductor_apply_context(
    aeron_context_t *aeron_ctx,
    const tp_client_context_t *context)
{
    if (NULL == aeron_ctx || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_apply_context: null input");
        return -1;
    }

    if (context->base && context->base->aeron_dir[0] != '\0')
    {
        if (aeron_context_set_dir(aeron_ctx, context->base->aeron_dir) < 0)
        {
            return -1;
        }
    }

    if (context->client_name[0] != '\0')
    {
        if (aeron_context_set_client_name(aeron_ctx, context->client_name) < 0)
        {
            return -1;
        }
    }

    if (context->error_handler != NULL)
    {
        if (aeron_context_set_error_handler(aeron_ctx, context->error_handler, context->error_handler_clientd) < 0)
        {
            return -1;
        }
    }

    if (context->driver_timeout_ns > 0)
    {
        if (aeron_context_set_driver_timeout_ms(aeron_ctx, context->driver_timeout_ns / 1000000u) < 0)
        {
            return -1;
        }
    }

    if (context->keepalive_interval_ns > 0)
    {
        if (aeron_context_set_keepalive_interval_ns(aeron_ctx, context->keepalive_interval_ns) < 0)
        {
            return -1;
        }
    }

    if (context->idle_sleep_duration_ns > 0)
    {
        if (aeron_context_set_idle_sleep_duration_ns(aeron_ctx, context->idle_sleep_duration_ns) < 0)
        {
            return -1;
        }
    }

    if (aeron_context_set_use_conductor_agent_invoker(aeron_ctx, context->use_agent_invoker) < 0)
    {
        return -1;
    }

    return 0;
}

static tp_client_conductor_state_t *tp_client_conductor_state(tp_client_conductor_t *conductor)
{
    if (NULL == conductor)
    {
        return NULL;
    }

    return (tp_client_conductor_state_t *)conductor->state;
}

static void tp_async_store_error(int *errcode, char *errmsg, size_t errmsg_len)
{
    int code = tp_errcode();
    const char *msg = tp_errmsg();

    if (errcode)
    {
        *errcode = code;
    }
    if (errmsg && errmsg_len > 0)
    {
        if (msg)
        {
            strncpy(errmsg, msg, errmsg_len - 1);
            errmsg[errmsg_len - 1] = '\0';
        }
        else
        {
            errmsg[0] = '\0';
        }
    }
}

static void tp_async_publication_fail(tp_async_add_publication_t *async)
{
    if (NULL == async)
    {
        return;
    }
    tp_async_store_error(&async->errcode, async->errmsg, sizeof(async->errmsg));
    atomic_store_explicit(&async->status, TP_ASYNC_ERROR, memory_order_release);
}

static void tp_async_subscription_fail(tp_async_add_subscription_t *async)
{
    if (NULL == async)
    {
        return;
    }
    tp_async_store_error(&async->errcode, async->errmsg, sizeof(async->errmsg));
    atomic_store_explicit(&async->status, TP_ASYNC_ERROR, memory_order_release);
}

static int tp_client_conductor_state_init(tp_client_conductor_t *conductor)
{
    tp_client_conductor_state_t *state = NULL;

    if (NULL == conductor)
    {
        return -1;
    }

    state = (tp_client_conductor_state_t *)calloc(1, sizeof(*state));
    if (NULL == state)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_client_conductor_state_init: alloc failed");
        return -1;
    }

    if (tp_mpsc_queue_init(&state->command_queue, TP_CLIENT_COMMAND_QUEUE_CAPACITY) < 0)
    {
        free(state);
        return -1;
    }

    conductor->state = state;
    return 0;
}

static void tp_client_poller_list_clear(tp_client_poller_entry_t **head)
{
    tp_client_poller_entry_t *entry = NULL;
    tp_client_poller_entry_t *next = NULL;

    if (NULL == head)
    {
        return;
    }

    entry = *head;
    while (entry)
    {
        next = entry->next;
        free(entry);
        entry = next;
    }
    *head = NULL;
}

static void tp_client_conductor_state_close(tp_client_conductor_t *conductor)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    tp_async_add_publication_t *pub = NULL;
    tp_async_add_subscription_t *sub = NULL;

    if (NULL == state)
    {
        return;
    }

    pub = state->pending_publications;
    while (pub)
    {
        tp_async_add_publication_t *next = pub->next;
        free(pub->channel);
        free(pub);
        pub = next;
    }

    sub = state->pending_subscriptions;
    while (sub)
    {
        tp_async_add_subscription_t *next = sub->next;
        free(sub->channel);
        free(sub);
        sub = next;
    }

    tp_subscription_close(&state->control_subscription);
    tp_subscription_close(&state->announce_subscription);
    tp_subscription_close(&state->qos_subscription);
    tp_subscription_close(&state->metadata_subscription);
    tp_subscription_close(&state->descriptor_subscription);

    tp_client_poller_list_clear(&state->pollers);
    tp_mpsc_queue_close(&state->command_queue);
    free(state);
    conductor->state = NULL;
}

static void tp_client_conductor_process_commands(tp_client_conductor_t *conductor)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    tp_client_cmd_t cmd;

    if (NULL == state)
    {
        return;
    }

    while (tp_mpsc_queue_poll(&state->command_queue, &cmd, sizeof(cmd)) > 0)
    {
        if (cmd.type == TP_CLIENT_CMD_ADD_PUBLICATION)
        {
            tp_async_add_publication_t *async = (tp_async_add_publication_t *)cmd.async;
            if (NULL == async || NULL == async->channel)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_client_conductor: invalid publication cmd");
                tp_async_publication_fail(async);
                continue;
            }

            if (aeron_async_add_publication(
                &async->aeron_async,
                conductor->aeron.aeron,
                async->channel,
                async->stream_id) < 0)
            {
                TP_SET_ERR(aeron_errcode(), "tp_client_conductor_add_publication: %s", aeron_errmsg());
                tp_async_publication_fail(async);
                continue;
            }

            async->next = state->pending_publications;
            state->pending_publications = async;
        }
        else if (cmd.type == TP_CLIENT_CMD_ADD_SUBSCRIPTION)
        {
            tp_async_add_subscription_t *async = (tp_async_add_subscription_t *)cmd.async;
            if (NULL == async || NULL == async->channel)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_client_conductor: invalid subscription cmd");
                tp_async_subscription_fail(async);
                continue;
            }

            if (aeron_async_add_subscription(
                &async->aeron_async,
                conductor->aeron.aeron,
                async->channel,
                async->stream_id,
                NULL,
                NULL,
                NULL,
                NULL) < 0)
            {
                TP_SET_ERR(aeron_errcode(), "tp_client_conductor_add_subscription: %s", aeron_errmsg());
                tp_async_subscription_fail(async);
                continue;
            }

            async->next = state->pending_subscriptions;
            state->pending_subscriptions = async;
        }
    }
}

static void tp_client_conductor_poll_pending(tp_client_conductor_t *conductor)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    tp_async_add_publication_t **pub_cursor = NULL;
    tp_async_add_subscription_t **sub_cursor = NULL;

    if (NULL == state)
    {
        return;
    }

    pub_cursor = &state->pending_publications;
    while (*pub_cursor)
    {
        tp_async_add_publication_t *async = *pub_cursor;
        aeron_publication_t *raw_pub = NULL;
        int result = aeron_async_add_publication_poll(&raw_pub, async->aeron_async);

        if (result == 0)
        {
            pub_cursor = &async->next;
            continue;
        }

        if (result < 0)
        {
            TP_SET_ERR(aeron_errcode(), "tp_client_conductor_add_publication_poll: %s", aeron_errmsg());
            tp_async_publication_fail(async);
        }
        else if (tp_publication_wrap(&async->publication, raw_pub) < 0)
        {
            aeron_publication_close(raw_pub, NULL, NULL);
            tp_async_publication_fail(async);
        }
        else
        {
            atomic_store_explicit(&async->status, TP_ASYNC_COMPLETE, memory_order_release);
        }

        *pub_cursor = async->next;
        async->next = NULL;
    }

    sub_cursor = &state->pending_subscriptions;
    while (*sub_cursor)
    {
        tp_async_add_subscription_t *async = *sub_cursor;
        aeron_subscription_t *raw_sub = NULL;
        int result = aeron_async_add_subscription_poll(&raw_sub, async->aeron_async);

        if (result == 0)
        {
            sub_cursor = &async->next;
            continue;
        }

        if (result < 0)
        {
            TP_SET_ERR(aeron_errcode(), "tp_client_conductor_add_subscription_poll: %s", aeron_errmsg());
            tp_async_subscription_fail(async);
        }
        else if (tp_subscription_wrap(&async->subscription, raw_sub) < 0)
        {
            aeron_subscription_close(raw_sub, NULL, NULL);
            tp_async_subscription_fail(async);
        }
        else
        {
            atomic_store_explicit(&async->status, TP_ASYNC_COMPLETE, memory_order_release);
        }

        *sub_cursor = async->next;
        async->next = NULL;
    }
}

static int tp_client_conductor_poll_pollers(tp_client_conductor_t *conductor)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    tp_client_poller_entry_t *entry = NULL;
    int total_work = 0;

    if (NULL == state)
    {
        return 0;
    }

    for (entry = state->pollers; NULL != entry; entry = entry->next)
    {
        int work = entry->poller(entry->clientd, entry->fragment_limit);
        if (work < 0)
        {
            return -1;
        }
        total_work += work;
    }

    return total_work;
}

int tp_client_conductor_init(
    tp_client_conductor_t *conductor,
    const tp_context_t *context,
    bool use_agent_invoker)
{
    tp_client_context_t shim;

    if (NULL == conductor || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_init: null input");
        return -1;
    }

    memset(&shim, 0, sizeof(shim));
    shim.base = *context;
    shim.use_agent_invoker = use_agent_invoker;

    return tp_client_conductor_init_with_client_context(conductor, &shim);
}

int tp_client_conductor_init_with_client_context(
    tp_client_conductor_t *conductor,
    const tp_client_context_t *context)
{
    aeron_context_t *aeron_ctx = NULL;
    aeron_t *aeron_client = NULL;

    if (NULL == conductor || NULL == context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_init_with_client_context: null input");
        return -1;
    }

    memset(conductor, 0, sizeof(*conductor));

    if (aeron_context_init(&aeron_ctx) < 0)
    {
        return -1;
    }

    if (tp_client_conductor_apply_context(aeron_ctx, context) < 0)
    {
        aeron_context_close(aeron_ctx);
        return -1;
    }

    if (aeron_init(&aeron_client, aeron_ctx) < 0)
    {
        aeron_context_close(aeron_ctx);
        return -1;
    }

    conductor->aeron.context = aeron_ctx;
    conductor->aeron.aeron = aeron_client;
    conductor->use_agent_invoker = context->use_agent_invoker;
    conductor->started = false;
    conductor->owns_aeron = true;
    conductor->state = NULL;

    if (tp_client_conductor_state_init(conductor) < 0)
    {
        tp_client_conductor_close(conductor);
        return -1;
    }

    return 0;
}

int tp_client_conductor_init_with_aeron(
    tp_client_conductor_t *conductor,
    void *aeron,
    bool use_agent_invoker,
    bool owns_aeron)
{
    if (NULL == conductor || NULL == aeron)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_init_with_aeron: null input");
        return -1;
    }

    memset(conductor, 0, sizeof(*conductor));
    conductor->aeron.aeron = (aeron_t *)aeron;
    conductor->aeron.context = aeron_context((aeron_t *)aeron);
    conductor->use_agent_invoker = use_agent_invoker;
    conductor->started = true;
    conductor->owns_aeron = owns_aeron;
    conductor->state = NULL;

    if (tp_client_conductor_state_init(conductor) < 0)
    {
        tp_client_conductor_close(conductor);
        return -1;
    }

    return 0;
}

int tp_client_conductor_start(tp_client_conductor_t *conductor)
{
    if (NULL == conductor || NULL == conductor->aeron.aeron)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_start: invalid input");
        return -1;
    }

    if (conductor->started)
    {
        return 0;
    }

    if (aeron_start(conductor->aeron.aeron) < 0)
    {
        return -1;
    }

    conductor->started = true;
    return 0;
}

int tp_client_conductor_close(tp_client_conductor_t *conductor)
{
    if (NULL == conductor)
    {
        return -1;
    }

    tp_client_conductor_state_close(conductor);

    if (NULL != conductor->aeron.aeron && conductor->owns_aeron)
    {
        aeron_close(conductor->aeron.aeron);
    }
    conductor->aeron.aeron = NULL;

    if (NULL != conductor->aeron.context && conductor->owns_aeron)
    {
        aeron_context_close(conductor->aeron.context);
    }
    conductor->aeron.context = NULL;

    conductor->started = false;
    return 0;
}

int tp_client_conductor_do_work(tp_client_conductor_t *conductor)
{
    if (NULL == conductor || NULL == conductor->aeron.aeron)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_do_work: invalid input");
        return -1;
    }

    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    int work = 0;

    if (conductor->use_agent_invoker)
    {
        int aeron_work = aeron_main_do_work(conductor->aeron.aeron);
        if (aeron_work < 0)
        {
            return -1;
        }
        work += aeron_work;
    }

    tp_client_conductor_process_commands(conductor);
    tp_client_conductor_poll_pending(conductor);

    if (state)
    {
        state->do_work_depth++;
    }

    if (NULL == state || state->do_work_depth == 1)
    {
        int poll_work = tp_client_conductor_poll_pollers(conductor);
        if (poll_work < 0)
        {
            if (state)
            {
                state->do_work_depth--;
            }
            return -1;
        }
        work += poll_work;
    }

    if (state)
    {
        state->do_work_depth--;
    }

    return work;
}

int tp_client_conductor_set_idle_sleep_duration_ns(tp_client_conductor_t *conductor, uint64_t sleep_ns)
{
    if (NULL == conductor || NULL == conductor->aeron.context)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_set_idle_sleep_duration_ns: invalid input");
        return -1;
    }

    return aeron_context_set_idle_sleep_duration_ns(conductor->aeron.context, sleep_ns);
}

int tp_client_conductor_register_poller(
    tp_client_conductor_t *conductor,
    tp_client_poller_t poller,
    void *clientd,
    int fragment_limit)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    tp_client_poller_entry_t *entry = NULL;

    if (NULL == conductor || NULL == poller || fragment_limit <= 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_register_poller: invalid input");
        return -1;
    }

    if (NULL == state)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_register_poller: missing state");
        return -1;
    }

    entry = (tp_client_poller_entry_t *)calloc(1, sizeof(*entry));
    if (NULL == entry)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_client_conductor_register_poller: alloc failed");
        return -1;
    }

    entry->poller = poller;
    entry->clientd = clientd;
    entry->fragment_limit = fragment_limit;
    entry->next = state->pollers;
    state->pollers = entry;
    return 0;
}

int tp_client_conductor_unregister_poller(
    tp_client_conductor_t *conductor,
    tp_client_poller_t poller,
    void *clientd)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    tp_client_poller_entry_t *prev = NULL;
    tp_client_poller_entry_t *entry = NULL;

    if (NULL == conductor || NULL == poller)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_unregister_poller: invalid input");
        return -1;
    }

    if (NULL == state)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_unregister_poller: missing state");
        return -1;
    }

    entry = state->pollers;
    while (entry)
    {
        if (entry->poller == poller && entry->clientd == clientd)
        {
            if (prev)
            {
                prev->next = entry->next;
            }
            else
            {
                state->pollers = entry->next;
            }
            free(entry);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }

    TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_unregister_poller: not registered");
    return -1;
}

int tp_client_conductor_set_subscription(
    tp_client_conductor_t *conductor,
    tp_client_subscription_kind_t kind,
    tp_subscription_t *subscription)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);

    if (NULL == conductor || NULL == state)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_set_subscription: invalid input");
        return -1;
    }

    switch (kind)
    {
        case TP_CLIENT_SUB_CONTROL:
            tp_subscription_close(&state->control_subscription);
            state->control_subscription = subscription;
            return 0;
        case TP_CLIENT_SUB_ANNOUNCE:
            tp_subscription_close(&state->announce_subscription);
            state->announce_subscription = subscription;
            return 0;
        case TP_CLIENT_SUB_QOS:
            tp_subscription_close(&state->qos_subscription);
            state->qos_subscription = subscription;
            return 0;
        case TP_CLIENT_SUB_METADATA:
            tp_subscription_close(&state->metadata_subscription);
            state->metadata_subscription = subscription;
            return 0;
        case TP_CLIENT_SUB_DESCRIPTOR:
            tp_subscription_close(&state->descriptor_subscription);
            state->descriptor_subscription = subscription;
            return 0;
        default:
            TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_set_subscription: invalid kind");
            return -1;
    }
}

tp_subscription_t *tp_client_conductor_get_subscription(
    tp_client_conductor_t *conductor,
    tp_client_subscription_kind_t kind)
{
    tp_client_conductor_state_t *state = NULL;

    if (NULL == conductor)
    {
        return NULL;
    }

    state = tp_client_conductor_state(conductor);

    if (NULL == state)
    {
        return NULL;
    }

    switch (kind)
    {
        case TP_CLIENT_SUB_CONTROL:
            return state->control_subscription;
        case TP_CLIENT_SUB_ANNOUNCE:
            return state->announce_subscription;
        case TP_CLIENT_SUB_QOS:
            return state->qos_subscription;
        case TP_CLIENT_SUB_METADATA:
            return state->metadata_subscription;
        case TP_CLIENT_SUB_DESCRIPTOR:
            return state->descriptor_subscription;
        default:
            return NULL;
    }
}

int tp_client_conductor_async_add_publication(
    tp_async_add_publication_t **async,
    tp_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    tp_client_cmd_t cmd;

    if (NULL == async || NULL == conductor || NULL == channel || NULL == state || NULL == conductor->aeron.aeron)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_async_add_publication: invalid input");
        return -1;
    }

    tp_async_add_publication_t *handle = (tp_async_add_publication_t *)calloc(1, sizeof(*handle));
    if (NULL == handle)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_client_conductor_async_add_publication: alloc failed");
        return -1;
    }

    handle->channel = tp_client_strdup(channel);
    if (NULL == handle->channel)
    {
        free(handle);
        TP_SET_ERR(ENOMEM, "%s", "tp_client_conductor_async_add_publication: channel alloc failed");
        return -1;
    }

    handle->stream_id = stream_id;
    atomic_init(&handle->status, TP_ASYNC_PENDING);
    handle->next = NULL;

    cmd.type = TP_CLIENT_CMD_ADD_PUBLICATION;
    cmd.async = handle;

    if (tp_mpsc_queue_offer(&state->command_queue, &cmd, sizeof(cmd)) < 0)
    {
        free(handle->channel);
        free(handle);
        return -1;
    }

    *async = handle;
    return 0;
}

int tp_client_conductor_async_add_publication_poll(
    tp_publication_t **publication,
    tp_async_add_publication_t *async)
{
    if (NULL == publication || NULL == async)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_async_add_publication_poll: invalid input");
        return -1;
    }

    int status = atomic_load_explicit(&async->status, memory_order_acquire);
    if (status == TP_ASYNC_PENDING)
    {
        return 0;
    }

    if (status == TP_ASYNC_ERROR)
    {
        TP_SET_ERR(async->errcode, "%s", async->errmsg);
        free(async->channel);
        free(async);
        return -1;
    }

    *publication = async->publication;
    free(async->channel);
    free(async);
    return 1;
}

int tp_client_conductor_async_add_subscription(
    tp_async_add_subscription_t **async,
    tp_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id)
{
    tp_client_conductor_state_t *state = tp_client_conductor_state(conductor);
    tp_client_cmd_t cmd;

    if (NULL == async || NULL == conductor || NULL == channel || NULL == state || NULL == conductor->aeron.aeron)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_async_add_subscription: invalid input");
        return -1;
    }

    tp_async_add_subscription_t *handle = (tp_async_add_subscription_t *)calloc(1, sizeof(*handle));
    if (NULL == handle)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_client_conductor_async_add_subscription: alloc failed");
        return -1;
    }

    handle->channel = tp_client_strdup(channel);
    if (NULL == handle->channel)
    {
        free(handle);
        TP_SET_ERR(ENOMEM, "%s", "tp_client_conductor_async_add_subscription: channel alloc failed");
        return -1;
    }

    handle->stream_id = stream_id;
    atomic_init(&handle->status, TP_ASYNC_PENDING);
    handle->next = NULL;

    cmd.type = TP_CLIENT_CMD_ADD_SUBSCRIPTION;
    cmd.async = handle;

    if (tp_mpsc_queue_offer(&state->command_queue, &cmd, sizeof(cmd)) < 0)
    {
        free(handle->channel);
        free(handle);
        return -1;
    }

    *async = handle;
    return 0;
}

int tp_client_conductor_async_add_subscription_poll(
    tp_subscription_t **subscription,
    tp_async_add_subscription_t *async)
{
    if (NULL == subscription || NULL == async)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_client_conductor_async_add_subscription_poll: invalid input");
        return -1;
    }

    int status = atomic_load_explicit(&async->status, memory_order_acquire);
    if (status == TP_ASYNC_PENDING)
    {
        return 0;
    }

    if (status == TP_ASYNC_ERROR)
    {
        TP_SET_ERR(async->errcode, "%s", async->errmsg);
        free(async->channel);
        free(async);
        return -1;
    }

    *subscription = async->subscription;
    free(async->channel);
    free(async);
    return 1;
}
