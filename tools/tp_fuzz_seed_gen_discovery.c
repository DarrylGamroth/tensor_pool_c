#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tensor_pool/tp_types.h"

#include "discovery/tensor_pool/discoveryResponse.h"
#include "discovery/tensor_pool/discoveryStatus.h"
#include "discovery/tensor_pool/messageHeader.h"
#include "discovery/tensor_pool/varAsciiEncoding.h"

static int tp_fuzz_mkdir(const char *path)
{
    if (mkdir(path, 0755) == 0)
    {
        return 0;
    }

    if (errno == EEXIST)
    {
        return 0;
    }

    return -1;
}

static int tp_fuzz_write(const char *dir, const char *name, const void *data, size_t length)
{
    char path[512];
    FILE *file;
    size_t written;

    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
    {
        return -1;
    }

    file = fopen(path, "wb");
    if (!file)
    {
        return -1;
    }

    written = fwrite(data, 1, length, file);
    fclose(file);

    return (written == length) ? 0 : -1;
}

static int tp_fuzz_seed_discovery_response(const char *dir)
{
    uint8_t buffer[2048];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_discoveryResponse response;
    struct tensor_pool_discoveryResponse_results results;
    struct tensor_pool_discoveryResponse_results_payloadPools pools;
    struct tensor_pool_discoveryResponse_results_tags tags;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_discoveryResponse_sbe_block_length();
    const char *header_uri = "shm:///tmp/header";
    const char *data_source_name = "camera";
    const char *driver_instance = "driver-1";
    const char *driver_channel = "aeron:ipc";
    const char *pool_uri = "shm:///tmp/pool1";
    const char *tag = "tag1";

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_discoveryResponse_sbe_schema_version());

    tensor_pool_discoveryResponse_wrap_for_encode(&response, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_discoveryResponse_set_requestId(&response, 1);
    tensor_pool_discoveryResponse_set_status(&response, tensor_pool_discoveryStatus_OK);
    tensor_pool_discoveryResponse_put_errorMessage(&response, "", 0);

    if (NULL == tensor_pool_discoveryResponse_results_wrap_for_encode(
        &results,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_sbe_position_ptr(&response),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        return -1;
    }

    if (NULL == tensor_pool_discoveryResponse_results_next(&results))
    {
        return -1;
    }

    tensor_pool_discoveryResponse_results_set_streamId(&results, 1000);
    tensor_pool_discoveryResponse_results_set_producerId(&results, 1);
    tensor_pool_discoveryResponse_results_set_epoch(&results, 1);
    tensor_pool_discoveryResponse_results_set_layoutVersion(&results, TP_LAYOUT_VERSION);
    tensor_pool_discoveryResponse_results_set_headerNslots(&results, 8);
    tensor_pool_discoveryResponse_results_set_headerSlotBytes(&results, TP_HEADER_SLOT_BYTES);
    tensor_pool_discoveryResponse_results_set_maxDims(&results, TP_MAX_DIMS);
    tensor_pool_discoveryResponse_results_set_dataSourceId(&results, 1);
    tensor_pool_discoveryResponse_results_set_driverControlStreamId(&results, 1000);

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        return -1;
    }

    if (NULL == tensor_pool_discoveryResponse_results_payloadPools_next(&pools))
    {
        return -1;
    }

    tensor_pool_discoveryResponse_results_payloadPools_set_poolId(&pools, 1);
    tensor_pool_discoveryResponse_results_payloadPools_set_poolNslots(&pools, 8);
    tensor_pool_discoveryResponse_results_payloadPools_set_strideBytes(&pools, 1024);
    if (tensor_pool_discoveryResponse_results_payloadPools_put_regionUri(&pools, pool_uri, strlen(pool_uri)) < 0)
    {
        return -1;
    }

    if (NULL == tensor_pool_discoveryResponse_results_tags_wrap_for_encode(
        &tags,
        (char *)buffer,
        1,
        tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        sizeof(buffer)))
    {
        return -1;
    }

    if (NULL == tensor_pool_discoveryResponse_results_tags_next(&tags))
    {
        return -1;
    }

    {
        struct tensor_pool_varAsciiEncoding tag_codec;
        size_t tag_len = strlen(tag);

        if (NULL == tensor_pool_discoveryResponse_results_tags_tag(&tags, &tag_codec))
        {
            return -1;
        }

        tensor_pool_varAsciiEncoding_set_length(&tag_codec, (uint32_t)tag_len);
        memcpy(tag_codec.buffer + tag_codec.offset + 4, tag, tag_len);
        if (!tensor_pool_discoveryResponse_results_tags_set_sbe_position(
            &tags,
            tag_codec.offset + 4 + tag_len))
        {
            return -1;
        }
    }

    if (tensor_pool_discoveryResponse_results_put_headerRegionUri(&results, header_uri, strlen(header_uri)) < 0)
    {
        return -1;
    }

    if (tensor_pool_discoveryResponse_results_put_dataSourceName(&results, data_source_name, strlen(data_source_name)) < 0)
    {
        return -1;
    }

    if (tensor_pool_discoveryResponse_results_put_driverInstanceId(&results, driver_instance, strlen(driver_instance)) < 0)
    {
        return -1;
    }

    if (tensor_pool_discoveryResponse_results_put_driverControlChannel(&results, driver_channel, strlen(driver_channel)) < 0)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "discovery_response.bin", buffer, tensor_pool_discoveryResponse_sbe_position(&response));
}

int main(int argc, char **argv)
{
    const char *base = "fuzz/corpus";
    char dir[512];

    if (argc > 1 && argv[1][0] != '\0')
    {
        base = argv[1];
    }

    if (tp_fuzz_mkdir("fuzz") < 0 || tp_fuzz_mkdir(base) < 0)
    {
        return 1;
    }

    if (snprintf(dir, sizeof(dir), "%s/%s", base, "tp_fuzz_discovery_decode") >= (int)sizeof(dir))
    {
        return 1;
    }

    if (tp_fuzz_mkdir(dir) < 0)
    {
        return 1;
    }

    if (tp_fuzz_seed_discovery_response(dir) < 0)
    {
        return 1;
    }

    return 0;
}
