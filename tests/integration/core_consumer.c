/* Copyright (C) 2026 Paul Moses
 * Also compiles as an external consumer using only installed headers.
 */
#include <stdint.h>
#include <stdlib.h>
#include "liblexa/base.h"
#include "liblexa/encoding.h"
#include "liblexa/dom.h"
#include "liblexa/css.h"

int
main(void)
{
    const uint8_t name[] = {'i', 'd'};
    lxa_span_t key = {name, sizeof(name)};
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_budget_t budget;
    lxa_table_t *table = NULL;
    lxa_decoder_t *decoder = NULL;
    lxa_dom_nodes_t *nodes = NULL;
    lxa_dom_ref_t element, attribute, found_attribute;
    lxa_span_t attribute_name, attribute_value;
    const uint8_t invalid_utf8[] = {0xc3};
    const uint8_t legacy_byte[] = {0xe9};
    lxa_encoding_progress_t progress;
    uint32_t scalar;
    bool found = false;
    void *value = NULL;
    if (lxa_budget_init(&budget, NULL) != LXA_OK
        || lxa_table_create_with_budget(&allocator, &budget, NULL,
                                        NULL, &table) != LXA_OK
        || lxa_table_set(table, key, (void *) (uintptr_t) 42,
                         NULL, NULL) != LXA_OK
        || lxa_table_find(table, key, &found, &value) != LXA_OK
        || !found || value != (void *) (uintptr_t) 42) {
        abort();
    }
    lxa_table_destroy(table);
    if (budget.used[LXA_BUDGET_MEMORY] != 0) {
        abort();
    }
    if (lxa_decoder_create(lxa_encoding_lookup((lxa_span_t) {
                                 (const uint8_t *) "windows-1252", 12}),
                           LXA_ENCODING_REPLACE, &allocator, &decoder)
        != LXA_OK
        || lxa_decoder_feed(decoder, (lxa_span_t) {
                                legacy_byte, sizeof(legacy_byte)},
                            &scalar, 1, &progress) != LXA_ENCODING_OK
        || progress.consumed != 1 || progress.produced != 1
        || scalar != 0x00e9) {
        abort();
    }
    lxa_decoder_destroy(decoder);
    if (lxa_dom_nodes_create(&allocator, &nodes) != LXA_OK
        || lxa_dom_nodes_append_html_element(nodes, (lxa_span_t) {
            (const uint8_t *) "a", 1}, &element) != LXA_OK
        || lxa_dom_nodes_append_html_attribute(nodes, (lxa_span_t) {
            (const uint8_t *) "href", 4}, (lxa_span_t) {
            (const uint8_t *) "old", 3}, &attribute) != LXA_OK
        || lxa_dom_nodes_attach_attribute(nodes, element, attribute) != LXA_OK
        || lxa_dom_nodes_find_attribute(nodes, element, (lxa_span_t) {
            (const uint8_t *) "href", 4}, &found_attribute) != LXA_OK
        || found_attribute.handle != attribute.handle
        || lxa_dom_nodes_attribute_set_value(nodes, attribute, (lxa_span_t) {
            (const uint8_t *) "new", 3}) != LXA_OK
        || lxa_dom_nodes_attribute_read(nodes, found_attribute, &attribute_name,
                                         &attribute_value) != LXA_OK
        || attribute_name.length != 4
        || attribute_value.length != 3
        || attribute_value.data[0] != 'n'
        || attribute_value.data[1] != 'e'
        || attribute_value.data[2] != 'w'
        || lxa_dom_nodes_validate(nodes) != LXA_OK) {
        abort();
    }
    lxa_dom_nodes_destroy(nodes);
    decoder = NULL;
    if (lxa_decoder_create(lxa_encoding_lookup((lxa_span_t) {
                                 (const uint8_t *) "utf-8", 5}),
                           LXA_ENCODING_REPLACE, &allocator, &decoder)
        != LXA_OK
        || lxa_decoder_feed(decoder, (lxa_span_t) {
                                invalid_utf8, sizeof(invalid_utf8)},
                            &scalar, 1, &progress) != LXA_ENCODING_OK
        || lxa_decoder_finish(decoder, &scalar, 1, &progress)
           != LXA_ENCODING_OK
        || progress.produced != 1 || scalar != 0xfffd
        || lxa_decoder_error_count(decoder) != 1) {
        abort();
    }
    lxa_decoder_destroy(decoder);
    lxa_css_tokens_t *tokens = NULL;
    const uint8_t css[] = ".foo { fill: red; }";
    if (lxa_css_tokens_create(&allocator, NULL, &tokens) != LXA_OK) abort();
    if (lxa_css_scan_bytes((lxa_span_t) {css, sizeof(css) - 1}, tokens)
        != LXA_CSS_SCAN_OK || lxa_css_tokens_length(tokens) < 5) abort();
    lxa_css_blocks_t blocks;
    lxa_css_rule_t rule;
    lxa_css_declarations_t *declarations = NULL;
    lxa_css_selector_list_t *selectors = NULL;
    if (lxa_css_blocks_init(&blocks, &allocator, NULL) != LXA_OK
        || lxa_css_blocks_build(&blocks, tokens) != LXA_OK
        || lxa_css_rule_parse(tokens, &blocks, 0, blocks.end, &rule) != LXA_OK
        || !rule.valid || !rule.has_block
        || lxa_css_declarations_create(&allocator, &blocks, &declarations) != LXA_OK
        || lxa_css_declarations_build(declarations, tokens, &blocks,
            rule.block_open + 1, rule.block_after, 1) != LXA_OK
        || lxa_css_declarations_length(declarations) != 1
        || lxa_css_selector_list_create(&allocator, &blocks, &selectors) != LXA_OK
        || lxa_css_selector_list_build(selectors, tokens, &blocks,
            rule.prelude_first, rule.prelude_after, 1) != LXA_OK
        || lxa_css_selector_list_length(selectors) != 1) abort();
    lxa_css_selector_list_destroy(selectors);
    lxa_css_declarations_destroy(declarations);
    lxa_css_blocks_destroy(&blocks);
    lxa_css_tokens_destroy(tokens);
    return 0;
}
