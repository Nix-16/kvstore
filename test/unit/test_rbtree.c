#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/kvs_rbtree.h"

/* 生成 key/value */
static void make_kv(char *kbuf, size_t ksz, char *vbuf, size_t vsz, int i)
{
    snprintf(kbuf, ksz, "k%06d", i);
    snprintf(vbuf, vsz, "v%06d", i);
}

/*
 * 1) 基本 CRUD
 */
static void test_basic_crud(void)
{
    kvs_rbtree_t t = {0};

    int rc = kvs_rbtree_create(&t);
    assert(rc == 0);

    rc = kvs_rbtree_set(&t, "k1", "v1");
    assert(rc == 0);

    assert(kvs_rbtree_exist(&t, "k1") == 0);

    char *v = kvs_rbtree_get(&t, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v1") == 0);

    rc = kvs_rbtree_set(&t, "k1", "v2");
    assert(rc > 0); /* exist */

    /* set 不覆盖：仍为 v1（如果你设计为覆盖，这里改成 v2） */
    v = kvs_rbtree_get(&t, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v1") == 0);

    rc = kvs_rbtree_mod(&t, "k1", "v2");
    assert(rc == 0);
    v = kvs_rbtree_get(&t, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v2") == 0);

    rc = kvs_rbtree_del(&t, "k1");
    assert(rc == 0);
    assert(kvs_rbtree_get(&t, "k1") == NULL);
    assert(kvs_rbtree_exist(&t, "k1") == 1);

    rc = kvs_rbtree_del(&t, "k1");
    assert(rc > 0); /* no exist */

    rc = kvs_rbtree_destory(&t);
    assert(rc == 0);
}

/*
 * 2) 参数健壮性（NULL + 未 create 状态）
 *    依据新头文件：
 *    - 未 create：set/del/mod/exist 返回 -2；get 返回 NULL；destroy 返回 -2
 *    - 参数错误：set/del/mod/exist 返回 -1；get 返回 NULL；create/destroy 返回 -1
 */
static void test_null_args_and_state(void)
{
    kvs_rbtree_t t = {0};

    /* 未 create 状态 */
    assert(kvs_rbtree_set(&t, "k", "v") == -2);
    assert(kvs_rbtree_get(&t, "k") == NULL);
    assert(kvs_rbtree_del(&t, "k") == -2);
    assert(kvs_rbtree_mod(&t, "k", "v") == -2);
    assert(kvs_rbtree_exist(&t, "k") == -2);
    assert(kvs_rbtree_destory(&t) == -2);

    assert(kvs_rbtree_create(&t) == 0);

    /* 参数错误 */
    assert(kvs_rbtree_create(NULL) == -1);

    assert(kvs_rbtree_set(NULL, "k", "v") == -1);
    assert(kvs_rbtree_set(&t, NULL, "v") == -1);
    assert(kvs_rbtree_set(&t, "k", NULL) == -1);

    assert(kvs_rbtree_get(NULL, "k") == NULL);
    assert(kvs_rbtree_get(&t, NULL) == NULL);

    assert(kvs_rbtree_del(NULL, "k") == -1);
    assert(kvs_rbtree_del(&t, NULL) == -1);

    assert(kvs_rbtree_mod(NULL, "k", "v") == -1);
    assert(kvs_rbtree_mod(&t, NULL, "v") == -1);
    assert(kvs_rbtree_mod(&t, "k", NULL) == -1);

    assert(kvs_rbtree_exist(NULL, "k") == -1);
    assert(kvs_rbtree_exist(&t, NULL) == -1);

    assert(kvs_rbtree_destory(&t) == 0);

    /* destroy again */
    assert(kvs_rbtree_destory(&t) == -2);
}

/*
 * 3) 拷贝语义测试（防悬挂指针）
 *    set 内部做 kvs_malloc+拷贝，应当通过。
 */
static void test_copy_semantics(void)
{
    kvs_rbtree_t t = {0};
    assert(kvs_rbtree_create(&t) == 0);

    char key_buf[16];
    char val_buf[16];
    strcpy(key_buf, "k1");
    strcpy(val_buf, "v1");

    assert(kvs_rbtree_set(&t, key_buf, val_buf) == 0);

    /* 修改原 buffer，若内部做拷贝，存储值不应被影响 */
    strcpy(val_buf, "v1_changed");

    char *v = kvs_rbtree_get(&t, "k1");
    assert(v != NULL);
    assert(strcmp(v, "v1") == 0);

    assert(kvs_rbtree_destory(&t) == 0);
}

/*
 * 4) 批量插入 -> 查询（覆盖旋转/修复）
 *    使用“逆序插入”让树必然发生旋转，否则只测到退化 BST 分支的概率更高。
 */
static void test_batch_insert_and_lookup(void)
{
    kvs_rbtree_t t = {0};
    assert(kvs_rbtree_create(&t) == 0);

    const int N = 2000;
    char k[64], v[64];

    /* 逆序插入 */
    for (int i = N - 1; i >= 0; i--)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_rbtree_set(&t, k, v) == 0);
    }

    /* count 校验（如果你保留了 count API） */
#ifdef KVS_RBTREE_COUNT_API
    assert(kvs_rbtree_count(&t) == N);
#else
    /* 若头文件没提供 count API，至少保证不崩溃即可 */
    (void)N;
#endif

    /* 抽查查找 */
    for (int i = 0; i < N; i += 137)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        char *got = kvs_rbtree_get(&t, k);
        assert(got != NULL);
        assert(strcmp(got, v) == 0);
        assert(kvs_rbtree_exist(&t, k) == 0);
    }

    /* 不存在的 key */
    assert(kvs_rbtree_get(&t, "no_such_key") == NULL);
    assert(kvs_rbtree_exist(&t, "no_such_key") == 1);

    assert(kvs_rbtree_destory(&t) == 0);
}

/*
 * 5) 删除多形态：删叶子/单子/双子（通过构造 + 删除顺序覆盖）
 *    这个用例的目标是覆盖 delete + fixup 的关键分支。
 */
static void test_delete_various_shapes(void)
{
    kvs_rbtree_t t = {0};
    assert(kvs_rbtree_create(&t) == 0);

    /*
     * 构造一组典型 BST 形态：
     * 插入顺序会导致不同结构，红黑树会旋转，但“删叶子/删单子/删双子”的情况都会出现。
     */
    const char *keys[] = {
        "m", "c", "t", "a", "e", "p", "z", "b", "d", "f", "o", "q", "y"};
    const char *vals[] = {
        "vm", "vc", "vt", "va", "ve", "vp", "vz", "vb", "vd", "vf", "vo", "vq", "vy"};
    const int M = (int)(sizeof(keys) / sizeof(keys[0]));

    for (int i = 0; i < M; i++)
    {
        assert(kvs_rbtree_set(&t, (char *)keys[i], (char *)vals[i]) == 0);
    }

    /* 删几个不同位置的点 */
    assert(kvs_rbtree_del(&t, "a") == 0); /* 叶子/近叶子 */
    assert(kvs_rbtree_get(&t, "a") == NULL);

    assert(kvs_rbtree_del(&t, "c") == 0); /* 常见为有子节点 */
    assert(kvs_rbtree_get(&t, "c") == NULL);

    assert(kvs_rbtree_del(&t, "t") == 0); /* 常见为双子 */
    assert(kvs_rbtree_get(&t, "t") == NULL);

    /* 删除不存在 */
    assert(kvs_rbtree_del(&t, "nope") == 1);

    /* 剩余 key 仍可正常查询（抽查） */
    assert(strcmp(kvs_rbtree_get(&t, "m"), "vm") == 0);
    assert(strcmp(kvs_rbtree_get(&t, "z"), "vz") == 0);
    assert(strcmp(kvs_rbtree_get(&t, "q"), "vq") == 0);

    assert(kvs_rbtree_destory(&t) == 0);
}

/*
 * 6) 批量插入 -> 删除一半 -> 再插入/再删除
 *    更接近真实负载，重点：不崩溃、逻辑正确、count 不乱（若可用）
 */
static void test_batch_insert_delete_insert_delete(void)
{
    kvs_rbtree_t t = {0};
    assert(kvs_rbtree_create(&t) == 0);

    const int N = 3000;
    char k[64], v[64];

    /* 插入 N 个 */
    for (int i = 0; i < N; i++)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_rbtree_set(&t, k, v) == 0);
    }

    /* 删除偶数 */
    for (int i = 0; i < N; i += 2)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_rbtree_del(&t, k) == 0);
    }

    /* 校验：奇数还在，偶数不在（抽查）
     * 注意：步长必须保证仍然落在奇数集合里，否则会抽到偶数（已被删除）误报失败。
     */
    for (int i = 1; i < N; i += 422)  /* 422 = 2 * 211，保证 i 永远是奇数 */
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        char *got = kvs_rbtree_get(&t, k);
        if (got == NULL)
        {
            printf("MISSING odd key=%s expected=%s\n", k, v);
        }
        assert(got != NULL);
        assert(strcmp(got, v) == 0);
    }

    /* 偶数应当都不存在（抽查） */
    for (int i = 0; i < N; i += 222)
    {
        make_kv(k, sizeof(k), v, sizeof(v), i);
        assert(kvs_rbtree_get(&t, k) == NULL);
    }

    /* 再插入一些新 key */
    for (int i = 0; i < 500; i++)
    {
        snprintf(k, sizeof(k), "n%d", i);
        snprintf(v, sizeof(v), "nv%d", i);
        assert(kvs_rbtree_set(&t, k, v) == 0);
    }

    /* 再删除这些新 key */
    for (int i = 0; i < 500; i++)
    {
        snprintf(k, sizeof(k), "n%d", i);
        assert(kvs_rbtree_del(&t, k) == 0);
        assert(kvs_rbtree_get(&t, k) == NULL);
    }

    assert(kvs_rbtree_destory(&t) == 0);
}

/*
 * 7) destroy 后再 create
 */
static void test_destroy_then_recreate(void)
{
    kvs_rbtree_t t = {0};
    assert(kvs_rbtree_create(&t) == 0);
    assert(kvs_rbtree_set(&t, "k", "v") == 0);

    assert(kvs_rbtree_destory(&t) == 0);

    /* 若 destroy 没置 nil=NULL，这里会返回 -2 */
    assert(kvs_rbtree_create(&t) == 0);
    assert(kvs_rbtree_destory(&t) == 0);
}

int main(void)
{
    test_basic_crud();
    test_null_args_and_state();
    test_copy_semantics();
    test_batch_insert_and_lookup();
    test_delete_various_shapes();
    test_batch_insert_delete_insert_delete();
    test_destroy_then_recreate();

    printf("ALL TESTS PASSED.\n");
    return 0;
}
