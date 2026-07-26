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
    lxa_css_declaration_t decl;
    size_t length = strlen(source);
    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes((lxa_span_t) {(const uint8_t *) source,
              length}, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    CSS_CHECK(lxa_css_declaration_parse(tokens, blocks,
              0, lxa_css_blocks_end(blocks), &decl) == LXA_OK && decl.valid);
    CSS_CHECK(lxa_buffer_init(&output, &allocator) == LXA_OK);
    CSS_CHECK(lxa_buffer_bind_budget(&output, lxa_css_blocks_budget(blocks)) == LXA_OK);
    lxa_span_t input = {(const uint8_t *) source, length};
    CSS_CHECK(lxa_css_value_text(tokens, blocks, decl.value_first,
              decl.value_after, input, 1024, &output) == LXA_OK);
    CSS_CHECK(output.length == strlen(expected)
              && strcmp((const char *) output.data, expected) == 0);
    if (output.length) {
        CSS_CHECK(lxa_css_value_text(tokens, blocks, decl.value_first,
                  decl.value_after, input, output.length - 1, &output)
                  == LXA_ERROR_LIMIT && output.length == 0
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_TOKEN_BYTES);
        CSS_CHECK(lxa_css_value_text(tokens, blocks, decl.value_first,
                  decl.value_after, (lxa_span_t) {input.data, 0},
                  1024, &output) == LXA_ERROR_ARGUMENT && output.length == 0);
        lxa_css_blocks_budget(blocks)->limits.max_work =
            lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_WORK];
        CSS_CHECK(lxa_css_value_text(tokens, blocks, decl.value_first,
                  decl.value_after, input, 1024, &output) == LXA_ERROR_LIMIT
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_WORK
                  && output.length == 0);
    }
    lxa_buffer_destroy(&output);
    if (expected[0] != '\0') {
        size_t retained = lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_MEMORY];
        lxa_css_blocks_budget(blocks)->limits.max_work = UINT64_MAX;
        lxa_css_blocks_budget(blocks)->limits.max_memory_bytes = retained;
        CSS_CHECK(lxa_buffer_init(&output, &allocator) == LXA_OK);
        CSS_CHECK(lxa_buffer_bind_budget(&output, lxa_css_blocks_budget(blocks)) == LXA_OK);
        CSS_CHECK(lxa_css_value_text(tokens, blocks, decl.value_first,
                  decl.value_after, input, 1024, &output) == LXA_ERROR_LIMIT
                  && lxa_css_blocks_limit_reason(blocks) == LXA_BUDGET_MEMORY
                  && output.length == 0);
        lxa_buffer_destroy(&output);
        CSS_CHECK(lxa_css_blocks_budget(blocks)->used[LXA_BUDGET_MEMORY] == retained);
    }
    lxa_css_blocks_destroy(blocks);
    lxa_css_tokens_destroy(tokens);
}

int
main(void)
{
    check("fill:url(#gradient)", "url(#gradient)");
    check("fill:UrL(#gradient)", "url(#gradient)");
    check("fill:url(#\\67 radient)", "url(#gradient)");
    check("stroke-width: calc(2px + 1px) !important",
          "calc(2px + 1px)");
    check("fill: red/**/blue", "red blue");
    check("fill: r\\65 d/**/blue", "red blue");
    check("fill: #ff0000", "#ff0000");
    check("font-family: 'a,b'", "'a,b'");
    check("opacity: 0.25", "0.25");
    check("stroke-width: 5\\70 t", "5pt");
    check("stroke-width: +1.5e2\\70 x", "+1.5e2px");
    check("color:", "");
    return 0;
}
