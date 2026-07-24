/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <string.h>
#include "liblexa/css.h"

static void
check(const char *source, const char *expected_uri, const char *media)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t tokens;
    lxa_css_blocks_t blocks;
    lxa_css_rule_t rule;
    lxa_css_import_t event;
    CSS_CHECK(lxa_css_tokens_init(&tokens, &allocator) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
              strlen(source)}, &tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_init(&blocks, &allocator, NULL) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(&blocks, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_rule_parse(&tokens, &blocks, 0, blocks.end, &rule)
              == LXA_OK && rule.kind == LXA_CSS_RULE_AT && !rule.has_block);
    CSS_CHECK(lxa_css_import_parse(&tokens, &blocks, &rule, &event) == LXA_OK);
    CSS_CHECK(event.valid == (expected_uri != NULL));
    if (expected_uri != NULL) {
        CSS_CHECK(event.uri.length == strlen(expected_uri)
                  && memcmp(event.uri.data, expected_uri,
                            event.uri.length) == 0);
        if (media != NULL) {
            lxa_css_token_t token;
            lxa_span_t text;
            CSS_CHECK(event.media_first < event.media_after);
            CSS_CHECK(lxa_css_tokens_get(&tokens, event.media_first,
                      &token, &text) == LXA_OK && text.length == strlen(media)
                      && memcmp(text.data, media, text.length) == 0);
        }
        else CSS_CHECK(event.media_first == event.media_after);
    }
    blocks.local_budget.limits.max_work =
        blocks.local_budget.used[LXA_BUDGET_WORK];
    CSS_CHECK(lxa_css_import_parse(&tokens, &blocks, &rule, &event)
              == LXA_ERROR_LIMIT && blocks.limit_reason == LXA_BUDGET_WORK
              && !event.valid);
    lxa_css_blocks_destroy(&blocks);
    lxa_css_tokens_destroy(&tokens);
}

int
main(void)
{
    check("@import 'child.css';", "child.css", NULL);
    check("@IMPORT url(child.css) screen;", "child.css", "screen");
    check("@import url( 'a\\2d b.css' ) screen;", "a-b.css", "screen");
    check("@import url(a\\)b.css);", "a)b.css", NULL);
    check("@import 'child.css' /* c */ screen;", "child.css", "screen");
    check("@import 'a.css'", "a.css", NULL);
    check("@import url(a b);", NULL, NULL);
    check("@import url('a.css' bad);", NULL, NULL);
    check("@import url();", NULL, NULL);
    check("@import '';", NULL, NULL);
    check("@import ;", NULL, NULL);
    check("@unknown 'a.css';", NULL, NULL);
    return 0;
}
