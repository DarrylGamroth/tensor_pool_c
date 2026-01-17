#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tensor_pool/tp_merge_map.h"
#include "tensor_pool/tp_seqlock.h"
#include "tensor_pool/tp_slot.h"
#include "tensor_pool/tp_tracelink.h"
#include "tensor_pool/tp_types.h"

#include "wire/tensor_pool/clockDomain.h"
#include "wire/tensor_pool/consumerConfig.h"
#include "wire/tensor_pool/consumerHello.h"
#include "wire/tensor_pool/controlResponse.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"
#include "wire/tensor_pool/dataSourceMeta.h"
#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/metaBlobAnnounce.h"
#include "wire/tensor_pool/metaBlobChunk.h"
#include "wire/tensor_pool/metaBlobComplete.h"
#include "wire/tensor_pool/mode.h"
#include "wire/tensor_pool/qosConsumer.h"
#include "wire/tensor_pool/qosProducer.h"
#include "wire/tensor_pool/regionType.h"
#include "wire/tensor_pool/responseCode.h"
#include "wire/tensor_pool/shmPoolAnnounce.h"
#include "wire/tensor_pool/shmRegionSuperblock.h"
#include "wire/tensor_pool/slotHeader.h"
#include "wire/tensor_pool/tensorHeader.h"

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

static void tp_fuzz_store_u16_le(uint8_t *buffer, size_t *offset, uint16_t value)
{
    buffer[(*offset)++] = (uint8_t)(value & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 8u) & 0xffu);
}

static void tp_fuzz_store_u32_le(uint8_t *buffer, size_t *offset, uint32_t value)
{
    buffer[(*offset)++] = (uint8_t)(value & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 8u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 16u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 24u) & 0xffu);
}

static void tp_fuzz_store_u64_le(uint8_t *buffer, size_t *offset, uint64_t value)
{
    buffer[(*offset)++] = (uint8_t)(value & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 8u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 16u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 24u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 32u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 40u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 48u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 56u) & 0xffu);
}

static size_t tp_fuzz_build_tensor_header(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_tensorHeader tensor;
    size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t i;

    if (capacity < header_len + tensor_pool_tensorHeader_sbe_block_length())
    {
        return 0;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        capacity);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_tensorHeader_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_tensorHeader_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_tensorHeader_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_tensorHeader_sbe_schema_version());

    tensor_pool_tensorHeader_wrap_for_encode(&tensor, (char *)buffer, header_len, capacity);
    tensor_pool_tensorHeader_set_dtype(&tensor, tensor_pool_dtype_FLOAT32);
    tensor_pool_tensorHeader_set_majorOrder(&tensor, tensor_pool_majorOrder_ROW);
    tensor_pool_tensorHeader_set_ndims(&tensor, 1);
    tensor_pool_tensorHeader_set_progressUnit(&tensor, tensor_pool_progressUnit_NONE);
    tensor_pool_tensorHeader_set_progressStrideBytes(&tensor, 0);

    for (i = 0; i < 8; i++)
    {
        tensor_pool_tensorHeader_set_dims(&tensor, i, (i == 0) ? 1 : 0);
        tensor_pool_tensorHeader_set_strides(&tensor, i, 0);
    }

    return header_len + (size_t)tensor_pool_tensorHeader_encoded_length(&tensor);
}

static int tp_fuzz_seed_tracelink(const char *dir)
{
    uint8_t buffer[256];
    size_t out_len = 0;
    uint64_t parents[2];
    tp_tracelink_set_t set;

    parents[0] = 0x1111u;
    parents[1] = 0x2222u;
    set.stream_id = 1000;
    set.epoch = 1;
    set.seq = 0;
    set.trace_id = 0x1234u;
    set.parents = parents;
    set.parent_count = 2;

    if (tp_tracelink_set_encode(buffer, sizeof(buffer), &set, &out_len) < 0)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "trace_link_set.bin", buffer, out_len);
}

static int tp_fuzz_seed_merge_map(const char *dir)
{
    uint8_t buffer[512];
    size_t out_len = 0;
    tp_sequence_merge_rule_t seq_rules[1];
    tp_timestamp_merge_rule_t ts_rules[1];
    tp_sequence_merge_map_t seq_map;
    tp_timestamp_merge_map_t ts_map;
    int rc;

    seq_rules[0].input_stream_id = 1001;
    seq_rules[0].rule_type = TP_MERGE_RULE_OFFSET;
    seq_rules[0].offset = 0;
    seq_rules[0].window_size = 0;

    seq_map.out_stream_id = 2000;
    seq_map.epoch = 1;
    seq_map.stale_timeout_ns = 1000000;
    seq_map.rules = seq_rules;
    seq_map.rule_count = 1;

    rc = tp_sequence_merge_map_encode(buffer, sizeof(buffer), &seq_map, &out_len);
    if (rc < 0 || tp_fuzz_write(dir, "sequence_map.bin", buffer, out_len) < 0)
    {
        return -1;
    }

    ts_rules[0].input_stream_id = 1002;
    ts_rules[0].rule_type = TP_MERGE_TIME_OFFSET_NS;
    ts_rules[0].timestamp_source = TP_TIMESTAMP_SOURCE_FRAME_DESCRIPTOR;
    ts_rules[0].offset_ns = 0;
    ts_rules[0].window_ns = 0;

    ts_map.out_stream_id = 2001;
    ts_map.epoch = 1;
    ts_map.stale_timeout_ns = 1000000;
    ts_map.clock_domain = TP_CLOCK_DOMAIN_MONOTONIC;
    ts_map.lateness_ns = 0;
    ts_map.rules = ts_rules;
    ts_map.rule_count = 1;

    rc = tp_timestamp_merge_map_encode(buffer, sizeof(buffer), &ts_map, &out_len);
    if (rc < 0 || tp_fuzz_write(dir, "timestamp_map.bin", buffer, out_len) < 0)
    {
        return -1;
    }

    rc = tp_sequence_merge_map_request_encode(buffer, sizeof(buffer), 2000, 1, &out_len);
    if (rc < 0 || tp_fuzz_write(dir, "sequence_request.bin", buffer, out_len) < 0)
    {
        return -1;
    }

    rc = tp_timestamp_merge_map_request_encode(buffer, sizeof(buffer), 2001, 1, &out_len);
    if (rc < 0 || tp_fuzz_write(dir, "timestamp_request.bin", buffer, out_len) < 0)
    {
        return -1;
    }

    return 0;
}

static int tp_fuzz_seed_consumer_hello(const char *dir)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_consumerHello msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_consumerHello_sbe_block_length();
    const char *channel = "aeron:ipc";

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_consumerHello_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_consumerHello_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_consumerHello_sbe_schema_version());

    tensor_pool_consumerHello_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_consumerHello_set_streamId(&msg, 1000);
    tensor_pool_consumerHello_set_consumerId(&msg, 2);
    tensor_pool_consumerHello_set_supportsShm(&msg, 1);
    tensor_pool_consumerHello_set_supportsProgress(&msg, 1);
    tensor_pool_consumerHello_set_mode(&msg, tensor_pool_mode_STREAM);
    tensor_pool_consumerHello_set_maxRateHz(&msg, 0);
    tensor_pool_consumerHello_set_expectedLayoutVersion(&msg, TP_LAYOUT_VERSION);
    tensor_pool_consumerHello_set_progressIntervalUs(&msg, tensor_pool_consumerHello_progressIntervalUs_null_value());
    tensor_pool_consumerHello_set_progressBytesDelta(&msg, tensor_pool_consumerHello_progressBytesDelta_null_value());
    tensor_pool_consumerHello_set_progressMajorDeltaUnits(&msg, tensor_pool_consumerHello_progressMajorDeltaUnits_null_value());
    tensor_pool_consumerHello_set_descriptorStreamId(&msg, 1100);
    tensor_pool_consumerHello_set_controlStreamId(&msg, 1000);
    if (tensor_pool_consumerHello_put_descriptorChannel(&msg, channel, strlen(channel)) < 0)
    {
        return -1;
    }
    if (tensor_pool_consumerHello_put_controlChannel(&msg, channel, strlen(channel)) < 0)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "consumer_hello.bin", buffer, tensor_pool_consumerHello_sbe_position(&msg));
}

static int tp_fuzz_seed_consumer_config(const char *dir)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_consumerConfig msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_consumerConfig_sbe_block_length();
    const char *channel = "aeron:ipc";

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_consumerConfig_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_consumerConfig_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_consumerConfig_sbe_schema_version());

    tensor_pool_consumerConfig_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_consumerConfig_set_streamId(&msg, 1000);
    tensor_pool_consumerConfig_set_consumerId(&msg, 2);
    tensor_pool_consumerConfig_set_useShm(&msg, 1);
    tensor_pool_consumerConfig_set_mode(&msg, tensor_pool_mode_STREAM);
    tensor_pool_consumerConfig_set_descriptorStreamId(&msg, 1100);
    tensor_pool_consumerConfig_set_controlStreamId(&msg, 1000);
    if (tensor_pool_consumerConfig_put_payloadFallbackUri(&msg, "", 0) < 0)
    {
        return -1;
    }
    if (tensor_pool_consumerConfig_put_descriptorChannel(&msg, channel, strlen(channel)) < 0)
    {
        return -1;
    }
    if (tensor_pool_consumerConfig_put_controlChannel(&msg, channel, strlen(channel)) < 0)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "consumer_config.bin", buffer, tensor_pool_consumerConfig_sbe_position(&msg));
}

static int tp_fuzz_seed_control_response(const char *dir)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_controlResponse msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_controlResponse_sbe_block_length();

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_controlResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_controlResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_controlResponse_sbe_schema_version());

    tensor_pool_controlResponse_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_controlResponse_set_correlationId(&msg, 1);
    tensor_pool_controlResponse_set_code(&msg, tensor_pool_responseCode_OK);
    if (tensor_pool_controlResponse_put_errorMessage(&msg, "ok", 2) < 0)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "control_response.bin", buffer, tensor_pool_controlResponse_sbe_position(&msg));
}

static int tp_fuzz_seed_data_source_announce(const char *dir)
{
    uint8_t buffer[512];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_dataSourceAnnounce msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_dataSourceAnnounce_sbe_block_length();

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_dataSourceAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_dataSourceAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_dataSourceAnnounce_sbe_schema_version());

    tensor_pool_dataSourceAnnounce_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_dataSourceAnnounce_set_streamId(&msg, 1000);
    tensor_pool_dataSourceAnnounce_set_producerId(&msg, 1);
    tensor_pool_dataSourceAnnounce_set_epoch(&msg, 1);
    tensor_pool_dataSourceAnnounce_set_metaVersion(&msg, 1);
    if (tensor_pool_dataSourceAnnounce_put_name(&msg, "camera", 6) < 0)
    {
        return -1;
    }
    if (tensor_pool_dataSourceAnnounce_put_summary(&msg, "example", 7) < 0)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "data_source_announce.bin", buffer, tensor_pool_dataSourceAnnounce_sbe_position(&msg));
}

static int tp_fuzz_seed_data_source_meta(const char *dir)
{
    uint8_t buffer[2048];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_dataSourceMeta msg;
    struct tensor_pool_dataSourceMeta_attributes attrs;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_dataSourceMeta_sbe_block_length();

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_dataSourceMeta_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_dataSourceMeta_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_dataSourceMeta_sbe_schema_version());

    tensor_pool_dataSourceMeta_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_dataSourceMeta_set_streamId(&msg, 1000);
    tensor_pool_dataSourceMeta_set_metaVersion(&msg, 1);
    tensor_pool_dataSourceMeta_set_timestampNs(&msg, 0);

    if (NULL == tensor_pool_dataSourceMeta_attributes_wrap_for_encode(
        &attrs,
        (char *)buffer,
        1,
        tensor_pool_dataSourceMeta_sbe_position_ptr(&msg),
        tensor_pool_dataSourceMeta_sbe_schema_version(),
        sizeof(buffer)))
    {
        return -1;
    }

    if (NULL == tensor_pool_dataSourceMeta_attributes_next(&attrs))
    {
        return -1;
    }

    if (tensor_pool_dataSourceMeta_attributes_put_key(&attrs, "dtype", 5) < 0)
    {
        return -1;
    }
    if (tensor_pool_dataSourceMeta_attributes_put_format(&attrs, "string", 6) < 0)
    {
        return -1;
    }
    if (tensor_pool_dataSourceMeta_attributes_put_value(&attrs, "float32", 7) < 0)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "data_source_meta.bin", buffer, tensor_pool_dataSourceMeta_sbe_position(&msg));
}

static int tp_fuzz_seed_meta_blob_announce(const char *dir)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_metaBlobAnnounce msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_metaBlobAnnounce_sbe_block_length();

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_metaBlobAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_metaBlobAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_metaBlobAnnounce_sbe_schema_version());

    tensor_pool_metaBlobAnnounce_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_metaBlobAnnounce_set_streamId(&msg, 1000);
    tensor_pool_metaBlobAnnounce_set_metaVersion(&msg, 1);
    tensor_pool_metaBlobAnnounce_set_blobType(&msg, 1);
    tensor_pool_metaBlobAnnounce_set_totalLen(&msg, 4);
    tensor_pool_metaBlobAnnounce_set_checksum(&msg, 0x1234u);

    return tp_fuzz_write(dir, "meta_blob_announce.bin", buffer, tensor_pool_metaBlobAnnounce_sbe_position(&msg));
}

static int tp_fuzz_seed_meta_blob_chunk(const char *dir)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_metaBlobChunk msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_metaBlobChunk_sbe_block_length();
    const char *bytes = "abcd";

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_metaBlobChunk_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_metaBlobChunk_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_metaBlobChunk_sbe_schema_version());

    tensor_pool_metaBlobChunk_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_metaBlobChunk_set_streamId(&msg, 1000);
    tensor_pool_metaBlobChunk_set_metaVersion(&msg, 1);
    tensor_pool_metaBlobChunk_set_chunkOffset(&msg, 0);
    if (tensor_pool_metaBlobChunk_put_bytes(&msg, bytes, 4) < 0)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "meta_blob_chunk.bin", buffer, tensor_pool_metaBlobChunk_sbe_position(&msg));
}

static int tp_fuzz_seed_meta_blob_complete(const char *dir)
{
    uint8_t buffer[256];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_metaBlobComplete msg;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_metaBlobComplete_sbe_block_length();

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_metaBlobComplete_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_metaBlobComplete_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_metaBlobComplete_sbe_schema_version());

    tensor_pool_metaBlobComplete_wrap_for_encode(&msg, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_metaBlobComplete_set_streamId(&msg, 1000);
    tensor_pool_metaBlobComplete_set_metaVersion(&msg, 1);
    tensor_pool_metaBlobComplete_set_checksum(&msg, 0x1234u);

    return tp_fuzz_write(dir, "meta_blob_complete.bin", buffer, tensor_pool_metaBlobComplete_sbe_position(&msg));
}

static int tp_fuzz_seed_shm_pool_announce(const char *dir)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmPoolAnnounce msg;
    struct tensor_pool_shmPoolAnnounce_payloadPools pools;
    const size_t header_len = tensor_pool_messageHeader_encoded_length();
    const size_t body_len = tensor_pool_shmPoolAnnounce_sbe_block_length();
    const char *header_uri = "shm:///tmp/header";
    const char *pool_uri = "shm:///tmp/pool1";
    size_t buffer_len = header_len + body_len +
        tensor_pool_shmPoolAnnounce_payloadPools_sbe_header_size() +
        tensor_pool_shmPoolAnnounce_headerRegionUri_header_length() +
        strlen(header_uri) +
        tensor_pool_shmPoolAnnounce_payloadPools_sbe_block_length() +
        tensor_pool_shmPoolAnnounce_payloadPools_regionUri_header_length() +
        strlen(pool_uri);
    uint8_t *buffer = calloc(1, buffer_len);
    int result = -1;

    if (buffer == NULL)
    {
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, (uint16_t)body_len);
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_shmPoolAnnounce_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_shmPoolAnnounce_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_shmPoolAnnounce_sbe_schema_version());

    tensor_pool_shmPoolAnnounce_wrap_for_encode(&msg, (char *)buffer, header_len, buffer_len);
    tensor_pool_shmPoolAnnounce_set_streamId(&msg, 1000);
    tensor_pool_shmPoolAnnounce_set_producerId(&msg, 1);
    tensor_pool_shmPoolAnnounce_set_epoch(&msg, 1);
    tensor_pool_shmPoolAnnounce_set_announceTimestampNs(&msg, 0);
    tensor_pool_shmPoolAnnounce_set_layoutVersion(&msg, TP_LAYOUT_VERSION);
    tensor_pool_shmPoolAnnounce_set_headerNslots(&msg, 8);
    tensor_pool_shmPoolAnnounce_set_headerSlotBytes(&msg, TP_HEADER_SLOT_BYTES);
    tensor_pool_shmPoolAnnounce_set_announceClockDomain(&msg, tensor_pool_clockDomain_MONOTONIC);

    if (NULL == tensor_pool_shmPoolAnnounce_payloadPools_wrap_for_encode(
        &pools,
        (char *)buffer,
        1,
        tensor_pool_shmPoolAnnounce_sbe_position_ptr(&msg),
        tensor_pool_shmPoolAnnounce_sbe_schema_version(),
        buffer_len))
    {
        goto cleanup;
    }

    if (!tensor_pool_shmPoolAnnounce_payloadPools_next(&pools))
    {
        goto cleanup;
    }

    tensor_pool_shmPoolAnnounce_payloadPools_set_poolId(&pools, 1);
    tensor_pool_shmPoolAnnounce_payloadPools_set_poolNslots(&pools, 8);
    tensor_pool_shmPoolAnnounce_payloadPools_set_strideBytes(&pools, 1024);
    if (tensor_pool_shmPoolAnnounce_payloadPools_put_regionUri(&pools, pool_uri, strlen(pool_uri)) < 0)
    {
        goto cleanup;
    }

    if (tensor_pool_shmPoolAnnounce_put_headerRegionUri(&msg, header_uri, strlen(header_uri)) < 0)
    {
        goto cleanup;
    }

    result = tp_fuzz_write(dir, "shm_pool_announce.bin", buffer, tensor_pool_shmPoolAnnounce_sbe_position(&msg));

cleanup:
    free(buffer);
    return result;
}

static int tp_fuzz_seed_qos(const char *dir)
{
    uint8_t buffer[128];
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_qosProducer qos_producer;
    struct tensor_pool_qosConsumer qos_consumer;
    size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t len;

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        sizeof(buffer));
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_qosProducer_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_qosProducer_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_qosProducer_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_qosProducer_sbe_schema_version());

    tensor_pool_qosProducer_wrap_for_encode(&qos_producer, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_qosProducer_set_streamId(&qos_producer, 1000);
    tensor_pool_qosProducer_set_producerId(&qos_producer, 1);
    tensor_pool_qosProducer_set_epoch(&qos_producer, 1);
    tensor_pool_qosProducer_set_currentSeq(&qos_producer, 42);
    tensor_pool_qosProducer_set_watermark(&qos_producer, 128);

    len = header_len + tensor_pool_qosProducer_sbe_block_length();
    if (tp_fuzz_write(dir, "qos_producer.bin", buffer, len) < 0)
    {
        return -1;
    }

    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_qosConsumer_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_qosConsumer_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_qosConsumer_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_qosConsumer_sbe_schema_version());

    tensor_pool_qosConsumer_wrap_for_encode(&qos_consumer, (char *)buffer, header_len, sizeof(buffer));
    tensor_pool_qosConsumer_set_streamId(&qos_consumer, 1000);
    tensor_pool_qosConsumer_set_consumerId(&qos_consumer, 2);
    tensor_pool_qosConsumer_set_epoch(&qos_consumer, 1);
    tensor_pool_qosConsumer_set_lastSeqSeen(&qos_consumer, 40);
    tensor_pool_qosConsumer_set_dropsGap(&qos_consumer, 1);
    tensor_pool_qosConsumer_set_dropsLate(&qos_consumer, 0);
    tensor_pool_qosConsumer_set_mode(&qos_consumer, tensor_pool_mode_STREAM);

    len = header_len + tensor_pool_qosConsumer_sbe_block_length();
    return tp_fuzz_write(dir, "qos_consumer.bin", buffer, len);
}

static int tp_fuzz_seed_slot_tensor(const char *dir)
{
    uint8_t slot[TP_HEADER_SLOT_BYTES];
    uint8_t header_bytes[256];
    struct tensor_pool_slotHeader slot_header;
    size_t header_len;

    memset(slot, 0, sizeof(slot));
    memset(header_bytes, 0, sizeof(header_bytes));

    header_len = tp_fuzz_build_tensor_header(header_bytes, sizeof(header_bytes));
    if (header_len == 0)
    {
        return -1;
    }

    tensor_pool_slotHeader_wrap_for_encode(&slot_header, (char *)slot, 0, sizeof(slot));
    tensor_pool_slotHeader_set_seqCommit(&slot_header, tp_seq_committed(1));
    tensor_pool_slotHeader_set_valuesLenBytes(&slot_header, 64);
    tensor_pool_slotHeader_set_payloadSlot(&slot_header, 0);
    tensor_pool_slotHeader_set_poolId(&slot_header, 1);
    tensor_pool_slotHeader_set_payloadOffset(&slot_header, 0);
    tensor_pool_slotHeader_set_timestampNs(&slot_header, 0);
    tensor_pool_slotHeader_set_metaVersion(&slot_header, 0);
    if (tensor_pool_slotHeader_put_headerBytes(&slot_header, (const char *)header_bytes, (uint32_t)header_len) == NULL)
    {
        return -1;
    }

    return tp_fuzz_write(dir, "slot_tensor.bin", slot, sizeof(slot));
}

static int tp_fuzz_seed_progress(const char *dir)
{
    uint8_t buffer[256];
    uint8_t header_bytes[256];
    size_t header_len;
    size_t offset = 0;

    memset(buffer, 0, sizeof(buffer));
    header_len = tp_fuzz_build_tensor_header(header_bytes, sizeof(header_bytes));
    if (header_len == 0)
    {
        return -1;
    }

    buffer[offset++] = 0x1u;
    tp_fuzz_store_u64_le(buffer, &offset, 1);
    tp_fuzz_store_u32_le(buffer, &offset, 1000);
    tp_fuzz_store_u64_le(buffer, &offset, 1);
    tp_fuzz_store_u32_le(buffer, &offset, 256);
    tp_fuzz_store_u32_le(buffer, &offset, 64);
    tp_fuzz_store_u16_le(buffer, &offset, 1);
    tp_fuzz_store_u32_le(buffer, &offset, 32);
    tp_fuzz_store_u32_le(buffer, &offset, TP_PROGRESS_PROGRESS);
    tp_fuzz_store_u64_le(buffer, &offset, 0);
    tp_fuzz_store_u32_le(buffer, &offset, 0);

    if (offset + header_len > sizeof(buffer))
    {
        return -1;
    }

    memcpy(buffer + offset, header_bytes, header_len);
    offset += header_len;

    return tp_fuzz_write(dir, "progress.bin", buffer, offset);
}

static int tp_fuzz_seed_claim(const char *dir)
{
    uint8_t buffer[32];
    size_t offset = 0;

    buffer[offset++] = 0x1u;
    tp_fuzz_store_u32_le(buffer, &offset, 512);
    tp_fuzz_store_u32_le(buffer, &offset, 128);
    tp_fuzz_store_u32_le(buffer, &offset, 1);

    return tp_fuzz_write(dir, "claim.bin", buffer, offset);
}

static int tp_fuzz_seed_superblock(const char *dir)
{
    uint8_t buffer[TP_SUPERBLOCK_SIZE_BYTES];
    struct tensor_pool_shmRegionSuperblock superblock;

    memset(buffer, 0, sizeof(buffer));

    tensor_pool_shmRegionSuperblock_wrap_for_encode(&superblock, (char *)buffer, 0, sizeof(buffer));
    tensor_pool_shmRegionSuperblock_set_magic(&superblock, TP_MAGIC_U64);
    tensor_pool_shmRegionSuperblock_set_layoutVersion(&superblock, TP_LAYOUT_VERSION);
    tensor_pool_shmRegionSuperblock_set_epoch(&superblock, 1);
    tensor_pool_shmRegionSuperblock_set_streamId(&superblock, 1000);
    tensor_pool_shmRegionSuperblock_set_regionType(&superblock, tensor_pool_regionType_HEADER_RING);
    tensor_pool_shmRegionSuperblock_set_poolId(&superblock, 0);
    tensor_pool_shmRegionSuperblock_set_nslots(&superblock, 8);
    tensor_pool_shmRegionSuperblock_set_slotBytes(&superblock, TP_HEADER_SLOT_BYTES);
    tensor_pool_shmRegionSuperblock_set_strideBytes(&superblock, 0);
    tensor_pool_shmRegionSuperblock_set_activityTimestampNs(&superblock, 0);
    tensor_pool_shmRegionSuperblock_set_pid(&superblock, 0);

    return tp_fuzz_write(dir, "superblock.bin", buffer, sizeof(buffer));
}

static int tp_fuzz_prepare_dir(const char *base, const char *name, char *out, size_t out_len)
{
    if (snprintf(out, out_len, "%s/%s", base, name) >= (int)out_len)
    {
        return -1;
    }

    return tp_fuzz_mkdir(out);
}

int main(int argc, char **argv)
{
    const char *base = "fuzz/corpus";
    char dir[512];
    char control_poller_dir[512];
    char control_decode_dir[512];
    char metadata_poller_dir[512];

    if (argc > 1 && argv[1][0] != '\0')
    {
        base = argv[1];
    }

    if (tp_fuzz_mkdir("fuzz") < 0 || tp_fuzz_mkdir(base) < 0)
    {
        return 1;
    }

    if (tp_fuzz_prepare_dir(base, "tp_fuzz_tracelink_decode", dir, sizeof(dir)) < 0 ||
        tp_fuzz_seed_tracelink(dir) < 0)
    {
        return 1;
    }

    if (tp_fuzz_prepare_dir(base, "tp_fuzz_merge_map_decode", dir, sizeof(dir)) < 0 ||
        tp_fuzz_seed_merge_map(dir) < 0)
    {
        return 1;
    }

    if (tp_fuzz_prepare_dir(base, "tp_fuzz_qos_poller", dir, sizeof(dir)) < 0 ||
        tp_fuzz_seed_qos(dir) < 0)
    {
        return 1;
    }

    if (tp_fuzz_prepare_dir(base, "tp_fuzz_slot_tensor_decode", dir, sizeof(dir)) < 0 ||
        tp_fuzz_seed_slot_tensor(dir) < 0)
    {
        return 1;
    }

    if (tp_fuzz_prepare_dir(base, "tp_fuzz_progress_validate", dir, sizeof(dir)) < 0 ||
        tp_fuzz_seed_progress(dir) < 0)
    {
        return 1;
    }

    if (tp_fuzz_prepare_dir(base, "tp_fuzz_producer_claim", dir, sizeof(dir)) < 0 ||
        tp_fuzz_seed_claim(dir) < 0)
    {
        return 1;
    }

    if (tp_fuzz_prepare_dir(base, "tp_fuzz_shm_superblock", dir, sizeof(dir)) < 0 ||
        tp_fuzz_seed_superblock(dir) < 0)
    {
        return 1;
    }

    if (tp_fuzz_prepare_dir(base, "tp_fuzz_control_poller", control_poller_dir, sizeof(control_poller_dir)) < 0 ||
        tp_fuzz_prepare_dir(base, "tp_fuzz_control_decode", control_decode_dir, sizeof(control_decode_dir)) < 0 ||
        tp_fuzz_prepare_dir(base, "tp_fuzz_metadata_poller", metadata_poller_dir, sizeof(metadata_poller_dir)) < 0)
    {
        return 1;
    }

    {
        if (tp_fuzz_seed_consumer_hello(control_poller_dir) < 0 ||
            tp_fuzz_seed_consumer_hello(control_decode_dir) < 0 ||
            tp_fuzz_seed_consumer_config(control_poller_dir) < 0 ||
            tp_fuzz_seed_consumer_config(control_decode_dir) < 0 ||
            tp_fuzz_seed_control_response(control_poller_dir) < 0 ||
            tp_fuzz_seed_control_response(control_decode_dir) < 0 ||
            tp_fuzz_seed_shm_pool_announce(control_poller_dir) < 0 ||
            tp_fuzz_seed_shm_pool_announce(control_decode_dir) < 0)
        {
            return 1;
        }

        if (tp_fuzz_seed_data_source_announce(metadata_poller_dir) < 0 ||
            tp_fuzz_seed_data_source_announce(control_decode_dir) < 0 ||
            tp_fuzz_seed_data_source_announce(control_poller_dir) < 0 ||
            tp_fuzz_seed_data_source_meta(metadata_poller_dir) < 0 ||
            tp_fuzz_seed_data_source_meta(control_decode_dir) < 0 ||
            tp_fuzz_seed_data_source_meta(control_poller_dir) < 0)
        {
            return 1;
        }

        if (tp_fuzz_seed_meta_blob_announce(metadata_poller_dir) < 0 ||
            tp_fuzz_seed_meta_blob_announce(control_decode_dir) < 0 ||
            tp_fuzz_seed_meta_blob_chunk(metadata_poller_dir) < 0 ||
            tp_fuzz_seed_meta_blob_chunk(control_decode_dir) < 0 ||
            tp_fuzz_seed_meta_blob_complete(metadata_poller_dir) < 0 ||
            tp_fuzz_seed_meta_blob_complete(control_decode_dir) < 0)
        {
            return 1;
        }

        if (tp_fuzz_seed_tracelink(control_poller_dir) < 0 ||
            tp_fuzz_seed_tracelink(control_decode_dir) < 0)
        {
            return 1;
        }

        if (tp_fuzz_seed_merge_map(control_poller_dir) < 0 ||
            tp_fuzz_seed_merge_map(control_decode_dir) < 0)
        {
            return 1;
        }
    }

    return 0;
}
