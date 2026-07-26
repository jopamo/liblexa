#include <stdint.h>
#include <stdlib.h>

#include "liblexa/base.h"

#define REQUIRE(condition) do { if (!(condition)) abort(); } while (0)

#define CHECK(call, expected) do {         \
    lxa_status_t status = (call);           \
    REQUIRE(status == (expected));           \
    (void) status;                           \
} while (0)

typedef struct {
    size_t allocations;
    size_t releases;
    int fail;
} fixture_t;

static void *
allocate(void *ctx, size_t size)
{
    fixture_t *f = ctx;
    f->allocations++;
    return f->fail ? NULL : malloc(size);
}

static void *
resize(void *ctx, void *ptr, size_t size)
{
    fixture_t *f = ctx;
    f->allocations++;
    return f->fail ? NULL : realloc(ptr, size);
}

static void
release(void *ctx, void *ptr)
{
    fixture_t *f = ctx;
    f->releases++;
    free(ptr);
}

static int
read_at(lxa_vector_t *vector, size_t index)
{
    void *value = NULL;
    CHECK(lxa_vector_at(vector, index, &value), LXA_OK);
    return *(int *) value;
}

static void
check_at(lxa_vector_t *vector, size_t index, int expected)
{
    int actual = read_at(vector, index);
    REQUIRE(actual == expected);
}

int
main(void)
{
    fixture_t f = {0, 0, 0};
    lxa_allocator_t allocator = {&f, allocate, resize, release};
    lxa_vector_t vector;
    int values[] = {10, 20, 30, 40, 50, 25};
    size_t i, old_capacity;
    void *out = (void *) &f;
    lxa_budget_t budget;
    lxa_limits_t limits = lxa_limits_default();

    CHECK(lxa_vector_init(&vector, &allocator, 0), LXA_ERROR_ARGUMENT);
    CHECK(lxa_vector_init(&vector, &allocator, sizeof(int)), LXA_OK);
    CHECK(lxa_vector_reserve(&vector, SIZE_MAX), LXA_ERROR_OVERFLOW);
    CHECK(lxa_vector_reserve(&vector, 2), LXA_OK);
    REQUIRE(vector.bytes.capacity >= 2 * sizeof(int));
    REQUIRE(lxa_vector_length(&vector) == 0 && vector.bytes.data != NULL);
    CHECK(lxa_vector_at(&vector, 0, &out), LXA_ERROR_ARGUMENT);
    REQUIRE(out == NULL);
    CHECK(lxa_vector_erase(&vector, 0, 0), LXA_OK);
    CHECK(lxa_vector_insert(&vector, 1, &values[0]), LXA_ERROR_ARGUMENT);
    CHECK(lxa_vector_push(&vector, NULL), LXA_ERROR_ARGUMENT);
    for (i = 0; i < 4; i++) {
        CHECK(lxa_vector_push(&vector, &values[i]), LXA_OK);
    }
    old_capacity = vector.bytes.capacity;
    f.fail = 1;
    CHECK(lxa_vector_push(&vector, &values[4]), LXA_ERROR_NO_MEMORY);
    REQUIRE(lxa_vector_length(&vector) == 4
           && vector.bytes.capacity == old_capacity);
    for (i = 0; i < 4; i++) {
        check_at(&vector, i, values[i]);
    }
    f.fail = 0;
    CHECK(lxa_vector_insert(&vector, 2, &values[5]), LXA_OK);
    REQUIRE(lxa_vector_length(&vector) == 5);
    check_at(&vector, 1, 20);
    check_at(&vector, 2, 25);
    check_at(&vector, 3, 30);
    CHECK(lxa_vector_erase(&vector, 1, 2), LXA_OK);
    REQUIRE(lxa_vector_length(&vector) == 3);
    check_at(&vector, 1, 30);
    CHECK(lxa_vector_erase(&vector, 3, 1), LXA_ERROR_ARGUMENT);
    CHECK(lxa_vector_erase(&vector, 3, 0), LXA_OK);
    lxa_vector_clear(&vector);
    REQUIRE(lxa_vector_length(&vector) == 0);
    lxa_vector_destroy(&vector);
    REQUIRE(f.releases == 1);
    REQUIRE(lxa_vector_length(&vector) == 0);

    limits.max_memory_bytes = 16;
    CHECK(lxa_budget_init(&budget, &limits), LXA_OK);
    CHECK(lxa_vector_init(&vector, &allocator, sizeof(int)), LXA_OK);
    CHECK(lxa_vector_bind_budget(&vector, &budget), LXA_OK);
    CHECK(lxa_vector_reserve(&vector, 2), LXA_OK);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 16);
    for (i = 0; i < 4; i++)
        CHECK(lxa_vector_push(&vector, &values[0]), LXA_OK);
    CHECK(lxa_vector_push(&vector, &values[1]), LXA_ERROR_LIMIT);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 16
           && lxa_vector_length(&vector) == 4);
    lxa_vector_destroy(&vector);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 0 && f.releases == 2);
    return 0;
}
