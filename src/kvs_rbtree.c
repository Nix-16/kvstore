#include "kvs_rbtree.h"

rbtree_node *rbtree_mini(rbtree *T, rbtree_node *x)
{
    while (x->left != T->nil)
    {
        x = x->left;
    }
    return x;
}

rbtree_node *rbtree_maxi(rbtree *T, rbtree_node *x)
{
    while (x->right != T->nil)
    {
        x = x->right;
    }
    return x;
}

rbtree_node *rbtree_successor(rbtree *T, rbtree_node *x)
{
    rbtree_node *y = x->parent;

    if (x->right != T->nil)
    {
        return rbtree_mini(T, x->right);
    }

    while ((y != T->nil) && (x == y->right))
    {
        x = y;
        y = y->parent;
    }
    return y;
}

void rbtree_left_rotate(rbtree *T, rbtree_node *x)
{

    rbtree_node *y = x->right; // x  --> y  ,  y --> x,   right --> left,  left --> right

    x->right = y->left; // 1 1
    if (y->left != T->nil)
    { // 1 2
        y->left->parent = x;
    }

    y->parent = x->parent; // 1 3
    if (x->parent == T->nil)
    { // 1 4
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

    y->left = x;   // 1 5
    x->parent = y; // 1 6
}

void rbtree_right_rotate(rbtree *T, rbtree_node *y)
{

    rbtree_node *x = y->left;

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

void rbtree_insert_fixup(rbtree *T, rbtree_node *z)
{

    while (z->parent->color == RED)
    { // z ---> RED
        if (z->parent == z->parent->parent->left)
        {
            rbtree_node *y = z->parent->parent->right;
            if (y->color == RED)
            {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;

                z = z->parent->parent; // z --> RED
            }
            else
            {

                if (z == z->parent->right)
                {
                    z = z->parent;
                    rbtree_left_rotate(T, z);
                }

                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rbtree_right_rotate(T, z->parent->parent);
            }
        }
        else
        {
            rbtree_node *y = z->parent->parent->left;
            if (y->color == RED)
            {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;

                z = z->parent->parent; // z --> RED
            }
            else
            {
                if (z == z->parent->left)
                {
                    z = z->parent;
                    rbtree_right_rotate(T, z);
                }

                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rbtree_left_rotate(T, z->parent->parent);
            }
        }
    }

    T->root->color = BLACK;
}

void rbtree_insert(rbtree *T, rbtree_node *z)
{

    rbtree_node *y = T->nil;
    rbtree_node *x = T->root;

    while (x != T->nil)
    {
        y = x;

        if (strcmp(z->key, x->key) < 0)
        {
            x = x->left;
        }
        else if (strcmp(z->key, x->key) > 0)
        {
            x = x->right;
        }
        else
        {
            return;
        }
    }

    z->parent = y;
    if (y == T->nil)
    {
        T->root = z;
    }
    else if (strcmp(z->key, y->key) < 0)
    {

        y->left = z;
    }
    else
    {
        y->right = z;
    }

    z->left = T->nil;
    z->right = T->nil;
    z->color = RED;

    rbtree_insert_fixup(T, z);
}

void rbtree_delete_fixup(rbtree *T, rbtree_node *x)
{

    while ((x != T->root) && (x->color == BLACK))
    {
        if (x == x->parent->left)
        {

            rbtree_node *w = x->parent->right;
            if (w->color == RED)
            {
                w->color = BLACK;
                x->parent->color = RED;

                rbtree_left_rotate(T, x->parent);
                w = x->parent->right;
            }

            if ((w->left->color == BLACK) && (w->right->color == BLACK))
            {
                w->color = RED;
                x = x->parent;
            }
            else
            {

                if (w->right->color == BLACK)
                {
                    w->left->color = BLACK;
                    w->color = RED;
                    rbtree_right_rotate(T, w);
                    w = x->parent->right;
                }

                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                rbtree_left_rotate(T, x->parent);

                x = T->root;
            }
        }
        else
        {

            rbtree_node *w = x->parent->left;
            if (w->color == RED)
            {
                w->color = BLACK;
                x->parent->color = RED;
                rbtree_right_rotate(T, x->parent);
                w = x->parent->left;
            }

            if ((w->left->color == BLACK) && (w->right->color == BLACK))
            {
                w->color = RED;
                x = x->parent;
            }
            else
            {

                if (w->left->color == BLACK)
                {
                    w->right->color = BLACK;
                    w->color = RED;
                    rbtree_left_rotate(T, w);
                    w = x->parent->left;
                }

                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                rbtree_right_rotate(T, x->parent);

                x = T->root;
            }
        }
    }

    x->color = BLACK;
}

rbtree_node *rbtree_delete(rbtree *T, rbtree_node *z)
{

    rbtree_node *y = T->nil;
    rbtree_node *x = T->nil;

    if ((z->left == T->nil) || (z->right == T->nil))
    {
        y = z;
    }
    else
    {
        y = rbtree_successor(T, z);
    }

    if (y->left != T->nil)
    {
        x = y->left;
    }
    else if (y->right != T->nil)
    {
        x = y->right;
    }

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

        void *tmp = z->key;
        z->key = y->key;
        y->key = tmp;

        tmp = z->value;
        z->value = y->value;
        y->value = tmp;
    }

    if (y->color == BLACK)
    {
        rbtree_delete_fixup(T, x);
    }

    return y;
}

rbtree_node *rbtree_search(rbtree *T, KEY_TYPE key)
{

    rbtree_node *node = T->root;
    while (node != T->nil)
    {

        if (strcmp(key, node->key) < 0)
        {
            node = node->left;
        }
        else if (strcmp(key, node->key) > 0)
        {
            node = node->right;
        }
        else
        {
            return node;
        }
    }
    return T->nil;
}

void rbtree_traversal(rbtree *T, rbtree_node *node)
{
    if (node != T->nil)
    {
        rbtree_traversal(T, node->left);

        printf("key:%s, value:%s\n", node->key, (char *)node->value);

        rbtree_traversal(T, node->right);
    }
}

typedef struct _rbtree kvs_rbtree_t;

kvs_rbtree_t global_rbtree;

// 5 + 2
int kvs_rbtree_create(kvs_rbtree_t *inst)
{

    if (inst == NULL)
        return 1;

    inst->nil = (rbtree_node *)kvs_malloc(sizeof(rbtree_node));
    inst->nil->color = BLACK;
    inst->root = inst->nil;

    return 0;
}

void kvs_rbtree_destory(kvs_rbtree_t *inst)
{

    if (inst == NULL)
        return;

    rbtree_node *node = NULL;

    while (!(node = inst->root))
    {

        rbtree_node *mini = rbtree_mini(inst, node);

        rbtree_node *cur = rbtree_delete(inst, mini);
        kvs_free(cur);
    }

    kvs_free(inst->nil);

    return;
}

int kvs_rbtree_set(kvs_rbtree_t *inst, char *key, char *value)
{

    if (!inst || !key || !value)
        return -1;

    rbtree_node *node = (rbtree_node *)kvs_malloc(sizeof(rbtree_node));

    node->key = kvs_malloc(strlen(key) + 1);
    if (!node->key)
        return -2;
    memset(node->key, 0, strlen(key) + 1);
    strcpy(node->key, key);

    node->value = kvs_malloc(strlen(value) + 1);
    if (!node->value)
        return -2;
    memset(node->value, 0, strlen(value) + 1);
    strcpy(node->value, value);

    rbtree_insert(inst, node);

    return 0;
}

char *kvs_rbtree_get(kvs_rbtree_t *inst, char *key)
{

    if (!inst || !key)
        return NULL;
    rbtree_node *node = rbtree_search(inst, key);
    if (!node)
        return NULL; // no exist
    if (node == inst->nil)
        return NULL;

    return node->value;
}

int kvs_rbtree_del(kvs_rbtree_t *inst, char *key)
{

    if (!inst || !key)
        return -1;

    rbtree_node *node = rbtree_search(inst, key);
    if (!node)
        return 1; // no exist

    rbtree_node *cur = rbtree_delete(inst, node);
    free(cur);

    return 0;
}

int kvs_rbtree_mod(kvs_rbtree_t *inst, char *key, char *value)
{

    if (!inst || !key || !value)
        return -1;

    rbtree_node *node = rbtree_search(inst, key);
    if (!node)
        return 1; // no exist
    if (node == inst->nil)
        return 1;

    kvs_free(node->value);

    node->value = kvs_malloc(strlen(value) + 1);
    if (!node->value)
        return -2;

    memset(node->value, 0, strlen(value) + 1);
    strcpy(node->value, value);

    return 0;
}

int kvs_rbtree_exist(kvs_rbtree_t *inst, char *key)
{

    if (!inst || !key)
        return -1;

    rbtree_node *node = rbtree_search(inst, key);
    if (!node)
        return 1; // no exist
    if (node == inst->nil)
        return 1;

    return 0;
}
