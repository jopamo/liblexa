#include "check.h"
#include "css/blocks.h"
#include "css/tokens.h"

int
main(void)
{
    static const uint8_t source[] = "@import \"a\";x:f(1)";
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_css_tokens_t *tokens = NULL;
    lxa_css_blocks_t *blocks = NULL;
    lxa_css_declarations_t *declarations = NULL;
    lxa_css_selector_list_t *selectors = NULL;
    lxa_css_rule_t rule, parsed;
    lxa_css_declaration_t declaration;
    lxa_css_import_t import;
    lxa_css_media_t media;
    lxa_css_syntax_t syntax;
    lxa_buffer_t output;
    lxa_span_t input = {source, sizeof(source) - 1};
    size_t end, first;

    CSS_CHECK(lxa_css_tokens_create(&allocator, NULL, &tokens) == LXA_OK);
    CSS_CHECK(lxa_css_scan_bytes(input, tokens) == LXA_CSS_SCAN_OK);
    CSS_CHECK(lxa_css_blocks_create(&allocator, NULL, &blocks) == LXA_OK);
    CSS_CHECK(lxa_css_rule_parse(tokens, blocks, 0, 1, &rule)
              == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_blocks_build(blocks, tokens) == LXA_OK);
    end = lxa_css_blocks_end(blocks);
    CSS_CHECK(lxa_css_rule_parse(tokens, blocks, 0, end, &rule) == LXA_OK);
    first = rule.after;
    CSS_CHECK(lxa_css_declarations_create(&allocator, blocks, &declarations)
              == LXA_OK);
    CSS_CHECK(lxa_css_selector_list_create(&allocator, blocks, &selectors)
              == LXA_OK);
    CSS_CHECK(lxa_buffer_init(&output, &allocator) == LXA_OK);
    CSS_CHECK(lxa_buffer_bind_budget(&output, lxa_css_blocks_budget(blocks))
              == LXA_OK);

    /* A caller range ending inside f() is invalid, not an internal failure. */
    CSS_CHECK(lxa_css_rule_parse(tokens, blocks, first, end - 1, &parsed)
              == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_declaration_parse(tokens, blocks, first, end - 1,
              &declaration) == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_declarations_build(declarations, tokens, blocks,
              first, end - 1, 10) == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_selector_list_build(selectors, tokens, blocks,
              first, end - 1, 10) == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_media_screen(tokens, blocks, first, end - 1, &media)
              == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_declaration_parse(tokens, blocks, first, end,
              &declaration) == LXA_OK && declaration.valid);
    CSS_CHECK(lxa_css_import_parse(tokens, blocks, &rule, &import) == LXA_OK
              && import.syntax == LXA_CSS_SYNTAX_SUPPORTED);
    parsed = rule;
    parsed.name = first;
    CSS_CHECK(lxa_css_import_parse(tokens, blocks, &parsed, &import)
              == LXA_ERROR_ARGUMENT);

#ifdef NDEBUG
    {
        void *slot;
        CSS_CHECK(lxa_vector_at(&tokens->entries, 0, &slot) == LXA_OK);
        lxa_css_token_entry_t *entry = slot;
        lxa_css_token_entry_t saved = *entry;
        entry->text_length = SIZE_MAX;
        CSS_CHECK(lxa_css_rule_parse(tokens, blocks, 0, end, &parsed)
                  == LXA_ERROR_INTERNAL);
        CSS_CHECK(lxa_css_declaration_parse(tokens, blocks, 0, end,
                  &declaration) == LXA_ERROR_INTERNAL);
        CSS_CHECK(lxa_css_declarations_build(declarations, tokens, blocks,
                  0, end, 10) == LXA_ERROR_INTERNAL);
        CSS_CHECK(lxa_css_declarations_length(declarations) == 0);
        CSS_CHECK(lxa_css_selector_list_build(selectors, tokens, blocks,
                  0, end, 10) == LXA_ERROR_INTERNAL);
        CSS_CHECK(lxa_css_selector_list_length(selectors) == 0);
        CSS_CHECK(lxa_css_selector_key(tokens, blocks, 0, end, 1024,
                  &output, &syntax) == LXA_ERROR_INTERNAL);
        CSS_CHECK(output.length == 0 && syntax == LXA_CSS_SYNTAX_INVALID);
        CSS_CHECK(lxa_css_import_parse(tokens, blocks, &rule, &import)
                  == LXA_ERROR_INTERNAL);
        CSS_CHECK(lxa_css_media_screen(tokens, blocks, 0, end, &media)
                  == LXA_ERROR_INTERNAL);
        CSS_CHECK(!media.applies && media.syntax == LXA_CSS_SYNTAX_INVALID);
        CSS_CHECK(lxa_css_value_text(tokens, blocks, 0, end, input, 1024,
                  &output) == LXA_ERROR_INTERNAL && output.length == 0);
        *entry = saved;
    }
#else
    (void) syntax;
#endif

    /* Clearing tokens invalidates caller-held ranges without an assertion. */
    lxa_css_tokens_clear(tokens);
    CSS_CHECK(lxa_css_rule_parse(tokens, blocks, 0, end, &parsed)
              == LXA_ERROR_ARGUMENT);
    CSS_CHECK(lxa_css_import_parse(tokens, blocks, &rule, &import)
              == LXA_ERROR_ARGUMENT);
    lxa_buffer_destroy(&output);
    lxa_css_selector_list_destroy(selectors);
    lxa_css_declarations_destroy(declarations);
    lxa_css_blocks_destroy(blocks);
    lxa_css_tokens_destroy(tokens);
    return 0;
}
