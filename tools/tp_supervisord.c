#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"
#include "tensor_pool/tp_supervisor.h"

#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "aeron_agent.h"

static volatile sig_atomic_t tp_supervisor_running = 1;

static void tp_supervisor_handle_signal(int sig)
{
    (void)sig;
    tp_supervisor_running = 0;
}

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s -c <config.toml>\n"
        "Options:\n"
        "  -c <path>  Supervisor config file\n"
        "  -h         Show help\n",
        name);
}

static void tp_supervisor_apply_log_level(tp_log_t *log)
{
    const char *level_str = getenv("TP_LOG_LEVEL");
    long level;
    char *endptr = NULL;

    if (NULL == level_str || NULL == log)
    {
        return;
    }

    level = strtol(level_str, &endptr, 10);
    if (endptr == level_str || *endptr != '\0')
    {
        return;
    }

    if (level < TP_LOG_ERROR)
    {
        level = TP_LOG_ERROR;
    }
    if (level > TP_LOG_TRACE)
    {
        level = TP_LOG_TRACE;
    }

    tp_log_set_level(log, (tp_log_level_t)level);
}

int main(int argc, char **argv)
{
    tp_supervisor_config_t config;
    tp_supervisor_t supervisor;
    uint64_t sleep_ns = 1000000ULL;
    uint64_t stats_interval_ns = 0;
    uint64_t next_stats_ns = 0;
    const char *config_path = NULL;
    const char *stats_env = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "c:h")) != -1)
    {
        switch (opt)
        {
            case 'c':
                config_path = optarg;
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (NULL == config_path && optind < argc)
    {
        config_path = argv[optind++];
    }

    if (NULL == config_path || optind < argc)
    {
        usage(argv[0]);
        return 1;
    }

    if (tp_supervisor_config_init(&config) < 0)
    {
        fprintf(stderr, "Supervisor config init failed: %s\n", tp_errmsg());
        return 1;
    }

    if (tp_supervisor_config_load(&config, config_path) < 0)
    {
        fprintf(stderr, "Supervisor config load failed: %s\n", tp_errmsg());
        tp_supervisor_config_close(&config);
        return 1;
    }

    tp_supervisor_apply_log_level(&config.base.log);

    if (tp_supervisor_init(&supervisor, &config) < 0)
    {
        fprintf(stderr, "Supervisor init failed: %s\n", tp_errmsg());
        tp_supervisor_config_close(&config);
        return 1;
    }

    if (tp_supervisor_start(&supervisor) < 0)
    {
        fprintf(stderr, "Supervisor start failed: %s\n", tp_errmsg());
        tp_supervisor_close(&supervisor);
        return 1;
    }

    stats_env = getenv("TP_SUPERVISOR_STATS_MS");
    if (stats_env && stats_env[0] != '\0')
    {
        stats_interval_ns = (uint64_t)strtoull(stats_env, NULL, 10) * 1000ULL * 1000ULL;
        next_stats_ns = tp_clock_now_ns() + stats_interval_ns;
    }

    signal(SIGINT, tp_supervisor_handle_signal);
    signal(SIGTERM, tp_supervisor_handle_signal);

    while (tp_supervisor_running)
    {
        int work = tp_supervisor_do_work(&supervisor);
        aeron_idle_strategy_sleeping_idle(&sleep_ns, work);
        if (stats_interval_ns > 0 && tp_clock_now_ns() >= next_stats_ns)
        {
            tp_supervisor_stats_t stats;
            if (tp_supervisor_get_stats(&supervisor, &stats) == 0)
            {
                fprintf(stderr,
                    "supervisor stats hello=%" PRIu64 " config=%" PRIu64
                    " qos_consumer=%" PRIu64 " qos_producer=%" PRIu64
                    " announce=%" PRIu64 " metadata=%" PRIu64 "\n",
                    stats.hello_count,
                    stats.config_count,
                    stats.qos_consumer_count,
                    stats.qos_producer_count,
                    stats.announce_count,
                    stats.metadata_count);
            }
            next_stats_ns = tp_clock_now_ns() + stats_interval_ns;
        }
    }

    tp_supervisor_close(&supervisor);
    return 0;
}
