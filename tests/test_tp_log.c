#include "tensor_pool/tp_error.h"
#include "tensor_pool/tp_log.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

typedef struct tp_log_capture_stct
{
    int calls;
    tp_log_level_t last_level;
    char last_message[128];
    void *last_clientd;
}
        tp_log_capture_t;

static void tp_log_capture(tp_log_level_t level, const char *message, void *clientd)
{
    tp_log_capture_t *capture = (tp_log_capture_t *)clientd;

    if (NULL == capture)
    {
        return;
    }

    capture->calls++;
    capture->last_level = level;
    capture->last_clientd = clientd;
    if (message)
    {
        strncpy(capture->last_message, message, sizeof(capture->last_message) - 1);
        capture->last_message[sizeof(capture->last_message) - 1] = '\0';
    }
}

static void test_log_init_and_levels(void)
{
    tp_log_t log;
    tp_log_capture_t capture;

    memset(&capture, 0, sizeof(capture));
    tp_log_init(&log);

    assert(log.handler != NULL);
    assert(log.min_level == TP_LOG_INFO);

    tp_log_set_handler(&log, tp_log_capture, &capture);
    tp_log_set_level(&log, TP_LOG_WARN);

    tp_log_emit(&log, TP_LOG_INFO, "skip");
    assert(capture.calls == 0);

    tp_log_emit(&log, TP_LOG_ERROR, "error %d", 7);
    assert(capture.calls == 1);
    assert(capture.last_level == TP_LOG_ERROR);
    assert(strstr(capture.last_message, "error 7") != NULL);
}

static void test_log_nulls(void)
{
    tp_log_t log;

    tp_log_init(&log);
    tp_log_set_handler(NULL, tp_log_capture, NULL);
    tp_log_set_level(NULL, TP_LOG_DEBUG);
    tp_log_emit(NULL, TP_LOG_INFO, "noop");

    log.handler = NULL;
    tp_log_emit(&log, TP_LOG_INFO, "noop");
}

static void test_error_macros(void)
{
    TP_SET_ERR(EINVAL, "bad %d", 5);
    assert(tp_errcode() == EINVAL);
    assert(strstr(tp_errmsg(), "bad 5") != NULL);

    TP_APPEND_ERR("%s", " detail");
    assert(strstr(tp_errmsg(), "detail") != NULL);
}

void tp_test_log(void)
{
    test_log_init_and_levels();
    test_log_nulls();
    test_error_macros();
}
