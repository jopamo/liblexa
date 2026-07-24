/* Copyright (C) 2026 Paul Moses */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "liblexa/base.h"

#define REQUIRE(condition) do { if (!(condition)) abort(); } while (0)
#define CHECK(call, expected) do {               \
    lxa_status_t result = (call);                 \
    REQUIRE(result == (expected));                \
} while (0)

typedef struct {
    unsigned calls, frees, fail_at;
} fixture_t;

static void *
allocate(void *context, size_t size)
{
    fixture_t *f = context;
    ++f->calls;
    return f->calls == f->fail_at ? NULL : malloc(size);
}

static void *
resize(void *context, void *ptr, size_t size)
{
    fixture_t *f = context;
    ++f->calls;
    return f->calls == f->fail_at ? NULL : realloc(ptr, size);
}

static void
release(void *context, void *ptr)
{
    fixture_t *f = context;
    REQUIRE(ptr != NULL);
    ++f->frees;
    free(ptr);
}

static lxa_status_t
fixed_key(void *context, uint8_t key[16])
{
    size_t i;
    (void) context;
    for (i = 0; i < 16; ++i) {
        key[i] = (uint8_t) i;
    }
    return LXA_OK;
}

static lxa_status_t
no_entropy(void *context, uint8_t key[16])
{
    (void) context;
    key[0] = 1;
    return LXA_ERROR_ENTROPY;
}

static lxa_status_t
count_items(void *context, lxa_span_t key, void *value)
{
    size_t *count = context;
    (void) key;
    (void) value;
    ++*count;
    return LXA_OK;
}

static lxa_status_t
stop_visit(void *context, lxa_span_t key, void *value)
{
    (void) context;
    (void) key;
    (void) value;
    return LXA_ERROR_ARGUMENT;
}

static void
random_sequence(const lxa_allocator_t *allocator)
{
    lxa_table_t *table = NULL;
    bool present[64] = {false};
    void *values[64] = {NULL};
    uint32_t seed = UINT32_C(0x3a0f71b2);
    size_t step, i, count = 0;
    CHECK(lxa_table_create_with_budget(allocator, NULL, fixed_key, NULL,
                                       &table), LXA_OK);
    for (step = 0; step < 2048; ++step) {
        uint8_t byte;
        lxa_span_t key;
        bool found = false;
        void *value = NULL;
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        byte = (uint8_t) (seed % 64);
        key = (lxa_span_t) {&byte, 1};
        if (seed % 11 == 0) {
            lxa_table_clear(table);
            memset(present, 0, sizeof(present));
            count = 0;
        }
        else if (seed % 3 == 0) {
            value = (void *) (uintptr_t) (step + 1);
            CHECK(lxa_table_set(table, key, value, &found, NULL), LXA_OK);
            REQUIRE(found == present[byte]);
            count += !present[byte];
            present[byte] = true;
            values[byte] = value;
        }
        else if (seed % 3 == 1) {
            CHECK(lxa_table_delete(table, key, &found, &value), LXA_OK);
            REQUIRE(found == present[byte]);
            if (found) {
                REQUIRE(value == values[byte]);
                --count;
                present[byte] = false;
            }
        }
        else {
            CHECK(lxa_table_find(table, key, &found, &value), LXA_OK);
            REQUIRE(found == present[byte]);
            if (found) {
                REQUIRE(value == values[byte]);
            }
        }
        REQUIRE(lxa_table_length(table) == count);
        if (step % 127 == 0) {
            for (i = 0; i < 64; ++i) {
                uint8_t probe = (uint8_t) i;
                lxa_span_t item = {&probe, 1};
                CHECK(lxa_table_find(table, item, &found, &value), LXA_OK);
                REQUIRE(found == present[i]);
                if (found) {
                    REQUIRE(value == values[i]);
                }
            }
        }
    }
    lxa_table_destroy(table);
}

static void
collision_sequence(const lxa_allocator_t *allocator)
{
    lxa_table_t *table = NULL;
    lxa_budget_t budget;
    lxa_limits_t limits = lxa_limits_default();
    uint8_t secret[16], keys[18][4];
    size_t candidate, count = 0, i;
    bool found;
    void *value;

    CHECK(fixed_key(NULL, secret), LXA_OK);
    for (candidate = 0; candidate < 100000 && count < 18; ++candidate) {
        uint8_t input[4] = {
            (uint8_t) candidate, (uint8_t) (candidate >> 8),
            (uint8_t) (candidate >> 16), (uint8_t) (candidate >> 24)
        };
        uint64_t hash = 0;
        CHECK(lxa_siphash24(secret, input, sizeof(input), &hash), LXA_OK);
        if ((hash & 63) == 0) {
            memcpy(keys[count++], input, sizeof(input));
        }
    }
    REQUIRE(count == 18);
    CHECK(lxa_budget_init(&budget, &limits), LXA_OK);
    CHECK(lxa_table_create_with_budget(allocator, &budget, fixed_key, NULL,
                                       &table), LXA_OK);
    for (i = 0; i < count; ++i) {
        CHECK(lxa_table_set(table, (lxa_span_t) {keys[i], 4},
                            &keys[i], NULL, NULL), LXA_OK);
    }
    REQUIRE(lxa_table_length(table) == count);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + 4;
    CHECK(lxa_table_find(table, (lxa_span_t) {keys[17], 4},
                         &found, &value), LXA_ERROR_LIMIT);
    REQUIRE(lxa_table_limit_reason(table) == LXA_BUDGET_WORK
            && !found && value == NULL && lxa_table_length(table) == count);
    budget.limits.max_work = limits.max_work;
    for (i = 0; i < count; ++i) {
        CHECK(lxa_table_find(table, (lxa_span_t) {keys[i], 4},
                             &found, &value), LXA_OK);
        REQUIRE(found && value == &keys[i]);
    }
    lxa_table_destroy(table);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 0);
}

static void
failure_sequence(void)
{
    unsigned fail_at;
    for (fail_at = 1; fail_at <= 13; ++fail_at) {
        fixture_t f = {0, 0, fail_at};
        lxa_allocator_t allocator = {&f, allocate, resize, release};
        lxa_budget_t budget;
        lxa_table_t *table = NULL;
        uint8_t keys[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        size_t inserted = 0, j;
        lxa_status_t status;
        CHECK(lxa_budget_init(&budget, NULL), LXA_OK);
        status = lxa_table_create_with_budget(&allocator, &budget,
                                               fixed_key, NULL, &table);
        REQUIRE(status == LXA_OK || status == LXA_ERROR_NO_MEMORY);
        if (status == LXA_OK) {
            for (j = 0; j < sizeof(keys); ++j) {
                status = lxa_table_set(table, (lxa_span_t) {&keys[j], 1},
                                        &keys[j], NULL, NULL);
                REQUIRE(status == LXA_OK || status == LXA_ERROR_NO_MEMORY);
                if (status != LXA_OK) {
                    break;
                }
                ++inserted;
            }
            REQUIRE(lxa_table_length(table) == inserted);
            for (j = 0; j < sizeof(keys); ++j) {
                bool present = false;
                void *value = NULL;
                CHECK(lxa_table_find(table, (lxa_span_t) {&keys[j], 1},
                                     &present, &value), LXA_OK);
                REQUIRE(present == (j < inserted));
                if (present) {
                    REQUIRE(value == &keys[j]);
                }
            }
            lxa_table_destroy(table);
        }
        else {
            REQUIRE(table == NULL);
        }
        REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 0);
        REQUIRE(f.frees == f.calls - (f.calls >= fail_at));
    }
}

static void
clear_sequence(void)
{
    fixture_t f = {0};
    lxa_allocator_t allocator = {&f, allocate, resize, release};
    lxa_limits_t limits = lxa_limits_default();
    lxa_budget_t budget;
    lxa_table_t *table = NULL;
    uint8_t keys[] = {1, 2, 3};
    lxa_span_t first = {&keys[0], 1};
    size_t work, memory;
    bool found = false;
    void *value = NULL;
    uint64_t used;
    CHECK(lxa_table_clear_cost(NULL, &work), LXA_ERROR_ARGUMENT);
    CHECK(lxa_table_clear_checked(NULL), LXA_ERROR_ARGUMENT);
    CHECK(lxa_budget_init(&budget, &limits), LXA_OK);
    CHECK(lxa_table_create_with_budget(&allocator, &budget, fixed_key, NULL,
                                       &table), LXA_OK);
    CHECK(lxa_table_clear_cost(table, &work), LXA_OK);
    REQUIRE(work == 0);
    CHECK(lxa_table_set(table, first, &keys[0], NULL, NULL), LXA_OK);
    CHECK(lxa_table_set(table, (lxa_span_t) {&keys[1], 1},
                        &keys[1], NULL, NULL), LXA_OK);
    CHECK(lxa_table_set(table, (lxa_span_t) {&keys[2], 1},
                        &keys[2], NULL, NULL), LXA_OK);
    CHECK(lxa_table_delete(table, (lxa_span_t) {&keys[2], 1},
                           &found, NULL), LXA_OK);
    REQUIRE(found);
    CHECK(lxa_table_clear_cost(table, &work), LXA_OK);
    REQUIRE(work == 2 * 16 + 2); /* scan + zero + two live frees */
    used = budget.used[LXA_BUDGET_WORK];
    memory = budget.used[LXA_BUDGET_MEMORY];
    budget.limits.max_work = used + work - 1;
    CHECK(lxa_table_clear_checked(table), LXA_ERROR_LIMIT);
    REQUIRE(lxa_table_limit_reason(table) == LXA_BUDGET_WORK
            && budget.used[LXA_BUDGET_WORK] == used
            && budget.used[LXA_BUDGET_MEMORY] == memory
            && lxa_table_length(table) == 2);
    budget.limits.max_work = limits.max_work;
    CHECK(lxa_table_find(table, first, &found, &value), LXA_OK);
    REQUIRE(found && value == &keys[0]);
    used = budget.used[LXA_BUDGET_WORK];
    budget.limits.max_work = used + work;
    CHECK(lxa_table_clear_checked(table), LXA_OK);
    REQUIRE(lxa_table_limit_reason(table) == LXA_BUDGET_COUNT
            && budget.used[LXA_BUDGET_WORK] == used + work
            && lxa_table_length(table) == 0
            && budget.used[LXA_BUDGET_MEMORY] == memory - 2);
    CHECK(lxa_table_clear_cost(table, &work), LXA_OK);
    REQUIRE(work == 2 * 16); /* retained capacity still costs work */
    CHECK(lxa_table_clear_checked(table), LXA_ERROR_LIMIT);
    REQUIRE(lxa_table_length(table) == 0);
    lxa_table_clear(table); /* unconditional teardown despite work limit */
    lxa_table_destroy(table);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 0);
}

int
main(void)
{
    fixture_t f = {0, 0, 0};
    lxa_allocator_t allocator = {&f, allocate, resize, release};
    lxa_budget_t budget;
    lxa_limits_t limits = lxa_limits_default();
    lxa_table_t *table = (lxa_table_t *) &f;
    lxa_span_t empty = {NULL, 0}, invalid = {NULL, 1};
    uint8_t colliding[3] = {0};
    unsigned i, matches = 0;
    bool found = true;
    void *value = &f;
    size_t count = 0;
    uint8_t key[16];
    size_t baseline;
    CHECK(lxa_table_create_with_budget(&allocator, NULL, no_entropy, NULL,
                                       &table), LXA_ERROR_ENTROPY);
    REQUIRE(table == NULL && f.calls == 0);
    CHECK(lxa_table_create_with_budget(&allocator, NULL, fixed_key, NULL,
                                       &table), LXA_OK);
    REQUIRE(table != NULL && lxa_table_length(table) == 0);
    CHECK(lxa_table_find(table, invalid, &found, &value), LXA_ERROR_ARGUMENT);
    REQUIRE(!found && value == NULL);
    CHECK(lxa_table_set(table, empty, NULL, &found, &value), LXA_OK);
    REQUIRE(!found && value == NULL && lxa_table_length(table) == 1);
    CHECK(lxa_table_find(table, empty, &found, &value), LXA_OK);
    REQUIRE(found && value == NULL);
    CHECK(lxa_table_set(table, empty, &f, &found, &value), LXA_OK);
    REQUIRE(found && value == NULL && lxa_table_length(table) == 1);
    CHECK(lxa_table_delete(table, empty, &found, &value), LXA_OK);
    REQUIRE(found && value == &f && lxa_table_length(table) == 0);
    CHECK(lxa_table_delete(table, empty, &found, &value), LXA_OK);
    REQUIRE(!found && value == NULL);
    CHECK(fixed_key(NULL, key), LXA_OK);
    /* Three distinct keys share their initial bucket; deletion cannot
     * terminate lookup early at a tombstone. */
    for (i = 0; i < 256 && matches < 3; ++i) {
        uint8_t byte = (uint8_t) i;
        uint64_t hash = 0;
        CHECK(lxa_siphash24(key, &byte, 1, &hash), LXA_OK);
        if ((hash & 15) == 0) {
            colliding[matches++] = byte;
        }
    }
    REQUIRE(matches == 3);
    for (i = 0; i < 3; ++i) {
        lxa_span_t item = {&colliding[i], 1};
        CHECK(lxa_table_set(table, item, (void *) (uintptr_t) (i + 1),
                            NULL, NULL), LXA_OK);
    }
    {
        lxa_span_t middle = {&colliding[1], 1};
        lxa_span_t last = {&colliding[2], 1};
        CHECK(lxa_table_delete(table, middle, &found, NULL), LXA_OK);
        REQUIRE(found);
        CHECK(lxa_table_find(table, last, &found, &value), LXA_OK);
        REQUIRE(found && value == (void *) (uintptr_t) 3);
        CHECK(lxa_table_set(table, middle, NULL, NULL, NULL), LXA_OK);
    }
    CHECK(lxa_table_each(table, count_items, &count), LXA_OK);
    REQUIRE(count == 3 && lxa_table_length(table) == 3);
    CHECK(lxa_table_each(table, stop_visit, NULL), LXA_ERROR_ARGUMENT);
    lxa_table_clear(table);
    REQUIRE(lxa_table_length(table) == 0);
    lxa_table_destroy(table);

    CHECK(lxa_budget_init(&budget, &limits), LXA_OK);
    CHECK(lxa_table_create_with_budget(&allocator, &budget, fixed_key, NULL,
                                       &table), LXA_OK);
    baseline = budget.used[LXA_BUDGET_MEMORY];
    budget.limits.max_name_bytes = 1;
    CHECK(lxa_table_set(table, (lxa_span_t) {(const uint8_t *) &f, 2},
                        NULL, NULL, NULL), LXA_ERROR_LIMIT);
    REQUIRE(lxa_table_limit_reason(table) == LXA_BUDGET_NAME_BYTES
            && lxa_table_length(table) == 0);
    budget.limits.max_name_bytes = limits.max_name_bytes;
    f.fail_at = f.calls + 2; /* key copy succeeds; slot-array allocation fails */
    CHECK(lxa_table_set(table, empty, &f, NULL, NULL), LXA_ERROR_NO_MEMORY);
    REQUIRE(lxa_table_length(table) == 0
            && budget.used[LXA_BUDGET_MEMORY] == baseline);
    f.fail_at = 0;
    budget.limits.max_memory_bytes = baseline + 1;
    CHECK(lxa_table_set(table, empty, &f, NULL, NULL), LXA_ERROR_LIMIT);
    REQUIRE(lxa_table_limit_reason(table) == LXA_BUDGET_MEMORY);
    REQUIRE(lxa_table_length(table) == 0
            && budget.used[LXA_BUDGET_MEMORY] == baseline);
    budget.limits.max_memory_bytes = limits.max_memory_bytes;
    CHECK(lxa_table_set(table, empty, &f, NULL, NULL), LXA_OK);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    CHECK(lxa_table_find(table, empty, &found, &value), LXA_ERROR_LIMIT);
    REQUIRE(lxa_table_limit_reason(table) == LXA_BUDGET_WORK
            && !found && value == NULL && lxa_table_length(table) == 1);
    budget.limits.max_work = limits.max_work;
    {
        uint8_t bytes[8];
        lxa_span_t item;
        for (i = 0; i < 7; ++i) {
            bytes[i] = (uint8_t) i;
            item = (lxa_span_t) {&bytes[i], 1};
            CHECK(lxa_table_set(table, item, &bytes[i], NULL, NULL), LXA_OK);
        }
        bytes[7] = 7;
        item = (lxa_span_t) {&bytes[7], 1};
        baseline = budget.used[LXA_BUDGET_MEMORY];
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + 18;
        CHECK(lxa_table_set(table, item, &bytes[7], NULL, NULL),
              LXA_ERROR_LIMIT);
        REQUIRE(lxa_table_limit_reason(table) == LXA_BUDGET_WORK
                && lxa_table_length(table) == 8
                && budget.used[LXA_BUDGET_MEMORY] == baseline);
        budget.limits.max_work = limits.max_work;
        f.fail_at = f.calls + 2; /* copied key, failed rehash allocation */
        CHECK(lxa_table_set(table, item, &bytes[7], NULL, NULL),
              LXA_ERROR_NO_MEMORY);
        REQUIRE(lxa_table_length(table) == 8
                && budget.used[LXA_BUDGET_MEMORY] == baseline);
        CHECK(lxa_table_find(table, (lxa_span_t) {&bytes[6], 1},
                             &found, &value), LXA_OK);
        REQUIRE(found && value == &bytes[6]);
        f.fail_at = 0;
        CHECK(lxa_table_set(table, item, &bytes[7], NULL, NULL), LXA_OK);
        REQUIRE(lxa_table_length(table) == 9);
    }
    lxa_table_destroy(table);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 0);
    random_sequence(&allocator);
    collision_sequence(&allocator);
    failure_sequence();
    clear_sequence();
    return 0;
}
