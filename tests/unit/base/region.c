#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "liblexa/base.h"

#define CHECK(condition) do { if (!(condition)) abort(); } while (0)

typedef struct {
    size_t calls, frees, fail_at;
} fixture_t;

static void *
allocate(void *ctx, size_t size)
{
    fixture_t *f = ctx;
    f->calls++;
    return f->calls == f->fail_at ? NULL : malloc(size);
}

static void *
resize(void *ctx, void *ptr, size_t size)
{
    fixture_t *f = ctx;
    f->calls++;
    return f->calls == f->fail_at ? NULL : realloc(ptr, size);
}

static void
release(void *ctx, void *ptr)
{
    fixture_t *f = ctx;
    f->frees++;
    free(ptr);
}

int
main(void)
{
    fixture_t f = {0, 0, 0};
    lxa_allocator_t allocator = {&f, allocate, resize, release};
    lxa_arena_t *arena = NULL;
    lxa_pool_t *pool = NULL;
    void *a, *b, *c;
    size_t count;
    union {long double ld; long long ll; void *p;} alignment;

    (void) alignment;
    f.fail_at = 1;
    CHECK(lxa_arena_create(&allocator, 32, &arena) == LXA_ERROR_NO_MEMORY
           && arena == NULL);
    f.fail_at = 0;
    CHECK(lxa_arena_create(&allocator, 0, &arena) == LXA_ERROR_ARGUMENT);
    CHECK(lxa_arena_create(&allocator, 32, &arena) == LXA_OK);
    CHECK(lxa_arena_alloc(arena, 0, &a) == LXA_ERROR_ARGUMENT && a == NULL);
    CHECK(lxa_arena_alloc(arena, SIZE_MAX, &a) == LXA_ERROR_OVERFLOW
           && a == NULL);
    CHECK(lxa_arena_alloc(arena, 1, &a) == LXA_OK);
    CHECK(lxa_arena_alloc(arena, 4, &b) == LXA_OK);
    CHECK(f.calls == 3); /* failed create, successful create, first chunk */
    CHECK((uintptr_t) a % sizeof(alignment) == 0);
    CHECK((uintptr_t) b % sizeof(alignment) == 0);
    memcpy(a, "x", 2);
    f.fail_at = f.calls + 1;
    CHECK(lxa_arena_alloc(arena, 80, &c) == LXA_ERROR_NO_MEMORY
           && c == NULL && strcmp(a, "x") == 0);
    f.fail_at = 0;
    CHECK(lxa_arena_alloc(arena, 80, &c) == LXA_OK);
    CHECK((uintptr_t) c % sizeof(alignment) == 0);
    count = f.frees;
    lxa_arena_reset(arena);
    CHECK(f.frees == count + 2);
    CHECK(lxa_arena_alloc(arena, 1, &a) == LXA_OK);
    lxa_arena_destroy(arena);
    CHECK(f.frees == f.calls - 2); /* failed create and chunk */

    count = f.calls;
    f.fail_at = f.calls + 1;
    CHECK(lxa_pool_create(&allocator, &pool) == LXA_ERROR_NO_MEMORY
           && pool == NULL);
    f.fail_at = 0;
    CHECK(lxa_pool_create(&allocator, &pool) == LXA_OK);
    CHECK(lxa_pool_alloc(pool, SIZE_MAX, &a) == LXA_ERROR_OVERFLOW
           && a == NULL);
    CHECK(lxa_pool_alloc(pool, 1, &a) == LXA_OK);
    CHECK(lxa_pool_alloc(pool, 1, &b) == LXA_OK);
    CHECK((uintptr_t) a % sizeof(alignment) == 0);
    CHECK((uintptr_t) b % sizeof(alignment) == 0);
    lxa_pool_free(pool, a); /* unlink head's predecessor */
    f.fail_at = f.calls + 1;
    CHECK(lxa_pool_alloc(pool, 1, &a) == LXA_ERROR_NO_MEMORY && a == NULL);
    f.fail_at = 0;
    CHECK(lxa_pool_alloc(pool, 1, &a) == LXA_OK);
    lxa_pool_free(pool, b); /* unlink tail */
    lxa_pool_free(pool, NULL);
    lxa_pool_reset(pool);
    CHECK(lxa_pool_alloc(pool, 1, &c) == LXA_OK);
    lxa_pool_destroy(pool);
    CHECK(f.frees == f.calls - 4); /* failed create and block */
    CHECK(f.calls > count);
    lxa_pool_destroy(NULL);
    lxa_arena_destroy(NULL);

    {
        fixture_t other = {0, 0, 0};
        lxa_allocator_t foreign = {&other, allocate, resize, release};
        lxa_pool_t *source, *destination, *incompatible;
        void *moved, *left, *resident;
        size_t before;

        CHECK(lxa_pool_create(&allocator, &source) == LXA_OK);
        CHECK(lxa_pool_create(&allocator, &destination) == LXA_OK);
        CHECK(lxa_pool_create(&foreign, &incompatible) == LXA_OK);
        CHECK(lxa_pool_alloc(source, 8, &moved) == LXA_OK);
        CHECK(lxa_pool_alloc(source, 8, &left) == LXA_OK);
        CHECK(lxa_pool_alloc(destination, 8, &resident) == LXA_OK);
        memcpy(moved, "kept", 5);
        CHECK(lxa_pool_compatible(destination, source));
        CHECK(!lxa_pool_compatible(incompatible, source));
        CHECK(!lxa_pool_compatible(NULL, source));
        CHECK(lxa_pool_transfer(incompatible, source, moved)
               == LXA_ERROR_ARGUMENT);
        CHECK(lxa_pool_transfer(destination, source, NULL)
               == LXA_ERROR_ARGUMENT);
        CHECK(lxa_pool_transfer(source, source, moved) == LXA_OK);
        before = f.calls;
        CHECK(lxa_pool_transfer(destination, source, moved) == LXA_OK);
        CHECK(f.calls == before);
        lxa_pool_reset(source);
        CHECK(strcmp(moved, "kept") == 0);
        lxa_pool_destroy(source);
        lxa_pool_free(destination, resident); /* moved block is list head */
        lxa_pool_free(destination, moved);
        lxa_pool_destroy(destination);
        lxa_pool_destroy(incompatible);
        CHECK(f.frees == f.calls - 4);
        CHECK(other.frees == other.calls);
    }
    {
        lxa_pool_t *source, *destination;
        void *moved;
        size_t before = f.frees;

        CHECK(lxa_pool_create(&allocator, &source) == LXA_OK);
        CHECK(lxa_pool_create(&allocator, &destination) == LXA_OK);
        CHECK(lxa_pool_alloc(source, 4, &moved) == LXA_OK);
        CHECK(lxa_pool_transfer(destination, source, moved) == LXA_OK);
        lxa_pool_destroy(source);
        CHECK(f.frees == before + 1);
        lxa_pool_reset(destination);
        CHECK(f.frees == before + 2);
        lxa_pool_destroy(destination);
        CHECK(f.frees == before + 3);
    }
    return 0;
}
