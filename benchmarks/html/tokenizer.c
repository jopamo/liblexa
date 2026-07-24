/* Copyright (C) 2026 Paul Moses */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "html/tokenizer.h"
#include "html/input_encoding.h"

typedef struct {
    lxa_budget_t *budget;
    size_t allocations, peak;
    uint64_t digest;
} measurement_t;

static void
sample(measurement_t *m)
{
    size_t used = m->budget->used[LXA_BUDGET_MEMORY];
    if (used > m->peak) m->peak = used;
}

static void *
allocate(void *context, size_t size)
{
    measurement_t *m = context;
    m->allocations++;
    sample(m);
    return malloc(size);
}

static void *
resize(void *context, void *ptr, size_t size)
{
    measurement_t *m = context;
    m->allocations++;
    sample(m);
    return realloc(ptr, size);
}

static void
release(void *context, void *ptr)
{
    sample(context);
    free(ptr);
}

static bool
named(lxa_span_t name, const char *literal)
{
    size_t length = strlen(literal);
    return name.length == length
           && memcmp(name.data, literal, length) == 0;
}

static lxa_status_t
on_token(void *context, lxa_html_tokenizer_t *tokenizer,
         const lxa_html_token_t *token)
{
    measurement_t *m = context;
    size_t i;
    sample(m);
    m->digest = m->digest * UINT64_C(1315423911)
                + token->kind + token->name.length
                + token->data.length + token->attribute_count;
    for (i = 0; i < token->attribute_count; i++) {
        m->digest += token->attributes[i].name.length
                     + token->attributes[i].value.length;
    }
    if (token->kind == LXA_HTML_START_TAG) {
        lxa_html_text_mode_t mode;
        if (named(token->name, "script")) mode = LXA_HTML_SCRIPT_DATA;
        else if (named(token->name, "title")
                 || named(token->name, "textarea")) mode = LXA_HTML_RCDATA;
        else if (named(token->name, "style")
                 || named(token->name, "xmp")
                 || named(token->name, "iframe")
                 || named(token->name, "noembed")) mode = LXA_HTML_RAWTEXT;
        else return LXA_OK;
        if (lxa_html_tokenizer_set_text_mode(tokenizer, mode, token->name)
            != LXA_HTML_OK) return LXA_ERROR_ARGUMENT;
    }
    return LXA_OK;
}

static int
run(lxa_span_t input, uint64_t *digest, size_t *allocations, size_t *peak)
{
    lxa_budget_t budget;
    measurement_t m = {0};
    lxa_allocator_t allocator = {&m, allocate, resize, release};
    lxa_buffer_t decoded;
    lxa_html_tokenizer_t *tokenizer = NULL;
    const lxa_encoding_t *encoding = NULL;
    lxa_limit_reason_t reason;
    size_t prefix = 0, malformed = 0;
    int failed = 1;
    if (lxa_budget_init(&budget, NULL) != LXA_OK) return 1;
    m.budget = &budget;
    if (lxa_html_input_select(input, 16u * 1024u * 1024u,
                              &encoding, &prefix) != LXA_HTML_INPUT_OK
        || lxa_buffer_init(&decoded, &allocator) != LXA_OK) return 1;
    if (lxa_buffer_bind_budget(&decoded, &budget) != LXA_OK) {
        lxa_buffer_destroy(&decoded);
        return 1;
    }
    if (lxa_html_input_decode(input, encoding, prefix, &budget, &decoded,
                              &malformed, &reason) != LXA_HTML_INPUT_OK
        || malformed != 0) goto done;
    sample(&m);
    if (lxa_html_tokenizer_create_with_budget(&allocator, on_token, NULL,
                                               &m, &budget, &tokenizer)
        != LXA_OK) goto done;
    if (lxa_html_tokenizer_feed_prepaid(tokenizer,
            (lxa_span_t) {decoded.data, decoded.length}) != LXA_HTML_OK
        || lxa_html_tokenizer_finish(tokenizer) != LXA_HTML_OK) goto done;
    sample(&m);
    *digest = m.digest;
    failed = 0;
done:
    lxa_html_tokenizer_destroy(tokenizer);
    lxa_buffer_destroy(&decoded);
    *allocations = m.allocations;
    *peak = m.peak;
    return failed || budget.used[LXA_BUDGET_MEMORY] != 0;
}

int
main(int argc, char **argv)
{
    FILE *file;
    uint8_t *input;
    long length;
    unsigned long count;
    char *end;
    uint64_t expected = 0, digest;
    size_t allocations, peak;
    struct timespec start, stop;
    if (argc != 3) {
        fputs("usage: liblexa_html_tokenizer FILE ITERATIONS\n", stderr);
        return 2;
    }
    errno = 0;
    count = strtoul(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\0'
        || count == 0 || count > 100000) return 2;
    file = fopen(argv[1], "rb");
    if (file == NULL) return 1;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0
        || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 2;
    }
    input = malloc((size_t) length);
    if (input == NULL) {
        fclose(file);
        return 1;
    }
    size_t received = fread(input, 1, (size_t) length, file);
    int closed = fclose(file);
    if (received != (size_t) length || closed != 0) {
        free(input);
        return 1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        free(input);
        return 1;
    }
    for (unsigned long i = 0; i < count; i++) {
        if (run((lxa_span_t) {input, (size_t) length},
                &digest, &allocations, &peak) != 0
            || (i != 0 && digest != expected)) {
            fputs("HTML decode/tokenizer failed\n", stderr);
            free(input);
            return 1;
        }
        expected = digest;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &stop) != 0) {
        free(input);
        return 1;
    }
    printf("bytes=%ld iterations=%lu wall_ms=%.3f allocations=%zu "
           "peak_budget_bytes=%zu digest=%" PRIu64 "\n",
           length, count,
           (stop.tv_sec - start.tv_sec) * 1000.0
           + (stop.tv_nsec - start.tv_nsec) / 1000000.0,
           allocations, peak, digest);
    free(input);
    return 0;
}
