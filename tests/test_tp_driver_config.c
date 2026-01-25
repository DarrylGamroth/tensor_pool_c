#include "tensor_pool/tp_driver.h"

#include <assert.h>
#include <string.h>

void tp_test_driver_config(void)
{
    tp_driver_config_t config;

    assert(tp_driver_config_init(&config) == 0);
    assert(tp_driver_config_load(&config, "../config/driver_integration_example.toml") == 0);

    assert(config.base.control_stream_id == 1000);
    assert(config.base.announce_stream_id == 1001);
    assert(config.base.qos_stream_id == 1200);
    assert(strcmp(config.shm_base_dir, "/dev/shm") == 0);
    assert(strcmp(config.shm_namespace, "default") == 0);
    assert(config.allow_dynamic_streams == false);
    assert(config.node_id_reuse_cooldown_ms == 1000);
    assert(config.profile_count == 1);
    assert(config.stream_count == 1);
    assert(config.profiles[0].header_nslots == 64);
    assert(config.profiles[0].pool_count == 1);
    assert(config.profiles[0].pools[0].pool_id == 1);
    assert(config.profiles[0].pools[0].stride_bytes == 1048576);

    tp_driver_config_close(&config);

    assert(tp_driver_config_init(&config) == 0);
    assert(tp_driver_config_load(&config, "../config/driver_integration_dynamic.toml") == 0);

    assert(config.allow_dynamic_streams == true);
    assert(config.node_id_reuse_cooldown_ms == 1000);
    assert(config.stream_id_ranges.count > 0);
    assert(strlen(config.default_profile) > 0);

    tp_driver_config_close(&config);
}
