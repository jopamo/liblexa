/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include "liblexa/css.h"

static void
check(lxa_css_tokens_t *tokens, size_t index, lxa_css_token_kind_t kind,
      const char *text, size_t offset, size_t length)
{
    lxa_css_token_t t;
    lxa_span_t span;
    CSS_CHECK(lxa_css_tokens_get(tokens, index, &t, &span) == LXA_OK);
    CSS_CHECK(t.kind == kind && t.offset == offset && t.length == length);
    CSS_CHECK(span.length == strlen(text));
    if (span.length) {
        CSS_CHECK(memcmp(span.data, text, span.length) == 0);
    }
}

static void
ascii(const char *string, lxa_css_unit_t *units)
{
    for (size_t i = 0; string[i]; i++) {
        units[i] = (lxa_css_unit_t) {(unsigned char) string[i], i, 1};
    }
}

typedef struct {
    size_t calls;
    size_t fail_at;
} fail_alloc_t;

static void *
allocate(void *context, size_t bytes)
{
    fail_alloc_t *f = context;
    return ++f->calls == f->fail_at ? NULL : malloc(bytes);
}

static void *
resize(void *context, void *ptr, size_t bytes)
{
    fail_alloc_t *f = context;
    return ++f->calls == f->fail_at ? NULL : realloc(ptr, bytes);
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
    lxa_css_tokens_t tokens;
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_unit_t units[128];
    lxa_css_token_t t;
    lxa_span_t span;
    const char *css = "@media a{width: +12.5px;#id /* ok */ x:y(2%)}";
    size_t n = strlen(css);
    CSS_CHECK(n < sizeof(units) / sizeof(units[0]));
    CSS_CHECK(lxa_css_tokens_init(&tokens, &allocator) == LXA_OK);
    ascii(css, units);
    CSS_CHECK(lxa_css_scan_units(units, n, n, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_AT_KEYWORD, "media", 0, 6);
    check(&tokens, 1, LXA_CSS_WHITESPACE, "", 6, 1);
    check(&tokens, 2, LXA_CSS_IDENT, "a", 7, 1);
    check(&tokens, 3, LXA_CSS_LEFT_BRACE, "", 8, 1);
    check(&tokens, 4, LXA_CSS_IDENT, "width", 9, 5);
    check(&tokens, 5, LXA_CSS_COLON, "", 14, 1);
    check(&tokens, 6, LXA_CSS_WHITESPACE, "", 15, 1);
    check(&tokens, 7, LXA_CSS_DIMENSION, "px", 16, 7);
    CSS_CHECK(lxa_css_tokens_get(&tokens, 7, &t, &span) == LXA_OK);
    CSS_CHECK(t.number == 12.5 && t.flags == LXA_CSS_SIGNED);
    check(&tokens, 8, LXA_CSS_SEMICOLON, "", 23, 1);
    check(&tokens, 9, LXA_CSS_HASH, "id", 24, 3);
    CSS_CHECK(lxa_css_tokens_get(&tokens, 9, &t, &span) == LXA_OK);
    CSS_CHECK(t.flags == LXA_CSS_ID_HASH);
    check(&tokens, 10, LXA_CSS_WHITESPACE, "", 27, 1);
    /* Comment is discarded, not represented by a token. */
    check(&tokens, 11, LXA_CSS_WHITESPACE, "", 36, 1);
    check(&tokens, 12, LXA_CSS_IDENT, "x", 37, 1);
    check(&tokens, 13, LXA_CSS_COLON, "", 38, 1);
    check(&tokens, 14, LXA_CSS_FUNCTION, "y", 39, 2);
    check(&tokens, 15, LXA_CSS_PERCENTAGE, "", 41, 2);
    check(&tokens, 16, LXA_CSS_RIGHT_PAREN, "", 43, 1);
    check(&tokens, 17, LXA_CSS_RIGHT_BRACE, "", 44, 1);
    check(&tokens, 18, LXA_CSS_EOF, "", n, 0);

    ascii("'hi\n.", units);
    CSS_CHECK(lxa_css_scan_units(units, 5, 5, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_BAD_STRING, "hi", 0, 3);
    check(&tokens, 1, LXA_CSS_WHITESPACE, "", 3, 1);
    check(&tokens, 2, LXA_CSS_DELIM, "", 4, 1);
    CSS_CHECK(lxa_css_tokens_get(&tokens, 2, &t, &span) == LXA_OK && t.codepoint == '.');

    ascii("a\\b", units);
    CSS_CHECK(lxa_css_scan_units(units, 3, 3, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_IDENT, "a\x0b", 0, 3);
    ascii("\\31 0", units);
    CSS_CHECK(lxa_css_scan_units(units, 5, 5, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_IDENT, "10", 0, 5);
    ascii("a\\110000", units);
    CSS_CHECK(lxa_css_scan_units(units, 8, 8, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_IDENT, "a\xef\xbf\xbd", 0, 8);
    ascii("\"a\\\nb\"", units);
    CSS_CHECK(lxa_css_scan_units(units, 6, 6, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_STRING, "ab", 0, 6);
    ascii("url(a)", units);
    CSS_CHECK(lxa_css_scan_units(units, 6, 6, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_URL, "a", 0, 6);
    ascii("url( \"hi\")", units);
    CSS_CHECK(lxa_css_scan_units(units, 10, 10, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_FUNCTION, "url", 0, 4);
    check(&tokens, 1, LXA_CSS_WHITESPACE, "", 4, 1);
    check(&tokens, 2, LXA_CSS_STRING, "hi", 5, 4);
    check(&tokens, 3, LXA_CSS_RIGHT_PAREN, "", 9, 1);
    ascii("url(a b)z", units);
    CSS_CHECK(lxa_css_scan_units(units, 9, 9, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_BAD_URL, "", 0, 8);
    check(&tokens, 1, LXA_CSS_IDENT, "z", 8, 1);
    ascii("url(a\\)b)", units);
    CSS_CHECK(lxa_css_scan_units(units, 9, 9, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_URL, "a)b", 0, 9);
    ascii("UrL(  a )", units);
    CSS_CHECK(lxa_css_scan_units(units, 9, 9, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_URL, "a", 0, 9);
    ascii("url(a\\) b)c", units);
    CSS_CHECK(lxa_css_scan_units(units, 11, 11, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_BAD_URL, "", 0, 10);
    check(&tokens, 1, LXA_CSS_IDENT, "c", 10, 1);
    ascii("url(a", units);
    CSS_CHECK(lxa_css_scan_units(units, 5, 5, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_URL, "a", 0, 5);
    {
        const char *numbers = "1e3 1e-3 -0E999 +.5e2px 0e999 1e-999";
        size_t length = strlen(numbers);
        ascii(numbers, units);
        CSS_CHECK(lxa_css_scan_units(units, length, length, &tokens) == LXA_CSS_SCAN_OK);
        check(&tokens, 0, LXA_CSS_NUMBER, "", 0, 3);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 0, &t, &span) == LXA_OK
               && t.number == 1000 && t.flags == 0);
        check(&tokens, 2, LXA_CSS_NUMBER, "", 4, 4);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 2, &t, &span) == LXA_OK
               && t.number > 0.00099 && t.number < 0.00101);
        check(&tokens, 4, LXA_CSS_NUMBER, "", 9, 6);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 4, &t, &span) == LXA_OK
               && t.number == 0 && t.flags == LXA_CSS_SIGNED);
        check(&tokens, 6, LXA_CSS_DIMENSION, "px", 16, 7);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 6, &t, &span) == LXA_OK
               && t.number == 50 && t.flags == LXA_CSS_SIGNED);
        check(&tokens, 8, LXA_CSS_NUMBER, "", 24, 5);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 8, &t, &span) == LXA_OK
               && t.number == 0);
        check(&tokens, 10, LXA_CSS_NUMBER, "", 30, 6);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 10, &t, &span) == LXA_OK
               && t.number == 0);
    }
    {
        const char *range = "1000000000000000000000000000000000000000e-39 1e-310 1e999";
        size_t length = strlen(range);
        ascii(range, units);
        CSS_CHECK(lxa_css_scan_units(units, length, length, &tokens) == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 0, &t, &span) == LXA_OK
               && t.kind == LXA_CSS_NUMBER && t.number > 0.99
               && t.number < 1.01);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 2, &t, &span) == LXA_OK
               && t.kind == LXA_CSS_NUMBER && t.number > 0
               && t.number < 1e-300);
        CSS_CHECK(lxa_css_tokens_get(&tokens, 4, &t, &span) == LXA_OK
               && t.kind == LXA_CSS_NUMBER && t.number > 1e308);
    }
    units[0] = (lxa_css_unit_t) {0x1f600, 0, 4};
    CSS_CHECK(lxa_css_scan_units(units, 1, 4, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_IDENT, "\xf0\x9f\x98\x80", 0, 4);
    units[0].value = '\r';
    CSS_CHECK(lxa_css_scan_units(units, 1, 4, &tokens) == LXA_CSS_SCAN_INVALID);
    CSS_CHECK(lxa_css_tokens_length(&tokens) == 0);
    units[0].value = 'a';
    units[0].offset = (size_t) -1;
    CSS_CHECK(lxa_css_scan_units(units, 1, (size_t) -1, &tokens) == LXA_CSS_SCAN_INVALID);
    CSS_CHECK(lxa_css_tokens_length(&tokens) == 0);
    {
        const char *input = "<!-- --> -- >";
        size_t length = strlen(input);
        ascii(input, units);
        CSS_CHECK(lxa_css_scan_units(units, length, length, &tokens) == LXA_CSS_SCAN_OK);
        check(&tokens, 0, LXA_CSS_CDO, "", 0, 4);
        check(&tokens, 1, LXA_CSS_WHITESPACE, "", 4, 1);
        check(&tokens, 2, LXA_CSS_CDC, "", 5, 3);
        check(&tokens, 3, LXA_CSS_WHITESPACE, "", 8, 1);
        check(&tokens, 4, LXA_CSS_IDENT, "--", 9, 2);
    }
    {
        lxa_css_unit_t normalized[] = {{'a', 0, 1}, {'\n', 1, 2}, {'b', 3, 1}};
        CSS_CHECK(lxa_css_scan_units(normalized, 3, 4, &tokens) == LXA_CSS_SCAN_OK);
        check(&tokens, 0, LXA_CSS_IDENT, "a", 0, 1);
        check(&tokens, 1, LXA_CSS_WHITESPACE, "", 1, 2);
        check(&tokens, 2, LXA_CSS_IDENT, "b", 3, 1);
    }
    {
        fail_alloc_t f = {0, 2};
        lxa_allocator_t failing = {&f, allocate, resize, release};
        lxa_css_tokens_t tmp;
        ascii("color:red", units);
        CSS_CHECK(lxa_css_tokens_init(&tmp, &failing) == LXA_OK);
        CSS_CHECK(lxa_css_scan_units(units, 9, 9, &tmp) == LXA_CSS_SCAN_NO_MEMORY);
        CSS_CHECK(lxa_css_tokens_length(&tmp) == 0);
        f.fail_at = 0;
        CSS_CHECK(lxa_css_scan_units(units, 9, 9, &tmp) == LXA_CSS_SCAN_OK);
        check(&tmp, 0, LXA_CSS_IDENT, "color", 0, 5);
        lxa_css_tokens_destroy(&tmp);
    }
    CSS_CHECK(lxa_css_scan_units(NULL, 0, 0, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_EOF, "", 0, 0);
    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_css_scan_cursor_t cursor;
        size_t produced;
        ascii("/* comment */", units);
        limits.initial_work_credit = 14;
        limits.work_per_input_byte = 0;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_scan_cursor_init_budget(&cursor, units, 13, 13,
                                                   &budget) == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_scan_cursor_batch(&cursor, units, &tokens, 2,
                                             &produced) == LXA_CSS_SCAN_LIMIT);
        CSS_CHECK(cursor.limit_reason == LXA_BUDGET_WORK && produced == 0);
        CSS_CHECK(lxa_css_tokens_length(&tokens) == 1);
        CSS_CHECK(cursor.closed);
        lxa_css_tokens_clear(&tokens);
    }
    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_css_scan_cursor_t cursor;
        size_t produced;
        ascii("a b", units);
        limits.max_work = 15;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_scan_cursor_init_budget(&cursor, units, 3, 3,
                                                   &budget) == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_scan_cursor_batch(&cursor, units, &tokens, 1,
                                             &produced) == LXA_CSS_SCAN_OUTPUT_FULL);
        CSS_CHECK(produced == 1);
        check(&tokens, 0, LXA_CSS_IDENT, "a", 0, 1);
        CSS_CHECK(lxa_css_scan_cursor_batch(&cursor, units, &tokens, 2,
                                             &produced) == LXA_CSS_SCAN_LIMIT);
        CSS_CHECK(cursor.limit_reason == LXA_BUDGET_WORK && cursor.closed);
        CSS_CHECK(lxa_css_tokens_length(&tokens) == 1);
        lxa_css_tokens_clear(&tokens);
    }
    {
        lxa_css_tokens_t bounded;
        lxa_css_scan_cursor_t cursor;
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        size_t produced;
        limits.max_memory_bytes = 16;
        ascii("x", units);
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_init(&bounded, &allocator) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_bind_budget(&bounded, &budget) == LXA_OK);
        CSS_CHECK(lxa_css_scan_cursor_init_budget(&cursor, units, 1, 1,
                                                   &budget) == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_scan_cursor_batch(&cursor, units, &bounded, 2,
                                             &produced) == LXA_CSS_SCAN_LIMIT);
        CSS_CHECK(cursor.limit_reason == LXA_BUDGET_MEMORY && produced == 0);
        CSS_CHECK(lxa_css_tokens_length(&bounded) == 0
                  && budget.used[LXA_BUDGET_MEMORY] == 0);
        lxa_css_tokens_destroy(&bounded);
    }
    lxa_css_tokens_destroy(&tokens);
    return 0;
}
