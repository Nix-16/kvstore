#pragma once
#include "kvs_alloc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>

#define KVS_ARRAY_SIZE 1024

typedef struct kvs_array_item_s
{
    char *key;
    char *value;
} kvs_array_item_t;

typedef struct kvs_array_s
{
    kvs_array_item_t *table;
    int idx;
    int total;
} kvs_array_t;

/*
 * kvs_array_create
 * @return:
 *  0   success
 * -1   inst == NULL 参数错误
 * -2   inst->table != NULL 重复create/状态错误
 * -3   内存分配失败
 */
int kvs_array_create(kvs_array_t *inst);

/*
 * kvs_array_destory
 * @return:
 *  0   success
 * -1   inst == NULL 参数错误
 * -2   inst->table == NULL 未create/重复destroy
 */
int kvs_array_destory(kvs_array_t *inst);

/*
 * kvs_array_set  (upsert: 不存在则新增，存在则覆盖 value)
 * @return:
 *  0   success (新增或覆盖都算成功)
 * -1   inst/key/value == NULL 参数错误
 * -2   inst->table == NULL 未create/状态错误
 * -3   内存分配失败（节点/字符串分配失败）
 * -4   空间不足（无可用洞位且容量已满）【仅在新增时可能出现】
 */
int kvs_array_set(kvs_array_t *inst, char *key, char *value);

/*
 * kvs_array_get
 * @return:
 *  char*  success (找到返回value指针)
 *  NULL   not found 或 参数/状态错误（若要区分错误原因，用 kvs_array_exist 先判定）
 */
char *kvs_array_get(kvs_array_t *inst, char *key);

/*
 * kvs_array_del
 * @return:
 *  0   success
 *  1   no exist
 * -1   inst/key == NULL 参数错误
 * -2   inst->table == NULL 未create/状态错误
 */
int kvs_array_del(kvs_array_t *inst, char *key);

/*
 * kvs_array_exist
 * @return:
 *  0   exist
 *  1   no exist
 * -1   inst/key == NULL 参数错误
 * -2   inst->table == NULL 未create/状态错误
 */
int kvs_array_exist(kvs_array_t *inst, char *key);
