/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <string.h>
#include "liblexa/css.h"

static void
check(const char *source, size_t expected)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t tokens;
    lxa_css_blocks_t blocks;
    lxa_css_selector_list_t list;
    lxa_css_rule_t rule;
    size_t first, after;
    CSS_CHECK(lxa_css_tokens_init(&tokens, &allocator) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
              strlen(source)}, &tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_init(&blocks, &allocator, NULL) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(&blocks, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_rule_parse(&tokens, &blocks, 0, blocks.end, &rule)
              == LXA_OK && rule.has_block);
    first = rule.prelude_first;
    after = rule.prelude_after;
    CSS_CHECK(lxa_css_selector_list_init(&list, &allocator, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_selector_list_build(&list, &tokens, &blocks,
              first, after, 5000) == LXA_OK);
    CSS_CHECK(list.valid == (expected != 0));
    CSS_CHECK(lxa_vector_length(&list.ranges) == expected);
    for (size_t i = 0; i < expected; i++) {
        lxa_css_selector_range_t range;
        CSS_CHECK(lxa_css_selector_list_get(&list, i, &range) == LXA_OK
                  && range.first >= first && range.after <= after
                  && range.first < range.after);
    }
    if (expected) {
        CSS_CHECK(lxa_css_selector_list_build(&list, &tokens, &blocks,
                  first, after, expected - 1) == LXA_ERROR_LIMIT
                  && list.count_limit && !list.valid
                  && lxa_vector_length(&list.ranges) == 0);
        CSS_CHECK(lxa_css_selector_list_build(&list, &tokens, &blocks,
                  first, after, expected) == LXA_OK && list.valid);
        lxa_css_selector_list_destroy(&list);
        size_t retained = blocks.local_budget.used[LXA_BUDGET_MEMORY];
        blocks.local_budget.limits.max_memory_bytes = retained;
        CSS_CHECK(lxa_css_selector_list_init(&list, &allocator, &blocks) == LXA_OK);
        CSS_CHECK(lxa_css_selector_list_build(&list, &tokens, &blocks,
                  first, after, expected) == LXA_ERROR_LIMIT
                  && list.limit_reason == LXA_BUDGET_MEMORY && !list.valid
                  && lxa_vector_length(&list.ranges) == 0);
        lxa_css_selector_list_destroy(&list);
        CSS_CHECK(blocks.local_budget.used[LXA_BUDGET_MEMORY] == retained);
        blocks.local_budget.limits.max_memory_bytes = UINT64_MAX;
        CSS_CHECK(lxa_css_selector_list_init(&list, &allocator, &blocks) == LXA_OK);
        blocks.local_budget.limits.max_work =
            blocks.local_budget.used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_selector_list_build(&list, &tokens, &blocks,
                  first, after, expected) == LXA_ERROR_LIMIT
                  && list.limit_reason == LXA_BUDGET_WORK && !list.valid
                  && lxa_vector_length(&list.ranges) == 0);
    }
    lxa_css_selector_list_destroy(&list);
    lxa_css_blocks_destroy(&blocks);
    lxa_css_tokens_destroy(&tokens);
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
