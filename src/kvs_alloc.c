#include "kvs_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 默认 system */
static void *(*g_malloc_fn)(size_t) = malloc;
static void  (*g_free_fn)(void *) = free;
static void *(*g_calloc_fn)(size_t, size_t) = calloc;
static void *(*g_realloc_fn)(void *, size_t) = realloc;

/* 若你将来引入 jemalloc，打开这些 include 和 wrap */
// #include <jemalloc/jemalloc.h>
// static void *je_malloc_wrap(size_t n) { return je_malloc(n); }
// static void  je_free_wrap(void *p) { je_free(p); }
// static void *je_calloc_wrap(size_t n, size_t s) { return je_calloc(n, s); }
// static void *je_realloc_wrap(void *p, size_t n) { return je_realloc(p, n); }

void *kvs_malloc(size_t size) { return g_malloc_fn(size); }
void  kvs_free(void *ptr) { if (ptr) g_free_fn(ptr); }
void *kvs_calloc(size_t nmemb, size_t size) { return g_calloc_fn(nmemb, size); }
void *kvs_realloc(void *ptr, size_t size) { return g_realloc_fn(ptr, size); }

void kvs_set_allocator(kvs_alloc_type_t type)
{
    switch (type)
    {
    case KVS_ALLOC_SYSTEM:
        g_malloc_fn = malloc;
        g_free_fn = free;
        g_calloc_fn = calloc;
        g_realloc_fn = realloc;
        printf("Using system malloc/free\n");
        break;

    case KVS_ALLOC_JEMALLOC:
        /* 真接入时务必四个一起切 */
        // g_malloc_fn = je_malloc_wrap;
        // g_free_fn = je_free_wrap;
        // g_calloc_fn = je_calloc_wrap;
        // g_realloc_fn = je_realloc_wrap;
        printf("Using jemalloc malloc/free (TODO wired)\n");
        break;

    case KVS_ALLOC_MYPOOL:
        /* pool 若暂不支持 calloc/realloc，要明确策略 */
        // g_malloc_fn = mypool_malloc;
        // g_free_fn = mypool_free;
        // g_calloc_fn = mypool_calloc;   // 或者 mypool_malloc + memset
        // g_realloc_fn = mypool_realloc; // 若不支持，返回 NULL
        printf("Using mypool malloc/free (TODO wired)\n");
        break;

    default:
        g_malloc_fn = malloc;
        g_free_fn = free;
        g_calloc_fn = calloc;
        g_realloc_fn = realloc;
        printf("Unknown allocator type, fallback to system malloc/free\n");
        break;
    }
}