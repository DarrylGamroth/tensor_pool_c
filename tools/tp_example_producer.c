#include "tensor_pool/tp_control.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_driver_client.h"
#include "tensor_pool/tp_producer.h"
#include "tensor_pool/tp_tensor.h"

#include "driver/tensor_pool/publishMode.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/hugepagesPolicy.h"
#include "driver/tensor_pool/responseCode.h"
#include "wire/tensor_pool/dtype.h"
#include "wire/tensor_pool/majorOrder.h"
#include "wire/tensor_pool/progressUnit.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s <aeron_dir> <control_channel> <stream_id> <client_id>\n", name);
}

int main(int argc, char **argv)
{
    tp_context_t context;
    tp_driver_client_t driver;
    tp_driver_attach_request_t request;
    tp_driver_attach_info_t info;
    tp_payload_pool_config_t *pool_cfg = NULL;
    tp_producer_t producer;
    tp_producer_config_t producer_cfg;
    tp_tensor_header_t header;
    float payload[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    uint32_t stream_id;
    uint32_t client_id;
    size_t i;

    if (argc != 5)
    {
        usage(argv[0]);
        return 1;
    }

    stream_id = (uint32_t)strtoul(argv[3], NULL, 10);
    client_id = (uint32_t)strtoul(argv[4], NULL, 10);

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
    request.role = tensor_pool_role_PRODUCER;
    request.expected_layout_version = 0;
    request.publish_mode = tensor_pool_publishMode_EXISTING_OR_CREATE;
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

    if (tp_producer_init(&producer, &context) < 0)
    {
        fprintf(stderr, "Producer init failed: %s\n", tp_errmsg());
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    memset(&producer_cfg, 0, sizeof(producer_cfg));
    producer_cfg.stream_id = info.stream_id;
    producer_cfg.producer_id = client_id;
    producer_cfg.epoch = info.epoch;
    producer_cfg.layout_version = info.layout_version;
    producer_cfg.header_nslots = info.header_nslots;
    producer_cfg.header_uri = info.header_region_uri;
    producer_cfg.pools = pool_cfg;
    producer_cfg.pool_count = info.pool_count;

    if (tp_producer_attach_direct(&producer, &producer_cfg) < 0)
    {
        fprintf(stderr, "Producer attach failed: %s\n", tp_errmsg());
        tp_producer_close(&producer);
        free(pool_cfg);
        tp_driver_attach_info_close(&info);
        tp_driver_client_close(&driver);
        return 1;
    }

    memset(&header, 0, sizeof(header));
    header.dtype = tensor_pool_dtype_FLOAT32;
    header.major_order = tensor_pool_majorOrder_ROW;
    header.ndims = 2;
    header.progress_unit = tensor_pool_progressUnit_NONE;
    header.dims[0] = 2;
    header.dims[1] = 2;

    if (tp_tensor_header_validate(&header, NULL) < 0)
    {
        fprintf(stderr, "Tensor header invalid: %s\n", tp_errmsg());
    }
    else if (tp_producer_publish_frame(
        &producer,
        1,
        0,
        &header,
        payload,
        sizeof(payload),
        pool_cfg[0].pool_id,
        0,
        0) < 0)
    {
        fprintf(stderr, "Publish failed: %s\n", tp_errmsg());
    }
    else
    {
        printf("Published frame seq=1 header_index=0 pool_id=%u\n", pool_cfg[0].pool_id);
    }

    tp_producer_close(&producer);
    free(pool_cfg);
    tp_driver_attach_info_close(&info);
    tp_driver_client_close(&driver);

    return 0;
}
