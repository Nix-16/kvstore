#include "kvs_aof.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <sys/time.h>

#include "kvstore.h"
#include "buffer.h"
#include "resp.h"
#include "kvstore.h"

static int g_aof_enabled = 0;
static int g_aof_fd = -1;
static kvs_aof_fsync_type_t g_aof_policy = KVS_AOF_FSYNC_NO;
static char g_aof_filename[256] = {0};
static int g_loading_aof = 0;
static long long g_last_fsync_ms = 0;

static long long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* 将一条命令编码成 RESP 并直接追加到 AOF */
static int aof_append_resp_command(int argc, const char **argv)
{
    char hdr[64];

    if (!g_aof_enabled || g_loading_aof)
        return 0;
    if (g_aof_fd < 0)
        return -1;
    if (argc <= 0 || !argv)
        return -1;

    int n = snprintf(hdr, sizeof(hdr), "*%d\r\n", argc);
    if (n <= 0) return -1;
    if (write_all(g_aof_fd, hdr, (size_t)n) != 0) return -1;

    for (int i = 0; i < argc; ++i) {
        size_t len = argv[i] ? strlen(argv[i]) : 0;

        n = snprintf(hdr, sizeof(hdr), "$%zu\r\n", len);
        if (n <= 0) return -1;
        if (write_all(g_aof_fd, hdr, (size_t)n) != 0) return -1;

        if (len > 0 && write_all(g_aof_fd, argv[i], len) != 0) return -1;
        if (write_all(g_aof_fd, "\r\n", 2) != 0) return -1;
    }

    if (g_aof_policy == KVS_AOF_FSYNC_ALWAYS) {
        if (fsync(g_aof_fd) != 0)
            return -1;
        g_last_fsync_ms = now_ms();
        // fprintf(stderr, "[AOF] fsync(always)\n");
    }

    return 0;
}

int kvs_aof_init(const char *filename, int enabled, kvs_aof_fsync_type_t policy)
{
    g_aof_enabled = enabled ? 1 : 0;
    g_aof_policy = policy;
    g_last_fsync_ms = now_ms();

    if (filename && *filename)
        snprintf(g_aof_filename, sizeof(g_aof_filename), "%s", filename);
    else
        snprintf(g_aof_filename, sizeof(g_aof_filename), "%s", "appendonly.aof");

    if (!g_aof_enabled)
        return 0;

    g_aof_fd = open(g_aof_filename, O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (g_aof_fd < 0)
        return -1;

    return 0;
}

void kvs_aof_close(void)
{
    if (g_aof_fd >= 0) {
        if (g_aof_policy != KVS_AOF_FSYNC_NO) {
            fsync(g_aof_fd);
        }
        close(g_aof_fd);
        g_aof_fd = -1;
    }
}

int kvs_aof_is_loading(void)
{
    return g_loading_aof;
}

int kvs_aof_maybe_fsync(void)
{
    if (!g_aof_enabled || g_aof_fd < 0)
        return 0;

    if (g_aof_policy != KVS_AOF_FSYNC_EVERYSEC)
        return 0;

    long long now = now_ms();
    if (now - g_last_fsync_ms >= 1000) {
        if (fsync(g_aof_fd) != 0)
            return -1;
        g_last_fsync_ms = now;
        // fprintf(stderr, "[AOF] fsync(everysec)\n");
    }
    return 0;
}

int kvs_aof_append_set(const char *key, const char *value)
{
    const char *argv[3];
    if (!key || !value) return -1;
    argv[0] = "SET";
    argv[1] = key;
    argv[2] = value;
    return aof_append_resp_command(3, argv);
}

int kvs_aof_append_del(const char *key)
{
    const char *argv[2];
    if (!key) return -1;
    argv[0] = "DEL";
    argv[1] = key;
    return aof_append_resp_command(2, argv);
}

int kvs_aof_append_hset(const char *key, const char *value)
{
    const char *argv[3];
    if (!key || !value) return -1;
    argv[0] = "HSET";
    argv[1] = key;
    argv[2] = value;
    return aof_append_resp_command(3, argv);
}

int kvs_aof_append_hdel(const char *key)
{
    const char *argv[2];
    if (!key) return -1;
    argv[0] = "HDEL";
    argv[1] = key;
    return aof_append_resp_command(2, argv);
}

int kvs_aof_append_rset(const char *key, const char *value)
{
    const char *argv[3];
    if (!key || !value) return -1;
    argv[0] = "RSET";
    argv[1] = key;
    argv[2] = value;
    return aof_append_resp_command(3, argv);
}

int kvs_aof_append_rdel(const char *key)
{
    const char *argv[2];
    if (!key) return -1;
    argv[0] = "RDEL";
    argv[1] = key;
    return aof_append_resp_command(2, argv);
}

int kvs_aof_load(void)
{
    if (!g_aof_enabled)
        return 0;

    int fd = open(g_aof_filename, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        return -1;
    }

    struct buffer in;
    if (buffer_init(&in, 4096) != 0) {
        close(fd);
        return -1;
    }

    char tmp[4096];
    for (;;) {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR) continue;
            buffer_free(&in);
            close(fd);
            return -1;
        }
        if (n == 0) break;
        if (buffer_append(&in, tmp, (size_t)n) != 0) {
            buffer_free(&in);
            close(fd);
            return -1;
        }
    }
    close(fd);

    g_loading_aof = 1;

    for (;;) {
        struct resp_cmd cmd;
        memset(&cmd, 0, sizeof(cmd));

        int prc = resp_try_parse(&in, &cmd);
        if (prc == 0) {
            /* 文件结束；若最后一条不完整，也在这里停下 */
            break;
        }
        if (prc < 0) {
            resp_cmd_free(&cmd);
            g_loading_aof = 0;
            buffer_free(&in);
            return -1;
        }

        if (cmd.argc <= 0) {
            resp_cmd_free(&cmd);
            continue;
        }

        const char *op = cmd.argv[0];
        int rc = 0;

        if (strcasecmp(op, "SET") == 0 && cmd.argc == 3) {
            rc = kvs_set(cmd.argv[1], cmd.argv[2]);
        } else if (strcasecmp(op, "DEL") == 0 && cmd.argc == 2) {
            rc = kvs_del(cmd.argv[1]);
            if (rc == 1) rc = 0; /* 删除不存在 key 视为可接受 */
        } else if (strcasecmp(op, "HSET") == 0 && cmd.argc == 3) {
            rc = kvs_hset(cmd.argv[1], cmd.argv[2]);
        } else if (strcasecmp(op, "HDEL") == 0 && cmd.argc == 2) {
            rc = kvs_hdel(cmd.argv[1]);
            if (rc == 1) rc = 0;
        } else if (strcasecmp(op, "RSET") == 0 && cmd.argc == 3) {
            rc = kvs_rset(cmd.argv[1], cmd.argv[2]);
        } else if (strcasecmp(op, "RDEL") == 0 && cmd.argc == 2) {
            rc = kvs_rdel(cmd.argv[1]);
            if (rc == 1) rc = 0;
        } else {
            resp_cmd_free(&cmd);
            g_loading_aof = 0;
            buffer_free(&in);
            return -1;
        }

        resp_cmd_free(&cmd);

        if (rc < 0) {
            g_loading_aof = 0;
            buffer_free(&in);
            return -1;
        }
    }

    g_loading_aof = 0;
    buffer_free(&in);
    return 0;
}