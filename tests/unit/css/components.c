/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <stdlib.h>
#include "css/blocks.h"

typedef struct {
    size_t calls;
    size_t fail_at;
    size_t live;
} failure_t;

static void *
allocate(void *ctx, size_t n)
{
    failure_t *f = ctx;
    if (++f->calls == f->fail_at) return NULL;
    void *ptr = malloc(n);
    if (ptr != NULL) f->live++;
    return ptr;
}

static void *
resize(void *ctx, void *ptr, size_t n)
{
    failure_t *f = ctx;
    bool fresh = ptr == NULL;
    if (++f->calls == f->fail_at) return NULL;
    void *next = realloc(ptr, n);
    if (next != NULL && fresh) f->live++;
    return next;
}

static void
release(void *ctx, void *ptr)
{
    failure_t *f = ctx;
    if (ptr != NULL) f->live--;
    free(ptr);
}

static void
check(lxa_css_blocks_t *blocks, size_t i, size_t open, size_t close,
      uint32_t parent, lxa_css_token_kind_t kind)
{
    lxa_css_block_t b;
    CSS_CHECK(lxa_css_blocks_get(blocks, i, &b) == LXA_OK);
    CSS_CHECK(b.open == open && b.close == close && b.parent == parent
              && b.opener == kind);
}

int
main(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_limits_t limits = lxa_limits_default();
    lxa_budget_t budget;
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t *blocks = NULL;
    lxa_css_component_range_t range;
    const uint8_t input[] = "f([)]a})";
    /* Parse bytes with the same shared budget used by the block index. */
    lxa_css_byte_stream_t *stream;
    size_t consumed, retained;
    CSS_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CSS_CHECK(lxa_css_tokens_create(&allocator, &budget, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_byte_stream_create_budget(&allocator, &budget, &stream)
              == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_byte_stream_feed(stream,
        (lxa_span_t) {input, sizeof(input) - 1}, &consumed) == LXA_CSS_SCAN_OK
        && consumed == sizeof(input) - 1);
    CSS_CHECK(lxa_css_byte_stream_finish(stream, tokens) == LXA_CSS_SCAN_OK);
    lxa_css_byte_stream_destroy(stream);
    CSS_CHECK(lxa_css_blocks_create(NULL, &budget, &blocks)
              == LXA_ERROR_ARGUMENT && blocks == NULL);
    CSS_CHECK(lxa_css_blocks_create(&allocator, &budget, NULL)
              == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_blocks_end(NULL) == 0
              && lxa_css_blocks_budget(NULL) == NULL
              && lxa_css_blocks_limit_reason(NULL) == LXA_BUDGET_COUNT);
    retained = budget.used[LXA_BUDGET_MEMORY];
    budget.limits.max_memory_bytes = retained;
    CSS_CHECK(lxa_css_blocks_create(&allocator, &budget, &blocks)
              == LXA_ERROR_LIMIT && blocks == NULL);
    CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
    lxa_css_blocks_destroy(blocks);
    budget.limits.max_memory_bytes = limits.max_memory_bytes;
    CSS_CHECK(lxa_css_blocks_create(&allocator, &budget, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_vector_length(&blocks->entries) == 2);
    check(blocks, 0, 0, 6, LXA_CSS_BLOCK_NONE, LXA_CSS_FUNCTION);
    check(blocks, 1, 1, 3, 0, LXA_CSS_LEFT_SQUARE);
    CSS_CHECK(budget.used[LXA_BUDGET_CSS_NESTING] == 0);
    CSS_CHECK(lxa_css_blocks_end(blocks) == 7 && blocks->ready);
    CSS_CHECK(lxa_css_blocks_next(blocks, 0, &range) == LXA_OK);
    CSS_CHECK(range.first == 0 && range.after == 7 && range.closed);
    CSS_CHECK(lxa_css_blocks_next(blocks, 1, &range) == LXA_OK);
    CSS_CHECK(range.first == 1 && range.after == 4 && range.closed);
    CSS_CHECK(lxa_css_blocks_next(blocks, 2, &range) == LXA_OK);
    CSS_CHECK(range.first == 2 && range.after == 3); /* mismatched closer */
    CSS_CHECK(lxa_css_blocks_next(blocks, 4, &range) == LXA_OK);
    CSS_CHECK(range.first == 4 && range.after == 5);
    CSS_CHECK(lxa_css_blocks_next(blocks, 7, &range) == LXA_ERROR_ARGUMENT);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    range = (lxa_css_component_range_t) {99, 99, false};
    CSS_CHECK(lxa_css_blocks_next(blocks, 0, &range) == LXA_ERROR_LIMIT);
    CSS_CHECK(lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK);
    CSS_CHECK(range.first == 99 && range.after == 99);
    budget.limits.max_work = UINT64_MAX;
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_vector_length(&blocks->entries) == 2);
    CSS_CHECK(lxa_css_blocks_get(blocks, 2, &(lxa_css_block_t){0})
              == LXA_ERROR_ARGUMENT);
    {
        lxa_css_blocks_t *fresh = NULL;
        size_t before = budget.used[LXA_BUDGET_MEMORY];
        CSS_CHECK(lxa_css_blocks_create(&allocator, &budget, &fresh) == LXA_OK);
        budget.limits.max_css_nesting = 0;
        CSS_CHECK(lxa_css_blocks_build(fresh, tokens) == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_blocks_limit_reason(fresh) == LXA_BUDGET_CSS_NESTING);
        CSS_CHECK(lxa_vector_length(&fresh->entries) == 0);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == before + sizeof(*fresh));
        lxa_css_blocks_destroy(fresh);
        CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == before);
        budget.limits.max_css_nesting = 4096;
    }
    retained = budget.used[LXA_BUDGET_MEMORY];
    budget.limits.max_css_nesting = 1;
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_ERROR_LIMIT);
    CSS_CHECK(lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_CSS_NESTING);
    CSS_CHECK(lxa_vector_length(&blocks->entries) == 0);
    CSS_CHECK(!blocks->ready && lxa_css_blocks_end(blocks) == 0);
    CSS_CHECK(lxa_css_blocks_next(blocks, 0, &range) == LXA_ERROR_ARGUMENT);
    CSS_CHECK(budget.used[LXA_BUDGET_CSS_NESTING] == 0);
    CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
    budget.limits.max_css_nesting = 4096;
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + 1;
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_ERROR_LIMIT);
    CSS_CHECK(lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK);
    CSS_CHECK(lxa_vector_length(&blocks->entries) == 0);
    budget.limits.max_work = UINT64_MAX;
    budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
    lxa_css_blocks_destroy(blocks);
    /* After destroy, only the caller-owned token list retains capacity. */
    CSS_CHECK(lxa_css_blocks_create(&allocator, &budget, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_ERROR_LIMIT);
    CSS_CHECK(lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_MEMORY);
    CSS_CHECK(lxa_vector_length(&blocks->entries) == 0);
    lxa_css_blocks_destroy(blocks);
    lxa_css_tokens_destroy(tokens);
    CSS_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);

    /* Unclosed blocks close at EOF; closing tokens of the wrong kind remain
     * simple values rather than terminating a block. */
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) "{(", 2},
                                  tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    check(blocks, 0, 0, 2, LXA_CSS_BLOCK_NONE, LXA_CSS_LEFT_BRACE);
    check(blocks, 1, 1, 2, 0, LXA_CSS_LEFT_PAREN);
    CSS_CHECK(lxa_css_blocks_next(blocks, 0, &range) == LXA_OK);
    CSS_CHECK(range.first == 0 && range.after == 2 && !range.closed);
    CSS_CHECK(lxa_css_blocks_next(blocks, 1, &range) == LXA_OK);
    CSS_CHECK(range.first == 1 && range.after == 2 && !range.closed);
    lxa_css_blocks_budget(blocks)->limits.max_work = 4;
    /* Three tokens plus two EOF closures exceed four units of work. */
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_ERROR_LIMIT);
    CSS_CHECK(lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK);
    CSS_CHECK(lxa_vector_length(&blocks->entries) == 0);
    CSS_CHECK(lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_CSS_NESTING] == 0);
    lxa_css_blocks_destroy(blocks);
    {
        failure_t f = {0};
        lxa_allocator_t failing = {&f, allocate, resize, release};
        for (size_t attempt = 1; attempt <= 4; attempt++) {
            f.calls = 0;
            f.fail_at = attempt;
            lxa_status_t status = lxa_css_blocks_create(&failing, NULL, &blocks);
            if (status == LXA_OK) status = lxa_css_blocks_build(blocks, tokens);
            CSS_CHECK(status == LXA_ERROR_NO_MEMORY);
            CSS_CHECK(lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_COUNT);
            if (blocks != NULL) {
                CSS_CHECK(lxa_vector_length(&blocks->entries) == 0);
                CSS_CHECK(lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_CSS_NESTING] == 0);
            }
            lxa_css_blocks_destroy(blocks);
            CSS_CHECK(f.live == 0);
        }
    }
    lxa_css_tokens_destroy(tokens);
    return 0;
}
