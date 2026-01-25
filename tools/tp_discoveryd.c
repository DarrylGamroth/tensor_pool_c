#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "aeron_agent.h"

static volatile sig_atomic_t tp_discovery_running = 1;

static void tp_discovery_handle_signal(int sig)
{
    (void)sig;
    tp_discovery_running = 0;
}

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s -c <config.toml>\n"
        "Options:\n"
        "  -c <path>  Discovery config file\n"
        "  -h         Show help\n",
        name);
}

static void tp_discovery_apply_log_level(tp_log_t *log)
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
    tp_discovery_service_config_t config;
    tp_discovery_service_t service;
    uint64_t sleep_ns = 1000000ULL;
    const char *config_path = NULL;
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

    if (tp_discovery_service_config_init(&config) < 0)
    {
        fprintf(stderr, "Discovery config init failed: %s\n", tp_errmsg());
        return 1;
    }

    if (tp_discovery_service_config_load(&config, config_path) < 0)
    {
        fprintf(stderr, "Discovery config load failed: %s\n", tp_errmsg());
        tp_discovery_service_config_close(&config);
        return 1;
    }

    tp_discovery_apply_log_level(&config.base.log);

    if (tp_discovery_service_init(&service, &config) < 0)
    {
        fprintf(stderr, "Discovery init failed: %s\n", tp_errmsg());
        tp_discovery_service_config_close(&config);
        return 1;
    }

    if (tp_discovery_service_start(&service) < 0)
    {
        fprintf(stderr, "Discovery start failed: %s\n", tp_errmsg());
        tp_discovery_service_close(&service);
        return 1;
    }

    signal(SIGINT, tp_discovery_handle_signal);
    signal(SIGTERM, tp_discovery_handle_signal);

    while (tp_discovery_running)
    {
        int work = tp_discovery_service_do_work(&service);
        aeron_idle_strategy_sleeping_idle(&sleep_ns, work);
    }

    tp_discovery_service_close(&service);
    return 0;
}
