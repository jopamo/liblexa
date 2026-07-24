/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include "liblexa/css.h"

static void
check(lxa_css_tokens_t *tokens, size_t index, lxa_css_token_kind_t kind,
      size_t start, size_t length, const char *text)
{
    lxa_css_token_t token;
    lxa_span_t value;
    CSS_CHECK(lxa_css_tokens_get(tokens, index, &token, &value) == LXA_OK);
    CSS_CHECK(token.kind == kind && token.offset == start && token.length == length);
    CSS_CHECK(value.length == strlen(text));
    if (value.length) {
        CSS_CHECK(memcmp(value.data, text, value.length) == 0);
    }
}

static void
compare(lxa_css_tokens_t *a, lxa_css_tokens_t *b)
{
    CSS_CHECK(lxa_css_tokens_length(a) == lxa_css_tokens_length(b));
    for (size_t i = 0; i < lxa_css_tokens_length(a); i++) {
        lxa_css_token_t x, y;
        lxa_span_t xs, ys;
        CSS_CHECK(lxa_css_tokens_get(a, i, &x, &xs) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_get(b, i, &y, &ys) == LXA_OK);
        CSS_CHECK(x.kind == y.kind && x.offset == y.offset && x.length == y.length);
        CSS_CHECK(x.number == y.number && x.flags == y.flags
               && x.codepoint == y.codepoint && xs.length == ys.length);
        if (xs.length) {
            CSS_CHECK(memcmp(xs.data, ys.data, xs.length) == 0);
        }
    }
}

typedef struct {
    size_t calls;
    size_t fail_at;
    size_t sizes[2];
} failure_t;

static void *
allocate(void *ctx, size_t n)
{
    failure_t *f = ctx;
    f->calls++;
    if (f->calls <= 2) {
        f->sizes[f->calls - 1] = n;
    }
    return f->calls == f->fail_at ? NULL : malloc(n);
}

static void *
resize(void *ctx, void *ptr, size_t n)
{
    failure_t *f = ctx;
    return ++f->calls == f->fail_at ? NULL : realloc(ptr, n);
}

static void
release(void *ctx, void *ptr)
{
    (void) ctx;
    free(ptr);
}

static uint32_t
next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

/* Check decoder replay, CRLF folding and scanner cursor progress together,
 * not only a fixed partition of a fixed input. This test is deterministic.
 */
static void
partitions(lxa_allocator_t *allocator)
{
    lxa_css_tokens_t reference, chunked;
    uint32_t rng = 0x81d283c5;
    uint8_t bytes[96];
    CSS_CHECK(lxa_css_tokens_init(&reference, allocator) == LXA_OK);
    CSS_CHECK(lxa_css_tokens_init(&chunked, allocator) == LXA_OK);
    for (size_t trial = 0; trial < 128; trial++) {
        size_t length = next_random(&rng) % sizeof(bytes);
        size_t position = 0;
        lxa_css_byte_stream_t *stream;
        lxa_css_scan_result_t expected, actual;
        for (size_t i = 0; i < length; i++) {
            bytes[i] = (uint8_t) next_random(&rng);
        }
        expected = lxa_css_scan_bytes((lxa_span_t) {bytes, length}, &reference);
        CSS_CHECK(lxa_css_byte_stream_create(allocator, &stream) == LXA_CSS_SCAN_OK);
        while (position < length) {
            size_t size = 1 + next_random(&rng) % 7;
            size_t consumed;
            if (size > length - position) {
                size = length - position;
            }
            CSS_CHECK(lxa_css_byte_stream_feed(stream,
                (lxa_span_t) {bytes + position, size}, &consumed)
                == LXA_CSS_SCAN_OK && consumed == size);
            position += size;
        }
        CSS_CHECK(lxa_css_byte_stream_seal(stream) == LXA_CSS_SCAN_OK);
        lxa_css_tokens_clear(&chunked);
        for (;;) {
            size_t produced;
            size_t capacity = 1 + next_random(&rng) % 5;
            actual = lxa_css_byte_stream_drain(stream, &chunked, capacity, &produced);
            CSS_CHECK(produced <= capacity);
            if (actual != LXA_CSS_SCAN_OUTPUT_FULL) {
                break;
            }
            CSS_CHECK(produced != 0);
        }
        CSS_CHECK(actual == expected);
        if (actual == LXA_CSS_SCAN_OK) {
            compare(&reference, &chunked);
        }
        lxa_css_byte_stream_destroy(stream);
    }
    lxa_css_tokens_destroy(&chunked);
    lxa_css_tokens_destroy(&reference);
}

/* A long token split at every byte must do the same scanner work as a
 * single feed. This catches quadratic tentative-token rescanning. */
static void
long_partitions(lxa_allocator_t *allocator)
{
    uint8_t input[32768];
    lxa_budget_t whole_budget, split_budget;
    lxa_css_tokens_t whole, split;
    lxa_css_byte_stream_t *stream;
    size_t consumed;
    memset(input, 'a', sizeof(input));
    CSS_CHECK(lxa_budget_init(&whole_budget, NULL) == LXA_OK);
    CSS_CHECK(lxa_budget_init(&split_budget, NULL) == LXA_OK);
    CSS_CHECK(lxa_css_tokens_init(&whole, allocator) == LXA_OK);
    CSS_CHECK(lxa_css_tokens_init(&split, allocator) == LXA_OK);
    CSS_CHECK(lxa_css_tokens_bind_budget(&whole, &whole_budget) == LXA_OK);
    CSS_CHECK(lxa_css_tokens_bind_budget(&split, &split_budget) == LXA_OK);
    CSS_CHECK(lxa_css_byte_stream_create_budget(allocator, &whole_budget,
                                                 &stream) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_byte_stream_feed(stream,
        (lxa_span_t) {input, sizeof(input)}, &consumed) == LXA_CSS_SCAN_OK
        && consumed == sizeof(input));
    CSS_CHECK(lxa_css_byte_stream_finish(stream, &whole) == LXA_CSS_SCAN_OK);
    lxa_css_byte_stream_destroy(stream);
    CSS_CHECK(lxa_css_byte_stream_create_budget(allocator, &split_budget,
                                                 &stream) == LXA_CSS_SCAN_OK);
    for (size_t i = 0; i < sizeof(input); i++) {
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input + i, 1}, &consumed) == LXA_CSS_SCAN_OK
            && consumed == 1);
    }
    CSS_CHECK(lxa_css_byte_stream_finish(stream, &split) == LXA_CSS_SCAN_OK);
    lxa_css_byte_stream_destroy(stream);
    compare(&whole, &split);
    CSS_CHECK(whole_budget.used[LXA_BUDGET_WORK]
              == split_budget.used[LXA_BUDGET_WORK]);
    CSS_CHECK(whole_budget.used[LXA_BUDGET_INPUT] == sizeof(input)
              && split_budget.used[LXA_BUDGET_INPUT] == sizeof(input));
    lxa_css_tokens_destroy(&whole);
    lxa_css_tokens_destroy(&split);
    CSS_CHECK(whole_budget.used[LXA_BUDGET_MEMORY] == 0
              && split_budget.used[LXA_BUDGET_MEMORY] == 0);
}

int
main(void)
{
    lxa_css_tokens_t tokens;
    lxa_allocator_t allocator = lxa_allocator_default();
    const uint8_t src[] = {
        0xef, 0xbb, 0xbf, 'a', '\r', '\n', 'b', ':', 0xf0, 0x9f, 0x98, 0x80,
        0xc3, ' ', 0, '\f', 'z', '\r'
    };
    const uint8_t replay[] = {0xed, 0xcb, '%', 0xf2, 0x90, '2'};
    const uint8_t incomplete[] = {'a', 0xe2, 0x82};
    const uint8_t error[] = {'1', 'e', '2'};
    CSS_CHECK(lxa_css_tokens_init(&tokens, &allocator) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {src, sizeof(src)}, &tokens)
           == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_IDENT, 3, 1, "a");
    check(&tokens, 1, LXA_CSS_WHITESPACE, 4, 2, "");
    check(&tokens, 2, LXA_CSS_IDENT, 6, 1, "b");
    check(&tokens, 3, LXA_CSS_COLON, 7, 1, "");
    check(&tokens, 4, LXA_CSS_IDENT, 8, 5, "\xf0\x9f\x98\x80\xef\xbf\xbd");
    check(&tokens, 5, LXA_CSS_WHITESPACE, 13, 1, "");
    check(&tokens, 6, LXA_CSS_IDENT, 14, 1, "\xef\xbf\xbd");
    check(&tokens, 7, LXA_CSS_WHITESPACE, 15, 1, "");
    check(&tokens, 8, LXA_CSS_IDENT, 16, 1, "z");
    check(&tokens, 9, LXA_CSS_WHITESPACE, 17, 1, "");
    check(&tokens, 10, LXA_CSS_EOF, 18, 0, "");

    {
        lxa_css_tokens_t split;
        lxa_css_byte_stream_t *stream;
        size_t count;
        CSS_CHECK(lxa_css_tokens_init(&split, &allocator) == LXA_OK);
        for (size_t cut = 0; cut <= sizeof(src); cut++) {
            CSS_CHECK(lxa_css_byte_stream_create(&allocator, &stream) == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_feed(stream, (lxa_span_t) {src, cut}, &count)
                   == LXA_CSS_SCAN_OK && count == cut);
            CSS_CHECK(lxa_css_byte_stream_feed(stream,
                (lxa_span_t) {src + cut, sizeof(src) - cut}, &count)
                   == LXA_CSS_SCAN_OK && count == sizeof(src) - cut);
            CSS_CHECK(lxa_css_byte_stream_finish(stream, &split) == LXA_CSS_SCAN_OK);
            compare(&tokens, &split);
            CSS_CHECK(lxa_css_byte_stream_feed(stream,
                (lxa_span_t) {src, sizeof(src)}, &count) == LXA_CSS_SCAN_INVALID
                   && count == 0);
            lxa_css_byte_stream_destroy(stream);
        }
        for (size_t capacity = 1; capacity <= 3; capacity++) {
            size_t produced, total = 0;
            CSS_CHECK(lxa_css_byte_stream_create(&allocator, &stream) == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_feed(stream,
                (lxa_span_t) {src, sizeof(src)}, &count) == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_seal(stream) == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_feed(stream,
                (lxa_span_t) {src, sizeof(src)}, &count) == LXA_CSS_SCAN_INVALID);
            CSS_CHECK(lxa_css_byte_stream_drain(stream, &split, 0, &produced)
                   == LXA_CSS_SCAN_OUTPUT_FULL && produced == 0);
            lxa_css_tokens_clear(&split);
            for (;;) {
                lxa_css_scan_result_t r = lxa_css_byte_stream_drain(stream,
                    &split, capacity, &produced);
                CSS_CHECK(produced >= 1 && produced <= capacity);
                total += produced;
                if (r == LXA_CSS_SCAN_OK) break;
                CSS_CHECK(r == LXA_CSS_SCAN_OUTPUT_FULL);
            }
            CSS_CHECK(total == lxa_css_tokens_length(&tokens));
            compare(&tokens, &split);
            CSS_CHECK(lxa_css_byte_stream_drain(stream, &split, 1, &produced)
                   == LXA_CSS_SCAN_INVALID && produced == 0);
            lxa_css_byte_stream_destroy(stream);
        }
        CSS_CHECK(lxa_css_byte_stream_create(&allocator, &stream) == LXA_CSS_SCAN_OK);
        for (size_t j = 0; j < sizeof(src); j++) {
            CSS_CHECK(lxa_css_byte_stream_feed(stream, (lxa_span_t) {src + j, 1}, &count)
                   == LXA_CSS_SCAN_OK && count == 1);
        }
        CSS_CHECK(lxa_css_byte_stream_finish(stream, &split) == LXA_CSS_SCAN_OK);
        compare(&tokens, &split);
        lxa_css_byte_stream_destroy(stream);
        lxa_css_tokens_destroy(&split);
    }
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {replay, sizeof(replay)}, &tokens)
              == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_IDENT, 0, 2,
          "\xef\xbf\xbd\xef\xbf\xbd");
    check(&tokens, 1, LXA_CSS_DELIM, 2, 1, "");
    check(&tokens, 2, LXA_CSS_IDENT, 3, 3, "\xef\xbf\xbd" "2");
    check(&tokens, 3, LXA_CSS_EOF, 6, 0, "");
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {incomplete, sizeof(incomplete)}, &tokens)
           == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_IDENT, 0, 3, "a\xef\xbf\xbd");
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {error, sizeof(error)}, &tokens)
           == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_NUMBER, 0, 3, "");
    {
        lxa_css_token_t number;
        lxa_span_t text;
        CSS_CHECK(lxa_css_tokens_get(&tokens, 0, &number, &text) == LXA_OK);
        CSS_CHECK(number.number == 100 && number.flags == 0);
    }
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {NULL, 0}, &tokens) == LXA_CSS_SCAN_OK);
    check(&tokens, 0, LXA_CSS_EOF, 0, 0, "");
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {NULL, 2}, &tokens) == LXA_CSS_SCAN_INVALID);
    CSS_CHECK(lxa_css_tokens_length(&tokens) == 0);
    {
        failure_t f = {0};
        lxa_allocator_t a = {&f, allocate, resize, release};
        lxa_css_tokens_t tmp;
        const uint8_t text[] = "red:blue";
        CSS_CHECK(lxa_css_tokens_init(&tmp, &a) == LXA_OK);
        for (size_t i = 1; i <= 5; i++) {
            f.calls = 0;
            f.fail_at = i;
            CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {text, sizeof(text) - 1}, &tmp)
                   == LXA_CSS_SCAN_NO_MEMORY);
            CSS_CHECK(lxa_css_tokens_length(&tmp) == 0);
        }
        {
            lxa_css_byte_stream_t *stream;
            size_t consumed;
            f.calls = 0;
            f.fail_at = 3; /* stream, decoder, then first scalar reservation */
            CSS_CHECK(lxa_css_byte_stream_create(&a, &stream) == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_feed(stream,
                (lxa_span_t) {text, sizeof(text) - 1}, &consumed)
                   == LXA_CSS_SCAN_NO_MEMORY && consumed == 1);
            CSS_CHECK(lxa_css_byte_stream_finish(stream, &tmp) == LXA_CSS_SCAN_INVALID);
            CSS_CHECK(lxa_css_tokens_length(&tmp) == 0);
            lxa_css_byte_stream_destroy(stream);
        }
        {
            const uint8_t long_name[] = "a abcdefghijklmnop";
            lxa_css_byte_stream_t *stream;
            size_t consumed, produced;
            f.fail_at = 0;
            CSS_CHECK(lxa_css_byte_stream_create(&a, &stream) == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_feed(stream,
                (lxa_span_t) {long_name, sizeof(long_name) - 1}, &consumed)
                   == LXA_CSS_SCAN_OK && consumed == sizeof(long_name) - 1);
            CSS_CHECK(lxa_css_byte_stream_seal(stream) == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_drain(stream, &tmp, 1, &produced)
                   == LXA_CSS_SCAN_OUTPUT_FULL && produced == 1);
            f.fail_at = f.calls + 1;
            CSS_CHECK(lxa_css_byte_stream_drain(stream, &tmp, 2, &produced)
                   == LXA_CSS_SCAN_NO_MEMORY);
            CSS_CHECK(lxa_css_tokens_length(&tmp) == 1 + produced);
            check(&tmp, 0, LXA_CSS_IDENT, 0, 1, "a");
            CSS_CHECK(lxa_css_byte_stream_drain(stream, &tmp, 2, &produced)
                   == LXA_CSS_SCAN_INVALID && produced == 0);
            lxa_css_byte_stream_destroy(stream);
            lxa_css_tokens_clear(&tmp);
        }
        f.fail_at = 0;
        CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {text, sizeof(text) - 1}, &tmp)
               == LXA_CSS_SCAN_OK);
        check(&tmp, 0, LXA_CSS_IDENT, 0, 3, "red");
        lxa_css_tokens_destroy(&tmp);
    }
    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_css_byte_stream_t *stream;
        lxa_css_tokens_t bounded;
        const uint8_t input[] = {'a', 'b', 'c'};
        size_t consumed;
        limits.max_input_bytes = 2;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input, sizeof(input)}, &consumed)
            == LXA_CSS_SCAN_LIMIT && consumed == 0);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_INPUT);
        CSS_CHECK(budget.used[LXA_BUDGET_INPUT] == 0);
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input, 2}, &consumed) == LXA_CSS_SCAN_OK
            && consumed == 2 && budget.used[LXA_BUDGET_INPUT] == 2);
        CSS_CHECK(budget.used[LXA_BUDGET_WORK] >= 2);
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input + 2, 1}, &consumed) == LXA_CSS_SCAN_LIMIT
            && consumed == 0);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_INPUT);
        size_t prior = lxa_css_tokens_length(&tokens);
        CSS_CHECK(lxa_css_byte_stream_finish(stream, &tokens)
                  == LXA_CSS_SCAN_INVALID);
        CSS_CHECK(lxa_css_tokens_length(&tokens) == prior);
        CSS_CHECK(lxa_css_tokens_init(&bounded, &allocator) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_bind_budget(&bounded, &budget) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_finish(stream, &bounded) == LXA_CSS_SCAN_OK);
        check(&bounded, 0, LXA_CSS_IDENT, 0, 2, "ab");
        lxa_css_byte_stream_destroy(stream);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY]
                  == bounded.text.capacity + bounded.entries.bytes.capacity);
        lxa_css_tokens_destroy(&bounded);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        limits.max_input_bytes = 256;
        limits.initial_work_credit = 0;
        limits.work_per_input_byte = 0;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input, 1}, &consumed) == LXA_CSS_SCAN_LIMIT
            && consumed == 0 && budget.used[LXA_BUDGET_INPUT] == 0);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_WORK);
        CSS_CHECK(lxa_css_byte_stream_finish(stream, &tokens) == LXA_CSS_SCAN_INVALID);
        lxa_css_byte_stream_destroy(stream);
    }
    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_css_byte_stream_t *stream;
        lxa_css_tokens_t bounded;
        const uint8_t name[] = "color";
        const uint8_t value[] = "\"hello\"";
        size_t consumed;
        limits.max_name_bytes = 3;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_init(&bounded, &allocator) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_bind_budget(&bounded, &budget) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {name, sizeof(name) - 1}, &consumed) == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_finish(stream, &bounded) == LXA_CSS_SCAN_LIMIT);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_NAME_BYTES);
        CSS_CHECK(lxa_css_tokens_length(&bounded) == 0);
        lxa_css_byte_stream_destroy(stream);
        lxa_css_tokens_destroy(&bounded);
        limits.max_name_bytes = 64;
        limits.max_token_bytes = 3;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_init(&bounded, &allocator) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_bind_budget(&bounded, &budget) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {value, sizeof(value) - 1}, &consumed) == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_finish(stream, &bounded) == LXA_CSS_SCAN_LIMIT);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_TOKEN_BYTES);
        CSS_CHECK(lxa_css_tokens_length(&bounded) == 0);
        lxa_css_byte_stream_destroy(stream);
        lxa_css_tokens_destroy(&bounded);
        {
            const uint8_t number[] = "12345";
            CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
            CSS_CHECK(lxa_css_tokens_init(&bounded, &allocator) == LXA_OK);
            CSS_CHECK(lxa_css_tokens_bind_budget(&bounded, &budget) == LXA_OK);
            CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                      == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_feed(stream,
                (lxa_span_t) {number, sizeof(number) - 1}, &consumed)
                == LXA_CSS_SCAN_OK);
            CSS_CHECK(lxa_css_byte_stream_finish(stream, &bounded)
                      == LXA_CSS_SCAN_LIMIT);
            CSS_CHECK(lxa_css_byte_stream_limit_reason(stream)
                      == LXA_BUDGET_TOKEN_BYTES);
            lxa_css_byte_stream_destroy(stream);
            lxa_css_tokens_destroy(&bounded);
        }
    }
    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_css_byte_stream_t *stream;
        const uint8_t input[] = "a b";
        size_t consumed, produced;
        limits.initial_work_credit = 3;
        limits.work_per_input_byte = 0;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input, sizeof(input) - 1}, &consumed)
            == LXA_CSS_SCAN_OK && consumed == 3);
        CSS_CHECK(lxa_css_byte_stream_seal(stream) == LXA_CSS_SCAN_LIMIT);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_WORK);
        CSS_CHECK(lxa_css_byte_stream_drain(stream, &tokens, 1, &produced)
                  == LXA_CSS_SCAN_INVALID && produced == 0);
        lxa_css_byte_stream_destroy(stream);
    }
    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_css_byte_stream_t *stream;
        lxa_css_tokens_t bounded;
        const uint8_t input[] = "color";
        size_t consumed, produced, baseline;
        limits.max_memory_bytes = 0;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_LIMIT && stream == NULL);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        limits.max_memory_bytes = 256u * 1024u * 1024u;
        {
            failure_t recording = {0};
            lxa_allocator_t a = {&recording, allocate, resize, release};
            CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
            CSS_CHECK(lxa_css_byte_stream_create_budget(&a, &budget, &stream)
                      == LXA_CSS_SCAN_OK);
            CSS_CHECK(recording.calls == 2 && recording.sizes[0] > 0
                      && recording.sizes[1] > 0);
            CSS_CHECK(budget.used[LXA_BUDGET_MEMORY]
                      == recording.sizes[0] + recording.sizes[1]);
            lxa_css_byte_stream_destroy(stream);
            CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
            limits.max_memory_bytes = recording.sizes[0];
            CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
            CSS_CHECK(lxa_css_byte_stream_create_budget(&a, &budget, &stream)
                      == LXA_CSS_SCAN_LIMIT && stream == NULL);
            CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
            limits.max_memory_bytes = 256u * 1024u * 1024u;
        }
        {
            failure_t failing = {0};
            lxa_allocator_t a = {&failing, allocate, resize, release};
            for (size_t attempt = 1; attempt <= 2; attempt++) {
                failing.calls = 0;
                failing.fail_at = attempt;
                CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
                CSS_CHECK(lxa_css_byte_stream_create_budget(&a, &budget, &stream)
                          == LXA_CSS_SCAN_NO_MEMORY && stream == NULL);
                CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
            }
        }
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_OK);
        baseline = budget.used[LXA_BUDGET_MEMORY];
        CSS_CHECK(baseline > 0);
        budget.limits.max_memory_bytes = baseline + 31;
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input, sizeof(input) - 1}, &consumed)
            == LXA_CSS_SCAN_LIMIT && consumed == 1);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_MEMORY);
        CSS_CHECK(budget.used[LXA_BUDGET_INPUT] == 1
                  && budget.used[LXA_BUDGET_MEMORY] == baseline);
        lxa_css_byte_stream_destroy(stream);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_OK);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == baseline);
        budget.limits.max_memory_bytes = baseline + 128;
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input, sizeof(input) - 1}, &consumed)
            == LXA_CSS_SCAN_OK && consumed == 5);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == baseline + 128);
        CSS_CHECK(lxa_css_byte_stream_seal(stream) == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_drain(stream, &tokens, 2, &produced)
                  == LXA_CSS_SCAN_INVALID && produced == 0);
        CSS_CHECK(lxa_css_tokens_init(&bounded, &allocator) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_bind_budget(&bounded, &budget) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_drain(stream, &bounded, 2, &produced)
                  == LXA_CSS_SCAN_LIMIT && produced == 0);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_MEMORY);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == baseline + 128);
        lxa_css_byte_stream_destroy(stream);
        lxa_css_tokens_destroy(&bounded);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_css_tokens_t bounded;
        lxa_css_byte_stream_t *stream;
        const uint8_t input[] = "a b";
        size_t consumed;
        limits.max_tokens = 1;
        CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_init(&bounded, &allocator) == LXA_OK);
        CSS_CHECK(lxa_css_tokens_bind_budget(&bounded, &budget) == LXA_OK);
        CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
                  == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input, sizeof(input) - 1}, &consumed)
            == LXA_CSS_SCAN_OK && consumed == sizeof(input) - 1);
        CSS_CHECK(lxa_css_byte_stream_finish(stream, &bounded) == LXA_CSS_SCAN_LIMIT);
        CSS_CHECK(lxa_css_byte_stream_limit_reason(stream) == LXA_BUDGET_TOKENS);
        CSS_CHECK(lxa_css_tokens_length(&bounded) == 0);
        CSS_CHECK(budget.used[LXA_BUDGET_TOKENS] == 1);
        lxa_css_byte_stream_destroy(stream);
        lxa_css_tokens_destroy(&bounded);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
    lxa_css_tokens_destroy(&tokens);
    partitions(&allocator);
    long_partitions(&allocator);
    return 0;
}
