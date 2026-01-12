#include "tensor_pool/tp_slot.h"

#include <errno.h>
#include <string.h>

#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"

#include "wire/tensor_pool/slotHeader.h"

uint8_t *tp_slot_at(void *base, uint32_t index)
{
    return (uint8_t *)base + TP_SUPERBLOCK_SIZE_BYTES + (index * TP_HEADER_SLOT_BYTES);
}

int tp_slot_decode(tp_slot_view_t *view, const uint8_t *slot, size_t slot_length, tp_log_t *log)
{
    struct tensor_pool_slotHeader header;
    const char *header_bytes;

    if (NULL == view || NULL == slot)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_slot_decode: null input");
        return -1;
    }

    if (slot_length < TP_HEADER_SLOT_BYTES)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_slot_decode: slot too small");
        return -1;
    }

    tensor_pool_slotHeader_wrap_for_decode(
        &header,
        (char *)slot,
        0,
        tensor_pool_slotHeader_sbe_block_length(),
        tensor_pool_slotHeader_sbe_schema_version(),
        slot_length);

    view->seq_commit = tensor_pool_slotHeader_seqCommit(&header);
    view->values_len_bytes = tensor_pool_slotHeader_valuesLenBytes(&header);
    view->payload_slot = tensor_pool_slotHeader_payloadSlot(&header);
    view->pool_id = tensor_pool_slotHeader_poolId(&header);
    view->payload_offset = tensor_pool_slotHeader_payloadOffset(&header);
    view->timestamp_ns = tensor_pool_slotHeader_timestampNs(&header);
    view->meta_version = tensor_pool_slotHeader_metaVersion(&header);

    header_bytes = tensor_pool_slotHeader_headerBytes(&header);
    if (NULL == header_bytes)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_slot_decode: headerBytes missing");
        return -1;
    }

    view->header_bytes_length = tensor_pool_slotHeader_headerBytes_length(&header);
    view->header_bytes = (const uint8_t *)header_bytes;

    if (NULL != log)
    {
        tp_log_emit(log, TP_LOG_DEBUG, "Decoded slot header bytes len=%u", view->header_bytes_length);
    }

    return 0;
}
