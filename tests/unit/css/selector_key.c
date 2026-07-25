/* Copyright (C) 2026 Paul Moses */
#include "check.h"
#include <string.h>
#include "liblexa/css.h"

static void
check(const char *source, const char *expected)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t *blocks = NULL;
    lxa_buffer_t output;
    bool supported = true;
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
        strlen(source)}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_buffer_init(&output, &allocator) == LXA_OK);
    CSS_CHECK(lxa_buffer_bind_budget(&output, lxa_css_blocks_budget(blocks)) == LXA_OK);
    CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, lxa_css_blocks_end(blocks),
              512, &output, &supported) == LXA_OK);
    CSS_CHECK(supported == (expected != NULL));
    if (expected != NULL) {
        CSS_CHECK(output.length == strlen(expected)
                  && strcmp((const char *) output.data, expected) == 0);
        CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, lxa_css_blocks_end(blocks),
                  output.length - 1, &output, &supported) == LXA_ERROR_LIMIT
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_NAME_BYTES
                  && output.length == 0 && !supported);
        lxa_buffer_destroy(&output);
        CSS_CHECK(lxa_buffer_init(&output, &allocator) == LXA_OK);
        CSS_CHECK(lxa_buffer_bind_budget(&output, lxa_css_blocks_budget(blocks)) == LXA_OK);
        lxa_css_blocks_budget(blocks)->limits.max_memory_bytes =
            lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_MEMORY];
        CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, lxa_css_blocks_end(blocks),
                  512, &output, &supported) == LXA_ERROR_LIMIT
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_MEMORY
                  && output.length == 0 && !supported);
        lxa_css_blocks_budget(blocks)->limits.max_memory_bytes = UINT64_MAX;
    }
    else CSS_CHECK(output.length == 0);
    if (lxa_css_blocks_end(blocks) != 0) {
        lxa_css_blocks_budget(blocks)->limits.max_work = lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, lxa_css_blocks_end(blocks),
                  512, &output, &supported) == LXA_ERROR_LIMIT
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK && !supported);
    }
    lxa_buffer_destroy(&output);
    lxa_css_blocks_destroy(blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    check("*", "*");
    check(" rect ", "rect");
    check(".klass", ".klass");
    check("#ident", "#ident");
    check("rect.klass", "rect.klass");
    check(".klass#ident", ".klass#ident");
    check("rect#ident", "rect#ident");
    check("rect#ident.klass", "rect.klass#ident");
    check("rect\\31 .foo", "rect1.foo");
    check("rect/**/.klass", "rect.klass");
    check("#ident.klass", ".klass#ident");
    check("rect .klass", NULL);
    check("rect:hover", NULL);
    check("rect[fill]", NULL);
    check("rect,.klass", NULL);
    check("rect.klass.extra", NULL);
    check("#a#b", NULL);
    check("*#ident", NULL);
    check(".", NULL);
    check("", NULL);
    return 0;
}
