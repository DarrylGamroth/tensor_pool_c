#include "tensor_pool/internal/tp_control_poller.h"

#include "driver/tensor_pool/messageHeader.h"
#include "driver/tensor_pool/shmDetachResponse.h"
#include "driver/tensor_pool/shmLeaseRevoked.h"
#include "driver/tensor_pool/shmDriverShutdown.h"
#include "driver/tensor_pool/role.h"
#include "driver/tensor_pool/leaseRevokeReason.h"
#include "driver/tensor_pool/responseCode.h"
#include "driver/tensor_pool/shutdownReason.h"

#include <assert.h>
#include <string.h>

typedef struct tp_control_poller_driver_state_stct
{
    int detach;
    int lease_revoked;
    int shutdown;
}
tp_control_poller_driver_state_t;

static void tp_on_detach_response(const tp_driver_detach_info_t *info, void *clientd)
{
    tp_control_poller_driver_state_t *state = (tp_control_poller_driver_state_t *)clientd;
    (void)info;
    state->detach++;
}

static void tp_on_lease_revoked(const tp_driver_lease_revoked_t *info, void *clientd)
{
    tp_control_poller_driver_state_t *state = (tp_control_poller_driver_state_t *)clientd;
    (void)info;
    state->lease_revoked++;
}

static void tp_on_shutdown(const tp_driver_shutdown_t *info, void *clientd)
{
    tp_control_poller_driver_state_t *state = (tp_control_poller_driver_state_t *)clientd;
    (void)info;
    state->shutdown++;
}

static size_t encode_detach_response(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmDetachResponse response;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmDetachResponse_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmDetachResponse_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmDetachResponse_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmDetachResponse_sbe_schema_version());

    tensor_pool_shmDetachResponse_wrap_for_encode(&response, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_shmDetachResponse_set_correlationId(&response, 44);
    tensor_pool_shmDetachResponse_set_code(&response, tensor_pool_responseCode_OK);
    tensor_pool_shmDetachResponse_put_errorMessage(&response, "ok", 2);

    return (size_t)tensor_pool_shmDetachResponse_sbe_position(&response);
}

static size_t encode_lease_revoked(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmLeaseRevoked revoked;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmLeaseRevoked_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmLeaseRevoked_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmLeaseRevoked_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmLeaseRevoked_sbe_schema_version());

    tensor_pool_shmLeaseRevoked_wrap_for_encode(&revoked, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_shmLeaseRevoked_set_timestampNs(&revoked, 99);
    tensor_pool_shmLeaseRevoked_set_leaseId(&revoked, 101);
    tensor_pool_shmLeaseRevoked_set_streamId(&revoked, 202);
    tensor_pool_shmLeaseRevoked_set_clientId(&revoked, 303);
    tensor_pool_shmLeaseRevoked_set_role(&revoked, tensor_pool_role_PRODUCER);
    tensor_pool_shmLeaseRevoked_set_reason(&revoked, tensor_pool_leaseRevokeReason_EXPIRED);
    tensor_pool_shmLeaseRevoked_put_errorMessage(&revoked, "expired", 7);

    return (size_t)tensor_pool_shmLeaseRevoked_sbe_position(&revoked);
}

static size_t encode_shutdown(uint8_t *buffer, size_t capacity)
{
    struct tensor_pool_messageHeader header;
    struct tensor_pool_shmDriverShutdown shutdown;

    tensor_pool_messageHeader_wrap(&header, (char *)buffer, 0, tensor_pool_messageHeader_sbe_schema_version(), capacity);
    tensor_pool_messageHeader_set_blockLength(&header, tensor_pool_shmDriverShutdown_sbe_block_length());
    tensor_pool_messageHeader_set_templateId(&header, tensor_pool_shmDriverShutdown_sbe_template_id());
    tensor_pool_messageHeader_set_schemaId(&header, tensor_pool_shmDriverShutdown_sbe_schema_id());
    tensor_pool_messageHeader_set_version(&header, tensor_pool_shmDriverShutdown_sbe_schema_version());

    tensor_pool_shmDriverShutdown_wrap_for_encode(&shutdown, (char *)buffer, tensor_pool_messageHeader_encoded_length(), capacity);
    tensor_pool_shmDriverShutdown_set_timestampNs(&shutdown, 500);
    tensor_pool_shmDriverShutdown_set_reason(&shutdown, tensor_pool_shutdownReason_ADMIN);
    tensor_pool_shmDriverShutdown_put_errorMessage(&shutdown, "admin", 5);

    return (size_t)tensor_pool_shmDriverShutdown_sbe_position(&shutdown);
}

static void test_control_poller_driver_dispatch(void)
{
    tp_control_poller_driver_state_t state;
    tp_control_poller_t poller;
    tp_control_handlers_t handlers;
    uint8_t buffer[256];
    size_t len;

    memset(&state, 0, sizeof(state));
    memset(&poller, 0, sizeof(poller));
    memset(&handlers, 0, sizeof(handlers));

    handlers.on_detach_response = tp_on_detach_response;
    handlers.on_lease_revoked = tp_on_lease_revoked;
    handlers.on_shutdown = tp_on_shutdown;
    handlers.clientd = &state;
    poller.handlers = handlers;

    len = encode_detach_response(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_lease_revoked(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    len = encode_shutdown(buffer, sizeof(buffer));
    tp_control_poller_handle_fragment(&poller, buffer, len);

    assert(state.detach == 1);
    assert(state.lease_revoked == 1);
    assert(state.shutdown == 1);
}

void tp_test_control_poller_driver(void)
{
    test_control_poller_driver_dispatch();
}
