#ifndef KVSTORE_H
#define KVSTORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* reactor 里的连接对象，这里只做前置声明，避免头文件耦合 */
struct connection;

/* 选择底层后端（先用 ARRAY，后续扩展） */
typedef enum {
    KVS_BACKEND_ARRAY = 0,
    KVS_BACKEND_HASH  = 1,
    KVS_BACKEND_RBTREE= 2
} kvs_backend_t;

/* KVStore 初始化参数 */
typedef struct kvs_options {
    kvs_backend_t backend;
} kvs_options_t;

/* 初始化/销毁 */
int kvs_init(const kvs_options_t *opt);
void kvs_fini(void);

/* 统一 KV API（先走数组） */
int   kvs_set(const char *key, const char *value);
char* kvs_get(const char *key);
int   kvs_del(const char *key);
int   kvs_exists(const char *key);

/* reactor 的 on_message 回调入口 */
int kvs_on_message(struct connection *c, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* KVSTORE_H */