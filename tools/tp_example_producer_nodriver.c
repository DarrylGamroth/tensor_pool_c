#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"
#include "tp_aeron_wrap.h"

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options] -a <aeron_dir> -c <channel> -s <stream_id> -H <header_uri> -P <pool_uri>\n"
        "Options:\n"
        "  -a <dir>     Aeron directory\n"
        "  -c <chan>    Control channel\n"
        "  -s <id>      Stream id\n"
        "  -i <id>      Client id (0 = auto-assign)\n"
        "  -H <uri>     Header region URI\n"
        "  -P <uri>     Pool region URI\n"
        "  -p <id>      Pool id\n"
        "  -t <bytes>   Pool stride bytes\n"
        "  -n <nslots>  Header nslots\n"
        "  -e <epoch>   Epoch\n"
        "  -l <ver>     Layout version\n"
        "  -f <count>   Frame count\n"
        "  -h           Show help\n"
        "Example header_uri: shm:file?path=/dev/shm/tensorpool-${USER}/demo/10000/1/header.ring\n"
        "Example pool_uri:   shm:file?path=/dev/shm/tensorpool-${USER}/demo/10000/1/1.pool\n",
        name);
}

static int wait_for_publication(tp_client_t *client, tp_publication_t *publication)
{
    int64_t deadline = tp_clock_now_ns() + 2 * 1000 * 1000 * 1000LL;

    while (tp_clock_now_ns() < deadline)
    {
        if (aeron_publication_is_connected(tp_publication_handle(publication)))
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

int main(int argc, char **argv)
{
    tp_client_context_t client_context;
    tp_client_t client;
    tp_payload_pool_config_t pool_cfg;
    tp_producer_t producer;
    tp_producer_context_t producer_context;
    tp_producer_config_t producer_cfg;
    tp_frame_t frame;
    tp_frame_metadata_t meta;
    tp_tensor_header_t header;
    float payload[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    uint32_t stream_id = 0;
    uint32_t client_id = 0;
    uint32_t header_nslots = 0;
    uint32_t layout_version = 0;
    uint64_t epoch = 0;
    uint16_t pool_id = 0;
    uint32_t pool_stride = 0;
    const char *header_uri = NULL;
    const char *pool_uri = NULL;
    const char *aeron_dir = NULL;
    const char *channel = NULL;
    int frame_count = 0;
    int opt;
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };

    while ((opt = getopt(argc, argv, "a:c:s:i:H:P:p:t:n:e:l:f:h")) != -1)
    {
        switch (opt)
        {
            case 'a':
                aeron_dir = optarg;
                break;
            case 'c':
                channel = optarg;
                break;
            case 's':
                stream_id = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'i':
                client_id = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'H':
                header_uri = optarg;
                break;
            case 'P':
                pool_uri = optarg;
                break;
            case 'p':
                pool_id = (uint16_t)strtoul(optarg, NULL, 10);
                break;
            case 't':
                pool_stride = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'n':
                header_nslots = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'e':
                epoch = (uint64_t)strtoull(optarg, NULL, 10);
                break;
            case 'l':
                layout_version = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'f':
                frame_count = (int)strtol(optarg, NULL, 10);
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if ((NULL == aeron_dir || NULL == channel || stream_id == 0 || header_uri == NULL ||
            pool_uri == NULL || header_nslots == 0 || frame_count == 0) &&
        (argc - optind) >= 12)
    {
        aeron_dir = argv[optind++];
        channel = argv[optind++];
        stream_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        client_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        header_uri = argv[optind++];
        pool_uri = argv[optind++];
        pool_id = (uint16_t)strtoul(argv[optind++], NULL, 10);
        pool_stride = (uint32_t)strtoul(argv[optind++], NULL, 10);
        header_nslots = (uint32_t)strtoul(argv[optind++], NULL, 10);
        epoch = (uint64_t)strtoull(argv[optind++], NULL, 10);
        layout_version = (uint32_t)strtoul(argv[optind++], NULL, 10);
        frame_count = (int)strtol(argv[optind++], NULL, 10);
    }

    if (NULL == aeron_dir || NULL == channel || stream_id == 0 || header_uri == NULL ||
        pool_uri == NULL || pool_stride == 0 || header_nslots == 0 || frame_count == 0 || optind < argc)
    {
        usage(argv[0]);
        return 1;
    }
    if (frame_count <= 0)
    {
        fprintf(stderr, "frame_count must be > 0\n");
        return 1;
    }

    if (tp_client_context_init(&client_context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    tp_client_context_set_aeron_dir(&client_context, aeron_dir);
    tp_client_context_set_control_channel(&client_context, channel, 1000);
    tp_client_context_set_descriptor_channel(&client_context, "aeron:ipc", 1100);
    tp_client_context_set_qos_channel(&client_context, "aeron:ipc", 1200);
    tp_client_context_set_metadata_channel(&client_context, "aeron:ipc", 1300);
    tp_context_set_allowed_paths(&client_context.base, allowed_paths, 2);

    if (tp_client_init(&client, &client_context) < 0 || tp_client_start(&client) < 0)
    {
        fprintf(stderr, "Client init failed: %s\n", tp_errmsg());
        return 1;
    }

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.pool_id = pool_id;
    pool_cfg.nslots = header_nslots;
    pool_cfg.stride_bytes = pool_stride;
    pool_cfg.uri = pool_uri;

    if (tp_producer_context_init(&producer_context) < 0)
    {
        fprintf(stderr, "Producer context init failed: %s\n", tp_errmsg());
        tp_client_close(&client);
        return 1;
    }

    producer_context.stream_id = stream_id;
    producer_context.producer_id = client_id;

    if (tp_producer_init(&producer, &client, &producer_context) < 0)
    {
        fprintf(stderr, "Producer init failed: %s\n", tp_errmsg());
        tp_client_close(&client);
        return 1;
    }

    memset(&producer_cfg, 0, sizeof(producer_cfg));
    producer_cfg.stream_id = stream_id;
    producer_cfg.producer_id = client_id;
    producer_cfg.epoch = epoch;
    producer_cfg.layout_version = layout_version;
    producer_cfg.header_nslots = header_nslots;
    producer_cfg.header_uri = header_uri;
    producer_cfg.pools = &pool_cfg;
    producer_cfg.pool_count = 1;

    if (tp_producer_attach(&producer, &producer_cfg) < 0)
    {
        fprintf(stderr, "Producer attach failed: %s\n", tp_errmsg());
        tp_producer_close(&producer);
        tp_client_close(&client);
        return 1;
    }

    if (wait_for_publication(&client, producer.descriptor_publication) < 0)
    {
        fprintf(stderr, "Descriptor publication not connected\n");
        tp_producer_close(&producer);
        tp_client_close(&client);
        return 1;
    }

    memset(&header, 0, sizeof(header));
    header.dtype = TP_DTYPE_FLOAT32;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.ndims = 2;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = 2;
    header.dims[1] = 2;

    if (tp_tensor_header_validate(&header, NULL) < 0)
    {
        fprintf(stderr, "Tensor header invalid: %s\n", tp_errmsg());
    }
    memset(&frame, 0, sizeof(frame));
    frame.tensor = &header;
    frame.payload = payload;
    frame.payload_len = sizeof(payload);
    frame.pool_id = pool_id;
    meta.timestamp_ns = 0;
    meta.meta_version = 0;

    {
        const char *delay_env = getenv("TP_FRAME_DELAY_US");
        long delay_us = delay_env ? strtol(delay_env, NULL, 10) : 1000;
        if (delay_us < 0)
        {
            delay_us = 0;
        }

        for (int i = 0; i < frame_count; i++)
        {
            int64_t position = tp_producer_offer_frame(&producer, &frame, &meta);
            if (position < 0)
            {
                fprintf(stderr, "Publish failed: %s\n", tp_errmsg());
                break;
            }
            printf("Published frame pool_id=%u seq=%" PRIi64 "\n", pool_id, position);
            if (delay_us > 0)
            {
                struct timespec ts = { 0, delay_us * 1000 };
                nanosleep(&ts, NULL);
            }
        }
    }


    tp_producer_close(&producer);
    tp_client_close(&client);

    return 0;
}
