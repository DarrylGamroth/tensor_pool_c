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

#include "wire/tensor_pool/messageHeader.h"
#include "wire/tensor_pool/mode.h"
#include "wire/tensor_pool/qosConsumer.h"
#include "wire/tensor_pool/qosProducer.h"
#include "wire/tensor_pool/regionType.h"
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

    return 0;
}
