#pragma once
#include "kvs_alloc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>

#define KVS_RBTREE_RED   1
#define KVS_RBTREE_BLACK 2

typedef struct kvs_rbtree_node_s {
    unsigned char color;
    struct kvs_rbtree_node_s *right;
    struct kvs_rbtree_node_s *left;
    struct kvs_rbtree_node_s *parent;

    /* 统一为字符串 KV：内部必须拷贝保存，避免悬挂指针 */
    char *key;
    char *value;
} kvs_rbtree_node_t;

typedef struct kvs_rbtree_s {
    kvs_rbtree_node_t *root;
    kvs_rbtree_node_t *nil;   /* RBTree 哨兵节点：create 时分配，destroy 时释放 */
    int count;                /* 可选但非常实用：统计节点数，便于测试/监控 */
} kvs_rbtree_t;

/*
 * kvs_rbtree_create
 * @return:
 *  0   success
 * -1   inst == NULL 参数错误
 * -2   inst->nil != NULL 重复create/状态错误
 * -3   内存分配失败（nil 分配失败）
 */
int kvs_rbtree_create(kvs_rbtree_t *inst);

/*
 * kvs_rbtree_destory
 * @return:
 *  0   success
 * -1   inst == NULL 参数错误
 * -2   inst->nil == NULL 未create/重复destroy
 *
 * 说明：
 * - 必须释放所有节点的 key/value/node
 * - 最后释放 nil，并将 root/nil/count 复位
 */
int kvs_rbtree_destory(kvs_rbtree_t *inst);

/*
 * kvs_rbtree_set
 * @return:
 *  0   success (新增或覆盖都返回 0)
 * -1   inst/key/value == NULL 参数错误
 * -2   inst->nil == NULL 未create/状态错误
 * -3   内存分配失败（节点或 key/value 拷贝失败）
 */
int kvs_rbtree_set(kvs_rbtree_t *inst, char *key, char *value);

/*
 * kvs_rbtree_get
 * @return:
 *  char*  success (找到返回value指针)
 *  NULL   not found 或 参数/状态错误
 */
char *kvs_rbtree_get(kvs_rbtree_t *inst, char *key);

/*
 * kvs_rbtree_del
 * @return:
 *  0   success
 *  1   no exist
 * -1   inst/key == NULL 参数错误
 * -2   inst->nil == NULL 未create/状态错误
 */
int kvs_rbtree_del(kvs_rbtree_t *inst, char *key);


/*
 * kvs_rbtree_exist
 * @return:
 *  0   exist
 *  1   no exist
 * -1   inst/key == NULL 参数错误
 * -2   inst->nil == NULL 未create/状态错误
 */
int kvs_rbtree_exist(kvs_rbtree_t *inst, char *key);

/*
 * kvs_rbtree_count (可选接口，但建议保留)
 * @return:
 *  >=0 节点数量；inst==NULL 返回 0
 */
int kvs_rbtree_count(kvs_rbtree_t *inst);

typedef int (*kvs_rbtree_visit_fn)(const char *key, const char *value, void *arg);

int kvs_rbtree_foreach(kvs_rbtree_t *inst, kvs_rbtree_visit_fn fn, void *arg);
int kvs_rbtree_count(kvs_rbtree_t *inst);