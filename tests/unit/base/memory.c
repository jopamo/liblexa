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
    unsigned calls;
    unsigned frees;
    unsigned fail_at;
} fixture_t;

static void *
allocate(void *context, size_t size)
{
    fixture_t *state = context;
    state->calls++;
    return state->calls == state->fail_at ? NULL : malloc(size);
}

static void *
resize(void *context, void *ptr, size_t size)
{
    fixture_t *state = context;
    state->calls++;
    return state->calls == state->fail_at ? NULL : realloc(ptr, size);
}

static void
release(void *context, void *ptr)
{
    fixture_t *state = context;
    state->frees++;
    free(ptr);
}

int
main(void)
{
    fixture_t state = {0, 0, 0};
    lxa_allocator_t allocator = {&state, allocate, resize, release};
    lxa_allocator_t system = lxa_allocator_default();
    void *ptr = NULL;
    void *output = (void *) &state;

    CHECK(lxa_alloc(&allocator, 0, &output), LXA_ERROR_ARGUMENT);
    REQUIRE(output == NULL && state.calls == 0);
    CHECK(lxa_alloc(NULL, 1, &output), LXA_ERROR_ARGUMENT);
    REQUIRE(output == NULL);
    CHECK(lxa_alloc(&allocator, 1, NULL), LXA_ERROR_ARGUMENT);
    CHECK(lxa_calloc(&allocator, SIZE_MAX, 2, &output), LXA_ERROR_OVERFLOW);
    REQUIRE(output == NULL && state.calls == 0);
    CHECK(lxa_calloc(&allocator, 0, 1, &output), LXA_ERROR_ARGUMENT);
    CHECK(lxa_alloc(&allocator, 4, &output), LXA_OK);
    ptr = output;
    memcpy(ptr, "abc", 4);
    state.fail_at = state.calls + 1;
    CHECK(lxa_resize(&allocator, &ptr, 8), LXA_ERROR_NO_MEMORY);
    REQUIRE(ptr == output && strcmp((char *) ptr, "abc") == 0);
    CHECK(lxa_resize(&allocator, &ptr, 0), LXA_ERROR_ARGUMENT);
    REQUIRE(state.calls == state.fail_at);
    CHECK(lxa_resize_array(&allocator, &ptr, SIZE_MAX, 2),
          LXA_ERROR_OVERFLOW);
    REQUIRE(ptr == output && state.calls == state.fail_at);
    state.fail_at = 0;
    CHECK(lxa_resize_array(&allocator, &ptr, 2, 4), LXA_OK);
    REQUIRE(strcmp((char *) ptr, "abc") == 0);
    lxa_free(&allocator, ptr);
    lxa_free(&allocator, NULL);
    REQUIRE(state.frees == 1);

    state.fail_at = state.calls + 1;
    CHECK(lxa_calloc(&allocator, 2, 4, &output), LXA_ERROR_NO_MEMORY);
    REQUIRE(output == NULL);
    state.fail_at = 0;
    CHECK(lxa_calloc(&allocator, 2, 4, &output), LXA_OK);
    REQUIRE(memcmp(output, "\0\0\0\0\0\0\0\0", 8) == 0);
    lxa_free(&allocator, output);
    REQUIRE(state.frees == 2);

    output = NULL;
    CHECK(lxa_resize(&system, &output, 16), LXA_OK);
    lxa_free(&system, output);
    return 0;
}
