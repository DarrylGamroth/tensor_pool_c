#include "tensor_pool/tp.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s <aeron_dir> <control_channel> <stream_id> <client_id>\n"
        "  %s <aeron_dir> <control_channel> <stream_id> <client_id> <header_uri> <pool_uri> <pool_id> <pool_stride> <header_nslots> <epoch> <layout_version>\n",
        name,
        name);
}

int main(int argc, char **argv)
{
    tp_client_context_t client_context;
    tp_client_t client;
    tp_driver_client_t driver;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_payload_pool_config_t *pool_cfg = NULL;
    tp_producer_t producer;
    tp_producer_context_t producer_context;
    tp_producer_config_t producer_cfg;
    tp_frame_t frame;
    tp_frame_metadata_t meta;
    tp_tensor_header_t header;
    float payload[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    uint32_t stream_id;
    uint32_t client_id;
    uint32_t header_nslots = 0;
    uint32_t layout_version = 0;
    uint64_t epoch = 0;
    uint16_t pool_id = 0;
    uint32_t pool_stride = 0;
    const char *header_uri = NULL;
    const char *pool_uri = NULL;
    const char *allowed_paths[] = { "/dev/shm", "/tmp" };
    int use_driver = 1;
    size_t i;

    if (argc != 5 && argc != 12)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);

    if (argc == 12)
    {
        use_driver = 0;
        header_uri = argv[5];
        pool_uri = argv[6];
        pool_id = (uint16_t)strtoul(argv[7], NULL, 10);
        pool_stride = (uint32_t)strtoul(argv[8], NULL, 10);
        header_nslots = (uint32_t)strtoul(argv[9], NULL, 10);
        epoch = (uint64_t)strtoull(argv[10], NULL, 10);
        layout_version = (uint32_t)strtoul(argv[11], NULL, 10);
    }

    if (tp_client_context_init(&client_context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    tp_client_context_set_aeron_dir(&client_context, argv[1]);
    tp_client_context_set_control_channel(&client_context, argv[2], 1000);
    tp_client_context_set_descriptor_channel(&client_context, "aeron:ipc", 1100);
    tp_client_context_set_qos_channel(&client_context, "aeron:ipc", 1200);
    tp_client_context_set_metadata_channel(&client_context, "aeron:ipc", 1300);
    tp_context_set_allowed_paths(&client_context.base, allowed_paths, 2);

    if (tp_client_init(&client, &client_context) < 0 || tp_client_start(&client) < 0)
    {
        fprintf(stderr, "Client init failed: %s\n", tp_errmsg());
        return 1;
    }

    if (use_driver)
    {
        if (tp_driver_client_init(&driver, &client) < 0)
        {
            fprintf(stderr, "Driver init failed: %s\n", tp_errmsg());
            return 1;
        }

        memset(&request, 0, sizeof(request));
        request.correlation_id = 1;
        request.stream_id = stream_id;
        request.client_id = client_id;
        request.role = TP_ROLE_PRODUCER;
        request.expected_layout_version = 0;
        request.publish_mode = TP_PUBLISH_MODE_EXISTING_OR_CREATE;
        request.require_hugepages = TP_HUGEPAGES_UNSPECIFIED;

        if (tp_driver_attach(&driver, &request, &info, 2 * 1000 * 1000 * 1000LL) < 0)
        {
            fprintf(stderr, "Attach failed: %s\n", tp_errmsg());
            tp_driver_client_close(&driver);
            return 1;
        }

        if (info.code != TP_RESPONSE_OK)
        {
            fprintf(stderr, "Attach rejected: code=%d error=%s\n", info.code, info.error_message);
            tp_driver_attach_info_close(&info);
            tp_driver_client_close(&driver);
            return 1;
        }

        pool_cfg = calloc(info.pool_count, sizeof(*pool_cfg));
        if (NULL == pool_cfg)
        {
            fprintf(stderr, "Allocation failed\n");
            tp_driver_attach_info_close(&info);
            tp_driver_client_close(&driver);
            return 1;
        }

        for (i = 0; i < info.pool_count; i++)
        {
            pool_cfg[i].pool_id = info.pools[i].pool_id;
            pool_cfg[i].nslots = info.pools[i].nslots;
            pool_cfg[i].stride_bytes = info.pools[i].stride_bytes;
            pool_cfg[i].uri = info.pools[i].region_uri;
        }
    }
    else
    {
        pool_cfg = calloc(1, sizeof(*pool_cfg));
        if (NULL == pool_cfg)
        {
            fprintf(stderr, "Allocation failed\n");
            return 1;
        }
        pool_cfg[0].pool_id = pool_id;
        pool_cfg[0].nslots = header_nslots;
        pool_cfg[0].stride_bytes = pool_stride;
        pool_cfg[0].uri = pool_uri;
    }

    if (tp_producer_context_init(&producer_context) < 0)
    {
        fprintf(stderr, "Producer context init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    producer_context.stream_id = use_driver ? info.stream_id : stream_id;
    producer_context.producer_id = client_id;

    if (tp_producer_init(&producer, &client, &producer_context) < 0)
    {
        fprintf(stderr, "Producer init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    memset(&producer_cfg, 0, sizeof(producer_cfg));
    producer_cfg.stream_id = use_driver ? info.stream_id : stream_id;
    producer_cfg.producer_id = client_id;
    producer_cfg.epoch = use_driver ? info.epoch : epoch;
    producer_cfg.layout_version = use_driver ? info.layout_version : layout_version;
    producer_cfg.header_nslots = use_driver ? info.header_nslots : header_nslots;
    producer_cfg.header_uri = use_driver ? info.header_region_uri : header_uri;
    producer_cfg.pools = pool_cfg;
    producer_cfg.pool_count = use_driver ? info.pool_count : 1;

    if (tp_producer_attach(&producer, &producer_cfg) < 0)
    {
        fprintf(stderr, "Producer attach failed: %s\n", tp_errmsg());
        tp_producer_close(&producer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
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
    frame.tensor = &header;
    frame.payload = payload;
    frame.payload_len = sizeof(payload);
    frame.pool_id = pool_cfg[0].pool_id;
    meta.timestamp_ns = 0;
    meta.meta_version = 0;

    {
        int64_t position = tp_producer_offer_frame(&producer, &frame, &meta);
        if (position < 0)
        {
            fprintf(stderr, "Publish failed: %s\n", tp_errmsg());
        }
        else
        {
            printf("Published frame pool_id=%u seq=%" PRIi64 "\n", pool_cfg[0].pool_id, position);
        }
    }

    tp_producer_close(&producer);
    free(pool_cfg);
    if (use_driver)
    {
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
    }
    tp_client_close(&client);

    return 0;
}
