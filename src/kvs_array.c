#include "kvs_array.h"

/*
 * global instance
 */
kvs_array_t global_array = {0};

static void shrink_tail(kvs_array_t *inst)
{
    // 收缩尾部空洞，避免 total 漂移
    while (inst->total > 0 && inst->table[inst->total - 1].key == NULL)
        inst->total--;
    if (inst->idx > inst->total)
        inst->idx = inst->total;
}

int kvs_array_create(kvs_array_t *inst)
{
    if (inst == NULL)
        return -1; // 参数错误

    if (inst->table != NULL)
        return -2; // 重复create/状态错误

    inst->table = kvs_malloc(KVS_ARRAY_SIZE * sizeof(kvs_array_item_t));
    if (inst->table == NULL)
        return -3; // 内存分配失败

    memset(inst->table, 0, KVS_ARRAY_SIZE * sizeof(kvs_array_item_t));
    inst->idx = 0;
    inst->total = 0;
    return 0;
}

int kvs_array_destory(kvs_array_t *inst)
{
    if (inst == NULL)
        return -1; // 参数错误
    if (inst->table == NULL)
        return -2; // 未create/重复destroy

    // 释放 key/value，避免泄漏
    for (int i = 0; i < inst->total; i++)
    {
        if (inst->table[i].key)
        {
            kvs_free(inst->table[i].key);
            inst->table[i].key = NULL;
        }
        if (inst->table[i].value)
        {
            kvs_free(inst->table[i].value);
            inst->table[i].value = NULL;
        }
    }

    kvs_free(inst->table);
    inst->table = NULL;
    inst->idx = 0;
    inst->total = 0;
    return 0;
}

int kvs_array_set(kvs_array_t *inst, char *key, char *value)
{
    if (inst == NULL || key == NULL || value == NULL)
        return -1; // 参数错误
    if (inst->table == NULL)
        return -2; // 未create/状态错误

    /* 1) 先查找是否已存在：存在则覆盖 value */
    for (int i = 0; i < inst->total; i++)
    {
        if (inst->table[i].key == NULL)
            continue;

        if (strcmp(inst->table[i].key, key) == 0)
        {
            /* 覆盖写：先申请新 value，成功后替换，避免 malloc 失败导致旧值丢失 */
            size_t vlen = strlen(value) + 1;
            char *newv = kvs_malloc(vlen);
            if (newv == NULL)
                return -3;
            memcpy(newv, value, vlen);

            if (inst->table[i].value)
                kvs_free(inst->table[i].value);

            inst->table[i].value = newv;
            return 0;
        }
    }

    /* 2) 不存在：走新增逻辑（复用洞位或尾插） */
    int slot = -1;

    /* 2.1 优先找洞位复用 */
    for (int i = 0; i < inst->total; i++)
    {
        if (inst->table[i].key == NULL)
        {
            slot = i;
            break;
        }
    }

    /* 2.2 无洞位则尾部追加 */
    if (slot == -1)
    {
        if (inst->total >= KVS_ARRAY_SIZE)
            return -4; // 空间不足
        slot = inst->total;
        inst->total++; // 仅尾部追加时增长 total
    }

    /* 3) 分配并拷贝 key/value */
    size_t klen = strlen(key) + 1;
    char *kcopy = kvs_malloc(klen);
    if (kcopy == NULL)
        return -3;
    memcpy(kcopy, key, klen);

    size_t vlen = strlen(value) + 1;
    char *vcopy = kvs_malloc(vlen);
    if (vcopy == NULL)
    {
        kvs_free(kcopy);
        return -3;
    }
    memcpy(vcopy, value, vlen);

    inst->table[slot].key = kcopy;
    inst->table[slot].value = vcopy;

    return 0;
}

char *kvs_array_get(kvs_array_t *inst, char *key)
{
    if (inst == NULL || key == NULL || inst->table == NULL)
        return NULL;

    for (int i = 0; i < inst->total; i++)
    {
        if (inst->table[i].key == NULL)
            continue;

        if (strcmp(inst->table[i].key, key) == 0)
            return inst->table[i].value;
    }
    return NULL;
}

int kvs_array_del(kvs_array_t *inst, char *key)
{
    if (inst == NULL || key == NULL)
        return -1; // 参数错误
    if (inst->table == NULL)
        return -2; // 未create/状态错误

    for (int i = 0; i < inst->total; i++)
    {
        if (inst->table[i].key == NULL)
            continue; // 必须跳过洞位，避免 strcmp(NULL, key)

        if (strcmp(inst->table[i].key, key) == 0)
        {
            kvs_free(inst->table[i].key);
            inst->table[i].key = NULL;

            if (inst->table[i].value)
            {
                kvs_free(inst->table[i].value);
                inst->table[i].value = NULL;
            }

            shrink_tail(inst);
            return 0;
        }
    }

    return 1; // no exist（固定返回1，避免你原来返回i导致语义不稳）
}

int kvs_array_exist(kvs_array_t *inst, char *key)
{
    if (inst == NULL || key == NULL)
        return -1; // 参数错误
    if (inst->table == NULL)
        return -2; // 未create/状态错误

    return (kvs_array_get(inst, key) != NULL) ? 0 : 1;
}
