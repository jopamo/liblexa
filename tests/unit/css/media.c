#include "check.h"
#include <string.h>
#include "liblexa/css.h"

static void
check(const char *source, bool expected, lxa_css_syntax_t syntax)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t *blocks = NULL;
    lxa_css_media_t media = {LXA_CSS_SYNTAX_SUPPORTED, !expected};
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
              strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_css_media_screen(tokens, blocks, 0, lxa_css_blocks_end(blocks),
              &media) == LXA_OK && media.applies == expected
              && media.syntax == syntax);
    if (lxa_css_blocks_end(blocks) != 0) {
        lxa_css_blocks_budget(blocks)->limits.max_work =
            lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_media_screen(tokens, blocks, 0, lxa_css_blocks_end(blocks),
                  &media) == LXA_ERROR_LIMIT && !media.applies
                  && media.syntax == LXA_CSS_SYNTAX_INVALID
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK);
    }
    lxa_css_blocks_destroy(blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    check("", true, LXA_CSS_SYNTAX_SUPPORTED); /* unqualified import */
    check("screen", true, LXA_CSS_SYNTAX_SUPPORTED);
    check("  ScReEn  ", true, LXA_CSS_SYNTAX_SUPPORTED);
    check("all", true, LXA_CSS_SYNTAX_SUPPORTED);
    check("only screen", true, LXA_CSS_SYNTAX_SUPPORTED);
    check("not print", true, LXA_CSS_SYNTAX_SUPPORTED);
    check("print, screen", true, LXA_CSS_SYNTAX_SUPPORTED);
    check("print", false, LXA_CSS_SYNTAX_SUPPORTED);
    check("not screen", false, LXA_CSS_SYNTAX_SUPPORTED);
    check("only print", false, LXA_CSS_SYNTAX_SUPPORTED);
    check("print, (unknown:1)", false, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("screen and (min-width:1px)", false, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("foo, print", false, LXA_CSS_SYNTAX_SUPPORTED);
    check("not foo", true, LXA_CSS_SYNTAX_SUPPORTED);
    check("only foo", false, LXA_CSS_SYNTAX_SUPPORTED);
    check("screen print", false, LXA_CSS_SYNTAX_INVALID);
    check("not not", false, LXA_CSS_SYNTAX_INVALID);
    check("not only", false, LXA_CSS_SYNTAX_INVALID);
    check("not and", false, LXA_CSS_SYNTAX_INVALID);
    check("not", false, LXA_CSS_SYNTAX_INVALID);
    check(" ", false, LXA_CSS_SYNTAX_INVALID);
    check(",screen", true, LXA_CSS_SYNTAX_INVALID);
    check("screen,not not", true, LXA_CSS_SYNTAX_INVALID);
    check("screen,(color)", true, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("(color),screen,", true, LXA_CSS_SYNTAX_UNSUPPORTED);
    return 0;
}
