#include "kvstore.h"

#include <string.h>
#include <strings.h> /* strcasecmp */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "src/kvs_array.h"
#include "src/kvs_hash.h"
#include "src/kvs_rbtree.h"
#include "src/resp.h"
#include "src/resp_reply.h"
#include "src/reactor.h"
#include "src/buffer.h"
#include "src/kvs_config.h"
#include "src/kvs_aof.h"

/* 你在 kvs_array.c 里定义的全局实例 */
extern kvs_array_t global_array;
extern kvs_hash_t global_hash;
extern kvs_rbtree_t global_rbtree;

int kvs_init()
{
    if (kvs_array_create(&global_array) != 0)
        return -1;
    if (kvs_hash_create(&global_hash) != 0)
        return -1;
    if (kvs_rbtree_create(&global_rbtree) != 0)
        return -1;

    return 0;
}

void kvs_fini(void)
{
    kvs_array_destory(&global_array);
    kvs_hash_destory(&global_hash);
    kvs_rbtree_destory(&global_rbtree);
}

/* -------- Array KV API （独立空间） -------- */

int kvs_set(const char *key, const char *value)
{
    if (!key || !value)
        return -1;
    return kvs_array_set(&global_array, (char *)key, (char *)value);
}

char *kvs_get(const char *key)
{
    if (!key)
        return NULL;
    return kvs_array_get(&global_array, (char *)key);
}

int kvs_del(const char *key)
{
    if (!key)
        return -1;
    return kvs_array_del(&global_array, (char *)key);
}

int kvs_exists(const char *key)
{
    if (!key)
        return -1;
    return kvs_array_exist(&global_array, (char *)key);
}

/* -------- Hash KV API（独立空间） -------- */
int kvs_hset(const char *key, const char *value)
{
    if (!key || !value)
        return -1;
    return kvs_hash_set(&global_hash, (char *)key, (char *)value);
}

char *kvs_hget(const char *key)
{
    if (!key)
        return NULL;
    return kvs_hash_get(&global_hash, (char *)key);
}

int kvs_hdel(const char *key)
{
    if (!key)
        return -1;
    return kvs_hash_del(&global_hash, (char *)key);
}

int kvs_hexists(const char *key)
{
    if (!key)
        return -1;
    return kvs_hash_exist(&global_hash, (char *)key);
}

/* -------- RBTree KV API（独立空间） -------- */
int kvs_rset(const char *key, const char *value)
{
    if (!key || !value)
        return -1;
    return kvs_rbtree_set(&global_rbtree, (char *)key, (char *)value);
}

char *kvs_rget(const char *key)
{
    if (!key)
        return NULL;
    return kvs_rbtree_get(&global_rbtree, (char *)key);
}

int kvs_rdel(const char *key)
{
    if (!key)
        return -1;
    return kvs_rbtree_del(&global_rbtree, (char *)key);
}

int kvs_rexists(const char *key)
{
    if (!key)
        return -1;
    return kvs_rbtree_exist(&global_rbtree, (char *)key);
}

/* -------- RESP 命令执行 -------- */

int handle_cmd(struct connection *c, const struct resp_cmd *cmd)
{
    if (!c || !cmd || cmd->argc <= 0)
    {
        return resp_reply_error(&c->out, "protocol error");
    }

    const char *op = cmd->argv[0];

    /* PING [msg] */
    if (strcasecmp(op, "PING") == 0)
    {
        if (cmd->argc == 1)
        {
            return resp_reply_simple(&c->out, "PONG");
        }
        else if (cmd->argc == 2)
        {
            return resp_reply_bulk(&c->out, cmd->argv[1], strlen(cmd->argv[1]));
        }
        else
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'ping'");
        }
    }

    /* SET key value */
    if (strcasecmp(op, "SET") == 0)
    {
        if (cmd->argc != 3)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'set'");
        }
        int rc = kvs_set(cmd->argv[1], cmd->argv[2]);
        if (rc < 0)
            return resp_reply_error(&c->out, "set failed");

        if (!kvs_aof_is_loading())
        {
            if (kvs_aof_append_set(cmd->argv[1], cmd->argv[2]) != 0)
            {
                return resp_reply_error(&c->out, "aof append failed");
            }
        }
        return resp_reply_simple(&c->out, "OK");
    }

    /* GET key */
    if (strcasecmp(op, "GET") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'get'");
        }
        char *v = kvs_get(cmd->argv[1]);
        if (!v)
            return resp_reply_nil(&c->out);
        return resp_reply_bulk(&c->out, v, strlen(v));
    }

    /* DEL key */
    if (strcasecmp(op, "DEL") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'del'");
        }
        int rc = kvs_del(cmd->argv[1]);
        if (rc < 0)
            return resp_reply_error(&c->out, "del failed");

        if (rc == 0 && !kvs_aof_is_loading())
        {
            if (kvs_aof_append_del(cmd->argv[1]) != 0)
            {
                return resp_reply_error(&c->out, "aof append failed");
            }
        }

        return resp_reply_integer(&c->out, (rc == 0) ? 1 : 0);
    }

    /* EXISTS key */
    if (strcasecmp(op, "EXISTS") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'exists'");
        }
        int rc = kvs_exists(cmd->argv[1]);
        if (rc < 0)
            return resp_reply_error(&c->out, "exists failed");
        /* exist: 0 表示存在；no exist: 1 -> 返回 1/0 */
        return resp_reply_integer(&c->out, (rc == 0) ? 1 : 0);
    }

    /* ---------------- Hash namespace: HSET/HGET/HDEL/HEXISTS ---------------- */

    if (strcasecmp(op, "HSET") == 0)
    {
        if (cmd->argc != 3)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'hset'");
        }
        int rc = kvs_hset(cmd->argv[1], cmd->argv[2]);
        if (rc < 0)
            return resp_reply_error(&c->out, "hset failed");

        if (!kvs_aof_is_loading())
        {
            if (kvs_aof_append_hset(cmd->argv[1], cmd->argv[2]) != 0)
            {
                return resp_reply_error(&c->out, "aof append failed");
            }
        }
        return resp_reply_simple(&c->out, "OK");
    }

    if (strcasecmp(op, "HGET") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'hget'");
        }
        char *v = kvs_hget(cmd->argv[1]);
        if (!v)
            return resp_reply_nil(&c->out);
        return resp_reply_bulk(&c->out, v, strlen(v));
    }

    if (strcasecmp(op, "HDEL") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'hdel'");
        }
        int rc = kvs_hdel(cmd->argv[1]);
        if (rc < 0)
            return resp_reply_error(&c->out, "hdel failed");

        if (rc == 0 && !kvs_aof_is_loading())
        {
            if (kvs_aof_append_hdel(cmd->argv[1]) != 0)
            {
                return resp_reply_error(&c->out, "aof append failed");
            }
        }

        return resp_reply_integer(&c->out, (rc == 0) ? 1 : 0);
    }

    if (strcasecmp(op, "HEXISTS") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'hexists'");
        }
        int rc = kvs_hexists(cmd->argv[1]);
        if (rc < 0)
            return resp_reply_error(&c->out, "hexists failed");
        return resp_reply_integer(&c->out, (rc == 0) ? 1 : 0);
    }

    /* ---------------- RBTree namespace: RSET/RGET/RDEL/REXISTS ---------------- */

    if (strcasecmp(op, "RSET") == 0)
    {
        if (cmd->argc != 3)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'rset'");
        }
        int rc = kvs_rset(cmd->argv[1], cmd->argv[2]);
        if (rc < 0)
            return resp_reply_error(&c->out, "rset failed");

        if (!kvs_aof_is_loading())
        {
            if (kvs_aof_append_rset(cmd->argv[1], cmd->argv[2]) != 0)
            {
                return resp_reply_error(&c->out, "aof append failed");
            }
        }
        return resp_reply_simple(&c->out, "OK");
    }

    if (strcasecmp(op, "RGET") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'rget'");
        }
        char *v = kvs_rget(cmd->argv[1]);
        if (!v)
            return resp_reply_nil(&c->out);
        return resp_reply_bulk(&c->out, v, strlen(v));
    }

    if (strcasecmp(op, "RDEL") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'rdel'");
        }
        int rc = kvs_rdel(cmd->argv[1]);
        if (rc < 0)
            return resp_reply_error(&c->out, "rdel failed");

        if (rc == 0 && !kvs_aof_is_loading())
        {
            if (kvs_aof_append_rdel(cmd->argv[1]) != 0)
            {
                return resp_reply_error(&c->out, "aof append failed");
            }
        }

        return resp_reply_integer(&c->out, (rc == 0) ? 1 : 0);
    }

    if (strcasecmp(op, "REXISTS") == 0)
    {
        if (cmd->argc != 2)
        {
            return resp_reply_error(&c->out, "wrong number of arguments for 'rexists'");
        }
        int rc = kvs_rexists(cmd->argv[1]);
        if (rc < 0)
            return resp_reply_error(&c->out, "rexists failed");
        return resp_reply_integer(&c->out, (rc == 0) ? 1 : 0);
    }

    return resp_reply_error(&c->out, "unknown command");
}

int kvs_on_message(struct connection *c, void *user_data)
{
    (void)user_data;

    for (;;)
    {
        struct resp_cmd cmd;
        memset(&cmd, 0, sizeof(cmd));

        int prc = resp_try_parse(&c->in, &cmd);
        if (prc == 0)
        {
            return 0; /* 半包 */
        }
        if (prc < 0)
        {
            return -1; /* 协议错误，reactor 会 close */
        }

        if (handle_cmd(c, &cmd) != 0)
        {
            resp_cmd_free(&cmd);
            return -1;
        }
        resp_cmd_free(&cmd);
    }
}

/* -------- server main：启动 reactor -------- */

static struct reactor *g_r = NULL;

static void on_sig(int sig)
{
    (void)sig;
    if (g_r)
        reactor_stop(g_r);
}

static void on_close(struct connection *c, void *user_data)
{
    (void)c;
    (void)user_data;
    /* 这里可选打印日志 */
}

int main()
{
    // /* 默认 6380，避免占用 6379（你 echo_server/系统 redis 可能会用） */
    // uint16_t port = (argc >= 2) ? (uint16_t)atoi(argv[1]) : 6380;

    kvs_config_t cfg;
    if (kvs_config_load_file(&cfg, "kvs.conf") != 0)
    {
        fprintf(stderr, "load config failed\n");
        return 1;
    }

    uint16_t port = (uint16_t)cfg.port;

    kvs_set_allocator(cfg.allocator);

    if (kvs_init() != 0)
    {
        fprintf(stderr, "kvs_init failed\n");
        return 1;
    }

    if (kvs_aof_init(cfg.appendfilename, cfg.appendonly, cfg.appendfsync) != 0)
    {
        fprintf(stderr, "kvs_aof_init failed\n");
        kvs_fini();
        return 1;
    }

    if (kvs_aof_load() != 0)
    {
        fprintf(stderr, "kvs_aof_load failed\n");
        kvs_aof_close();
        kvs_fini();
        return 1;
    }

    g_r = reactor_create(1024);
    if (!g_r)
    {
        fprintf(stderr, "reactor_create failed\n");
        kvs_fini();
        return 1;
    }

    /* 把 reactor 指针作为 user_data 传给回调（kvs_on_message 里要用 connection_send） */
    reactor_set_callbacks(g_r, kvs_on_message, on_close, g_r);

    if (reactor_listen(g_r, cfg.bind_ip, port, 128) != 0)
    {
        fprintf(stderr, "reactor_listen failed on port %u\n", port);
        reactor_destroy(g_r);
        g_r = NULL;
        kvs_fini();
        return 1;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    printf("kvstore_server listening on %s:%u\n", cfg.bind_ip, port);
    fflush(stdout);

    reactor_run(g_r);

    reactor_destroy(g_r);
    g_r = NULL;
    kvs_aof_close();
    kvs_fini();
    return 0;
}