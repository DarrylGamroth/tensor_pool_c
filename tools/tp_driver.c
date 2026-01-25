#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "aeron_agent.h"

static volatile sig_atomic_t tp_driver_running = 1;

static void tp_driver_handle_signal(int sig)
{
    (void)sig;
    tp_driver_running = 0;
}

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s -c <config.toml>\n"
        "Options:\n"
        "  -c <path>  Driver config file\n"
        "  -h         Show help\n",
        name);
}

static void tp_driver_apply_log_level(tp_log_t *log)
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
    tp_driver_config_t config;
    tp_driver_t driver;
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

    if (tp_driver_config_init(&config) < 0)
    {
        fprintf(stderr, "Config init failed: %s\n", tp_errmsg());
        return 1;
    }

    if (tp_driver_config_load(&config, config_path) < 0)
    {
        fprintf(stderr, "Config load failed: %s\n", tp_errmsg());
        tp_driver_config_close(&config);
        return 1;
    }

    tp_driver_apply_log_level(&config.base.log);

    if (tp_driver_init(&driver, &config) < 0)
    {
        fprintf(stderr, "Driver init failed: %s\n", tp_errmsg());
        tp_driver_config_close(&config);
        return 1;
    }

    if (tp_driver_start(&driver) < 0)
    {
        fprintf(stderr, "Driver start failed: %s\n", tp_errmsg());
        tp_driver_close(&driver);
        return 1;
    }

    signal(SIGINT, tp_driver_handle_signal);
    signal(SIGTERM, tp_driver_handle_signal);

    while (tp_driver_running)
    {
        int work = tp_driver_do_work(&driver);
        aeron_idle_strategy_sleeping_idle(&sleep_ns, work);
    }

    tp_driver_close(&driver);
    return 0;
}
