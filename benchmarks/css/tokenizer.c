#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "liblexa/css.h"

typedef struct {
    size_t requests;
    lxa_budget_t *budget;
    uint64_t peak;
} measurements_t;

static void *
allocate(void *ctx, size_t size)
{
    measurements_t *m = ctx;
    m->requests++;
    if (m->budget->used[LXA_BUDGET_MEMORY] > m->peak) {
        m->peak = m->budget->used[LXA_BUDGET_MEMORY];
    }
    return malloc(size);
}

static void *
resize(void *ctx, void *ptr, size_t size)
{
    measurements_t *m = ctx;
    m->requests++;
    if (m->budget->used[LXA_BUDGET_MEMORY] > m->peak) {
        m->peak = m->budget->used[LXA_BUDGET_MEMORY];
    }
    return realloc(ptr, size);
}

static void
release(void *ctx, void *ptr)
{
    (void) ctx;
    free(ptr);
}

static int
run(const uint8_t *input, size_t length, size_t chunk, size_t *token_count,
    uint64_t *digest, size_t *requests, uint64_t *peak)
{
    lxa_budget_t budget;
    measurements_t m = {0};
    lxa_allocator_t allocator = {&m, allocate, resize, release};
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_byte_stream_t *stream = NULL;
    lxa_css_scan_result_t result;
    int error = 0;
    m.budget = &budget;
    if (lxa_budget_init(&budget, NULL) != LXA_OK
        || lxa_css_tokens_create(&allocator, &budget, &tokens) != LXA_OK) {
        return 1;
    }
    if (lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
           != LXA_CSS_SCAN_OK) {
        error = 1;
        goto done;
    }
    for (size_t at = 0; at < length;) {
        size_t consumed = 0;
        size_t n = length - at < chunk ? length - at : chunk;
        result = lxa_css_byte_stream_feed(stream,
            (lxa_span_t) {input + at, n}, &consumed);
        if (result != LXA_CSS_SCAN_OK || consumed != n) {
            error = 1;
            goto done;
        }
        at += n;
    }
    if (lxa_css_byte_stream_finish(stream, tokens) != LXA_CSS_SCAN_OK) {
        error = 1;
        goto done;
    }
    *token_count = lxa_css_tokens_length(tokens);
    *digest = 0;
    for (size_t i = 0; i < *token_count; i++) {
        lxa_css_token_t token;
        lxa_span_t text;
        if (lxa_css_tokens_get(tokens, i, &token, &text) != LXA_OK) {
            error = 1;
            goto done;
        }
        *digest = (*digest * UINT64_C(1315423911))
                  ^ ((uint64_t) token.kind << 32) ^ token.length ^ text.length;
    }
    *requests = m.requests;
    *peak = m.peak;
done:
    lxa_css_byte_stream_destroy(stream);
    lxa_css_tokens_destroy(tokens);
    return error || budget.used[LXA_BUDGET_MEMORY] != 0;
}

int
main(int argc, char **argv)
{
    static const char pattern[] = "a{color:red; margin:0px} ";
    char *end;
    unsigned long requested;
    uint8_t *input;
    uint64_t expected = 0;
    size_t expected_tokens = 0;
    if (argc != 2) {
        fputs("usage: liblexa_css_tokenizer BYTES (1..262144)\n", stderr);
        return 2;
    }
    errno = 0;
    requested = strtoul(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0'
        || requested == 0 || requested > 262144) {
        fputs("invalid byte count\n", stderr);
        return 2;
    }
    size_t length = (size_t) requested;
    input = malloc(length);
    if (input == NULL) return 1;
    for (size_t i = 0; i < length; i++) {
        input[i] = (uint8_t) pattern[i % (sizeof(pattern) - 1)];
    }
    for (size_t mode = 0; mode < 3; mode++) {
        size_t chunk = mode == 0 ? length : mode == 1 ? 64 : 1;
        for (unsigned repeat = 0; repeat < 3; repeat++) {
            size_t tokens = 0, requests = 0;
            uint64_t digest = 0, peak = 0;
            clock_t start = clock();
            if (start == (clock_t) -1
                || run(input, length, chunk, &tokens, &digest,
                       &requests, &peak) != 0) {
                fputs("CSS scan failed\n", stderr);
                free(input);
                return 1;
            }
            clock_t stop = clock();
            if (stop == (clock_t) -1
                || (expected_tokens != 0
                    && (expected != digest || expected_tokens != tokens))) {
                fputs("CSS token mismatch\n", stderr);
                free(input);
                return 1;
            }
            expected = digest;
            expected_tokens = tokens;
            printf("bytes=%zu chunk=%zu repeat=%u tokens=%zu cpu_ms=%.3f alloc_calls=%zu peak_bytes=%llu digest=%llu\n",
                   length, chunk, repeat, tokens,
                   (double) (stop - start) * 1000 / CLOCKS_PER_SEC, requests,
                   (unsigned long long) peak, (unsigned long long) digest);
        }
    }
    free(input);
    return 0;
}
