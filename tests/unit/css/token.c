/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <stdlib.h>
#include <string.h>

#include "css/tokens.h"

typedef struct {
    size_t calls;
    size_t fail_at;
} fixture_t;

static void *
allocate(void *context, size_t size)
{
    fixture_t *f = context;
    return ++f->calls == f->fail_at ? NULL : malloc(size);
}

static void *
resize(void *context, void *ptr, size_t size)
{
    fixture_t *f = context;
    return ++f->calls == f->fail_at ? NULL : realloc(ptr, size);
}

static void
release(void *context, void *ptr)
{
    (void) context;
    free(ptr);
}

int
main(void)
{
    fixture_t fixture = {0, 0};
    lxa_allocator_t allocator = {&fixture, allocate, resize, release};
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_token_t in = {0}, out;
    lxa_span_t text;
    char name[] = "color";
    char long_text[64];

    {
        lxa_budget_t budget;
        CSS_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_create(NULL, &budget, &tokens)
                  == LXA_ERROR_ARGUMENT && tokens == NULL);
        CSS_CHECK(lxa_css_tokens_create(&allocator, &budget, NULL)
                  == LXA_ERROR_ARGUMENT);
        fixture.fail_at = fixture.calls + 1;
        CSS_CHECK(lxa_css_tokens_create(&allocator, &budget, &tokens)
                  == LXA_ERROR_NO_MEMORY && tokens == NULL);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                  && lxa_css_tokens_limit_reason(tokens) == LXA_BUDGET_COUNT);
        lxa_css_tokens_destroy(tokens);
        fixture.fail_at = 0;
    }
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    in.kind = LXA_CSS_IDENT;
    in.offset = 4;
    in.length = 5;
    CSS_CHECK(lxa_css_tokens_append(tokens, in,
        (lxa_span_t) {(const uint8_t *) name, 5}) == LXA_OK);
    name[0] = 'X';
    CSS_CHECK(lxa_css_tokens_get(tokens, 0, &out, &text) == LXA_OK);
    CSS_CHECK(out.kind == LXA_CSS_IDENT && out.offset == 4 && out.length == 5);
    CSS_CHECK(text.length == 5 && memcmp(text.data, "color", 5) == 0);

    memset(long_text, 'a', sizeof(long_text));
    fixture.fail_at = fixture.calls + 1;
    CSS_CHECK(lxa_css_tokens_append(tokens, in,
        (lxa_span_t) {(const uint8_t *) long_text, sizeof(long_text)})
        == LXA_ERROR_NO_MEMORY);
    CSS_CHECK(lxa_css_tokens_length(tokens) == 1);
    CSS_CHECK(memcmp(text.data, "color", 5) == 0);
    /* Vector growth may succeed before text growth fails; prior views remain. */
    fixture.fail_at = fixture.calls + 2;
    CSS_CHECK(lxa_css_tokens_append(tokens, in,
        (lxa_span_t) {(const uint8_t *) long_text, sizeof(long_text)})
        == LXA_ERROR_NO_MEMORY);
    CSS_CHECK(lxa_css_tokens_length(tokens) == 1);
    CSS_CHECK(memcmp(text.data, "color", 5) == 0);
    fixture.fail_at = 0;
    CSS_CHECK(lxa_css_tokens_append(tokens, in,
        (lxa_span_t) {(const uint8_t *) long_text, sizeof(long_text)})
        == LXA_OK);
    CSS_CHECK(lxa_css_tokens_length(tokens) == 2);
    CSS_CHECK(lxa_css_tokens_get(tokens, 1, &out, &text) == LXA_OK);
    CSS_CHECK(text.length == sizeof(long_text)
           && memcmp(text.data, long_text, sizeof(long_text)) == 0);
    CSS_CHECK(lxa_css_tokens_get(tokens, 2, &out, &text) == LXA_ERROR_ARGUMENT);
    CSS_CHECK(text.data == NULL && text.length == 0);
    in.offset = SIZE_MAX;
    CSS_CHECK(lxa_css_tokens_append(tokens, in, (lxa_span_t) {NULL, 0})
           == LXA_ERROR_OVERFLOW);
    CSS_CHECK(lxa_css_tokens_length(tokens) == 2);
    in.offset = 0;
    in.kind = (lxa_css_token_kind_t) -1;
    CSS_CHECK(lxa_css_tokens_append(tokens, in, (lxa_span_t) {NULL, 0})
           == LXA_ERROR_ARGUMENT);
    in.kind = LXA_CSS_TOKEN_COUNT;
    CSS_CHECK(lxa_css_tokens_append(tokens, in, (lxa_span_t) {NULL, 0})
           == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_tokens_length(tokens) == 2);
    lxa_css_tokens_clear(tokens);
    CSS_CHECK(lxa_css_tokens_length(tokens) == 0);
    lxa_css_tokens_destroy(tokens);
    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_css_tokens_t *bounded = NULL;
        in.kind = LXA_CSS_IDENT;
        in.offset = 0;
        limits.max_memory_bytes = 0;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_create(&allocator, &budget, &bounded)
                  == LXA_ERROR_LIMIT && bounded == NULL);
        CSS_CHECK(lxa_css_tokens_length(bounded) == 0
                  && budget.used[LXA_BUDGET_MEMORY] == 0);
        lxa_css_tokens_destroy(bounded);
        limits.max_memory_bytes = 1024;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_create(&allocator, &budget, &bounded) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_append(bounded, in,
            (lxa_span_t) {(const uint8_t *) name, 5}) == LXA_OK);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY]
                  == sizeof(*bounded) + bounded->text.capacity
                     + bounded->entries.bytes.capacity);
        lxa_css_tokens_destroy(bounded);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        limits.max_token_bytes = 4;
        limits.max_name_bytes = 2;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_create(&allocator, &budget, &bounded) == LXA_OK);
        in.kind = LXA_CSS_IDENT;
        in.length = 5;
        CSS_CHECK(lxa_css_tokens_append(bounded, in,
            (lxa_span_t) {(const uint8_t *) name, 1}) == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_tokens_limit_reason(bounded) == LXA_BUDGET_TOKEN_BYTES);
        in.length = 1;
        CSS_CHECK(lxa_css_tokens_append(bounded, in,
            (lxa_span_t) {(const uint8_t *) name, 5}) == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_tokens_limit_reason(bounded) == LXA_BUDGET_TOKEN_BYTES);
        limits.max_token_bytes = 8;
        budget.limits.max_token_bytes = 8;
        CSS_CHECK(lxa_css_tokens_append(bounded, in,
            (lxa_span_t) {(const uint8_t *) name, 5}) == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_tokens_limit_reason(bounded) == LXA_BUDGET_NAME_BYTES);
        CSS_CHECK(lxa_css_tokens_length(bounded) == 0
                  && budget.used[LXA_BUDGET_MEMORY] == sizeof(*bounded)
                  && budget.used[LXA_BUDGET_TOKENS] == 0);
        in.kind = LXA_CSS_STRING;
        CSS_CHECK(lxa_css_tokens_append(bounded, in,
            (lxa_span_t) {(const uint8_t *) name, 5}) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_limit_reason(bounded) == LXA_BUDGET_COUNT);
        CSS_CHECK(lxa_css_tokens_length(bounded) == 1);
        lxa_css_tokens_destroy(bounded);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        in.kind = LXA_CSS_IDENT;
        in.length = 5;
        limits.max_tokens = 1;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_create(&allocator, &budget, &bounded) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_append(bounded, in,
            (lxa_span_t) {(const uint8_t *) "x", 1}) == LXA_OK);
        CSS_CHECK(budget.used[LXA_BUDGET_TOKENS] == 1);
        CSS_CHECK(lxa_css_tokens_append(bounded, in,
            (lxa_span_t) {(const uint8_t *) "y", 1}) == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_tokens_limit_reason(bounded) == LXA_BUDGET_TOKENS);
        CSS_CHECK(lxa_css_tokens_length(bounded) == 1);
        CSS_CHECK(lxa_css_tokens_get(bounded, 0, &out, &text) == LXA_OK);
        CSS_CHECK(text.length == 1 && text.data[0] == 'x');
        lxa_css_tokens_clear(bounded);
        CSS_CHECK(lxa_css_tokens_append(bounded, in,
            (lxa_span_t) {(const uint8_t *) "z", 1}) == LXA_OK);
        CSS_CHECK(budget.used[LXA_BUDGET_TOKENS] == 1);
        lxa_css_tokens_destroy(bounded);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                  && budget.used[LXA_BUDGET_TOKENS] == 1);
    }
    return 0;
}
