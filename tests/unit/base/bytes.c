#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "liblexa/base.h"

#define REQUIRE(condition) do { if (!(condition)) abort(); } while (0)

#define CHECK(call, expected) do {         \
    lxa_status_t status = (call);           \
    REQUIRE(status == (expected));           \
    (void) status;                           \
} while (0)

typedef struct {
    unsigned calls, releases;
    int fail;
} fixture_t;

static void *
allocate(void *context, size_t size)
{
    fixture_t *f = context;
    f->calls++;
    return f->fail ? NULL : malloc(size);
}

static void *
resize(void *context, void *ptr, size_t size)
{
    fixture_t *f = context;
    f->calls++;
    return f->fail ? NULL : realloc(ptr, size);
}

static void
release(void *context, void *ptr)
{
    fixture_t *f = context;
    f->releases++;
    free(ptr);
}

int
main(void)
{
    fixture_t f = {0, 0, 0};
    lxa_allocator_t allocator = {&f, allocate, resize, release};
    lxa_buffer_t buffer;
    lxa_buffer_t second;
    lxa_span_t text = {(const uint8_t *) "abcdef", 6};
    lxa_span_t empty = {NULL, 0};
    lxa_span_t invalid = {NULL, 1};
    lxa_budget_t budget;
    lxa_limits_t limits = lxa_limits_default();
    uint8_t *old;
    size_t old_capacity;

    CHECK(lxa_buffer_init(NULL, &allocator), LXA_ERROR_ARGUMENT);
    CHECK(lxa_buffer_init(&buffer, NULL), LXA_ERROR_ARGUMENT);
    CHECK(lxa_buffer_init(&buffer, &allocator), LXA_OK);
    CHECK(lxa_buffer_append(&buffer, empty), LXA_OK);
    REQUIRE(buffer.data == NULL && buffer.length == 0);
    CHECK(lxa_buffer_reserve_exact(&buffer, 7), LXA_OK);
    REQUIRE(buffer.capacity == 7 && buffer.length == 0);
    CHECK(lxa_buffer_append(&buffer, invalid), LXA_ERROR_ARGUMENT);
    CHECK(lxa_buffer_append(&buffer, text), LXA_OK);
    REQUIRE(buffer.length == 6 && memcmp(buffer.data, text.data, 6) == 0);
    {
        lxa_span_t huge = {text.data, SIZE_MAX};
        CHECK(lxa_buffer_append(&buffer, huge), LXA_ERROR_OVERFLOW);
        REQUIRE(buffer.length == 6 && memcmp(buffer.data, text.data, 6) == 0);
    }
    CHECK(lxa_buffer_append_self(&buffer, 1, 4), LXA_OK);
    REQUIRE(buffer.length == 10 && memcmp(buffer.data, "abcdefbcde", 10) == 0);
    CHECK(lxa_buffer_append_self(&buffer, 9, 2), LXA_ERROR_ARGUMENT);
    old = buffer.data;
    old_capacity = buffer.capacity;
    f.fail = 1;
    CHECK(lxa_buffer_append_self(&buffer, 0, 10), LXA_ERROR_NO_MEMORY);
    REQUIRE(buffer.data == old && buffer.capacity == old_capacity);
    REQUIRE(buffer.length == 10 && memcmp(buffer.data, "abcdefbcde", 10) == 0);
    f.fail = 0;
    CHECK(lxa_buffer_append_self(&buffer, 0, 10), LXA_OK);
    REQUIRE(buffer.length == 20
           && memcmp(buffer.data, "abcdefbcdeabcdefbcde", 20) == 0);
    CHECK(lxa_buffer_truncate(&buffer, 21), LXA_ERROR_ARGUMENT);
    CHECK(lxa_buffer_truncate(&buffer, 3), LXA_OK);
    CHECK(lxa_buffer_terminate(&buffer), LXA_OK);
    REQUIRE(buffer.data[3] == 0 && buffer.length == 3);
    old = buffer.data;
    old_capacity = buffer.capacity;
    f.fail = 1;
    CHECK(lxa_buffer_reserve_exact(&buffer, old_capacity + 1),
          LXA_ERROR_NO_MEMORY);
    REQUIRE(buffer.data == old && buffer.capacity == old_capacity);
    CHECK(lxa_buffer_reserve(&buffer, old_capacity + 1),
          LXA_ERROR_NO_MEMORY);
    REQUIRE(buffer.data == old && buffer.capacity == old_capacity);
    REQUIRE(buffer.length == 3 && memcmp(buffer.data, "abc", 3) == 0);
    f.fail = 0;
    CHECK(lxa_buffer_reserve(&buffer, old_capacity + 1), LXA_OK);
    REQUIRE(buffer.capacity > old_capacity && buffer.length == 3);
    CHECK(lxa_buffer_append_self(&buffer, 0, 3), LXA_OK);
    REQUIRE(buffer.length == 6 && memcmp(buffer.data, "abcabc", 6) == 0);
    lxa_buffer_clear(&buffer);
    REQUIRE(buffer.length == 0 && buffer.capacity > 0);
    CHECK(lxa_buffer_terminate(&buffer), LXA_OK);
    REQUIRE(buffer.data[0] == 0);
    lxa_buffer_destroy(&buffer);
    REQUIRE(buffer.data == NULL && f.releases == 1);

    limits.max_memory_bytes = 21;
    CHECK(lxa_budget_init(&budget, &limits), LXA_OK);
    CHECK(lxa_buffer_init(&buffer, &allocator), LXA_OK);
    CHECK(lxa_buffer_bind_budget(&buffer, &budget), LXA_OK);
    CHECK(lxa_buffer_reserve(&buffer, 1), LXA_OK);
    REQUIRE(buffer.capacity == 16 && budget.used[LXA_BUDGET_MEMORY] == 16);
    CHECK(lxa_buffer_bind_budget(&buffer, &budget), LXA_ERROR_ARGUMENT);
    CHECK(lxa_buffer_reserve(&buffer, 17), LXA_ERROR_LIMIT);
    REQUIRE(buffer.capacity == 16 && budget.used[LXA_BUDGET_MEMORY] == 16);
    f.fail = 1;
    CHECK(lxa_buffer_reserve_exact(&buffer, 20), LXA_ERROR_NO_MEMORY);
    REQUIRE(buffer.capacity == 16 && budget.used[LXA_BUDGET_MEMORY] == 16);
    f.fail = 0;
    CHECK(lxa_buffer_reserve_exact(&buffer, 20), LXA_OK);
    REQUIRE(buffer.capacity == 20 && budget.used[LXA_BUDGET_MEMORY] == 20);
    CHECK(lxa_buffer_reserve_exact(&buffer, 21), LXA_OK);
    REQUIRE(buffer.capacity == 21 && budget.used[LXA_BUDGET_MEMORY] == 21);
    CHECK(lxa_buffer_init(&second, &allocator), LXA_OK);
    CHECK(lxa_buffer_bind_budget(&second, &budget), LXA_OK);
    CHECK(lxa_buffer_reserve_exact(&second, 1), LXA_ERROR_LIMIT);
    REQUIRE(second.data == NULL && budget.used[LXA_BUDGET_MEMORY] == 21);
    lxa_buffer_destroy(&buffer);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 0 && f.releases == 2);
    CHECK(lxa_buffer_reserve_exact(&second, 1), LXA_OK);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 1);
    lxa_buffer_destroy(&second);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 0 && f.releases == 3);
    return 0;
}
