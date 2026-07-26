#include "check.h"
#include <string.h>
#include "liblexa/css.h"

static void
check(const char *source, const char *expected, lxa_css_syntax_t expected_syntax)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t *blocks = NULL;
    lxa_buffer_t output;
    lxa_css_syntax_t syntax = LXA_CSS_SYNTAX_SUPPORTED;
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
        strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_buffer_init(&output, &allocator) == LXA_OK);
    CSS_CHECK(lxa_buffer_bind_budget(&output, lxa_css_blocks_budget(blocks)) == LXA_OK);
    CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, lxa_css_blocks_end(blocks),
              512, &output, &syntax) == LXA_OK);
    CSS_CHECK(syntax == expected_syntax);
    if (expected != NULL) {
        CSS_CHECK(output.length == strlen(expected)
                  && strcmp((const char *) output.data, expected) == 0);
        CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, lxa_css_blocks_end(blocks),
                  output.length - 1, &output, &syntax) == LXA_ERROR_LIMIT
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_NAME_BYTES
                  && output.length == 0 && syntax == LXA_CSS_SYNTAX_INVALID);
        lxa_buffer_destroy(&output);
        CSS_CHECK(lxa_buffer_init(&output, &allocator) == LXA_OK);
        CSS_CHECK(lxa_buffer_bind_budget(&output, lxa_css_blocks_budget(blocks)) == LXA_OK);
        lxa_css_blocks_budget(blocks)->limits.max_memory_bytes =
            lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_MEMORY];
        CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, lxa_css_blocks_end(blocks),
                  512, &output, &syntax) == LXA_ERROR_LIMIT
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_MEMORY
                  && output.length == 0 && syntax == LXA_CSS_SYNTAX_INVALID);
        lxa_css_blocks_budget(blocks)->limits.max_memory_bytes = UINT64_MAX;
    }
    else CSS_CHECK(output.length == 0);
    if (lxa_css_blocks_end(blocks) != 0) {
        lxa_css_blocks_budget(blocks)->limits.max_work = lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, lxa_css_blocks_end(blocks),
                  512, &output, &syntax) == LXA_ERROR_LIMIT
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK
                  && syntax == LXA_CSS_SYNTAX_INVALID);
    }
    lxa_buffer_destroy(&output);
    lxa_css_blocks_destroy(blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    check("*", "*", LXA_CSS_SYNTAX_SUPPORTED);
    check(" rect ", "rect", LXA_CSS_SYNTAX_SUPPORTED);
    check(".klass", ".klass", LXA_CSS_SYNTAX_SUPPORTED);
    check("#ident", "#ident", LXA_CSS_SYNTAX_SUPPORTED);
    check("rect.klass", "rect.klass", LXA_CSS_SYNTAX_SUPPORTED);
    check(".klass#ident", ".klass#ident", LXA_CSS_SYNTAX_SUPPORTED);
    check("rect#ident", "rect#ident", LXA_CSS_SYNTAX_SUPPORTED);
    check("rect#ident.klass", "rect.klass#ident", LXA_CSS_SYNTAX_SUPPORTED);
    check("rect\\31 .foo", "rect1.foo", LXA_CSS_SYNTAX_SUPPORTED);
    check("rect/**/.klass", "rect.klass", LXA_CSS_SYNTAX_SUPPORTED);
    check("#ident.klass", ".klass#ident", LXA_CSS_SYNTAX_SUPPORTED);
    check("rect .klass", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("rect:hover", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("rect[fill]", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("rect,.klass", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("rect.klass.extra", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("#a#b", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("*#ident", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    check(".", NULL, LXA_CSS_SYNTAX_INVALID);
    check("", NULL, LXA_CSS_SYNTAX_INVALID);
    check(". a", NULL, LXA_CSS_SYNTAX_INVALID);
    check("..a", NULL, LXA_CSS_SYNTAX_INVALID);
    check(".a.", NULL, LXA_CSS_SYNTAX_INVALID);
    check("#123", NULL, LXA_CSS_SYNTAX_INVALID);
    check("rect*", NULL, LXA_CSS_SYNTAX_INVALID);
    check("42", NULL, LXA_CSS_SYNTAX_INVALID);
    check("rect|a", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    check("rect>circle", NULL, LXA_CSS_SYNTAX_UNSUPPORTED);
    return 0;
}
