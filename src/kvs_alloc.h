#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "kvs_config.h" 
// #include <jemalloc/jemalloc.h>


void kvs_set_allocator(kvs_alloc_type_t type);

void *kvs_malloc(size_t size);

void kvs_free(void *ptr);