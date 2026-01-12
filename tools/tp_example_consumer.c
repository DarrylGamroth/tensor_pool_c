#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_consumer.h"
#include "tensor_pool/tp_tensor.h"

#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/responseCode.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s <aeron_dir> <control_channel> <stream_id> <client_id> <seq> <header_index>\n", name);
}

int main(int argc, char **argv)
{
    tp_context_t context;
    tp_driver_client_t driver;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_consumer_pool_config_t *pool_cfg = NULL;
    tp_consumer_t consumer;
    tp_consumer_config_t consumer_cfg;
    tp_frame_view_t frame;
    uint32_t stream_id;
    uint32_t client_id;
    uint64_t seq;
    uint32_t header_index;
    size_t i;
    int result;

    if (argc != 7)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);
    seq = (uint64_t)strtoull(argv[5], NULL, 10);
    header_index = (uint32_t)strtoul(argv[6], NULL, 10);

    if (tp_context_init(&context) < 0)
    {
        fprintf(stderr, "Failed to init context\n");
        return 1;
    }

    tp_context_set_aeron_dir(&context, argv[1]);
    tp_context_set_control_channel(&context, argv[2], 1000);
    tp_context_set_descriptor_channel(&context, "aeron:ipc", 1100);
    tp_context_set_qos_channel(&context, "aeron:ipc", 1200);
    tp_context_set_metadata_channel(&context, "aeron:ipc", 1300);

    if (tp_driver_client_init(&driver, &context) < 0)
    {
        fprintf(stderr, "Driver init failed: %s\n", tp_errmsg());
        return 1;
    }

    memset(&request, 0, sizeof(request));
    request.correlation_id = 1;
    request.stream_id = stream_id;
    request.client_id = client_id;
    request.role = tensor_pool_role_CONSUMER;
    request.expected_layout_version = 0;
    request.publish_mode = tensor_pool_publishMode_REQUIRE_EXISTING;
    request.require_hugepages = tensor_pool_hugepagesPolicy_UNSPECIFIED;

    if (tp_driver_attach(&driver, &request, &info, 2 * 1000 * 1000 * 1000LL) < 0)
    {
        fprintf(stderr, "Attach failed: %s\n", tp_errmsg());
        tp_driver_client_close(&driver);
        return 1;
    }

    if (info.code != tensor_pool_responseCode_OK)
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

    if (tp_consumer_init(&consumer, &context) < 0)
    {
        fprintf(stderr, "Consumer init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    memset(&consumer_cfg, 0, sizeof(consumer_cfg));
    consumer_cfg.stream_id = info.stream_id;
    consumer_cfg.epoch = info.epoch;
    consumer_cfg.layout_version = info.layout_version;
    consumer_cfg.header_nslots = info.header_nslots;
    consumer_cfg.header_uri = info.header_region_uri;
    consumer_cfg.pools = pool_cfg;
    consumer_cfg.pool_count = info.pool_count;

    if (tp_consumer_attach_direct(&consumer, &consumer_cfg) < 0)
    {
        fprintf(stderr, "Consumer attach failed: %s\n", tp_errmsg());
        tp_consumer_close(&consumer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    result = tp_consumer_read_frame(&consumer, seq, header_index, &frame);
    if (result == 0)
    {
        printf("Frame seq=%" PRIu64 " dims=%d x %d payload_len=%u\n",
            seq,
            frame.tensor.dims[0],
            frame.tensor.dims[1],
            frame.payload_len);
        if (frame.payload_len >= sizeof(float))
        {
            const float *values = (const float *)frame.payload;
            printf("payload[0]=%f\n", values[0]);
        }
    }
    else
    {
        printf("Frame not available (result=%d)\n", result);
    }

    tp_consumer_close(&consumer);
    free(pool_cfg);
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(&driver);

    return 0;
}
