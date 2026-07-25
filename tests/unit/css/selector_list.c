/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <string.h>
#include "liblexa/css.h"

static void
check(const char *source, size_t expected)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t blocks;
    lxa_css_selector_list_t *list = NULL;
    lxa_css_rule_t rule;
    size_t first, after;
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
              strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_init(&blocks, &allocator, NULL) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(&blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_css_rule_parse(tokens, &blocks, 0, blocks.end, &rule)
              == LXA_OK && rule.has_block);
    first = rule.prelude_first;
    after = rule.prelude_after;
    CSS_CHECK(lxa_css_selector_list_create(&allocator, &blocks, &list) == LXA_OK);
    CSS_CHECK(lxa_css_selector_list_build(list, tokens, &blocks,
              first, after, 5000) == LXA_OK);
    CSS_CHECK(lxa_css_selector_list_length(list) == expected);
    CSS_CHECK(lxa_css_selector_list_get(list, expected,
              &(lxa_css_selector_range_t){0}) == LXA_ERROR_ARGUMENT);
    for (size_t i = 0; i < expected; i++) {
        lxa_css_selector_range_t range;
        CSS_CHECK(lxa_css_selector_list_get(list, i, &range) == LXA_OK
                  && range.first >= first && range.after <= after
                  && range.first < range.after);
    }
    if (expected) {
        CSS_CHECK(lxa_css_selector_list_build(list, tokens, &blocks,
                  first, after, expected - 1) == LXA_ERROR_LIMIT
                  && lxa_css_selector_list_count_limit(list)
                  && lxa_css_selector_list_length(list) == 0);
        CSS_CHECK(lxa_css_selector_list_build(list, tokens, &blocks,
                  first, after, expected) == LXA_OK);
        lxa_css_selector_list_destroy(list);
        size_t retained = blocks.local_budget.used[LXA_BUDGET_MEMORY];
        blocks.local_budget.limits.max_memory_bytes = retained;
        CSS_CHECK(lxa_css_selector_list_create(&allocator, &blocks, &list)
                  == LXA_ERROR_LIMIT && list == NULL);
        CSS_CHECK(lxa_css_selector_list_length(NULL) == 0
                  && lxa_css_selector_list_limit_reason(NULL) == LXA_BUDGET_COUNT
                  && !lxa_css_selector_list_count_limit(NULL));
        lxa_css_selector_list_destroy(list);
        CSS_CHECK(blocks.local_budget.used[LXA_BUDGET_MEMORY] == retained);
        blocks.local_budget.limits.max_memory_bytes = UINT64_MAX;
        CSS_CHECK(lxa_css_selector_list_create(&allocator, &blocks, &list) == LXA_OK);
        blocks.local_budget.limits.max_memory_bytes =
            blocks.local_budget.used[LXA_BUDGET_MEMORY];
        CSS_CHECK(lxa_css_selector_list_build(list, tokens, &blocks,
                  first, after, expected) == LXA_ERROR_LIMIT
                  && lxa_css_selector_list_limit_reason(list) == LXA_BUDGET_MEMORY
                  && lxa_css_selector_list_length(list) == 0);
        lxa_css_selector_list_destroy(list);
        CSS_CHECK(blocks.local_budget.used[LXA_BUDGET_MEMORY] == retained);
        blocks.local_budget.limits.max_memory_bytes = UINT64_MAX;
        CSS_CHECK(lxa_css_selector_list_create(&allocator, &blocks, &list) == LXA_OK);
        blocks.local_budget.limits.max_work =
            blocks.local_budget.used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_selector_list_build(list, tokens, &blocks,
                  first, after, expected) == LXA_ERROR_LIMIT
                  && lxa_css_selector_list_limit_reason(list) == LXA_BUDGET_WORK
                  && lxa_css_selector_list_length(list) == 0);
    }
    lxa_css_selector_list_destroy(list);
    lxa_css_blocks_destroy(&blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    check("a, .b, #id { color:red }", 3);
    check("a:is(.b,#c), [foo='x,y'], c { color:red }", 3);
    check("a/*comment*/, b {x:y}", 2);
    check("a,,b {x:y}", 0);
    check(",a{x:y}", 0);
    check("a,{x:y}", 0);
    check("  {x:y}", 0);
    return 0;
}
