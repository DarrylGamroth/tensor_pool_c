#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#define main tp_control_listen_main
#include "../tools/tp_control_listen.c"
#undef main

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void test_control_listen_json_escape(void)
{
    FILE *tmp = NULL;
    int saved = -1;
    char output[128];
    const char *text = "a\"b\\c";
    tp_string_view_t view;
    size_t n = 0;
    int result = -1;

    view.data = text;
    view.length = (uint32_t)strlen(text);

    tmp = tmpfile();
    if (NULL == tmp)
    {
        goto cleanup;
    }

    fflush(stdout);
    saved = dup(fileno(stdout));
    if (saved < 0)
    {
        goto cleanup;
    }

    if (dup2(fileno(tmp), fileno(stdout)) < 0)
    {
        goto cleanup;
    }

    tp_print_json_string_view("key", &view);
    fflush(stdout);

    if (dup2(saved, fileno(stdout)) < 0)
    {
        goto cleanup;
    }

    if (fseek(tmp, 0, SEEK_SET) != 0)
    {
        goto cleanup;
    }

    memset(output, 0, sizeof(output));
    n = fread(output, 1, sizeof(output) - 1, tmp);
    output[n] = '\0';

    assert(strcmp(output, "\"key\":\"a\\\"b\\\\c\"") == 0);
    result = 0;

cleanup:
    if (saved >= 0)
    {
        close(saved);
    }
    if (tmp)
    {
        fclose(tmp);
    }
    assert(result == 0);
}

void tp_test_control_listen_json(void)
{
    test_control_listen_json_escape();
}
