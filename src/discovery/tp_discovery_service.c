#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_discovery_service.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "tensor_pool/tp_clock.h"
#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_types.h"
#include "tp_aeron_wrap.h"

#include "wire/tensor_pool/shmPoolAnnounce.h"
#include "wire/tensor_pool/dataSourceAnnounce.h"

#include "discovery/tensor_pool/messageHeader.h"
#include "discovery/tensor_pool/discoveryRequest.h"
#include "discovery/tensor_pool/discoveryResponse.h"

typedef struct tp_discovery_pool_entry_stct
{
    uint16_t pool_id;
    uint32_t pool_nslots;
    uint32_t stride_bytes;
    char region_uri[4096];
}
tp_discovery_pool_entry_t;

typedef struct tp_discovery_entry_stct
{
    uint32_t stream_id;
    uint32_t producer_id;
    uint64_t epoch;
    uint32_t layout_version;
    uint32_t header_nslots;
    uint16_t header_slot_bytes;
    uint8_t max_dims;
    uint64_t data_source_id;
    char header_region_uri[4096];
    char data_source_name[256];
    char **tags;
    size_t tag_count;
    tp_discovery_pool_entry_t *pools;
    size_t pool_count;
    uint64_t last_announce_ns;
}
tp_discovery_entry_t;

typedef struct tp_discovery_publication_stct
{
    char channel[1024];
    int32_t stream_id;
    tp_publication_t *publication;
}
tp_discovery_publication_t;

static tp_discovery_entry_t *tp_discovery_entries(tp_discovery_service_t *service)
{
    return (tp_discovery_entry_t *)service->entries;
}

static tp_discovery_publication_t *tp_discovery_publications(tp_discovery_service_t *service)
{
    return (tp_discovery_publication_t *)service->publications;
}

static tp_discovery_entry_t *tp_discovery_find_entry(tp_discovery_service_t *service, uint32_t stream_id)
{
    size_t i;

    if (NULL == service || stream_id == 0)
    {
        return NULL;
    }

    for (i = 0; i < service->entry_count; i++)
    {
        if (tp_discovery_entries(service)[i].stream_id == stream_id)
        {
            return &tp_discovery_entries(service)[i];
        }
    }

    return NULL;
}

static int tp_discovery_entry_reserve(tp_discovery_service_t *service, uint32_t stream_id, tp_discovery_entry_t **out)
{
    tp_discovery_entry_t *entries;
    size_t new_count;

    if (NULL == service || NULL == out)
    {
        return -1;
    }

    *out = tp_discovery_find_entry(service, stream_id);
    if (*out)
    {
        return 0;
    }

    new_count = service->entry_count + 1;
    entries = (tp_discovery_entry_t *)realloc(service->entries, new_count * sizeof(*entries));
    if (NULL == entries)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_discovery_entry_reserve: allocation failed");
        return -1;
    }

    memset(&entries[new_count - 1], 0, sizeof(entries[new_count - 1]));
    entries[new_count - 1].stream_id = stream_id;
    entries[new_count - 1].data_source_id = TP_NULL_U64;
    entries[new_count - 1].max_dims = TP_MAX_DIMS;

    service->entries = entries;
    service->entry_count = new_count;
    *out = &entries[new_count - 1];
    return 0;
}

static void tp_discovery_entry_clear(tp_discovery_entry_t *entry)
{
    size_t i;

    if (NULL == entry)
    {
        return;
    }

    free(entry->pools);
    entry->pools = NULL;
    entry->pool_count = 0;

    for (i = 0; i < entry->tag_count; i++)
    {
        free(entry->tags[i]);
    }
    free(entry->tags);
    entry->tags = NULL;
    entry->tag_count = 0;
}

static uint64_t tp_discovery_expiry_ns(const tp_discovery_service_t *service)
{
    uint64_t period_ns = (uint64_t)service->config.announce_period_ms * 1000000ULL;
    if (period_ns == 0)
    {
        period_ns = 1000ULL * 1000000ULL;
    }
    return period_ns * 3ULL;
}

static void tp_discovery_prune_expired(tp_discovery_service_t *service)
{
    size_t i = 0;
    uint64_t now = tp_clock_now_ns();
    uint64_t expiry = tp_discovery_expiry_ns(service);

    while (i < service->entry_count)
    {
        tp_discovery_entry_t *entry = &tp_discovery_entries(service)[i];
        if (entry->last_announce_ns != 0 && now > entry->last_announce_ns + expiry)
        {
            tp_discovery_entry_clear(entry);
            if (i + 1 < service->entry_count)
            {
                memmove(entry, entry + 1, (service->entry_count - i - 1) * sizeof(*entry));
            }
            service->entry_count--;
            continue;
        }
        i++;
    }
}

int tp_discovery_service_apply_announce(
    tp_discovery_service_t *service,
    const tp_shm_pool_announce_t *announce)
{
    tp_discovery_entry_t *entry = NULL;
    size_t i;
    uint64_t now;

    if (NULL == service || NULL == announce || announce->stream_id == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_apply_announce: invalid input");
        return -1;
    }

    if (announce->header_slot_bytes != TP_HEADER_SLOT_BYTES)
    {
        tp_log_emit(&service->config.base.log, TP_LOG_WARN,
            "tp_discovery: ignoring announce with invalid header_slot_bytes");
        return -1;
    }

    if (announce->pool_count == 0 || announce->header_nslots == 0)
    {
        tp_log_emit(&service->config.base.log, TP_LOG_WARN,
            "tp_discovery: ignoring announce with empty pools");
        return -1;
    }

    if (tp_discovery_entry_reserve(service, announce->stream_id, &entry) < 0)
    {
        return -1;
    }

    if (entry->epoch != 0 && announce->epoch < entry->epoch)
    {
        return 0;
    }

    tp_discovery_entry_clear(entry);

    entry->producer_id = announce->producer_id;
    entry->epoch = announce->epoch;
    entry->layout_version = announce->layout_version;
    entry->header_nslots = announce->header_nslots;
    entry->header_slot_bytes = announce->header_slot_bytes;
    entry->max_dims = TP_MAX_DIMS;

    if (announce->header_region_uri)
    {
        strncpy(entry->header_region_uri, announce->header_region_uri, sizeof(entry->header_region_uri) - 1);
    }
    else
    {
        entry->header_region_uri[0] = '\0';
    }

    entry->pool_count = announce->pool_count;
    entry->pools = (tp_discovery_pool_entry_t *)calloc(entry->pool_count, sizeof(*entry->pools));
    if (NULL == entry->pools)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_discovery_service_apply_announce: pool allocation failed");
        return -1;
    }

    for (i = 0; i < entry->pool_count; i++)
    {
        const tp_shm_pool_announce_pool_t *pool = &announce->pools[i];

        if (pool->pool_nslots != announce->header_nslots)
        {
            tp_log_emit(&service->config.base.log, TP_LOG_WARN,
                "tp_discovery: ignoring announce with pool_nslots mismatch");
            tp_discovery_entry_clear(entry);
            return -1;
        }

        entry->pools[i].pool_id = pool->pool_id;
        entry->pools[i].pool_nslots = pool->pool_nslots;
        entry->pools[i].stride_bytes = pool->stride_bytes;
        if (pool->region_uri)
        {
            strncpy(entry->pools[i].region_uri, pool->region_uri, sizeof(entry->pools[i].region_uri) - 1);
        }
    }

    now = tp_clock_now_ns();
    entry->last_announce_ns = now;
    return 0;
}

int tp_discovery_service_apply_data_source(
    tp_discovery_service_t *service,
    const tp_data_source_announce_t *announce)
{
    tp_discovery_entry_t *entry = NULL;

    if (NULL == service || NULL == announce || announce->stream_id == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_apply_data_source: invalid input");
        return -1;
    }

    if (tp_discovery_entry_reserve(service, announce->stream_id, &entry) < 0)
    {
        return -1;
    }

    if (entry->epoch != 0 && announce->epoch < entry->epoch)
    {
        return 0;
    }

    entry->producer_id = announce->producer_id;
    entry->epoch = announce->epoch;

    if (announce->name)
    {
        strncpy(entry->data_source_name, announce->name, sizeof(entry->data_source_name) - 1);
    }

    entry->last_announce_ns = tp_clock_now_ns();
    return 0;
}

int tp_discovery_service_set_tags(
    tp_discovery_service_t *service,
    uint32_t stream_id,
    const char **tags,
    size_t tag_count)
{
    tp_discovery_entry_t *entry = NULL;
    size_t i;

    if (NULL == service || stream_id == 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_set_tags: invalid input");
        return -1;
    }

    if (tp_discovery_entry_reserve(service, stream_id, &entry) < 0)
    {
        return -1;
    }

    for (i = 0; i < entry->tag_count; i++)
    {
        free(entry->tags[i]);
    }
    free(entry->tags);
    entry->tags = NULL;
    entry->tag_count = 0;

    if (NULL == tags || tag_count == 0)
    {
        entry->last_announce_ns = tp_clock_now_ns();
        return 0;
    }

    entry->tags = (char **)calloc(tag_count, sizeof(char *));
    if (NULL == entry->tags)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_discovery_service_set_tags: tags allocation failed");
        return -1;
    }
    entry->tag_count = tag_count;

    for (i = 0; i < tag_count; i++)
    {
        const char *tag = tags[i] ? tags[i] : "";
        size_t len = strlen(tag) + 1;
        entry->tags[i] = (char *)malloc(len);
        if (NULL == entry->tags[i])
        {
            size_t j;

            for (j = 0; j < i; j++)
            {
                free(entry->tags[j]);
            }
            free(entry->tags);
            entry->tags = NULL;
            entry->tag_count = 0;
            TP_SET_ERR(ENOMEM, "%s", "tp_discovery_service_set_tags: tag copy failed");
            return -1;
        }
        memcpy(entry->tags[i], tag, len);
    }

    entry->last_announce_ns = tp_clock_now_ns();
    return 0;
}

static bool tp_discovery_entry_has_tag(const tp_discovery_entry_t *entry, const char *tag)
{
    size_t i;

    if (NULL == entry || NULL == tag)
    {
        return false;
    }

    for (i = 0; i < entry->tag_count; i++)
    {
        if (0 == strcmp(entry->tags[i], tag))
        {
            return true;
        }
    }

    return false;
}

static bool tp_discovery_entry_matches(
    const tp_discovery_entry_t *entry,
    const tp_discovery_request_t *request)
{
    size_t i;

    if (request->stream_id != TP_NULL_U32 && request->stream_id != entry->stream_id)
    {
        return false;
    }

    if (request->producer_id != TP_NULL_U32 && request->producer_id != entry->producer_id)
    {
        return false;
    }

    if (request->data_source_id != TP_NULL_U64 && request->data_source_id != entry->data_source_id)
    {
        return false;
    }

    if (request->data_source_name && request->data_source_name[0] != '\0')
    {
        if (0 != strcmp(request->data_source_name, entry->data_source_name))
        {
            return false;
        }
    }

    if (request->tag_count > 0)
    {
        for (i = 0; i < request->tag_count; i++)
        {
            if (!tp_discovery_entry_has_tag(entry, request->tags[i]))
            {
                return false;
            }
        }
    }

    return true;
}

static void tp_discovery_result_fill(
    tp_discovery_result_t *result,
    const tp_discovery_entry_t *entry,
    const tp_discovery_service_t *service)
{
    size_t i;

    memset(result, 0, sizeof(*result));
    result->stream_id = entry->stream_id;
    result->producer_id = entry->producer_id;
    result->epoch = entry->epoch;
    result->layout_version = entry->layout_version;
    result->header_nslots = entry->header_nslots;
    result->header_slot_bytes = entry->header_slot_bytes;
    result->max_dims = entry->max_dims;
    result->data_source_id = entry->data_source_id;
    result->driver_control_stream_id = (uint32_t)service->config.driver_control_stream_id;
    strncpy(result->header_region_uri, entry->header_region_uri, sizeof(result->header_region_uri) - 1);
    strncpy(result->data_source_name, entry->data_source_name, sizeof(result->data_source_name) - 1);
    strncpy(result->driver_instance_id, service->config.driver_instance_id, sizeof(result->driver_instance_id) - 1);
    strncpy(result->driver_control_channel, service->config.driver_control_channel,
        sizeof(result->driver_control_channel) - 1);

    result->pool_count = entry->pool_count;
    if (entry->pool_count > 0)
    {
        result->pools = (tp_discovery_pool_info_t *)calloc(entry->pool_count, sizeof(*result->pools));
        if (result->pools)
        {
            for (i = 0; i < entry->pool_count; i++)
            {
                result->pools[i].pool_id = entry->pools[i].pool_id;
                result->pools[i].nslots = entry->pools[i].pool_nslots;
                result->pools[i].stride_bytes = entry->pools[i].stride_bytes;
                strncpy(result->pools[i].region_uri, entry->pools[i].region_uri,
                    sizeof(result->pools[i].region_uri) - 1);
            }
        }
    }

    result->tag_count = entry->tag_count;
    if (entry->tag_count > 0)
    {
        result->tags = (char **)calloc(entry->tag_count, sizeof(char *));
        if (result->tags)
        {
            for (i = 0; i < entry->tag_count; i++)
            {
                size_t len = strlen(entry->tags[i]) + 1;
                result->tags[i] = (char *)malloc(len);
                if (result->tags[i])
                {
                    memcpy(result->tags[i], entry->tags[i], len);
                }
            }
        }
    }
}

int tp_discovery_service_query(
    tp_discovery_service_t *service,
    const tp_discovery_request_t *request,
    tp_discovery_response_t *response)
{
    size_t i;
    size_t match_count = 0;
    uint64_t now;
    uint64_t expiry;

    if (NULL == service || NULL == request || NULL == response)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_query: invalid input");
        return -1;
    }

    memset(response, 0, sizeof(*response));
    response->request_id = request->request_id;
    response->status = tensor_pool_discoveryStatus_OK;

    tp_discovery_prune_expired(service);

    now = tp_clock_now_ns();
    expiry = tp_discovery_expiry_ns(service);

    for (i = 0; i < service->entry_count; i++)
    {
        tp_discovery_entry_t *entry = &tp_discovery_entries(service)[i];
        if (entry->last_announce_ns == 0 || now > entry->last_announce_ns + expiry)
        {
            continue;
        }
        if (tp_discovery_entry_matches(entry, request))
        {
            match_count++;
        }
    }

    if (match_count > service->config.max_results)
    {
        response->status = tensor_pool_discoveryStatus_ERROR;
        strncpy(response->error_message, "result limit exceeded", sizeof(response->error_message) - 1);
        return 0;
    }

    if (match_count == 0)
    {
        response->status = tensor_pool_discoveryStatus_OK;
        return 0;
    }

    response->results = (tp_discovery_result_t *)calloc(match_count, sizeof(*response->results));
    if (NULL == response->results)
    {
        TP_SET_ERR(ENOMEM, "%s", "tp_discovery_service_query: result allocation failed");
        return -1;
    }

    response->result_count = match_count;
    match_count = 0;
    for (i = 0; i < service->entry_count; i++)
    {
        tp_discovery_entry_t *entry = &tp_discovery_entries(service)[i];
        if (entry->last_announce_ns == 0 || now > entry->last_announce_ns + expiry)
        {
            continue;
        }
        if (tp_discovery_entry_matches(entry, request))
        {
            tp_discovery_result_fill(&response->results[match_count++], entry, service);
        }
    }

    return 0;
}

void tp_discovery_service_response_close(tp_discovery_response_t *response)
{
    size_t i;
    size_t j;

    if (NULL == response)
    {
        return;
    }

    for (i = 0; i < response->result_count; i++)
    {
        tp_discovery_result_t *result = &response->results[i];
        free(result->pools);
        result->pools = NULL;
        result->pool_count = 0;
        for (j = 0; j < result->tag_count; j++)
        {
            free(result->tags[j]);
        }
        free(result->tags);
        result->tags = NULL;
        result->tag_count = 0;
    }

    free(response->results);
    response->results = NULL;
    response->result_count = 0;
}

static tp_discovery_publication_t *tp_discovery_get_publication(
    tp_discovery_service_t *service,
    const char *channel,
    int32_t stream_id)
{
    size_t i;
    tp_discovery_publication_t *pubs;

    for (i = 0; i < service->publication_count; i++)
    {
        if (service->publications)
        {
            tp_discovery_publication_t *entry = &tp_discovery_publications(service)[i];
            if (entry->stream_id == stream_id && 0 == strcmp(entry->channel, channel))
            {
                return entry;
            }
        }
    }

    pubs = (tp_discovery_publication_t *)realloc(service->publications,
        (service->publication_count + 1) * sizeof(*pubs));
    if (NULL == pubs)
    {
        return NULL;
    }

    service->publications = pubs;
    pubs = &tp_discovery_publications(service)[service->publication_count];
    memset(pubs, 0, sizeof(*pubs));
    strncpy(pubs->channel, channel, sizeof(pubs->channel) - 1);
    pubs->stream_id = stream_id;

    if (tp_aeron_add_publication(&pubs->publication, &service->aeron, channel, stream_id) < 0)
    {
        return NULL;
    }

    service->publication_count++;
    return pubs;
}

static int tp_discovery_encode_response(
    uint8_t *buffer,
    size_t buffer_len,
    const tp_discovery_response_t *response)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_discoveryResponse resp;
    struct tensor_pool_discoveryResponse_results results;
    size_t header_len = tensor_pool_messageHeader_encoded_length();
    size_t i;

    tensor_pool_messageHeader_wrap(&msg_header, (char *)buffer, 0,
        tensor_pool_messageHeader_sbe_schema_version(), buffer_len);
    tensor_pool_messageHeader_set_blockLength(&msg_header, tensor_pool_discoveryResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&msg_header, tensor_pool_discoveryResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&msg_header, tensor_pool_discoveryResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&msg_header, tensor_pool_discoveryResponse_sbe_schema_version());

    tensor_pool_discoveryResponse_wrap_for_encode(&resp, (char *)buffer, header_len, buffer_len);
    tensor_pool_discoveryResponse_set_requestId(&resp, response->request_id);
    tensor_pool_discoveryResponse_set_status(&resp, (enum tensor_pool_discoveryStatus)response->status);

    if (NULL == tensor_pool_discoveryResponse_results_wrap_for_encode(
        &results,
        (char *)buffer,
        (uint16_t)response->result_count,
        tensor_pool_discoveryResponse_sbe_position_ptr(&resp),
        tensor_pool_discoveryResponse_sbe_schema_version(),
        buffer_len))
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: results wrap failed");
        return -1;
    }

    for (i = 0; i < response->result_count; i++)
    {
        const tp_discovery_result_t *result = &response->results[i];
        struct tensor_pool_discoveryResponse_results_payloadPools pools;
        struct tensor_pool_discoveryResponse_results_tags tags;
        size_t j;

        if (!tensor_pool_discoveryResponse_results_next(&results))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: results next failed");
            return -1;
        }

        tensor_pool_discoveryResponse_results_set_streamId(&results, result->stream_id);
        tensor_pool_discoveryResponse_results_set_producerId(&results, result->producer_id);
        tensor_pool_discoveryResponse_results_set_epoch(&results, result->epoch);
        tensor_pool_discoveryResponse_results_set_layoutVersion(&results, result->layout_version);
        tensor_pool_discoveryResponse_results_set_headerNslots(&results, result->header_nslots);
        tensor_pool_discoveryResponse_results_set_headerSlotBytes(&results, result->header_slot_bytes);
        tensor_pool_discoveryResponse_results_set_maxDims(&results, result->max_dims);
        tensor_pool_discoveryResponse_results_set_dataSourceId(&results, result->data_source_id);
        tensor_pool_discoveryResponse_results_set_driverControlStreamId(&results, result->driver_control_stream_id);

        if (NULL == tensor_pool_discoveryResponse_results_payloadPools_wrap_for_encode(
            &pools,
            (char *)buffer,
            (uint16_t)result->pool_count,
            tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
            tensor_pool_discoveryResponse_sbe_schema_version(),
            buffer_len))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: pools wrap failed");
            return -1;
        }

        for (j = 0; j < result->pool_count; j++)
        {
            if (!tensor_pool_discoveryResponse_results_payloadPools_next(&pools))
            {
                TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: pools next failed");
                return -1;
            }

            tensor_pool_discoveryResponse_results_payloadPools_set_poolId(&pools, result->pools[j].pool_id);
            tensor_pool_discoveryResponse_results_payloadPools_set_poolNslots(&pools, result->pools[j].nslots);
            tensor_pool_discoveryResponse_results_payloadPools_set_strideBytes(&pools, result->pools[j].stride_bytes);
            if (tensor_pool_discoveryResponse_results_payloadPools_put_regionUri(
                &pools, result->pools[j].region_uri, strlen(result->pools[j].region_uri)) < 0)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: pool uri encode failed");
                return -1;
            }
        }

        if (NULL == tensor_pool_discoveryResponse_results_tags_wrap_for_encode(
            &tags,
            (char *)buffer,
            (uint16_t)result->tag_count,
            tensor_pool_discoveryResponse_results_sbe_position_ptr(&results),
            tensor_pool_discoveryResponse_sbe_schema_version(),
            buffer_len))
        {
            TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: tags wrap failed");
            return -1;
        }

        for (j = 0; j < result->tag_count; j++)
        {
            if (!tensor_pool_discoveryResponse_results_tags_next(&tags))
            {
                TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: tags next failed");
                return -1;
            }
            if (tensor_pool_discoveryResponse_results_tags_put_tag(&tags, result->tags[j],
                strlen(result->tags[j])) < 0)
            {
                TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: tag encode failed");
                return -1;
            }
        }

        if (tensor_pool_discoveryResponse_results_put_headerRegionUri(&results,
            result->header_region_uri, strlen(result->header_region_uri)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: header uri encode failed");
            return -1;
        }
        if (tensor_pool_discoveryResponse_results_put_dataSourceName(&results,
            result->data_source_name, strlen(result->data_source_name)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: data source name encode failed");
            return -1;
        }
        if (tensor_pool_discoveryResponse_results_put_driverInstanceId(&results,
            result->driver_instance_id, strlen(result->driver_instance_id)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: driver instance encode failed");
            return -1;
        }
        if (tensor_pool_discoveryResponse_results_put_driverControlChannel(&results,
            result->driver_control_channel, strlen(result->driver_control_channel)) < 0)
        {
            TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: driver control channel encode failed");
            return -1;
        }
    }

    if (tensor_pool_discoveryResponse_put_errorMessage(&resp, response->error_message,
        strlen(response->error_message)) < 0)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_encode_response: error message encode failed");
        return -1;
    }

    return (int)tensor_pool_discoveryResponse_sbe_position(&resp);
}

static void tp_discovery_free_request(tp_discovery_request_t *request)
{
    size_t i;

    if (NULL == request)
    {
        return;
    }

    free((void *)request->response_channel);
    request->response_channel = NULL;
    free((void *)request->data_source_name);
    request->data_source_name = NULL;

    for (i = 0; i < request->tag_count; i++)
    {
        free((void *)request->tags[i]);
    }
    free((void *)request->tags);
    request->tags = NULL;
    request->tag_count = 0;
}

static int tp_discovery_decode_request(
    const uint8_t *buffer,
    size_t length,
    tp_discovery_request_t *out)
{
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_discoveryRequest request;
    struct tensor_pool_discoveryRequest_tags tags;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    size_t i;

    if (NULL == buffer || NULL == out || length < tensor_pool_messageHeader_encoded_length())
    {
        return -1;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);

    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);

    if (schema_id != tensor_pool_discoveryRequest_sbe_schema_id() ||
        template_id != tensor_pool_discoveryRequest_sbe_template_id())
    {
        return 1;
    }

    if (version > tensor_pool_discoveryRequest_sbe_schema_version())
    {
        return -1;
    }

    if (block_length != tensor_pool_discoveryRequest_sbe_block_length())
    {
        return -1;
    }

    tensor_pool_discoveryRequest_wrap_for_decode(
        &request,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    tp_discovery_request_init(out);
    out->request_id = tensor_pool_discoveryRequest_requestId(&request);
    out->client_id = tensor_pool_discoveryRequest_clientId(&request);
    out->response_stream_id = tensor_pool_discoveryRequest_responseStreamId(&request);
    out->stream_id = tensor_pool_discoveryRequest_streamId(&request);
    out->producer_id = tensor_pool_discoveryRequest_producerId(&request);
    out->data_source_id = tensor_pool_discoveryRequest_dataSourceId(&request);

    {
        uint32_t len = tensor_pool_discoveryRequest_responseChannel_length(&request);
        const char *val = tensor_pool_discoveryRequest_responseChannel(&request);
        if (len > 0)
        {
            char *copy = (char *)calloc(len + 1, 1);
            if (NULL == copy)
            {
                TP_SET_ERR(ENOMEM, "%s", "tp_discovery_decode_request: response_channel alloc failed");
                return -1;
            }
            memcpy(copy, val, len);
            out->response_channel = copy;
        }
    }

    {
        uint32_t len = tensor_pool_discoveryRequest_dataSourceName_length(&request);
        const char *val = tensor_pool_discoveryRequest_dataSourceName(&request);
        if (len > 0)
        {
            char *copy = (char *)calloc(len + 1, 1);
            if (NULL == copy)
            {
                TP_SET_ERR(ENOMEM, "%s", "tp_discovery_decode_request: data_source_name alloc failed");
                return -1;
            }
            memcpy(copy, val, len);
            out->data_source_name = copy;
        }
    }

    tensor_pool_discoveryRequest_tags_wrap_for_decode(
        &tags,
        (char *)buffer,
        tensor_pool_discoveryRequest_sbe_position_ptr(&request),
        version,
        length);

    if (tensor_pool_discoveryRequest_tags_count(&tags) > 0)
    {
        out->tag_count = (size_t)tensor_pool_discoveryRequest_tags_count(&tags);
        out->tags = (const char **)calloc(out->tag_count, sizeof(char *));
        if (NULL == out->tags)
        {
            TP_SET_ERR(ENOMEM, "%s", "tp_discovery_decode_request: tags alloc failed");
            return -1;
        }

        for (i = 0; i < out->tag_count; i++)
        {
            uint32_t len;
            const char *val;

            if (!tensor_pool_discoveryRequest_tags_next(&tags))
            {
                break;
            }

            len = tensor_pool_discoveryRequest_tags_tag_length(&tags);
            val = tensor_pool_discoveryRequest_tags_tag(&tags);
            out->tags[i] = (const char *)calloc(len + 1, 1);
            if (out->tags[i])
            {
                memcpy((void *)out->tags[i], val, len);
            }
        }
    }

    return 0;
}

static int tp_discovery_send_error(
    tp_discovery_service_t *service,
    const tp_discovery_request_t *request,
    const char *message)
{
    tp_discovery_response_t response;
    tp_discovery_publication_t *pub;
    uint8_t buffer[1024];
    int encoded_len;

    if (NULL == request || NULL == request->response_channel || request->response_channel[0] == '\0')
    {
        return 0;
    }

    if (request->response_stream_id == 0)
    {
        return 0;
    }

    memset(&response, 0, sizeof(response));
    response.request_id = request->request_id;
    response.status = tensor_pool_discoveryStatus_ERROR;
    strncpy(response.error_message, message ? message : "invalid request", sizeof(response.error_message) - 1);

    encoded_len = tp_discovery_encode_response(buffer, sizeof(buffer), &response);
    if (encoded_len < 0)
    {
        return -1;
    }

    pub = tp_discovery_get_publication(service, request->response_channel, (int32_t)request->response_stream_id);
    if (NULL == pub)
    {
        return -1;
    }

    return (int)aeron_publication_offer(tp_publication_handle(pub->publication), buffer, (size_t)encoded_len, NULL, NULL);
}

static void tp_discovery_handle_request(tp_discovery_service_t *service, const uint8_t *buffer, size_t length)
{
    tp_discovery_request_t request;
    tp_discovery_response_t response;
    tp_discovery_publication_t *pub;
    uint8_t out_buffer[4096];
    int decoded;
    int encoded_len;

    memset(&request, 0, sizeof(request));
    decoded = tp_discovery_decode_request(buffer, length, &request);
    if (decoded != 0)
    {
        tp_discovery_free_request(&request);
        return;
    }

    if (NULL == request.response_channel || request.response_channel[0] == '\0')
    {
        tp_discovery_free_request(&request);
        return;
    }

    if (request.response_stream_id == 0)
    {
        tp_discovery_send_error(service, &request, "response_stream_id must be non-zero");
        tp_discovery_free_request(&request);
        return;
    }

    if (tp_discovery_service_query(service, &request, &response) < 0)
    {
        tp_discovery_send_error(service, &request, tp_errmsg());
        tp_discovery_free_request(&request);
        return;
    }

    encoded_len = tp_discovery_encode_response(out_buffer, sizeof(out_buffer), &response);
    if (encoded_len >= 0)
    {
        pub = tp_discovery_get_publication(service, request.response_channel, (int32_t)request.response_stream_id);
        if (pub)
        {
            aeron_publication_offer(tp_publication_handle(pub->publication),
                out_buffer, (size_t)encoded_len, NULL, NULL);
        }
    }

    tp_discovery_service_response_close(&response);
    tp_discovery_free_request(&request);
}

static void tp_discovery_on_request_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_discovery_service_t *service = (tp_discovery_service_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    uint16_t schema_id;

    (void)header;

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);

    if (schema_id != tensor_pool_discoveryRequest_sbe_schema_id())
    {
        return;
    }

    tp_discovery_handle_request(service, buffer, length);
}

static void tp_discovery_on_announce_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_discovery_service_t *service = (tp_discovery_service_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_shmPoolAnnounce announce;
    struct tensor_pool_shmPoolAnnounce_payloadPools pools;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    tp_shm_pool_announce_t msg;
    tp_shm_pool_announce_pool_t *pool_items = NULL;
    size_t i;

    (void)header;

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);

    if (schema_id != tensor_pool_shmPoolAnnounce_sbe_schema_id() ||
        template_id != tensor_pool_shmPoolAnnounce_sbe_template_id())
    {
        return;
    }

    if (version > tensor_pool_shmPoolAnnounce_sbe_schema_version() ||
        block_length != tensor_pool_shmPoolAnnounce_sbe_block_length())
    {
        return;
    }

    tensor_pool_shmPoolAnnounce_wrap_for_decode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(&msg, 0, sizeof(msg));
    msg.stream_id = tensor_pool_shmPoolAnnounce_streamId(&announce);
    msg.producer_id = tensor_pool_shmPoolAnnounce_producerId(&announce);
    msg.epoch = tensor_pool_shmPoolAnnounce_epoch(&announce);
    msg.announce_timestamp_ns = tensor_pool_shmPoolAnnounce_announceTimestampNs(&announce);
    msg.layout_version = tensor_pool_shmPoolAnnounce_layoutVersion(&announce);
    msg.header_nslots = tensor_pool_shmPoolAnnounce_headerNslots(&announce);
    msg.header_slot_bytes = tensor_pool_shmPoolAnnounce_headerSlotBytes(&announce);
    msg.header_region_uri = tensor_pool_shmPoolAnnounce_headerRegionUri(&announce);
    msg.pool_count = 0;

    tensor_pool_shmPoolAnnounce_payloadPools_wrap_for_decode(
        &pools,
        (char *)buffer,
        tensor_pool_shmPoolAnnounce_sbe_position_ptr(&announce),
        version,
        length);

    msg.pool_count = (size_t)tensor_pool_shmPoolAnnounce_payloadPools_count(&pools);
    if (msg.pool_count > 0)
    {
        pool_items = (tp_shm_pool_announce_pool_t *)calloc(msg.pool_count, sizeof(*pool_items));
        if (NULL == pool_items)
        {
            return;
        }

        for (i = 0; i < msg.pool_count; i++)
        {
            if (!tensor_pool_shmPoolAnnounce_payloadPools_next(&pools))
            {
                break;
            }

            pool_items[i].pool_id = tensor_pool_shmPoolAnnounce_payloadPools_poolId(&pools);
            pool_items[i].pool_nslots = tensor_pool_shmPoolAnnounce_payloadPools_poolNslots(&pools);
            pool_items[i].stride_bytes = tensor_pool_shmPoolAnnounce_payloadPools_strideBytes(&pools);
            pool_items[i].region_uri = tensor_pool_shmPoolAnnounce_payloadPools_regionUri(&pools);
        }
    }

    msg.pools = pool_items;
    tp_discovery_service_apply_announce(service, &msg);
    free(pool_items);
}

static void tp_discovery_on_metadata_fragment(
    void *clientd,
    const uint8_t *buffer,
    size_t length,
    aeron_header_t *header)
{
    tp_discovery_service_t *service = (tp_discovery_service_t *)clientd;
    struct tensor_pool_messageHeader msg_header;
    struct tensor_pool_dataSourceAnnounce announce;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t block_length;
    uint16_t version;
    tp_data_source_announce_t msg;

    (void)header;

    if (length < tensor_pool_messageHeader_encoded_length())
    {
        return;
    }

    tensor_pool_messageHeader_wrap(
        &msg_header,
        (char *)buffer,
        0,
        tensor_pool_messageHeader_sbe_schema_version(),
        length);
    template_id = tensor_pool_messageHeader_templateId(&msg_header);
    schema_id = tensor_pool_messageHeader_schemaId(&msg_header);
    block_length = tensor_pool_messageHeader_blockLength(&msg_header);
    version = tensor_pool_messageHeader_version(&msg_header);

    if (schema_id != tensor_pool_dataSourceAnnounce_sbe_schema_id() ||
        template_id != tensor_pool_dataSourceAnnounce_sbe_template_id())
    {
        return;
    }

    if (version > tensor_pool_dataSourceAnnounce_sbe_schema_version() ||
        block_length != tensor_pool_dataSourceAnnounce_sbe_block_length())
    {
        return;
    }

    tensor_pool_dataSourceAnnounce_wrap_for_decode(
        &announce,
        (char *)buffer,
        tensor_pool_messageHeader_encoded_length(),
        block_length,
        version,
        length);

    memset(&msg, 0, sizeof(msg));
    msg.stream_id = tensor_pool_dataSourceAnnounce_streamId(&announce);
    msg.producer_id = tensor_pool_dataSourceAnnounce_producerId(&announce);
    msg.epoch = tensor_pool_dataSourceAnnounce_epoch(&announce);
    msg.meta_version = tensor_pool_dataSourceAnnounce_metaVersion(&announce);
    msg.name = tensor_pool_dataSourceAnnounce_name(&announce);
    msg.summary = tensor_pool_dataSourceAnnounce_summary(&announce);

    tp_discovery_service_apply_data_source(service, &msg);
}

int tp_discovery_service_init(tp_discovery_service_t *service, tp_discovery_service_config_t *config)
{
    if (NULL == service || NULL == config)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_init: null input");
        return -1;
    }

    memset(service, 0, sizeof(*service));
    service->config = *config;
    memset(config, 0, sizeof(*config));
    return 0;
}

int tp_discovery_service_start(tp_discovery_service_t *service)
{
    if (NULL == service)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_start: null service");
        return -1;
    }

    if (tp_aeron_client_init(&service->aeron, &service->config.base) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_subscription(&service->request_subscription, &service->aeron,
            service->config.request_channel, service->config.request_stream_id) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_subscription(&service->announce_subscription, &service->aeron,
            service->config.announce_channel, service->config.announce_stream_id) < 0)
    {
        return -1;
    }

    if (tp_aeron_add_subscription(&service->metadata_subscription, &service->aeron,
            service->config.metadata_channel, service->config.metadata_stream_id) < 0)
    {
        return -1;
    }

    if (tp_fragment_assembler_create(&service->request_assembler, tp_discovery_on_request_fragment, service) < 0)
    {
        return -1;
    }

    if (tp_fragment_assembler_create(&service->announce_assembler, tp_discovery_on_announce_fragment, service) < 0)
    {
        return -1;
    }

    if (tp_fragment_assembler_create(&service->metadata_assembler, tp_discovery_on_metadata_fragment, service) < 0)
    {
        return -1;
    }

    return 0;
}

int tp_discovery_service_do_work(tp_discovery_service_t *service)
{
    int fragments = 0;

    if (NULL == service)
    {
        TP_SET_ERR(EINVAL, "%s", "tp_discovery_service_do_work: null service");
        return -1;
    }

    if (service->announce_subscription && service->announce_assembler)
    {
        fragments += aeron_subscription_poll(
            tp_subscription_handle(service->announce_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(service->announce_assembler),
            10);
    }

    if (service->metadata_subscription && service->metadata_assembler)
    {
        fragments += aeron_subscription_poll(
            tp_subscription_handle(service->metadata_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(service->metadata_assembler),
            10);
    }

    if (service->request_subscription && service->request_assembler)
    {
        fragments += aeron_subscription_poll(
            tp_subscription_handle(service->request_subscription),
            aeron_fragment_assembler_handler,
            tp_fragment_assembler_handle(service->request_assembler),
            10);
    }

    tp_discovery_prune_expired(service);
    return fragments;
}

int tp_discovery_service_close(tp_discovery_service_t *service)
{
    size_t i;

    if (NULL == service)
    {
        return 0;
    }

    tp_fragment_assembler_close(&service->request_assembler);
    tp_fragment_assembler_close(&service->announce_assembler);
    tp_fragment_assembler_close(&service->metadata_assembler);
    tp_subscription_close(&service->request_subscription);
    tp_subscription_close(&service->announce_subscription);
    tp_subscription_close(&service->metadata_subscription);

    for (i = 0; i < service->publication_count; i++)
    {
        tp_publication_close(&tp_discovery_publications(service)[i].publication);
    }
    free(service->publications);
    service->publications = NULL;
    service->publication_count = 0;

    for (i = 0; i < service->entry_count; i++)
    {
        tp_discovery_entry_clear(&tp_discovery_entries(service)[i]);
    }
    free(service->entries);
    service->entries = NULL;
    service->entry_count = 0;

    tp_aeron_client_close(&service->aeron);
    tp_discovery_service_config_close(&service->config);
    memset(service, 0, sizeof(*service));
    return 0;
}
