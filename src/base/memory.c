/* Copyright (C) 2026 Paul Moses */
#include <stdlib.h>
#include <string.h>

#include "liblexa/base.h"

static void *
runtime_allocate(void *context, size_t size)
{
    (void) context;
    return malloc(size);
}

static void *
runtime_resize(void *context, void *ptr, size_t size)
{
    (void) context;
    return realloc(ptr, size);
}

static void
runtime_release(void *context, void *ptr)
{
    (void) context;
    free(ptr);
}

lxa_allocator_t
lxa_allocator_default(void)
{
    lxa_allocator_t allocator = {NULL, runtime_allocate, runtime_resize,
                                 runtime_release};
    return allocator;
}

static int
allocator_valid(const lxa_allocator_t *allocator)
{
    return allocator != NULL && allocator->allocate != NULL
           && allocator->resize != NULL && allocator->release != NULL;
}

lxa_status_t
lxa_alloc(const lxa_allocator_t *allocator, size_t size, void **out)
{
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (!allocator_valid(allocator) || size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = allocator->allocate(allocator->context, size);
    return *out == NULL ? LXA_ERROR_NO_MEMORY : LXA_OK;
}

lxa_status_t
lxa_calloc(const lxa_allocator_t *allocator, size_t count, size_t size,
           void **out)
{
    size_t bytes;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (!allocator_valid(allocator) || count == 0 || size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_mul(count, size, &bytes);
    if (status != LXA_OK) {
        return status;
    }
    *out = allocator->allocate(allocator->context, bytes);
    if (*out == NULL) {
        return LXA_ERROR_NO_MEMORY;
    }
    memset(*out, 0, bytes);
    return LXA_OK;
}

lxa_status_t
lxa_resize(const lxa_allocator_t *allocator, void **ptr, size_t size)
{
    void *replacement;
    if (!allocator_valid(allocator) || ptr == NULL || size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    replacement = allocator->resize(allocator->context, *ptr, size);
    if (replacement == NULL) {
        return LXA_ERROR_NO_MEMORY;
    }
    *ptr = replacement;
    return LXA_OK;
}

lxa_status_t
lxa_resize_array(const lxa_allocator_t *allocator, void **ptr,
                 size_t count, size_t item_size)
{
    size_t bytes;
    lxa_status_t status;
    if (!allocator_valid(allocator) || ptr == NULL
        || count == 0 || item_size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_mul(count, item_size, &bytes);
    if (status != LXA_OK) {
        return status;
    }
    return lxa_resize(allocator, ptr, bytes);
}

void
lxa_free(const lxa_allocator_t *allocator, void *ptr)
{
    if (ptr != NULL) {
        allocator->release(allocator->context, ptr);
    }
}
