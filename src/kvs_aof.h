#ifndef KVS_AOF_H
#define KVS_AOF_H

#include "kvs_config.h"

int kvs_aof_init(const char *filename, int enabled, kvs_aof_fsync_type_t policy);
void kvs_aof_close(void);

int kvs_aof_load(void);
int kvs_aof_is_loading(void);

int kvs_aof_append_set(const char *key, const char *value);
int kvs_aof_append_del(const char *key);

int kvs_aof_append_hset(const char *key, const char *value);
int kvs_aof_append_hdel(const char *key);

int kvs_aof_append_rset(const char *key, const char *value);
int kvs_aof_append_rdel(const char *key);

int kvs_aof_maybe_fsync(void);

#endif /* KVS_AOF_H */