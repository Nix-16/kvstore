#ifndef KVSTORE_H
#define KVSTORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* reactor 里的连接对象，这里只做前置声明，避免头文件耦合 */
struct connection;

/* 初始化/销毁 */
int  kvs_init(void);
void kvs_fini(void);

/* 默认命名空间（SET/GET/DEL/EXISTS -> array） */
int   kvs_set(const char *key, const char *value);
char* kvs_get(const char *key);
int   kvs_del(const char *key);
int   kvs_exists(const char *key);

/* Hash 命名空间 */
int   kvs_hset(const char *key, const char *value);
char* kvs_hget(const char *key);
int   kvs_hdel(const char *key);
int   kvs_hexists(const char *key);

/* RBTree 命名空间 */
int   kvs_rset(const char *key, const char *value);
char* kvs_rget(const char *key);
int   kvs_rdel(const char *key);
int   kvs_rexists(const char *key);

/* reactor 的 on_message 回调入口 */
int kvs_on_message(struct connection *c, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* KVSTORE_H */