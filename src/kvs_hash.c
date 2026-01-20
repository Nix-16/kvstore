#include "kvs_hash.h"


/* djb2：简单、稳定、分布比“字符求和”好很多 */
static unsigned int kvs_hash_str(const char *s)
{
    unsigned int h = 5381u;
    unsigned char c;

    if (!s)
        return 0u;

    while ((c = (unsigned char)*s++) != 0)
    {
        h = ((h << 5) + h) + c; /* h * 33 + c */
    }
    return h;
}

/* 统一的索引计算：使用 inst->max_slots，而不是写死宏 */
static int kvs_hash_index(const kvs_hash_t *inst, const char *key)
{
    if (!inst || !inst->buckets || !key || inst->max_slots <= 0)
    {
        return -1;
    }
    return (int)(kvs_hash_str(key) % (unsigned int)inst->max_slots);
}

static kvs_hash_node_t *kvs_hash_create_node(const char *key, const char *value)
{
    kvs_hash_node_t *node = (kvs_hash_node_t *)kvs_malloc(sizeof(kvs_hash_node_t));
    if (!node)
        return NULL;

    node->key = NULL;
    node->value = NULL;
    node->next = NULL;

    /* copy key */
    size_t klen = strlen(key);
    node->key = (char *)kvs_malloc(klen + 1);
    if (!node->key)
    {
        kvs_free(node);
        return NULL;
    }
    memcpy(node->key, key, klen + 1);

    /* copy value */
    size_t vlen = strlen(value);
    node->value = (char *)kvs_malloc(vlen + 1);
    if (!node->value)
    {
        kvs_free(node->key);
        kvs_free(node);
        return NULL;
    }
    memcpy(node->value, value, vlen + 1);

    return node;
}


int kvs_hash_create(kvs_hash_t *inst)
{
    if (!inst)
        return -1;
    if (inst->buckets != NULL)
        return -2;

    inst->max_slots = KVS_HASH_DEFAULT_SLOTS;
    inst->count = 0;

    inst->buckets = (kvs_hash_node_t **)kvs_malloc(sizeof(kvs_hash_node_t *) * inst->max_slots);
    if (!inst->buckets)
    {
        inst->max_slots = 0;
        return -3;
    }
    memset(inst->buckets, 0, sizeof(kvs_hash_node_t *) * inst->max_slots);

    return 0;
}

int kvs_hash_destory(kvs_hash_t *inst)
{
    if (!inst)
        return -1;
    if (!inst->buckets)
        return -2;

    for (int i = 0; i < inst->max_slots; i++)
    {
        kvs_hash_node_t *cur = inst->buckets[i];
        while (cur)
        {
            kvs_hash_node_t *next = cur->next;
            if (cur->key)
                kvs_free(cur->key);
            if (cur->value)
                kvs_free(cur->value);
            kvs_free(cur);
            cur = next;
        }
        inst->buckets[i] = NULL;
    }

    kvs_free(inst->buckets);
    inst->buckets = NULL;
    inst->max_slots = 0;
    inst->count = 0;

    return 0;
}

int kvs_hash_set(kvs_hash_t *inst, char *key, char *value)
{
    if (!inst || !key || !value)
        return -1;
    if (!inst->buckets)
        return -2;

    int idx = kvs_hash_index(inst, key);
    if (idx < 0)
        return -2;

    /* 查重 */
    kvs_hash_node_t *cur = inst->buckets[idx];
    while (cur)
    {
        if (strcmp(cur->key, key) == 0)
        {
            return 1; /* exist */
        }
        cur = cur->next;
    }

    /* 创建并头插 */
    kvs_hash_node_t *node = kvs_hash_create_node(key, value);
    if (!node)
        return -3;

    node->next = inst->buckets[idx];
    inst->buckets[idx] = node;
    inst->count++;

    return 0;
}

char *kvs_hash_get(kvs_hash_t *inst, char *key)
{
    if (!inst || !key)
        return NULL;
    if (!inst->buckets)
        return NULL;

    int idx = kvs_hash_index(inst, key);
    if (idx < 0)
        return NULL;

    kvs_hash_node_t *cur = inst->buckets[idx];
    while (cur)
    {
        if (strcmp(cur->key, key) == 0)
        {
            return cur->value;
        }
        cur = cur->next;
    }
    return NULL;
}

int kvs_hash_del(kvs_hash_t *inst, char *key)
{
    if (!inst || !key)
        return -1;
    if (!inst->buckets)
        return -2;

    int idx = kvs_hash_index(inst, key);
    if (idx < 0)
        return -2;

    kvs_hash_node_t *prev = NULL;
    kvs_hash_node_t *cur = inst->buckets[idx];

    while (cur)
    {
        if (strcmp(cur->key, key) == 0)
        {
            /* 摘链 */
            if (prev)
                prev->next = cur->next;
            else
                inst->buckets[idx] = cur->next;

            /* 释放 */
            kvs_free(cur->key);
            kvs_free(cur->value);
            kvs_free(cur);

            inst->count--;
            return 0;
        }
        prev = cur;
        cur = cur->next;
    }

    return 1; /* no exist */
}

int kvs_hash_mod(kvs_hash_t *inst, char *key, char *value)
{
    if (!inst || !key || !value)
        return -1;
    if (!inst->buckets)
        return -2;

    int idx = kvs_hash_index(inst, key);
    if (idx < 0)
        return -2;

    kvs_hash_node_t *cur = inst->buckets[idx];
    while (cur)
    {
        if (strcmp(cur->key, key) == 0)
            break;
        cur = cur->next;
    }
    if (!cur)
        return 1; /* no exist */

    /* 先分配新 value，成功后再替换，避免丢旧值 */
    size_t vlen = strlen(value);
    char *newv = (char *)kvs_malloc(vlen + 1);
    if (!newv)
        return -3;
    memcpy(newv, value, vlen + 1);

    kvs_free(cur->value);
    cur->value = newv;

    return 0;
}

int kvs_hash_exist(kvs_hash_t *inst, char *key)
{
    if (!inst || !key)
        return -1;
    if (!inst->buckets)
        return -2;

    return (kvs_hash_get(inst, key) != NULL) ? 0 : 1;
}
