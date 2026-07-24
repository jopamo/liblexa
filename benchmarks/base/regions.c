/* Copyright (C) 2026 Paul Moses */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "liblexa/base.h"

typedef struct {
    size_t requests;
    size_t requested_bytes;
} counts_t;

static void *
count_allocate(void *context, size_t size)
{
    counts_t *counts = context;
    counts->requests++;
    counts->requested_bytes += size;
    return malloc(size);
}

static void *
count_resize(void *context, void *ptr, size_t size)
{
    counts_t *counts = context;
    counts->requests++;
    counts->requested_bytes += size;
    return realloc(ptr, size);
}

static void
count_release(void *context, void *ptr)
{
    (void) context;
    free(ptr);
}

static int
run(size_t count, int use_pool)
{
    counts_t counts = {0, 0};
    lxa_allocator_t allocator = {&counts, count_allocate,
                                 count_resize, count_release};
    lxa_arena_t *arena = NULL;
    lxa_pool_t *pool = NULL;
    void **pointers = NULL;
    size_t i, checksum = 0;
    clock_t start, finish;
    int failed = 0;

    if (use_pool) {
        pointers = malloc(count * sizeof(*pointers));
        if (pointers == NULL) {
            fputs("cannot allocate pool pointer array\n", stderr);
            return 1;
        }
    }
    start = clock();
    if ((use_pool ? lxa_pool_create(&allocator, &pool)
                  : lxa_arena_create(&allocator, 4096, &arena)) != LXA_OK) {
        fputs("cannot create region\n", stderr);
        free(pointers);
        return 1;
    }
    for (i = 0; i < count; i++) {
        unsigned char *ptr = NULL;
        lxa_status_t status = use_pool
            ? lxa_pool_alloc(pool, 32, (void **) &ptr)
            : lxa_arena_alloc(arena, 32, (void **) &ptr);
        if (status != LXA_OK) {
            fprintf(stderr, "%s allocation failed at item %zu (status %d)\n",
                    use_pool ? "pool" : "arena", i, status);
            failed = 1;
            break;
        }
        ptr[0] = (unsigned char) i;
        ptr[31] = (unsigned char) (i >> 8);
        checksum += ptr[0] + ptr[31];
        if (use_pool) {
            pointers[i] = ptr;
        }
    }
    if (use_pool) {
        while (i != 0) {
            lxa_pool_free(pool, pointers[--i]);
        }
        lxa_pool_destroy(pool);
    }
    else {
        lxa_arena_destroy(arena);
    }
    finish = clock();
    free(pointers);
    if (failed || start == (clock_t) -1 || finish == (clock_t) -1) {
        return 1;
    }
    printf("%s\t%zu\t%zu\t%zu\t%.6f\t%zu\n",
           use_pool ? "pool" : "arena", count, counts.requests,
           counts.requested_bytes,
           (double) (finish - start) / CLOCKS_PER_SEC, checksum);
    return 0;
}

int
main(int argc, char **argv)
{
    char *end;
    unsigned long long number;
    if (argc != 2) {
        fputs("usage: regions COUNT (1..1000000)\n", stderr);
        return 2;
    }
    errno = 0;
    number = strtoull(argv[1], &end, 10);
    if (errno || end == argv[1] || *end != '\0' || number < 1
        || number > 1000000 || number > SIZE_MAX / sizeof(void *)) {
        fputs("invalid count\n", stderr);
        return 2;
    }
    puts("region\titems\trequests\trequested_bytes\tcpu_seconds\tchecksum");
    return run((size_t) number, 0) || run((size_t) number, 1);
}
