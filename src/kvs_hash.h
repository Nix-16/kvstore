#pragma once
#include "kvs_alloc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>

#define KVS_HASH_DEFAULT_SLOTS 1024 // 默认桶数，可按需调整

typedef struct kvs_hash_node_s
{
    char *key;
    char *value;
    struct kvs_hash_node_s *next;
} kvs_hash_node_t;

typedef struct kvs_hash_s
{
    kvs_hash_node_t **buckets; // 桶数组，每个桶是链表头指针
    int max_slots;             // 桶数
    int count;                 // 总键数
} kvs_hash_t;

/*
 * kvs_hash_create
 * @return:
 *  0   success
 * -1   inst == NULL 参数错误
 * -2   inst->buckets != NULL 重复create/状态错误
 * -3   内存分配失败
 */
int kvs_hash_create(kvs_hash_t *inst);

/*
 * kvs_hash_destory
 * @return:
 *  0   success
 * -1   inst == NULL 参数错误
 * -2   inst->buckets == NULL 未create/重复destroy
 */
int kvs_hash_destory(kvs_hash_t *inst);

/*
 * kvs_hash_set
 * 语义：若 key 不存在则新增；若已存在则覆盖 value（与数组后端对齐）
 * @return:
 *  0   success (新增或覆盖都返回 0)
 * -1   inst/key/value == NULL 参数错误
 * -2   inst->buckets == NULL 未create/状态错误
 * -3   内存分配失败（节点/字符串分配失败）
 */
int kvs_hash_set(kvs_hash_t *inst, char *key, char *value);

/*
 * kvs_hash_get
 * @return:
 *  char*  success (找到返回value指针)
 *  NULL   not found 或 参数/状态错误（若要区分错误原因，用 kvs_hash_exist 先判定）
 */
char *kvs_hash_get(kvs_hash_t *inst, char *key);

/*
 * kvs_hash_del
 * @return:
 *  0   success
 *  1   no exist
 * -1   inst/key == NULL 参数错误
 * -2   inst->buckets == NULL 未create/状态错误
 */
int kvs_hash_del(kvs_hash_t *inst, char *key);

/*
 * kvs_hash_exist
 * @return:
 *  0   exist
 *  1   no exist
 * -1   inst/key == NULL 参数错误
 * -2   inst->buckets == NULL 未create/状态错误
 */
int kvs_hash_exist(kvs_hash_t *inst, char *key);
