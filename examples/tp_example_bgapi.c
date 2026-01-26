#include "tensor_pool/tp.h"
#include "tp_sample_util.h"

#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct tp_bgapi_slot_stct
{
    tp_buffer_claim_t claim;
}
tp_bgapi_slot_t;

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options] -a <aeron_dir> -c <channel> -s <stream_id>\n"
        "Options:\n"
        "  -a <dir>     Aeron directory\n"
        "  -c <chan>    Control channel\n"
        "  -s <id>      Stream id\n"
        "  -i <id>      Client id (0 = auto-assign)\n"
        "  -n <count>   Slots to announce (default: 8)\n"
        "  -h           Show help\n",
        name);
}

static void tp_example_detach_driver(tp_driver_client_t *driver)
{
    if (NULL == driver)
    {
        return;
    }
    if (tp_driver_client_active_lease_id(driver) != 0 && tp_driver_client_publication(driver) != NULL)
    {
        if (tp_driver_detach(
            driver,
            0,
            tp_driver_client_active_lease_id(driver),
            tp_driver_client_active_stream_id(driver),
            tp_driver_client_id(driver),
            tp_driver_client_role(driver)) < 0)
        {
            fprintf(stderr, "Driver detach failed: %s\n", tp_errmsg());
        }
    }
}

int main(int argc, char **argv)
{
    tp_context_t *client_context = NULL;
    tp_client_t *client = NULL;
    tp_driver_client_t *driver = NULL;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_async_attach_t *async_attach = NULL;
    tp_payload_pool_config_t *pool_cfg = NULL;
    tp_producer_t *producer = NULL;
    tp_producer_context_t producer_context;
    tp_producer_config_t producer_cfg;
    tp_tensor_header_t header;
    tp_frame_metadata_t meta;
    tp_bgapi_slot_t *slots = NULL;
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };
    size_t slot_count = 8;
    uint32_t stream_id = 0;
    uint32_t client_id = 0;
    uint32_t resolved_client_id = 0;
    const char *aeron_dir = NULL;
    const char *channel = NULL;
    int poll_result = 0;
    int result = 1;
    bool client_inited = false;
    bool driver_inited = false;
    bool attach_info_valid = false;
    bool producer_inited = false;
    int opt;
    size_t i;
    size_t pool_count = 0;

    while ((opt = getopt(argc, argv, "a:c:s:i:n:h")) != -1)
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
            case 'n':
                slot_count = (size_t)strtoul(optarg, NULL, 10);
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if ((NULL == aeron_dir || NULL == channel || stream_id == 0) && (argc - optind) >= 4)
    {
        aeron_dir = argv[optind++];
        channel = argv[optind++];
        stream_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        client_id = (uint32_t)strtoul(argv[optind++], NULL, 10);
        if (optind < argc)
        {
            slot_count = (size_t)strtoul(argv[optind++], NULL, 10);
        }
    }

    if (NULL == aeron_dir || NULL == channel || stream_id == 0 || optind < argc)
    {
        usage(argv[0]);
        return 1;
    }

    if (tp_example_init_client_context(
            &client_context,
            aeron_dir,
            channel,
            1001,
            allowed_paths,
            2) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    if (tp_client_init(&client, client_context) < 0 || tp_client_start(client) < 0)
    {
        fprintf(stderr, "Client init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    client_inited = true;

    if (tp_driver_client_init(&driver, client) < 0)
    {
        fprintf(stderr, "Driver init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    driver_inited = true;

    memset(&request, 0, sizeof(request));
    request.correlation_id = 0;
    request.stream_id = stream_id;
    request.client_id = client_id;
    request.role = TP_ROLE_PRODUCER;
    request.expected_layout_version = 0;
    request.publish_mode = TP_PUBLISH_MODE_EXISTING_OR_CREATE;
    request.require_hugepages = TP_HUGEPAGES_UNSPECIFIED;
    {
        const char *env = getenv("TP_DESIRED_NODE_ID");
        request.desired_node_id = (env && env[0] != '\0') ? (uint32_t)strtoul(env, NULL, 10) : TP_NULL_U32;
    }

    if (tp_driver_attach_async(driver, &request, &async_attach) < 0)
    {
        fprintf(stderr, "Attach async failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    {
        struct timespec ts = { 0, 10 * 1000 * 1000 };
        uint64_t deadline = (uint64_t)tp_clock_now_ns() + 2ULL * 1000ULL * 1000ULL * 1000ULL;

        while (true)
        {
            poll_result = tp_driver_attach_poll(async_attach, &info);
            if (poll_result < 0)
            {
                fprintf(stderr, "Attach poll failed: %s\n", tp_errmsg());
                goto cleanup;
            }
            if (poll_result > 0)
            {
                attach_info_valid = true;
                break;
            }
            if ((uint64_t)tp_clock_now_ns() >= deadline)
            {
                fprintf(stderr, "Attach timed out\n");
                goto cleanup;
            }
            tp_client_do_work(client);
            nanosleep(&ts, NULL);
        }
    }

    if (info.code != TP_RESPONSE_OK)
    {
        fprintf(stderr, "Attach rejected: code=%d error=%s\n", info.code, info.error_message);
        goto cleanup;
    }

    resolved_client_id = tp_driver_client_id(driver);
    if (resolved_client_id == 0)
    {
        resolved_client_id = request.client_id;
    }

    pool_cfg = calloc(info.pool_count, sizeof(*pool_cfg));
    if (NULL == pool_cfg)
    {
        fprintf(stderr, "Allocation failed\n");
        goto cleanup;
    }

    if (tp_producer_context_init(&producer_context) < 0)
    {
        fprintf(stderr, "Producer context init failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    producer_context.stream_id = info.stream_id;
    producer_context.producer_id = resolved_client_id;
    producer_context.fixed_pool_mode = true;

    if (tp_producer_init(&producer, client, &producer_context) < 0)
    {
        fprintf(stderr, "Producer init failed: %s\n", tp_errmsg());
        goto cleanup;
    }
    producer_inited = true;

    if (tp_driver_attach_producer_config(
            &info,
            resolved_client_id,
            pool_cfg,
            info.pool_count,
            &producer_cfg,
            &pool_count) < 0)
    {
        fprintf(stderr, "Producer config failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    if (tp_producer_attach(producer, &producer_cfg) < 0)
    {
        fprintf(stderr, "Producer attach failed: %s\n", tp_errmsg());
        goto cleanup;
    }

    slots = calloc(slot_count, sizeof(*slots));
    if (NULL == slots)
    {
        fprintf(stderr, "Slot allocation failed\n");
        goto cleanup;
    }

    memset(&header, 0, sizeof(header));
    header.dtype = TP_DTYPE_UINT8;
    header.major_order = TP_MAJOR_ORDER_ROW;
    header.ndims = 1;
    header.progress_unit = TP_PROGRESS_NONE;
    header.dims[0] = pool_cfg[0].stride_bytes;

    meta.timestamp_ns = 0;
    meta.meta_version = 0;

    for (i = 0; i < slot_count; i++)
    {
        if (tp_producer_try_claim(producer, pool_cfg[0].stride_bytes, &slots[i].claim) < 0)
        {
            fprintf(stderr, "try_claim failed: %s\n", tp_errmsg());
            goto cleanup;
        }
        slots[i].claim.tensor = header;
    }

    for (i = 0; i < slot_count; i++)
    {
        tp_buffer_claim_t *claim = &slots[i].claim;
        if (NULL == claim->payload)
        {
            continue;
        }
        memset(claim->payload, 0, claim->payload_len);
        if (tp_producer_commit_claim(producer, claim, &meta) < 0)
        {
            fprintf(stderr, "commit failed: %s\n", tp_errmsg());
        }
        if (tp_producer_queue_claim(producer, claim) < 0)
        {
            fprintf(stderr, "queue failed: %s\n", tp_errmsg());
        }
    }

    result = 0;

cleanup:
    free(slots);
    if (producer_inited)
    {
        tp_producer_close(producer);
    }
    free(pool_cfg);
    if (attach_info_valid)
    {
        tp_driver_attach_info_close(&info);
    }
    if (driver_inited)
    {
        tp_example_detach_driver(driver);
        tp_driver_client_close(driver);
    }
    if (client_inited)
    {
        tp_client_close(client);
    }
    return result;
}
