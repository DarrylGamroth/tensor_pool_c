#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tensor_pool/tp_context.h"
#include "tensor_pool/tp_shm.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int create_temp_file(char *path_template, size_t size)
{
    int fd = mkstemp(path_template);
    if (fd < 0)
    {
        return -1;
    }

    if (ftruncate(fd, (off_t)size) != 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}

void tp_test_shm_security(void)
{
    tp_context_t ctx;
    tp_shm_region_t region = { 0 };
    const char *allowed_paths[1];
    const char *file_template = "/tmp/tp_shm_secXXXXXX";
    const char *fifo_template = "/tmp/tp_shm_fifoXXXXXX";
    const char *link_template = "/tmp/tp_shm_linkXXXXXX";
    const char *base_template = "/tmp/tp_allowedXXXXXX";
    char file_path[64];
    char fifo_path[64];
    char link_path[64];
    char base_dir[64];
    char uri[512];
    int fd = -1;
    int result = -1;
    int step = 0;

    memset(&ctx, 0, sizeof(ctx));
    step = 1;
    if (tp_context_init(&ctx) < 0)
    {
        goto cleanup;
    }

    allowed_paths[0] = "/tmp";
    tp_context_set_allowed_paths(&ctx, allowed_paths, 1);
    step = 2;
    if (tp_context_finalize_allowed_paths(&ctx) < 0)
    {
        goto cleanup;
    }

    step = 3;
    strncpy(file_path, file_template, sizeof(file_path) - 1);
    file_path[sizeof(file_path) - 1] = '\0';
    fd = create_temp_file(file_path, TP_SUPERBLOCK_SIZE_BYTES);
    if (fd < 0)
    {
        goto cleanup;
    }

    snprintf(uri, sizeof(uri), "shm:file?path=%s", file_path);
    step = 4;
    if (tp_shm_map(&region, uri, 0, &ctx.allowed_paths, NULL) != 0)
    {
        goto cleanup;
    }
    tp_shm_unmap(&region, NULL);
    close(fd);
    fd = -1;
    unlink(file_path);
    tp_context_clear_allowed_paths(&ctx);

    step = 5;
    strncpy(base_dir, base_template, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';
    if (NULL == mkdtemp(base_dir))
    {
        goto cleanup;
    }

    allowed_paths[0] = base_dir;
    tp_context_set_allowed_paths(&ctx, allowed_paths, 1);
    step = 6;
    if (tp_context_finalize_allowed_paths(&ctx) < 0)
    {
        goto cleanup;
    }

    step = 7;
    strncpy(file_path, file_template, sizeof(file_path) - 1);
    file_path[sizeof(file_path) - 1] = '\0';
    fd = create_temp_file(file_path, TP_SUPERBLOCK_SIZE_BYTES);
    if (fd < 0)
    {
        goto cleanup;
    }

    snprintf(uri, sizeof(uri), "shm:file?path=%s", file_path);
    step = 8;
    if (tp_shm_map(&region, uri, 0, &ctx.allowed_paths, NULL) == 0)
    {
        goto cleanup;
    }
    close(fd);
    fd = -1;
    unlink(file_path);
    tp_context_clear_allowed_paths(&ctx);
    rmdir(base_dir);

    allowed_paths[0] = "/tmp";
    tp_context_set_allowed_paths(&ctx, allowed_paths, 1);
    step = 9;
    if (tp_context_finalize_allowed_paths(&ctx) < 0)
    {
        goto cleanup;
    }

    step = 10;
    strncpy(fifo_path, fifo_template, sizeof(fifo_path) - 1);
    fifo_path[sizeof(fifo_path) - 1] = '\0';
    if (mkstemp(fifo_path) >= 0)
    {
        unlink(fifo_path);
    }
    if (mkfifo(fifo_path, 0600) != 0)
    {
        goto cleanup;
    }

    snprintf(uri, sizeof(uri), "shm:file?path=%s", fifo_path);
    step = 11;
    if (tp_shm_map(&region, uri, 0, &ctx.allowed_paths, NULL) == 0)
    {
        goto cleanup;
    }
    unlink(fifo_path);
    tp_context_clear_allowed_paths(&ctx);

    allowed_paths[0] = "/tmp";
    tp_context_set_allowed_paths(&ctx, allowed_paths, 1);
    step = 12;
    if (tp_context_finalize_allowed_paths(&ctx) < 0)
    {
        goto cleanup;
    }

    step = 13;
    strncpy(file_path, file_template, sizeof(file_path) - 1);
    file_path[sizeof(file_path) - 1] = '\0';
    fd = create_temp_file(file_path, TP_SUPERBLOCK_SIZE_BYTES);
    if (fd < 0)
    {
        goto cleanup;
    }

    strncpy(link_path, link_template, sizeof(link_path) - 1);
    link_path[sizeof(link_path) - 1] = '\0';
    if (mkstemp(link_path) >= 0)
    {
        unlink(link_path);
    }
    step = 14;
    if (symlink(file_path, link_path) != 0)
    {
        goto cleanup;
    }

    snprintf(uri, sizeof(uri), "shm:file?path=%s", link_path);
    step = 15;
    if (tp_shm_map(&region, uri, 0, &ctx.allowed_paths, NULL) == 0)
    {
        goto cleanup;
    }

    close(fd);
    fd = -1;
    unlink(file_path);
    unlink(link_path);
    tp_context_clear_allowed_paths(&ctx);

    result = 0;

cleanup:
    tp_context_clear_allowed_paths(&ctx);
    if (fd >= 0)
    {
        close(fd);
    }
    unlink(file_path);
    unlink(fifo_path);
    unlink(link_path);
    rmdir(base_dir);

    if (result != 0)
    {
        fprintf(stderr, "tp_test_shm_security failed at step %d: %s\n", step, strerror(errno));
    }
    assert(result == 0);
}
