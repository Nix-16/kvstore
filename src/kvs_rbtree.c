#include "kvs_rbtree.h"

/*
 * global instance
 */
kvs_rbtree_t global_rbtree = {0};

static int kvs_key_cmp(const char *a, const char *b)
{
    return strcmp(a, b);
}

/* ============================
 * RBTree 内部：最小/后继
 * ============================ */
static kvs_rbtree_node_t *rbtree_min(kvs_rbtree_t *T, kvs_rbtree_node_t *x)
{
    while (x->left != T->nil)
        x = x->left;
    return x;
}

static kvs_rbtree_node_t *rbtree_successor(kvs_rbtree_t *T, kvs_rbtree_node_t *x)
{
    kvs_rbtree_node_t *y = x->parent;

    if (x->right != T->nil)
    {
        return rbtree_min(T, x->right);
    }

    while ((y != T->nil) && (x == y->right))
    {
        x = y;
        y = y->parent;
    }
    return y;
}

/* ============================
 * 旋转
 * ============================ */
static void rbtree_left_rotate(kvs_rbtree_t *T, kvs_rbtree_node_t *x)
{
    kvs_rbtree_node_t *y = x->right;

    x->right = y->left;
    if (y->left != T->nil)
    {
        y->left->parent = x;
    }

    y->parent = x->parent;
    if (x->parent == T->nil)
    {
        T->root = y;
    }
    else if (x == x->parent->left)
    {
        x->parent->left = y;
    }
    else
    {
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;
}

static void rbtree_right_rotate(kvs_rbtree_t *T, kvs_rbtree_node_t *y)
{
    kvs_rbtree_node_t *x = y->left;

    y->left = x->right;
    if (x->right != T->nil)
    {
        x->right->parent = y;
    }

    x->parent = y->parent;
    if (y->parent == T->nil)
    {
        T->root = x;
    }
    else if (y == y->parent->right)
    {
        y->parent->right = x;
    }
    else
    {
        y->parent->left = x;
    }

    x->right = y;
    y->parent = x;
}

/* ============================
 * 插入修复
 * ============================ */
static void rbtree_insert_fixup(kvs_rbtree_t *T, kvs_rbtree_node_t *z)
{
    while (z->parent->color == KVS_RBTREE_RED)
    {
        if (z->parent == z->parent->parent->left)
        {
            kvs_rbtree_node_t *y = z->parent->parent->right; /* uncle */
            if (y->color == KVS_RBTREE_RED)
            {
                z->parent->color = KVS_RBTREE_BLACK;
                y->color = KVS_RBTREE_BLACK;
                z->parent->parent->color = KVS_RBTREE_RED;
                z = z->parent->parent;
            }
            else
            {
                if (z == z->parent->right)
                {
                    z = z->parent;
                    rbtree_left_rotate(T, z);
                }
                z->parent->color = KVS_RBTREE_BLACK;
                z->parent->parent->color = KVS_RBTREE_RED;
                rbtree_right_rotate(T, z->parent->parent);
            }
        }
        else
        {
            kvs_rbtree_node_t *y = z->parent->parent->left; /* uncle */
            if (y->color == KVS_RBTREE_RED)
            {
                z->parent->color = KVS_RBTREE_BLACK;
                y->color = KVS_RBTREE_BLACK;
                z->parent->parent->color = KVS_RBTREE_RED;
                z = z->parent->parent;
            }
            else
            {
                if (z == z->parent->left)
                {
                    z = z->parent;
                    rbtree_right_rotate(T, z);
                }
                z->parent->color = KVS_RBTREE_BLACK;
                z->parent->parent->color = KVS_RBTREE_RED;
                rbtree_left_rotate(T, z->parent->parent);
            }
        }
    }
    T->root->color = KVS_RBTREE_BLACK;
}

/* ============================
 * 插入：返回 0=插入成功, 1=已存在
 * ============================ */
static int rbtree_insert(kvs_rbtree_t *T, kvs_rbtree_node_t *z)
{
    kvs_rbtree_node_t *y = T->nil;
    kvs_rbtree_node_t *x = T->root;

    while (x != T->nil)
    {
        y = x;
        int c = kvs_key_cmp(z->key, x->key);
        if (c < 0)
            x = x->left;
        else if (c > 0)
            x = x->right;
        else
            return 1; /* exist */
    }

    z->parent = y;
    if (y == T->nil)
    {
        T->root = z;
    }
    else if (kvs_key_cmp(z->key, y->key) < 0)
    {
        y->left = z;
    }
    else
    {
        y->right = z;
    }

    z->left = T->nil;
    z->right = T->nil;
    z->color = KVS_RBTREE_RED;

    rbtree_insert_fixup(T, z);
    return 0;
}

/* ============================
 * 删除修复
 * ============================ */
static void rbtree_delete_fixup(kvs_rbtree_t *T, kvs_rbtree_node_t *x)
{
    while ((x != T->root) && (x->color == KVS_RBTREE_BLACK))
    {
        if (x == x->parent->left)
        {
            kvs_rbtree_node_t *w = x->parent->right;
            if (w->color == KVS_RBTREE_RED)
            {
                w->color = KVS_RBTREE_BLACK;
                x->parent->color = KVS_RBTREE_RED;
                rbtree_left_rotate(T, x->parent);
                w = x->parent->right;
            }

            if ((w->left->color == KVS_RBTREE_BLACK) &&
                (w->right->color == KVS_RBTREE_BLACK))
            {
                w->color = KVS_RBTREE_RED;
                x = x->parent;
            }
            else
            {
                if (w->right->color == KVS_RBTREE_BLACK)
                {
                    w->left->color = KVS_RBTREE_BLACK;
                    w->color = KVS_RBTREE_RED;
                    rbtree_right_rotate(T, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = KVS_RBTREE_BLACK;
                w->right->color = KVS_RBTREE_BLACK;
                rbtree_left_rotate(T, x->parent);
                x = T->root;
            }
        }
        else
        {
            kvs_rbtree_node_t *w = x->parent->left;
            if (w->color == KVS_RBTREE_RED)
            {
                w->color = KVS_RBTREE_BLACK;
                x->parent->color = KVS_RBTREE_RED;
                rbtree_right_rotate(T, x->parent);
                w = x->parent->left;
            }

            if ((w->left->color == KVS_RBTREE_BLACK) &&
                (w->right->color == KVS_RBTREE_BLACK))
            {
                w->color = KVS_RBTREE_RED;
                x = x->parent;
            }
            else
            {
                if (w->left->color == KVS_RBTREE_BLACK)
                {
                    w->right->color = KVS_RBTREE_BLACK;
                    w->color = KVS_RBTREE_RED;
                    rbtree_left_rotate(T, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = KVS_RBTREE_BLACK;
                w->left->color = KVS_RBTREE_BLACK;
                rbtree_right_rotate(T, x->parent);
                x = T->root;
            }
        }
    }
    x->color = KVS_RBTREE_BLACK;
}

/*
 * 删除：返回“被真正摘除的节点 y”，调用方负责释放 y 的 key/value/node。
 */
static kvs_rbtree_node_t *rbtree_delete(kvs_rbtree_t *T, kvs_rbtree_node_t *z)
{
    kvs_rbtree_node_t *y;
    kvs_rbtree_node_t *x;

    if ((z->left == T->nil) || (z->right == T->nil))
    {
        y = z;
    }
    else
    {
        y = rbtree_successor(T, z);
    }

    x = (y->left != T->nil) ? y->left : y->right;
    x->parent = y->parent;

    if (y->parent == T->nil)
    {
        T->root = x;
    }
    else if (y == y->parent->left)
    {
        y->parent->left = x;
    }
    else
    {
        y->parent->right = x;
    }

    if (y != z)
    {
        /* 交换 key/value 指针（不是拷贝内容） */
        char *tk = z->key;
        z->key = y->key;
        y->key = tk;
        char *tv = z->value;
        z->value = y->value;
        y->value = tv;
    }

    if (y->color == KVS_RBTREE_BLACK)
    {
        rbtree_delete_fixup(T, x);
    }

    /* 注意：y 已经从树里摘除 */
    y->left = y->right = y->parent = T->nil;
    return y;
}

/* ============================
 * 搜索：找不到返回 T->nil
 * ============================ */
static kvs_rbtree_node_t *rbtree_search(kvs_rbtree_t *T, const char *key)
{
    kvs_rbtree_node_t *node = T->root;
    while (node != T->nil)
    {
        int c = kvs_key_cmp(key, node->key);
        if (c < 0)
            node = node->left;
        else if (c > 0)
            node = node->right;
        else
            return node;
    }
    return T->nil;
}

/* ============================
 * 内部：释放单节点（含 key/value）
 * ============================ */
static void rbtree_free_node(kvs_rbtree_node_t *n)
{
    if (!n)
        return;
    if (n->key)
        kvs_free(n->key);
    if (n->value)
        kvs_free(n->value);
    kvs_free(n);
}

/* ============================
 * 对外接口
 * ============================ */

kvs_rbtree_t global_rbtree;

int kvs_rbtree_create(kvs_rbtree_t *inst)
{
    if (!inst)
        return -1;
    if (inst->nil != NULL)
        return -2;

    inst->nil = (kvs_rbtree_node_t *)kvs_malloc(sizeof(kvs_rbtree_node_t));
    if (!inst->nil)
        return -3;

    /* nil 哨兵：BLACK，指针指向自身更稳（避免 NULL 判断） */
    inst->nil->color = KVS_RBTREE_BLACK;
    inst->nil->left = inst->nil;
    inst->nil->right = inst->nil;
    inst->nil->parent = inst->nil;
    inst->nil->key = NULL;
    inst->nil->value = NULL;

    inst->root = inst->nil;
    inst->count = 0;

    return 0;
}

int kvs_rbtree_destory(kvs_rbtree_t *inst)
{
    if (!inst)
        return -1;
    if (!inst->nil)
        return -2;

    /* 循环删除最小节点，直到树空（root==nil） */
    while (inst->root != inst->nil)
    {
        kvs_rbtree_node_t *mini = rbtree_min(inst, inst->root);
        kvs_rbtree_node_t *removed = rbtree_delete(inst, mini);
        rbtree_free_node(removed);
        inst->count--;
    }

    kvs_free(inst->nil);
    inst->nil = NULL;
    inst->root = NULL;
    inst->count = 0;

    return 0;
}

int kvs_rbtree_set(kvs_rbtree_t *inst, char *key, char *value)
{
    if (!inst || !key || !value)
        return -1;
    if (!inst->nil)
        return -2;

    /* 1) 若已存在：覆盖 value（对齐 array 的 set 语义） */
    kvs_rbtree_node_t *exist = rbtree_search(inst, key);
    if (exist != inst->nil)
    {
        /* 分配新 value，成功后替换，避免 malloc 失败丢旧值 */
        size_t vlen = strlen(value);
        char *newv = (char *)kvs_malloc(vlen + 1);
        if (!newv)
            return -3;
        memcpy(newv, value, vlen + 1);

        if (exist->value)
            kvs_free(exist->value);
        exist->value = newv;

        return 0; /* 覆盖也算 success */
    }

    /* 2) 不存在：创建新节点并插入 */
    kvs_rbtree_node_t *node = (kvs_rbtree_node_t *)kvs_malloc(sizeof(kvs_rbtree_node_t));
    if (!node)
        return -3;

    node->left = node->right = node->parent = inst->nil;
    node->color = KVS_RBTREE_RED;
    node->key = NULL;
    node->value = NULL;

    /* 拷贝 key */
    size_t klen = strlen(key);
    node->key = (char *)kvs_malloc(klen + 1);
    if (!node->key)
    {
        kvs_free(node);
        return -3;
    }
    memcpy(node->key, key, klen + 1);

    /* 拷贝 value */
    size_t vlen = strlen(value);
    node->value = (char *)kvs_malloc(vlen + 1);
    if (!node->value)
    {
        kvs_free(node->key);
        kvs_free(node);
        return -3;
    }
    memcpy(node->value, value, vlen + 1);

    /* 插入：理论上不会 exist（我们已提前 search），但保守处理 */
    int rc = rbtree_insert(inst, node);
    if (rc != 0)
    {
        rbtree_free_node(node);
        return -3;
    }

    inst->count++;
    return 0;
}

char *kvs_rbtree_get(kvs_rbtree_t *inst, char *key)
{
    if (!inst || !key)
        return NULL;
    if (!inst->nil)
        return NULL;

    kvs_rbtree_node_t *node = rbtree_search(inst, key);
    if (node == inst->nil)
        return NULL;
    return node->value;
}

int kvs_rbtree_del(kvs_rbtree_t *inst, char *key)
{
    if (!inst || !key)
        return -1;
    if (!inst->nil)
        return -2;

    kvs_rbtree_node_t *node = rbtree_search(inst, key);
    if (node == inst->nil)
        return 1;

    kvs_rbtree_node_t *removed = rbtree_delete(inst, node);
    rbtree_free_node(removed);
    inst->count--;

    return 0;
}

int kvs_rbtree_exist(kvs_rbtree_t *inst, char *key)
{
    if (!inst || !key)
        return -1;
    if (!inst->nil)
        return -2;

    kvs_rbtree_node_t *node = rbtree_search(inst, key);
    return (node == inst->nil) ? 1 : 0;
}

int kvs_rbtree_count(kvs_rbtree_t *inst)
{
    if (!inst)
        return 0;
    return inst->count;
}
