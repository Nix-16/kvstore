#include "kvs_snapshot.h"
#include "kvs_config.h"
#include "kvs_array.h"
#include "kvs_hash.h"
#include "kvs_rbtree.h"
#include "kvs_alloc.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>   // fsync
#include <fcntl.h>    // fileno

/* 外部全局配置与全局实例 */
extern kvs_config_t global_config;
extern kvs_array_t global_array;
extern kvs_hash_t global_hash;
extern kvs_rbtree_t global_rbtree;

#define KVS_SNAPSHOT_VERSION 1

typedef struct
{
    char magic[4];              /* "KVS1" */
    uint32_t version;
    uint32_t array_count;
    uint32_t hash_count;
    uint32_t rbtree_count;
} kvs_snapshot_header_t;

static int write_one_kv(FILE *fp, const char *key, const char *value)
{
    uint32_t klen = (uint32_t)strlen(key);
    uint32_t vlen = (uint32_t)strlen(value);

    if (fwrite(&klen, sizeof(klen), 1, fp) != 1) return -1;
    if (fwrite(&vlen, sizeof(vlen), 1, fp) != 1) return -1;
    if (fwrite(key, 1, klen, fp) != klen) return -1;
    if (fwrite(value, 1, vlen, fp) != vlen) return -1;

    return 0;
}

/* ---------- array 遍历写盘回调 ---------- */
static int snapshot_array_visit(const char *key, const char *value, void *arg)
{
    FILE *fp = (FILE *)arg;
    return write_one_kv(fp, key, value);
}

/* ---------- hash 遍历写盘回调 ---------- */
static int snapshot_hash_visit(const char *key, const char *value, void *arg)
{
    FILE *fp = (FILE *)arg;
    return write_one_kv(fp, key, value);
}

/* ---------- rbtree 遍历写盘回调 ---------- */
static int snapshot_rbtree_visit(const char *key, const char *value, void *arg)
{
    FILE *fp = (FILE *)arg;
    return write_one_kv(fp, key, value);
}

int kvs_snapshot_save(void)
{
    if (!global_config.snapshot_enabled)
        return -1;
    if (global_config.snapshot_file[0] == '\0')
        return -1;

    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", global_config.snapshot_file);

    FILE *fp = fopen(tmpfile, "wb");
    if (!fp)
        return -2;

    kvs_snapshot_header_t hdr;
    memcpy(hdr.magic, "KVS1", 4);
    hdr.version = KVS_SNAPSHOT_VERSION;
    hdr.array_count  = (uint32_t)kvs_array_count(&global_array);
    hdr.hash_count   = (uint32_t)kvs_hash_count(&global_hash);
    hdr.rbtree_count = (uint32_t)kvs_rbtree_count(&global_rbtree);

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return -3;
    }

    if (kvs_array_foreach(&global_array, snapshot_array_visit, fp) != 0)
    {
        fclose(fp);
        return -5;
    }

    if (kvs_hash_foreach(&global_hash, snapshot_hash_visit, fp) != 0)
    {
        fclose(fp);
        return -5;
    }

    if (kvs_rbtree_foreach(&global_rbtree, snapshot_rbtree_visit, fp) != 0)
    {
        fclose(fp);
        return -5;
    }

    fflush(fp);
    if (fsync(fileno(fp)) != 0)
    {
        fclose(fp);
        return -4;
    }

    if (fclose(fp) != 0)
        return -4;

    if (rename(tmpfile, global_config.snapshot_file) != 0)
        return -4;

    return 0;
}

static int read_one_kv(FILE *fp, char **out_key, char **out_value)
{
    uint32_t klen = 0, vlen = 0;

    if (fread(&klen, sizeof(klen), 1, fp) != 1) return -1;
    if (fread(&vlen, sizeof(vlen), 1, fp) != 1) return -1;

    char *key = (char *)kvs_malloc(klen + 1);
    char *value = (char *)kvs_malloc(vlen + 1);
    if (!key || !value)
    {
        if (key) kvs_free(key);
        if (value) kvs_free(value);
        return -1;
    }

    if (fread(key, 1, klen, fp) != klen ||
        fread(value, 1, vlen, fp) != vlen)
    {
        kvs_free(key);
        kvs_free(value);
        return -1;
    }

    key[klen] = '\0';
    value[vlen] = '\0';

    *out_key = key;
    *out_value = value;
    return 0;
}

int kvs_snapshot_load(void)
{
    if (!global_config.snapshot_enabled)
        return -1;
    if (global_config.snapshot_file[0] == '\0')
        return -1;

    FILE *fp = fopen(global_config.snapshot_file, "rb");
    if (!fp)
        return 1;   /* 文件不存在，视为正常 */

    kvs_snapshot_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return -3;
    }

    if (memcmp(hdr.magic, "KVS1", 4) != 0 || hdr.version != KVS_SNAPSHOT_VERSION)
    {
        fclose(fp);
        return -3;
    }

    for (uint32_t i = 0; i < hdr.array_count; i++)
    {
        char *key = NULL, *value = NULL;
        if (read_one_kv(fp, &key, &value) != 0)
        {
            fclose(fp);
            return -4;
        }

        if (kvs_array_set(&global_array, key, value) != 0)
        {
            kvs_free(key);
            kvs_free(value);
            fclose(fp);
            return -5;
        }

        kvs_free(key);
        kvs_free(value);
    }

    for (uint32_t i = 0; i < hdr.hash_count; i++)
    {
        char *key = NULL, *value = NULL;
        if (read_one_kv(fp, &key, &value) != 0)
        {
            fclose(fp);
            return -4;
        }

        if (kvs_hash_set(&global_hash, key, value) != 0)
        {
            kvs_free(key);
            kvs_free(value);
            fclose(fp);
            return -5;
        }

        kvs_free(key);
        kvs_free(value);
    }

    for (uint32_t i = 0; i < hdr.rbtree_count; i++)
    {
        char *key = NULL, *value = NULL;
        if (read_one_kv(fp, &key, &value) != 0)
        {
            fclose(fp);
            return -4;
        }

        if (kvs_rbtree_set(&global_rbtree, key, value) != 0)
        {
            kvs_free(key);
            kvs_free(value);
            fclose(fp);
            return -5;
        }

        kvs_free(key);
        kvs_free(value);
    }

    fclose(fp);
    return 0;
}