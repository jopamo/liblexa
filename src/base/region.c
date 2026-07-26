#include <stddef.h>

#include "liblexa/base.h"

/* Keep trailing storage aligned for the fundamental types supported by C99. */
typedef union {
    long double real;
    long long integer;
    void *pointer;
} lxa_storage_align_t;

typedef struct lxa_arena_chunk {
    struct lxa_arena_chunk *next;
    size_t used;
    size_t capacity;
    lxa_storage_align_t alignment;
} lxa_arena_chunk_t;

struct lxa_arena {
    lxa_allocator_t allocator;
    size_t minimum_chunk_size;
    lxa_arena_chunk_t *chunks;
};

typedef struct lxa_pool_block {
    struct lxa_pool_block *next;
    struct lxa_pool_block *prev;
    lxa_storage_align_t alignment;
} lxa_pool_block_t;

struct lxa_pool {
    lxa_allocator_t allocator;
    lxa_pool_block_t *blocks;
};

static size_t
storage_alignment(void)
{
    struct probe {
        char byte;
        lxa_storage_align_t storage;
    };
    return offsetof(struct probe, storage);
}

static int
valid_allocator(const lxa_allocator_t *allocator)
{
    return allocator != NULL && allocator->allocate != NULL
           && allocator->resize != NULL && allocator->release != NULL;
}

lxa_status_t
lxa_arena_create(const lxa_allocator_t *allocator, size_t minimum_chunk_size,
                 lxa_arena_t **out)
{
    lxa_arena_t *arena;
    lxa_status_t status;
    void *storage;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (!valid_allocator(allocator) || minimum_chunk_size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_alloc(allocator, sizeof(*arena), &storage);
    if (status != LXA_OK) {
        return status;
    }
    arena = storage;
    arena->allocator = *allocator;
    arena->minimum_chunk_size = minimum_chunk_size;
    arena->chunks = NULL;
    *out = arena;
    return LXA_OK;
}

lxa_status_t
lxa_arena_alloc(lxa_arena_t *arena, size_t size, void **out)
{
    lxa_arena_chunk_t *chunk;
    lxa_status_t status;
    size_t aligned, capacity, bytes;
    void *storage;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (arena == NULL || size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_align(size, storage_alignment(), &aligned);
    if (status != LXA_OK) {
        return status;
    }
    chunk = arena->chunks;
    if (chunk == NULL || aligned > chunk->capacity - chunk->used) {
        capacity = aligned > arena->minimum_chunk_size
                   ? aligned : arena->minimum_chunk_size;
        status = lxa_size_add(sizeof(*chunk), capacity, &bytes);
        if (status != LXA_OK) {
            return status;
        }
        status = lxa_alloc(&arena->allocator, bytes, &storage);
        if (status != LXA_OK) {
            return status;
        }
        chunk = storage;
        chunk->next = arena->chunks;
        chunk->capacity = capacity;
        chunk->used = 0;
        arena->chunks = chunk;
    }
    *out = (unsigned char *) (chunk + 1) + chunk->used;
    chunk->used += aligned;
    return LXA_OK;
}

void
lxa_arena_reset(lxa_arena_t *arena)
{
    lxa_arena_chunk_t *chunk, *next;
    if (arena == NULL) {
        return;
    }
    for (chunk = arena->chunks; chunk != NULL; chunk = next) {
        next = chunk->next;
        lxa_free(&arena->allocator, chunk);
    }
    arena->chunks = NULL;
}

void
lxa_arena_destroy(lxa_arena_t *arena)
{
    lxa_allocator_t allocator;
    if (arena == NULL) {
        return;
    }
    allocator = arena->allocator;
    lxa_arena_reset(arena);
    lxa_free(&allocator, arena);
}

lxa_status_t
lxa_pool_create(const lxa_allocator_t *allocator, lxa_pool_t **out)
{
    lxa_pool_t *pool;
    lxa_status_t status;
    void *storage;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (!valid_allocator(allocator)) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_alloc(allocator, sizeof(*pool), &storage);
    if (status != LXA_OK) {
        return status;
    }
    pool = storage;
    pool->allocator = *allocator;
    pool->blocks = NULL;
    *out = pool;
    return LXA_OK;
}

lxa_status_t
lxa_pool_alloc(lxa_pool_t *pool, size_t size, void **out)
{
    lxa_pool_block_t *block;
    lxa_status_t status;
    size_t bytes;
    void *storage;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (pool == NULL || size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_add(sizeof(*block), size, &bytes);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_alloc(&pool->allocator, bytes, &storage);
    if (status != LXA_OK) {
        return status;
    }
    block = storage;
    block->prev = NULL;
    block->next = pool->blocks;
    if (pool->blocks != NULL) {
        pool->blocks->prev = block;
    }
    pool->blocks = block;
    *out = block + 1;
    return LXA_OK;
}

void
lxa_pool_free(lxa_pool_t *pool, void *ptr)
{
    lxa_pool_block_t *block;
    if (ptr == NULL) {
        return;
    }
    block = (lxa_pool_block_t *) ptr - 1;
    if (block->prev == NULL) {
        pool->blocks = block->next;
    }
    else {
        block->prev->next = block->next;
    }
    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
    lxa_free(&pool->allocator, block);
}

int
lxa_pool_compatible(const lxa_pool_t *destination, const lxa_pool_t *source)
{
    const lxa_allocator_t *a, *b;
    if (destination == NULL || source == NULL) {
        return 0;
    }
    a = &destination->allocator;
    b = &source->allocator;
    return a->context == b->context && a->allocate == b->allocate
           && a->resize == b->resize && a->release == b->release;
}

lxa_status_t
lxa_pool_transfer(lxa_pool_t *destination, lxa_pool_t *source, void *ptr)
{
    lxa_pool_block_t *block;
    if (ptr == NULL || !lxa_pool_compatible(destination, source)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (destination == source) {
        return LXA_OK;
    }
    block = (lxa_pool_block_t *) ptr - 1;
    if (block->prev == NULL) {
        source->blocks = block->next;
    }
    else {
        block->prev->next = block->next;
    }
    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
    block->prev = NULL;
    block->next = destination->blocks;
    if (destination->blocks != NULL) {
        destination->blocks->prev = block;
    }
    destination->blocks = block;
    return LXA_OK;
}

void
lxa_pool_reset(lxa_pool_t *pool)
{
    lxa_pool_block_t *block, *next;
    if (pool == NULL) {
        return;
    }
    for (block = pool->blocks; block != NULL; block = next) {
        next = block->next;
        lxa_free(&pool->allocator, block);
    }
    pool->blocks = NULL;
}

void
lxa_pool_destroy(lxa_pool_t *pool)
{
    lxa_allocator_t allocator;
    if (pool == NULL) {
        return;
    }
    allocator = pool->allocator;
    lxa_pool_reset(pool);
    lxa_free(&allocator, pool);
}
