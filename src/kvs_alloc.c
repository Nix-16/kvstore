#include "kvs_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 默认：system malloc/free
static void *(*g_malloc_fn)(size_t) = malloc;
static void (*g_free_fn)(void *) = free;

// static void *je_malloc_wrap(size_t size) { return je_malloc(size); }
// static void je_free_wrap(void *ptr) { je_free(ptr); }

void kvs_set_allocator(kvs_alloc_type_t type)
{
    switch (type)
    {
    case KVS_ALLOC_SYSTEM:
        g_malloc_fn = malloc;
        g_free_fn = free;
        printf("Using system malloc/free\n");
        break;

    case KVS_ALLOC_JEMALLOC:
        // g_malloc_fn = je_malloc_wrap;
        // g_free_fn = je_free_wrap;
        printf("Using jemalloc malloc/free\n");
        break;

    case KVS_ALLOC_MYPOOL:
        // 自定义内存池分配器的实现
        printf("Using mypool malloc/free\n");
        break;
    default:
        // 默认 system
        g_malloc_fn = malloc;
        g_free_fn = free;
        printf("Unknown allocator type, fallback to system malloc/free\n");
    }
}

void *kvs_malloc(size_t size)
{
    return g_malloc_fn(size);
}

void kvs_free(void *ptr)
{
    if (ptr)
        g_free_fn(ptr);
}