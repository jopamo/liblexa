/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <string.h>
#include "css/tokens.h"

static void
parse(const char *source, lxa_css_rule_kind_t kind, bool valid,
      bool block, size_t before_block, size_t after)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t *blocks = NULL;
    lxa_css_rule_t rule;
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
        strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_css_rule_parse(tokens, blocks, 0, lxa_css_blocks_end(blocks), &rule)
              == LXA_OK);
    CSS_CHECK(rule.valid == valid && rule.kind == kind);
    if (valid) {
        CSS_CHECK(rule.has_block == block && rule.after == after);
        if (block) {
            CSS_CHECK(rule.block_open == before_block);
            CSS_CHECK(rule.block_after == (strchr(source, '}')
                ? after - 1 : lxa_css_blocks_end(blocks)));
        }
        else {
            CSS_CHECK(rule.prelude_after == before_block);
        }
        lxa_css_blocks_budget(blocks)->limits.max_work = lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_rule_parse(tokens, blocks, 0, lxa_css_blocks_end(blocks), &rule)
                  == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK && !rule.valid);
    }
    lxa_css_blocks_destroy(blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    parse("@media screen{a{x:y}}", LXA_CSS_RULE_AT, true, true, 3, 11);
    parse("@a f(1;2);z{}", LXA_CSS_RULE_AT, true, false, 7, 8);
    parse("a[x=y]{}", LXA_CSS_RULE_QUALIFIED, true, true, 6, 8);
    parse("a{color:red", LXA_CSS_RULE_QUALIFIED, true, true, 1, 5);
    parse("@x", LXA_CSS_RULE_AT, true, false, 1, 1);
    parse("broken", LXA_CSS_RULE_QUALIFIED, false, false, 0, 0);
    parse("--x:foo{bad}", LXA_CSS_RULE_QUALIFIED, false, false, 0, 0);
    {
        /* A token list without an explicit EOF can end on a matched close. */
        lxa_allocator_t allocator = lxa_allocator_default();
        lxa_css_tokens_t *tokens = NULL;
        lxa_css_blocks_t *blocks = NULL;
        lxa_css_rule_t rule;
        lxa_css_token_t token = {0};
        CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
        token.kind = LXA_CSS_AT_KEYWORD;
        CSS_CHECK(lxa_css_tokens_append(tokens, token,
                  (lxa_span_t) {(const uint8_t *) "a", 1}) == LXA_OK);
        token.kind = LXA_CSS_LEFT_BRACE;
        CSS_CHECK(lxa_css_tokens_append(tokens, token,
                  (lxa_span_t) {NULL, 0}) == LXA_OK);
        token.kind = LXA_CSS_RIGHT_BRACE;
        CSS_CHECK(lxa_css_tokens_append(tokens, token,
                  (lxa_span_t) {NULL, 0}) == LXA_OK);
        CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
        CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
        CSS_CHECK(lxa_css_rule_parse(tokens, blocks, 0, 3, &rule)
                  == LXA_OK && rule.valid && rule.has_block
                  && rule.block_open == 1 && rule.block_after == 2
                  && rule.after == 3);
        lxa_css_blocks_destroy(blocks);
        lxa_css_tokens_destroy(tokens);
    }
    return 0;
}
