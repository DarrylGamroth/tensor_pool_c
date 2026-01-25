#include "tp_sample_util.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int tp_example_init_client_context(
    tp_client_context_t *ctx,
    const char *aeron_dir,
    const char *channel,
    int32_t announce_stream_id,
    const char *const *allowed_paths,
    size_t allowed_path_count)
{
    if (NULL == ctx || NULL == aeron_dir || NULL == channel)
    {
        return -1;
    }

    if (tp_client_context_init(ctx) < 0)
    {
        return -1;
    }

    tp_client_context_set_aeron_dir(ctx, aeron_dir);
    tp_client_context_set_control_channel(ctx, channel, 1000);
    tp_client_context_set_announce_channel(ctx, channel, announce_stream_id);
    tp_client_context_set_descriptor_channel(ctx, channel, 1100);
    tp_client_context_set_qos_channel(ctx, channel, 1200);
    tp_client_context_set_metadata_channel(ctx, channel, 1300);
    if (allowed_paths && allowed_path_count > 0)
    {
        tp_context_set_allowed_paths(&ctx->base, allowed_paths, allowed_path_count);
    }

    return 0;
}

int tp_example_init_client_context_nodriver(
    tp_client_context_t *ctx,
    const char *aeron_dir,
    const char *channel,
    const char *const *allowed_paths,
    size_t allowed_path_count)
{
    if (NULL == ctx || NULL == aeron_dir || NULL == channel)
    {
        return -1;
    }

    if (tp_client_context_init(ctx) < 0)
    {
        return -1;
    }

    tp_client_context_set_aeron_dir(ctx, aeron_dir);
    tp_client_context_set_control_channel(ctx, channel, 1000);
    tp_client_context_set_descriptor_channel(ctx, "aeron:ipc", 1100);
    tp_client_context_set_qos_channel(ctx, "aeron:ipc", 1200);
    tp_client_context_set_metadata_channel(ctx, "aeron:ipc", 1300);
    if (allowed_paths && allowed_path_count > 0)
    {
        tp_context_set_allowed_paths(&ctx->base, allowed_paths, allowed_path_count);
    }

    return 0;
}

void tp_example_log_publication_status(const char *label, tp_publication_t *publication)
{
    if (NULL == publication)
    {
        fprintf(stderr, "%s publication unavailable\n", label);
        return;
    }

    fprintf(stderr,
        "%s publication channel=%s stream_id=%d status=%" PRId64 " connected=%d\n",
        label,
        tp_publication_channel(publication),
        tp_publication_stream_id(publication),
        tp_publication_channel_status(publication),
        tp_publication_is_connected(publication) ? 1 : 0);
}

void tp_example_log_subscription_status(
    const char *label,
    tp_subscription_t *subscription,
    int *last_images,
    int64_t *last_status)
{
    int image_count;
    int64_t status;

    if (NULL == subscription || NULL == last_images || NULL == last_status)
    {
        return;
    }

    image_count = tp_subscription_image_count(subscription);
    status = tp_subscription_channel_status(subscription);
    if (image_count != *last_images || status != *last_status)
    {
        fprintf(stderr, "%s images=%d channel_status=%" PRId64 "\n", label, image_count, status);
        *last_images = image_count;
        *last_status = status;
    }
}

int tp_example_wait_for_publication(tp_client_t *client, tp_publication_t *publication, int64_t timeout_ns)
{
    int64_t deadline = tp_clock_now_ns() + timeout_ns;

    while (tp_clock_now_ns() < deadline)
    {
        if (tp_publication_is_connected(publication))
        {
            return 0;
        }
        tp_client_do_work(client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

    return -1;
}

int tp_example_wait_for_subscription(tp_client_t *client, tp_subscription_t *subscription, int64_t timeout_ns)
{
    int64_t deadline = tp_clock_now_ns() + timeout_ns;

    while (tp_clock_now_ns() < deadline)
    {
        if (tp_subscription_is_connected(subscription))
        {
            return 0;
        }
        tp_client_do_work(client);
        {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }

    return -1;
}
