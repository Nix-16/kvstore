#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/kvs_array.h"

/* 生成 key/value */
static void make_kv(char *kbuf, size_t ksz, char *vbuf, size_t vsz, int i)
{
    snprintf(kbuf, ksz, "k%d", i);
    snprintf(vbuf, vsz, "v%d", i);
}

/*
 * 1) 基本 CRUD（覆盖写语义：set(upsert)）
 */
static void test_basic_crud(void)
{
    kvs_array_t a = (kvs_array_t){0};

    int rc = kvs_array_create(&a);
    assert(rc == 0);
    assert(a.table != NULL);

    rc = kvs_array_set(&a, "k1", "v1");
    assert(rc == 0);

    assert(kvs_array_exist(&a, "k1") == 0);

    char *v = kvs_array_get(&a, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v1") == 0);

    /* set 覆盖：应成功返回 0，且值变为 v2 */
    rc = kvs_array_set(&a, "k1", "v2");
    assert(rc == 0);

    v = kvs_array_get(&a, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v2") == 0);

    /* mod 仍然可用：把 v2 改回 v3 */
    rc = kvs_array_mod(&a, "k1", "v3");
    assert(rc == 0);
    v = kvs_array_get(&a, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v3") == 0);

    rc = kvs_array_del(&a, "k1");
    assert(rc == 0);
    assert(kvs_array_get(&a, "k1") == NULL);
    assert(kvs_array_exist(&a, "k1") == 1);

    rc = kvs_array_del(&a, "k1");
    assert(rc > 0); /* no exist */

    kvs_array_destory(&a);
}

/*
 * 2) 参数健壮性（不崩溃 + 返回值符合注释）
 */
static void test_null_args(void)
{
    kvs_array_t a = (kvs_array_t){0};
    assert(kvs_array_create(&a) == 0);

    /* set 参数错误：<0 */
    assert(kvs_array_set(NULL, "k", "v") < 0);
    assert(kvs_array_set(&a, NULL, "v") < 0);
    assert(kvs_array_set(&a, "k", NULL) < 0);

    /* get 参数错误：NULL */
    assert(kvs_array_get(NULL, "k") == NULL);
    assert(kvs_array_get(&a, NULL) == NULL);

    /* del 参数错误：<0 */
    assert(kvs_array_del(NULL, "k") < 0);
    assert(kvs_array_del(&a, NULL) < 0);

    /* mod 参数错误：<0 */
    assert(kvs_array_mod(NULL, "k", "v") < 0);
    assert(kvs_array_mod(&a, NULL, "v") < 0);
    assert(kvs_array_mod(&a, "k", NULL) < 0);

    /* exist 参数错误：<0 */
    assert(kvs_array_exist(NULL, "k") < 0);
    assert(kvs_array_exist(&a, NULL) < 0);

    kvs_array_destory(&a);
}

/*
 * 3) value/key 拷贝语义测试（防止悬挂指针）
 *    set() 内部 kvs_malloc+拷贝，应当通过。
 */
static void test_copy_semantics(void)
{
    kvs_array_t a = (kvs_array_t){0};
    assert(kvs_array_create(&a) == 0);

    char key_buf[16];
    char val_buf[16];
    strcpy(key_buf, "k1");
    strcpy(val_buf, "v1");

    assert(kvs_array_set(&a, key_buf, val_buf) == 0);

    /* 修改原 buffer，若内部做拷贝，存储值不应被影响 */
    strcpy(val_buf, "v1_changed");

    char *v = kvs_array_get(&a, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v1") == 0);

    kvs_array_destory(&a);
}

/*
 * 4) 容量满 + 删除中间后仍可插入（洞位复用）
 *    另外：数组满时，对“已存在 key 的 set 覆盖”也应成功（不占新槽）。
 */
static void test_capacity_full_and_hole_reuse(void)
{
    kvs_array_t a = (kvs_array_t){0};
    assert(kvs_array_create(&a) == 0);

    char k[64], v[64];

    /* 填满 */
    for (int i = 0; i < KVS_ARRAY_SIZE; i++)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        int rc = kvs_array_set(&a, k, v);
        assert(rc == 0);
    }

    /* 再插入一个“新 key”：必须失败（数组已满） */
    int rc = kvs_array_set(&a, "k_over", "v_over");
    assert(rc < 0);

    /* 但对“已存在 key”做 set 覆盖：必须成功 */
    rc = kvs_array_set(&a, "k100", "v100_new");
    assert(rc == 0);
    char *got2 = kvs_array_get(&a, "k100");
    assert(got2 != NULL);
    assert(strcmp(got2, "v100_new") == 0);

    /* 删除一个“中间 key”，形成洞位 */
    rc = kvs_array_del(&a, "k100");
    assert(rc == 0);

    /* 现在应该能插入新 key（复用洞位），且不应仍然报满 */
    rc = kvs_array_set(&a, "k_new", "v_new");
    assert(rc == 0);

    /* 校验可取回 */
    char *got = kvs_array_get(&a, "k_new");
    assert(got != NULL);
    assert(strcmp(got, "v_new") == 0);

    kvs_array_destory(&a);
}

/*
 * 5) 批量插入 -> 删除一半 -> 再插入（更接近真实使用）
 *    重点是：不崩溃，且插入/查询逻辑仍正确。
 */
static void test_batch_insert_delete_insert(void)
{
    kvs_array_t a = (kvs_array_t){0};
    assert(kvs_array_create(&a) == 0);

    char k[64], v[64];

    /* 插入 200 个 */
    for (int i = 0; i < 200; i++)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_array_set(&a, k, v) == 0);
    }

    /* 删除偶数 key */
    for (int i = 0; i < 200; i += 2)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_array_del(&a, k) == 0);
    }

    /* 再插入 100 个新 key，应当成功（复用洞位） */
    for (int i = 0; i < 100; i++)
    {
        snprintf(k, sizeof(k), "n%d", i);
        snprintf(v, sizeof(v), "nv%d", i);
        int rc = kvs_array_set(&a, k, v);
        assert(rc == 0);
    }

    /* 校验：奇数原 key 还在 */
    for (int i = 1; i < 200; i += 2)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        char *got = kvs_array_get(&a, k);
        assert(got != NULL);
        assert(strcmp(got, v) == 0);
    }

    /* 校验：偶数原 key 不在 */
    for (int i = 0; i < 200; i += 2)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_array_get(&a, k) == NULL);
    }

    kvs_array_destory(&a);
}

/*
 * 6) destroy 后再 create（如果 destroy 没置 table=NULL，这里会失败）
 */
static void test_destroy_then_recreate(void)
{
    kvs_array_t a = (kvs_array_t){0};
    assert(kvs_array_create(&a) == 0);
    assert(kvs_array_set(&a, "k", "v") == 0);

    kvs_array_destory(&a);

    assert(kvs_array_create(&a) == 0);
    kvs_array_destory(&a);
}

int main(void)
{
    test_basic_crud();
    test_null_args();
    test_copy_semantics();
    test_capacity_full_and_hole_reuse();
    test_batch_insert_delete_insert();
    test_destroy_then_recreate();

    printf("ALL TESTS PASSED.\n");
    return 0;
}