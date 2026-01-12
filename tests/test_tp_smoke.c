#include "tensor_pool/tp_version.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    uint32_t version = tp_version_compose(TP_VERSION_MAJOR, TP_VERSION_MINOR, TP_VERSION_PATCH);

    assert(version == 0x000100);

    return 0;
}
