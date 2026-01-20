#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/kvs_hash.h"

/* 生成 key/value */
static void make_kv(char *kbuf, size_t ksz, char *vbuf, size_t vsz, int i)
{
    snprintf(kbuf, ksz, "k%d", i);
    snprintf(vbuf, vsz, "v%d", i);
}

/* ============================
 * 1) 基本 CRUD
 * ============================ */
static void test_basic_crud(void)
{
    kvs_hash_t h = {0};

    int rc = kvs_hash_create(&h);
    assert(rc == 0);
    assert(h.buckets != NULL);
    assert(h.max_slots > 0);

    rc = kvs_hash_set(&h, "k1", "v1");
    assert(rc == 0);

    assert(kvs_hash_exist(&h, "k1") == 0);

    char *v = kvs_hash_get(&h, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v1") == 0);

    rc = kvs_hash_set(&h, "k1", "v2");
    assert(rc > 0); /* exist */

    /* set 不覆盖：仍为 v1（如果你设计为覆盖，这里改成 v2） */
    v = kvs_hash_get(&h, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v1") == 0);

    rc = kvs_hash_mod(&h, "k1", "v2");
    assert(rc == 0);
    v = kvs_hash_get(&h, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v2") == 0);

    rc = kvs_hash_del(&h, "k1");
    assert(rc == 0);
    assert(kvs_hash_get(&h, "k1") == NULL);
    assert(kvs_hash_exist(&h, "k1") == 1);

    rc = kvs_hash_del(&h, "k1");
    assert(rc > 0); /* no exist */

    rc = kvs_hash_destory(&h);
    assert(rc == 0);
}

/* ============================
 * 2) 参数健壮性（NULL + 未 create 状态）
 *    注意：hash 的头文件约定了未 create 为 -2
 * ============================ */
static void test_null_args_and_state(void)
{
    kvs_hash_t h = {0};

    /* 未 create 状态：-2（按头文件） */
    assert(kvs_hash_set(&h, "k", "v") == -2);
    assert(kvs_hash_get(&h, "k") == NULL);
    assert(kvs_hash_del(&h, "k") == -2);
    assert(kvs_hash_mod(&h, "k", "v") == -2);
    assert(kvs_hash_exist(&h, "k") == -2);
    assert(kvs_hash_destory(&h) == -2);

    assert(kvs_hash_create(&h) == 0);

    /* set 参数错误：-1 */
    assert(kvs_hash_set(NULL, "k", "v") == -1);
    assert(kvs_hash_set(&h, NULL, "v") == -1);
    assert(kvs_hash_set(&h, "k", NULL) == -1);

    /* get 参数错误：NULL */
    assert(kvs_hash_get(NULL, "k") == NULL);
    assert(kvs_hash_get(&h, NULL) == NULL);

    /* del 参数错误：-1 */
    assert(kvs_hash_del(NULL, "k") == -1);
    assert(kvs_hash_del(&h, NULL) == -1);

    /* mod 参数错误：-1 */
    assert(kvs_hash_mod(NULL, "k", "v") == -1);
    assert(kvs_hash_mod(&h, NULL, "v") == -1);
    assert(kvs_hash_mod(&h, "k", NULL) == -1);

    /* exist 参数错误：-1 */
    assert(kvs_hash_exist(NULL, "k") == -1);
    assert(kvs_hash_exist(&h, NULL) == -1);

    assert(kvs_hash_destory(&h) == 0);
}

/* ============================
 * 3) value/key 拷贝语义测试（防悬挂指针）
 * ============================ */
static void test_copy_semantics(void)
{
    kvs_hash_t h = {0};
    assert(kvs_hash_create(&h) == 0);

    char key_buf[16];
    char val_buf[16];
    strcpy(key_buf, "k1");
    strcpy(val_buf, "v1");

    assert(kvs_hash_set(&h, key_buf, val_buf) == 0);

    /* 修改原 buffer，若内部做拷贝，存储值不应被影响 */
    strcpy(val_buf, "v1_changed");
    strcpy(key_buf, "k1_changed"); /* key_buf 改不影响查找，因为查找用字面量 */

    char *v = kvs_hash_get(&h, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v1") == 0);

    assert(kvs_hash_destory(&h) == 0);
}

/* ============================
 * 4) 冲突链表测试（强行制造同桶冲突）
 *
 * 关键点：
 * - bucket 数固定为 KVS_HASH_DEFAULT_SLOTS
 * - 我们通过 “kX + N*slots” 来制造同余（对常见 hash%slots 来说）
 *
 * 注意：你的实现如果不是 hash(key)%slots（比如用了其他策略或扩容），
 * 这个测试需要调整冲突构造方式。
 * ============================ */
static void test_collision_chain_del_head_mid_tail(void)
{
    kvs_hash_t h = {0};
    assert(kvs_hash_create(&h) == 0);
    assert(h.max_slots == KVS_HASH_DEFAULT_SLOTS);

    char k1[64], k2[64], k3[64];
    /* 采用可预期的 key 模式，尽量制造相同 idx（同余类） */
    snprintf(k1, sizeof(k1), "k_base");
    snprintf(k2, sizeof(k2), "k_base_%d", h.max_slots);
    snprintf(k3, sizeof(k3), "k_base_%d", 2 * h.max_slots);

    /* 插入三条 */
    assert(kvs_hash_set(&h, k1, "v1") == 0);
    assert(kvs_hash_set(&h, k2, "v2") == 0);
    assert(kvs_hash_set(&h, k3, "v3") == 0);

    /* 三条都能取到 */
    assert(strcmp(kvs_hash_get(&h, k1), "v1") == 0);
    assert(strcmp(kvs_hash_get(&h, k2), "v2") == 0);
    assert(strcmp(kvs_hash_get(&h, k3), "v3") == 0);

    /*
     * 删除链表头/中间/尾：
     * 由于 set 头插，最后插入的 k3 更可能在链表头。
     * 我们依次删 k3(头)、k2(中)、k1(尾) 来覆盖分支。
     */
    assert(kvs_hash_del(&h, k3) == 0);
    assert(kvs_hash_get(&h, k3) == NULL);
    assert(strcmp(kvs_hash_get(&h, k2), "v2") == 0);
    assert(strcmp(kvs_hash_get(&h, k1), "v1") == 0);

    assert(kvs_hash_del(&h, k2) == 0);
    assert(kvs_hash_get(&h, k2) == NULL);
    assert(strcmp(kvs_hash_get(&h, k1), "v1") == 0);

    assert(kvs_hash_del(&h, k1) == 0);
    assert(kvs_hash_get(&h, k1) == NULL);

    /* 再删一次：no exist */
    assert(kvs_hash_del(&h, k1) == 1);

    assert(kvs_hash_destory(&h) == 0);
}

/* ============================
 * 5) 批量插入 -> 删除一半 -> 再插入
 * ============================ */
static void test_batch_insert_delete_insert(void)
{
    kvs_hash_t h = {0};
    assert(kvs_hash_create(&h) == 0);

    char k[64], v[64];

    /* 插入 2000 个 */
    for (int i = 0; i < 2000; i++)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_hash_set(&h, k, v) == 0);
    }
    assert(h.count == 2000);

    /* 删除偶数 key */
    for (int i = 0; i < 2000; i += 2)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_hash_del(&h, k) == 0);
    }
    assert(h.count == 1000);

    /* 校验：奇数原 key 还在 */
    for (int i = 1; i < 2000; i += 2)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        char *got = kvs_hash_get(&h, k);
        assert(got != NULL);
        assert(strcmp(got, v) == 0);
    }

    /* 校验：偶数原 key 不在 */
    for (int i = 0; i < 2000; i += 2)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_hash_get(&h, k) == NULL);
    }

    /* 再插入 500 个新 key，应当成功 */
    for (int i = 0; i < 500; i++)
    {
        snprintf(k, sizeof(k), "n%d", i);
        snprintf(v, sizeof(v), "nv%d", i);
        assert(kvs_hash_set(&h, k, v) == 0);
    }
    assert(h.count == 1500);

    /* 抽查新 key */
    for (int i = 0; i < 500; i += 37)
    {
        snprintf(k, sizeof(k), "n%d", i);
        snprintf(v, sizeof(v), "nv%d", i);
        char *got = kvs_hash_get(&h, k);
        assert(got != NULL);
        assert(strcmp(got, v) == 0);
    }

    assert(kvs_hash_destory(&h) == 0);
}

/* ============================
 * 6) destroy 后再 create
 * ============================ */
static void test_destroy_then_recreate(void)
{
    kvs_hash_t h = {0};
    assert(kvs_hash_create(&h) == 0);
    assert(kvs_hash_set(&h, "k", "v") == 0);

    assert(kvs_hash_destory(&h) == 0);

    /* 若 destroy 没置 buckets=NULL，这里会返回 -2 */
    assert(kvs_hash_create(&h) == 0);
    assert(kvs_hash_destory(&h) == 0);
}

int main(void)
{
    test_basic_crud();
    test_null_args_and_state();
    test_copy_semantics();
    test_collision_chain_del_head_mid_tail();
    test_batch_insert_delete_insert();
    test_destroy_then_recreate();

    printf("ALL TESTS PASSED.\n");
    return 0;
}
