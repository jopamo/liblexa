/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <string.h>
#include "liblexa/css.h"

static void
check(const char *source, bool expected)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t blocks;
    bool applies = !expected;
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
              strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_init(&blocks, &allocator, NULL) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(&blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_css_media_screen(tokens, &blocks, 0, blocks.end,
              &applies) == LXA_OK && applies == expected);
    if (blocks.end != 0) {
        blocks.local_budget.limits.max_work =
            blocks.local_budget.used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_media_screen(tokens, &blocks, 0, blocks.end,
                  &applies) == LXA_ERROR_LIMIT && !applies
                  && blocks.limit_reason == LXA_BUDGET_WORK);
    }
    lxa_css_blocks_destroy(&blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    check("", true); /* unqualified import */
    check("screen", true);
    check("  ScReEn  ", true);
    check("all", true);
    check("only screen", true);
    check("not print", true);
    check("print, screen", true);
    check("print", false);
    check("not screen", false);
    check("only print", false);
    check("print, (unknown:1)", false);
    check("screen and (min-width:1px)", false);
    check("foo, print", false);
    check("not foo", true);
    check("only foo", false);
    check("screen print", false);
    return 0;
}
