/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <string.h>
#include <stdlib.h>
#include "liblexa/css.h"

typedef struct { size_t calls, fail_at; } failure_t;

static void *
allocate(void *ctx, size_t n)
{
    failure_t *f = ctx;
    return ++f->calls == f->fail_at ? NULL : malloc(n);
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

static void
run(const char *source, const char *names[], size_t expected)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t blocks;
    lxa_css_declarations_t *list = NULL;
    lxa_css_rule_t rule;
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
        strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_init(&blocks, &allocator, NULL) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(&blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_css_rule_parse(tokens, &blocks, 0, blocks.end, &rule)
              == LXA_OK && rule.has_block);
    CSS_CHECK(lxa_css_declarations_create(NULL, &blocks, &list)
              == LXA_ERROR_ARGUMENT && list == NULL);
    CSS_CHECK(lxa_css_declarations_create(&allocator, NULL, &list)
              == LXA_ERROR_ARGUMENT && list == NULL);
    CSS_CHECK(lxa_css_declarations_create(&allocator, &blocks, NULL)
              == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_declarations_length(NULL) == 0
              && lxa_css_declarations_limit_reason(NULL) == LXA_BUDGET_COUNT
              && !lxa_css_declarations_count_limit(NULL));
    CSS_CHECK(lxa_css_declarations_create(&allocator, &blocks, &list) == LXA_OK);
    CSS_CHECK(lxa_css_declarations_build(list, tokens, &blocks,
              rule.block_open + 1, rule.block_after, 50000) == LXA_OK);
    CSS_CHECK(lxa_css_declarations_length(list) == expected);
    for (size_t i = 0; i < expected; i++) {
        lxa_css_declaration_t decl;
        lxa_css_token_t token;
        lxa_span_t text;
        CSS_CHECK(lxa_css_declarations_get(list, i, &decl) == LXA_OK
                  && decl.valid);
        CSS_CHECK(lxa_css_tokens_get(tokens, decl.name, &token, &text)
                  == LXA_OK && text.length == strlen(names[i])
                  && memcmp(text.data, names[i], text.length) == 0);
        if (i == expected - 1 && expected == 3) CSS_CHECK(decl.important);
    }
    CSS_CHECK(lxa_css_declarations_get(list, expected, &(lxa_css_declaration_t){0})
              != LXA_OK);
    if (expected != 0) {
        CSS_CHECK(lxa_css_declarations_build(list, tokens, &blocks,
                  rule.block_open + 1, rule.block_after, expected - 1)
                  == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_declarations_count_limit(list)
                  && lxa_css_declarations_length(list) == 0);
        CSS_CHECK(lxa_css_declarations_build(list, tokens, &blocks,
                  rule.block_open + 1, rule.block_after, expected) == LXA_OK);
        blocks.local_budget.limits.max_work =
            blocks.local_budget.used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_declarations_build(list, tokens, &blocks,
                  rule.block_open + 1, rule.block_after, expected)
                  == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_declarations_limit_reason(list) == LXA_BUDGET_WORK
                  && lxa_css_declarations_length(list) == 0);
    }
    lxa_css_declarations_destroy(list);
    lxa_css_blocks_destroy(&blocks);
    lxa_css_tokens_destroy(tokens);
}

static void
allocation_failures(void)
{
    static const char source[] = "a{a:x;b:x;c:x;d:x;e:x;f:x;g:x;h:x;i:x;}";
    lxa_allocator_t allocator = lxa_allocator_default();
    failure_t f = {0};
    lxa_allocator_t failing = {&f, allocate, resize, release};
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t blocks;
    lxa_css_declarations_t *list = NULL;
    lxa_css_rule_t rule;
    bool succeeded = false;
    size_t retained;
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
              sizeof(source) - 1}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_init(&blocks, &allocator, NULL) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(&blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_css_rule_parse(tokens, &blocks, 0, blocks.end, &rule)
              == LXA_OK && rule.has_block);
    retained = blocks.local_budget.used[LXA_BUDGET_MEMORY];
    for (size_t attempt = 1; attempt <= 16; attempt++) {
        f.calls = 0;
        f.fail_at = attempt;
        lxa_status_t status = lxa_css_declarations_create(&failing, &blocks, &list);
        if (status == LXA_OK)
            status = lxa_css_declarations_build(list, tokens,
                &blocks, rule.block_open + 1, rule.block_after, 9);
        if (status == LXA_OK) {
            CSS_CHECK(lxa_css_declarations_length(list) == 9);
            succeeded = true;
        }
        else {
            CSS_CHECK(status == LXA_ERROR_NO_MEMORY
                      && lxa_css_declarations_length(list) == 0);
        }
        lxa_css_declarations_destroy(list);
        CSS_CHECK(blocks.local_budget.used[LXA_BUDGET_MEMORY] == retained);
        if (succeeded) break;
    }
    CSS_CHECK(succeeded);
    lxa_css_blocks_destroy(&blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    static const char *names[] = {"fill", "stroke", "--x"};
    run("a { fill: rgb(1;2); @media x { .nested { x:y } } "
        ".child { bogus:1 } stroke: blue; --x: {a:b} !important; }",
        names, 3);
    run("a{;;; @x f(1;2); invalid; @z{ color:red } }", NULL, 0);
    run("a{ color: red; }", (const char *[]) {"color"}, 1);
    allocation_failures();
    return 0;
}
