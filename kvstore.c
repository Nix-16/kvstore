#include "kvstore.h"

#include <string.h>
#include <strings.h>  /* strcasecmp */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "src/kvs_array.h"
#include "src/resp.h"
#include "src/resp_reply.h"
#include "src/reactor.h"
#include "src/buffer.h"

/* 当前仅启用数组后端 */
static kvs_options_t g_opt = { KVS_BACKEND_ARRAY };

/* 你在 kvs_array.c 里定义的全局实例 */
extern kvs_array_t global_array;

int kvs_init(const kvs_options_t *opt)
{
    if (opt) {
        g_opt = *opt;
    } else {
        g_opt.backend = KVS_BACKEND_ARRAY;
    }

    /* 先只支持数组 */
    if (g_opt.backend != KVS_BACKEND_ARRAY) {
        /* 以后再扩展 */
        g_opt.backend = KVS_BACKEND_ARRAY;
    }

    return kvs_array_create(&global_array);
}

void kvs_fini(void)
{
    kvs_array_destory(&global_array);
}

/* -------- 统一 KV API（先走数组） -------- */

int kvs_set(const char *key, const char *value)
{
    if (!key || !value) return -1;
    return kvs_array_set(&global_array, (char*)key, (char*)value);
}

char* kvs_get(const char *key)
{
    if (!key) return NULL;
    return kvs_array_get(&global_array, (char*)key);
}

int kvs_del(const char *key)
{
    if (!key) return -1;
    return kvs_array_del(&global_array, (char*)key);
}

int kvs_exists(const char *key)
{
    if (!key) return -1;
    return kvs_array_exist(&global_array, (char*)key);
}

/* -------- RESP 命令执行 -------- */

static int handle_cmd(struct connection *c, const struct resp_cmd *cmd)
{
    if (!c || !cmd || cmd->argc <= 0) {
        return resp_reply_error(&c->out, "protocol error");
    }

    const char *op = cmd->argv[0];

    /* PING [msg] */
    if (strcasecmp(op, "PING") == 0) {
        if (cmd->argc == 1) {
            return resp_reply_simple(&c->out, "PONG");
        } else if (cmd->argc == 2) {
            return resp_reply_bulk(&c->out, cmd->argv[1], strlen(cmd->argv[1]));
        } else {
            return resp_reply_error(&c->out, "wrong number of arguments for 'ping'");
        }
    }

    /* SET key value */
    if (strcasecmp(op, "SET") == 0) {
        if (cmd->argc != 3) {
            return resp_reply_error(&c->out, "wrong number of arguments for 'set'");
        }
        int rc = kvs_set(cmd->argv[1], cmd->argv[2]);
        if (rc < 0) return resp_reply_error(&c->out, "set failed");
        return resp_reply_simple(&c->out, "OK");
    }

    /* GET key */
    if (strcasecmp(op, "GET") == 0) {
        if (cmd->argc != 2) {
            return resp_reply_error(&c->out, "wrong number of arguments for 'get'");
        }
        char *v = kvs_get(cmd->argv[1]);
        if (!v) return resp_reply_nil(&c->out);
        return resp_reply_bulk(&c->out, v, strlen(v));
    }

    /* DEL key */
    if (strcasecmp(op, "DEL") == 0) {
        if (cmd->argc != 2) {
            return resp_reply_error(&c->out, "wrong number of arguments for 'del'");
        }
        int rc = kvs_del(cmd->argv[1]);
        if (rc < 0) return resp_reply_error(&c->out, "del failed");
        /* 你的 del：0=success, 1=noexist -> 返回 1/0 */
        return resp_reply_integer(&c->out, (rc == 0) ? 1 : 0);
    }

    /* EXISTS key */
    if (strcasecmp(op, "EXISTS") == 0) {
        if (cmd->argc != 2) {
            return resp_reply_error(&c->out, "wrong number of arguments for 'exists'");
        }
        int rc = kvs_exists(cmd->argv[1]);
        if (rc < 0) return resp_reply_error(&c->out, "exists failed");
        /* exist: 0 表示存在；no exist: 1 -> 返回 1/0 */
        return resp_reply_integer(&c->out, (rc == 0) ? 1 : 0);
    }

    return resp_reply_error(&c->out, "unknown command");
}

/* -------- reactor 回调：增量解析 + 执行 + flush out -------- */

int kvs_on_message(struct connection *c, void *user_data)
{
    struct reactor *r = (struct reactor *)user_data;

    for (;;) {
        struct resp_cmd cmd;
        memset(&cmd, 0, sizeof(cmd));

        int prc = resp_try_parse(&c->in, &cmd);
        if (prc == 0) {
            /* 半包：等待下次 EPOLLIN */
            return 0;
        }
        if (prc < 0) {
            /* 协议错：断开 */
            return -1;
        }

        /* 执行业务并把 RESP 写入 c->out */
        if (handle_cmd(c, &cmd) != 0) {
            resp_cmd_free(&cmd);
            return -1;
        }
        resp_cmd_free(&cmd);

        /* 关键：把 out buffer 真正发出去（按 echo_server 的模式） */
        size_t out_n = buffer_readable_bytes(&c->out);
        if (out_n > 0) {
            const char *out_p = buffer_peek(&c->out);

            if (connection_send(r, c, out_p, out_n) != 0) {
                return -1;
            }

            /* 清空 out */
            buffer_retrieve(&c->out, out_n);
        }

        /* 继续循环解析下一条（如果 in buffer 里还有数据） */
    }
}

/* -------- server main：启动 reactor -------- */

static struct reactor *g_r = NULL;

static void on_sig(int sig)
{
    (void)sig;
    if (g_r) reactor_stop(g_r);
}

static void on_close(struct connection *c, void *user_data)
{
    (void)c;
    (void)user_data;
    /* 这里可选打印日志 */
}

int main(int argc, char **argv)
{
    /* 默认 6380，避免占用 6379（你 echo_server/系统 redis 可能会用） */
    uint16_t port = (argc >= 2) ? (uint16_t)atoi(argv[1]) : 6380;

    if (kvs_init(NULL) != 0) {
        fprintf(stderr, "kvs_init failed\n");
        return 1;
    }

    g_r = reactor_create(1024);
    if (!g_r) {
        fprintf(stderr, "reactor_create failed\n");
        kvs_fini();
        return 1;
    }

    /* 把 reactor 指针作为 user_data 传给回调（kvs_on_message 里要用 connection_send） */
    reactor_set_callbacks(g_r, kvs_on_message, on_close, g_r);

    if (reactor_listen(g_r, "0.0.0.0", port, 128) != 0) {
        fprintf(stderr, "reactor_listen failed on port %u\n", port);
        reactor_destroy(g_r);
        g_r = NULL;
        kvs_fini();
        return 1;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    printf("kvstore_server listening on 0.0.0.0:%u\n", port);
    fflush(stdout);

    reactor_run(g_r);

    reactor_destroy(g_r);
    g_r = NULL;
    kvs_fini();
    return 0;
}