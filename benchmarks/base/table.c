/* Keyed core table: insertion, reverse-order lookup, deletion and traversal.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "liblexa/base.h"

typedef struct {
    size_t requests, bytes;
    size_t peak;
    lxa_budget_t *budget;
    int overflow;
} counts_t;

static void *
count_alloc(void *context, size_t size)
{
    counts_t *c = context;
    ++c->requests;
    if (size > SIZE_MAX - c->bytes) {
        c->overflow = 1;
        return NULL;
    }
    c->bytes += size;
    if (c->budget->used[LXA_BUDGET_MEMORY] > c->peak) {
        c->peak = (size_t) c->budget->used[LXA_BUDGET_MEMORY];
    }
    return malloc(size);
}

static void *
count_resize(void *context, void *ptr, size_t size)
{
    counts_t *c = context;
    ++c->requests;
    if (size > SIZE_MAX - c->bytes) {
        c->overflow = 1;
        return NULL;
    }
    c->bytes += size;
    if (c->budget->used[LXA_BUDGET_MEMORY] > c->peak) {
        c->peak = (size_t) c->budget->used[LXA_BUDGET_MEMORY];
    }
    return realloc(ptr, size);
}

static void
count_free(void *context, void *ptr)
{
    (void) context;
    free(ptr);
}

static lxa_span_t
index_key(size_t index, uint8_t bytes[8])
{
    unsigned j;
    for (j = 0; j < 8; ++j) {
        bytes[j] = (uint8_t) ((uint64_t) index >> (j * 8));
    }
    return (lxa_span_t) {bytes, 8};
}

static lxa_status_t
visit(void *context, lxa_span_t key, void *value)
{
    size_t *seen = context;
    if (key.length != 8 || value == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    ++*seen;
    return LXA_OK;
}

int
main(int argc, char **argv)
{
    unsigned long long requested;
    char *end;
    size_t count, i, retained, seen = 0, checksum = 0;
    uint8_t key[8];
    counts_t counts = {0};
    lxa_allocator_t allocator = {&counts, count_alloc, count_resize,
                                 count_free};
    lxa_budget_t budget;
    lxa_table_t *table = NULL;
    clock_t started, inserted, found, deleted, traversed;
    int error = 0;

    if (argc != 2) {
        fputs("usage: table COUNT (1..1000000)\n", stderr);
        return 2;
    }
    errno = 0;
    requested = strtoull(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0'
        || requested == 0 || requested > 1000000 || requested > SIZE_MAX / 8) {
        fputs("invalid count\n", stderr);
        return 2;
    }
    count = (size_t) requested;
    counts.budget = &budget;
    if (lxa_budget_init(&budget, NULL) != LXA_OK
        || lxa_budget_accept_input(&budget, count * 8, NULL) != LXA_OK) {
        fputs("budget initialization failed\n", stderr);
        return 1;
    }
    started = clock();
    if (lxa_table_create_with_budget(&allocator, &budget, NULL, NULL,
                                     &table) != LXA_OK) {
        fputs("table initialization failed\n", stderr);
        return 1;
    }
    for (i = 0; i < count; ++i) {
        if (lxa_table_set(table, index_key(i, key),
                          (void *) (uintptr_t) (i + 1), NULL, NULL) != LXA_OK) {
            fprintf(stderr, "insert failed at %zu (limit %d)\n", i,
                    lxa_table_limit_reason(table));
            error = 1;
            break;
        }
    }
    inserted = clock();
    retained = (size_t) budget.used[LXA_BUDGET_MEMORY];
    if (!error) {
        for (i = count; i != 0; --i) {
            bool present = false;
            void *value = NULL;
            if (lxa_table_find(table, index_key(i - 1, key),
                               &present, &value) != LXA_OK
                || !present || value != (void *) (uintptr_t) i) {
                fprintf(stderr, "lookup failed at %zu\n", i - 1);
                error = 1;
                break;
            }
            checksum += (size_t) (uintptr_t) value;
        }
    }
    found = clock();
    if (!error) {
        for (i = 0; i < count; i += 2) {
            bool present = false;
            if (lxa_table_delete(table, index_key(i, key),
                                 &present, NULL) != LXA_OK || !present) {
                fprintf(stderr, "delete failed at %zu\n", i);
                error = 1;
                break;
            }
        }
    }
    deleted = clock();
    if (!error && (lxa_table_each(table, visit, &seen) != LXA_OK
                   || seen != count / 2 || lxa_table_length(table) != seen)) {
        fputs("traversal failed\n", stderr);
        error = 1;
    }
    traversed = clock();
    lxa_table_destroy(table);
    if (error || counts.overflow || budget.used[LXA_BUDGET_MEMORY] != 0
        || started == (clock_t) -1 || inserted == (clock_t) -1
        || found == (clock_t) -1 || deleted == (clock_t) -1
        || traversed == (clock_t) -1) {
        return 1;
    }
    printf("%zu\t%zu\t%zu\t%zu\t%zu\t%.6f\t%.6f\t%.6f\t%.6f\t%zu\n",
           count, counts.requests, counts.bytes, retained, counts.peak,
           (double) (inserted - started) / CLOCKS_PER_SEC,
           (double) (found - inserted) / CLOCKS_PER_SEC,
           (double) (deleted - found) / CLOCKS_PER_SEC,
           (double) (traversed - deleted) / CLOCKS_PER_SEC, checksum);
    return 0;
}
