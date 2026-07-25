/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <string.h>
#include "liblexa/css.h"

static void
parse(const char *source, bool valid, bool important, size_t value_count)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t *blocks = NULL;
    lxa_css_declaration_t decl;
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
        strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_css_declaration_parse(tokens, blocks, 0, lxa_css_blocks_end(blocks), &decl)
              == LXA_OK);
    CSS_CHECK(decl.valid == valid);
    if (valid) {
        lxa_css_token_t token;
        lxa_span_t spelling;
        CSS_CHECK(decl.important == important);
        CSS_CHECK(decl.value_after >= decl.value_first);
        CSS_CHECK(decl.value_after - decl.value_first == value_count);
        CSS_CHECK(lxa_css_tokens_get(tokens, decl.name, &token, &spelling)
                  == LXA_OK && token.kind == LXA_CSS_IDENT);
        lxa_css_blocks_budget(blocks)->limits.max_work = lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_declaration_parse(tokens, blocks, 0, lxa_css_blocks_end(blocks),
                                            &decl) == LXA_ERROR_LIMIT);
        CSS_CHECK(lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK);
        CSS_CHECK(!decl.valid);
    }
    lxa_css_blocks_destroy(blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    parse(" color : rgb(0;1) ! IMPORTANT  ", true, true, 5);
    parse("color: red   ", true, false, 1);
    parse("color: {} !important", true, true, 2);
    parse("--x: {} extra", true, false, 4);
    parse("color: {} extra", false, false, 0);
    parse("width: x; height: y", false, false, 0);
    parse("width x", false, false, 0);
    parse("123: red", false, false, 0);
    parse("color: red !notimportant", true, false, 4);
    parse("color:", true, false, 0);
    {
        lxa_allocator_t allocator = lxa_allocator_default();
        lxa_css_tokens_t *tokens = NULL;
        lxa_css_blocks_t *blocks = NULL;
        lxa_css_declaration_t decl;
        const char *source = "color: red; width: 3px";
        CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
        CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
            strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
        CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
        CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
        CSS_CHECK(lxa_css_declaration_parse(tokens, blocks, 0, 4, &decl)
                  == LXA_OK && decl.valid && decl.name == 0
                  && decl.value_first == 3 && decl.value_after == 4);
        CSS_CHECK(lxa_css_declaration_parse(tokens, blocks, 5,
                                            lxa_css_blocks_end(blocks), &decl)
                  == LXA_OK && decl.valid && decl.name == 6
                  && decl.value_first == 9 && decl.value_after == 10);
        lxa_css_blocks_destroy(blocks);
        lxa_css_tokens_destroy(tokens);
    }
    return 0;
}
