#pragma once

typedef enum
{
    KVS_ALLOC_SYSTEM = 0,
    KVS_ALLOC_JEMALLOC,
    KVS_ALLOC_MYPOOL
} kvs_alloc_type_t;

typedef enum
{
    KVS_NET_REACTOR = 0,
    KVS_NET_PROACTOR,
    KVS_NET_NTYCO
} kvs_net_type_t;

typedef enum
{
    KVS_AOF_FSYNC_NO = 0,
    KVS_AOF_FSYNC_ALWAYS,
    KVS_AOF_FSYNC_EVERYSEC
} kvs_aof_fsync_type_t;

typedef struct
{
    char bind_ip[64];
    int port;

    kvs_alloc_type_t allocator;
    kvs_net_type_t network;

   int appendonly;
    char appendfilename[256];
    kvs_aof_fsync_type_t appendfsync;

} kvs_config_t;

int kvs_config_load_file(kvs_config_t *cfg, const char *path);