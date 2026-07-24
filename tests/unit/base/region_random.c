/* Copyright (C) 2026 Paul Moses */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "liblexa/base.h"

#define CHECK(condition) do { if (!(condition)) abort(); } while (0)

typedef struct {
    size_t calls, live, fail_at;
} fixture_t;

typedef struct {
    unsigned char *ptr;
    size_t size;
    unsigned char byte;
    unsigned owner;
} slot_t;

static void *
allocate(void *context, size_t size)
{
    fixture_t *f = context;
    void *ptr;
    f->calls++;
    if (f->calls == f->fail_at) {
        return NULL;
    }
    ptr = malloc(size);
    if (ptr != NULL) {
        f->live++;
    }
    return ptr;
}

static void *
resize(void *context, void *ptr, size_t size)
{
    fixture_t *f = context;
    void *result;
    f->calls++;
    if (f->calls == f->fail_at) {
        return NULL;
    }
    result = realloc(ptr, size);
    if (result != NULL && ptr == NULL) {
        f->live++;
    }
    return result;
}

static void
release(void *context, void *ptr)
{
    fixture_t *f = context;
    CHECK(f->live > 0);
    f->live--;
    free(ptr);
}

static uint32_t
next(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void
forget(slot_t *slots, size_t count, unsigned owner)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (slots[i].ptr != NULL && slots[i].owner == owner) {
            slots[i].ptr = NULL;
        }
    }
}

int
main(void)
{
    fixture_t f = {0, 0, 0};
    lxa_allocator_t allocator = {&f, allocate, resize, release};
    lxa_pool_t *pools[2];
    lxa_arena_t *arena;
    slot_t slots[64] = {{0}};
    slot_t temporary[32] = {{0}};
    uint32_t seed = UINT32_C(0x5f231897);
    size_t i, step;

    CHECK(lxa_pool_create(&allocator, &pools[0]) == LXA_OK);
    CHECK(lxa_pool_create(&allocator, &pools[1]) == LXA_OK);
    CHECK(lxa_arena_create(&allocator, 32, &arena) == LXA_OK);
    for (step = 0; step < 4096; ++step) {
        uint32_t choice = next(&seed);
        size_t index = next(&seed) % 64;
        slot_t *slot = &slots[index];
        unsigned owner = choice & 1;

        if (choice % 31 == 0) {
            lxa_pool_reset(pools[owner]);
            forget(slots, 64, owner);
        }
        else if (slot->ptr == NULL) {
            void *storage = NULL;
            size_t size = 1 + next(&seed) % 128;
            lxa_status_t status;
            if (step % 13 == 0) {
                f.fail_at = f.calls + 1;
            }
            status = lxa_pool_alloc(pools[owner], size, &storage);
            if (status == LXA_OK) {
                slot->ptr = storage;
                slot->size = size;
                slot->byte = (unsigned char) next(&seed);
                slot->owner = owner;
                memset(storage, slot->byte, size);
            }
            else {
                CHECK(status == LXA_ERROR_NO_MEMORY && storage == NULL);
            }
            f.fail_at = 0;
        }
        else if (choice & 4) {
            lxa_pool_free(pools[slot->owner], slot->ptr);
            slot->ptr = NULL;
        }
        else {
            unsigned destination = 1 - slot->owner;
            CHECK(lxa_pool_transfer(pools[destination], pools[slot->owner],
                                     slot->ptr) == LXA_OK);
            slot->owner = destination;
        }

        index = next(&seed) % 32;
        slot = &temporary[index];
        if (choice % 19 == 0) {
            lxa_arena_reset(arena);
            forget(temporary, 32, 2);
        }
        else if (slot->ptr == NULL) {
            void *storage = NULL;
            size_t size = 1 + next(&seed) % 80;
            lxa_status_t status;
            if (step % 17 == 0) {
                f.fail_at = f.calls + 1;
            }
            status = lxa_arena_alloc(arena, size, &storage);
            if (status == LXA_OK) {
                slot->ptr = storage;
                slot->size = size;
                slot->byte = (unsigned char) next(&seed);
                slot->owner = 2;
                memset(storage, slot->byte, size);
            }
            else {
                CHECK(status == LXA_ERROR_NO_MEMORY && storage == NULL);
            }
            f.fail_at = 0;
        }
        for (i = 0; i < 64; ++i) {
            if (slots[i].ptr != NULL) {
                size_t n;
                for (n = 0; n < slots[i].size; ++n) {
                    CHECK(slots[i].ptr[n] == slots[i].byte);
                }
            }
        }
        for (i = 0; i < 32; ++i) {
            if (temporary[i].ptr != NULL) {
                size_t n;
                for (n = 0; n < temporary[i].size; ++n) {
                    CHECK(temporary[i].ptr[n] == temporary[i].byte);
                }
            }
        }
    }
    lxa_pool_destroy(pools[0]);
    lxa_pool_destroy(pools[1]);
    lxa_arena_destroy(arena);
    CHECK(f.live == 0);
    return 0;
}
