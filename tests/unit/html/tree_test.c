/* Copyright (C) 2026 Paul Moses */
#include "tests/support/test_check.h"
#include <string.h>
#include "dom/node_store_internal.h"
#include "html/tree.h"
#include "liblexa/serialize.h"

static lxa_span_t
span(const char *text)
{
    lxa_span_t result = {(const uint8_t *) text, strlen(text)};
    return result;
}

typedef struct {
    uint8_t data[256];
    size_t length;
} sink_t;

static lxa_write_result_t
write_bytes(void *context, lxa_span_t bytes)
{
    sink_t *sink = context;
    lxa_write_result_t result = {0, LXA_ERROR_LIMIT};
    if (bytes.length > sizeof(sink->data) - sink->length) return result;
    memcpy(sink->data + sink->length, bytes.data, bytes.length);
    sink->length += bytes.length;
    result.consumed = bytes.length;
    result.status = LXA_OK;
    return result;
}

int
main(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_dom_nodes_t *nodes = NULL;
    lxa_html_tree_t tree;
    lxa_html_tokenizer_t *tokenizer = NULL;
    lxa_html_token_t token = {0};
    lxa_budget_t budget;
    lxa_limits_t limits = lxa_limits_default();
    lxa_budget_kind_t reason;
    sink_t sink = {{0}, 0};
    uint8_t lang_value[] = {'e', 'n'};
    lxa_html_attribute_t html_attributes[2] = {
        { {(const uint8_t *) "lang", 4}, {lang_value, sizeof(lang_value)} },
        { {(const uint8_t *) "lang", 4}, {(const uint8_t *) "ignored", 7} }
    };
    lxa_html_attribute_t body_attribute = {
        {(const uint8_t *) "class", 5},
        {(const uint8_t *) "a&b", 3}
    };
    lxa_html_attribute_t base_attribute = {
        {(const uint8_t *) "href", 4},
        {(const uint8_t *) "/root", 5}
    };
    lxa_html_attribute_t image_attribute = {
        {(const uint8_t *) "src", 3},
        {(const uint8_t *) "a&b", 3}
    };
    const char expected[] =
        "<!DOCTYPE html><!--before--><html lang=\"en\">"
        "<head><base href=\"/root\"></head>"
        "<body class=\"a&amp;b\">"
        "hello &amp; world<div><span>nested</span></div>"
        "<img src=\"a&amp;b\"></body></html>";
    const char custom_expected[] =
        "<!DOCTYPE html><!--before--><html lang=\"en\">"
        "<head><base href=\"/root\"></head>"
        "<body class=\"a&amp;b\">hello &amp; world<div><span>nested</span></div>"
        "<img src=\"a&amp;b\">"
        "<x_panel:one @event.key=\"value\"></x_panel:one></body></html>";
    TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token.kind = LXA_HTML_DOCTYPE;
    token.name = span("html");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_COMMENT;
    token.data = span("before");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("html");
    token.attributes = html_attributes;
    token.attribute_count = 2;
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    lang_value[0] = 'X'; /* callback-scope input cannot remain borrowed */
    token.name = span("base"); /* implicit head before the explicit body */
    token.attributes = &base_attribute;
    token.attribute_count = 1;
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("body"); /* implicit head */
    token.attributes = &body_attribute;
    token.attribute_count = 1;
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_TEXT;
    token.data = span("hello & world");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("div");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("span");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_TEXT;
    token.data = span("nested");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_END_TAG;
    token.name = span("span");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("div");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 1);
    token.kind = LXA_HTML_START_TAG;
    token.name = span("img");
    token.attributes = &image_attribute;
    token.attribute_count = 1;
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_EOF;
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
        write_bytes, &sink, &reason) == LXA_OK);
    TEST_CHECK(sink.length == sizeof(expected) - 1
               && memcmp(sink.data, expected, sink.length) == 0);
    {
        lxa_dom_ref_t custom, attribute;
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes,
            span("x_panel:one"), &custom) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes,
            span("@event.key"), span("value"), &attribute) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, custom, attribute)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, tree.body,
            custom) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(custom_expected) - 1
                   && memcmp(sink.data, custom_expected, sink.length) == 0);
    }
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("template");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    token.kind = LXA_HTML_EOF;
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_EOF;
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT); /* latched, no retry on a partial tree */
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("html");
    token.attributes = html_attributes;
    token.attribute_count = 2;
    budget.limits.max_attributes = (uint32_t) budget.used[LXA_BUDGET_ATTRIBUTES];
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    budget.limits.max_attributes = limits.max_attributes;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("div");
    budget.limits.max_open_elements = 1;
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 1);
    budget.limits.max_open_elements = limits.max_open_elements;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<p><a href='one&amp;two'><strong>bold</strong>"
             "<em>italic</em><code>x</code></a></p>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t paragraph, link, href, original_href;
        lxa_span_t key, value;
        lxa_buffer_t text;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, tree.body, &paragraph)
                   == LXA_OK && paragraph.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, paragraph, &link)
                   == LXA_OK && link.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, link, span("href"), &href)
                   == LXA_OK && href.handle != LXA_HANDLE_NONE);
        original_href = href;
        TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, href, &key, &value)
                   == LXA_OK && key.length == 4
                   && value.length == 7
                   && memcmp(value.data, "one&two", 7) == 0);
        TEST_CHECK(lxa_buffer_init(&text, &allocator) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_text_content(nodes, link, &text, 16) == LXA_OK);
        TEST_CHECK(text.length == 11
                   && memcmp(text.data, "bolditalicx", 11) == 0);
        lxa_buffer_destroy(&text);
        TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, href,
            span("new&\"")) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, link, span("href"), &href)
                   == LXA_OK && href.handle == original_href.handle);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    }
    {
        const char inline_expected[] =
            "<html><head></head><body><p><a href=\"new&amp;&quot;\">"
            "<strong>bold</strong><em>italic</em><code>x</code>"
            "</a></p></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(inline_expected) - 1
                   && memcmp(sink.data, inline_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<p>intro<section><article><blockquote><p>quote</p>"
             "</blockquote></article></section>")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char sections_expected[] =
            "<html><head></head><body><p>intro</p><section><article>"
            "<blockquote><p>quote</p></blockquote></article></section>"
            "</body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(sections_expected) - 1
                   && memcmp(sink.data, sections_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer, span("<p>above<pre>\r"))
               == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("\n<code>x\n</code></pre><pre>\n\nrest</pre>")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char pre_expected[] =
            "<html><head></head><body><p>above</p>"
            "<pre><code>x\n</code></pre><pre>\nrest</pre></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(pre_expected) - 1
                   && memcmp(sink.data, pre_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<p>terms<dl><dt>one<dd><p>first<dt>two<dd>second</dl>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char definitions_expected[] =
            "<html><head></head><body><p>terms</p><dl>"
            "<dt>one</dt><dd><p>first</p></dd>"
            "<dt>two</dt><dd>second</dd></dl></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(definitions_expected) - 1
                   && memcmp(sink.data, definitions_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<ul><li><p>one</li><li>two</li></ul>"
             "<dl><dt><p>term</dt><dd><p>detail</dd></dl>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char explicit_expected[] =
            "<html><head></head><body><ul><li><p>one</p></li>"
            "<li>two</li></ul><dl><dt><p>term</p></dt>"
            "<dd><p>detail</p></dd></dl></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(explicit_expected) - 1
                   && memcmp(sink.data, explicit_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<head><script>if (a < b) { x = 1; }</scr"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("ipt></head><body><script><!-- x <b> "
             "<script>inner</script> --></script><p>after</p>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char script_expected[] =
            "<html><head><script>if (a < b) { x = 1; }</script></head>"
            "<body><script><!-- x <b> <script>inner</script> --></script>"
            "<p>after</p></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(script_expected) - 1
                   && memcmp(sink.data, script_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<x-note id='one'>hello<x-inner>inner</x-inner></x-note>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char unknown_expected[] =
            "<html><head></head><body><x-note id=\"one\">hello"
            "<x-inner>inner</x-inner></x-note></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(unknown_expected) - 1
                   && memcmp(sink.data, unknown_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<p>before</p><plaintext>one<b>two")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("</b>&amp;")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char plain_expected[] =
            "<html><head></head><body><p>before</p>"
            "<plaintext>one<b>two</b>&amp;</plaintext></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(plain_expected) - 1
                   && memcmp(sink.data, plain_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<p>intro<table class='x'><tr><td>one</td>"
             "<td><strong>two</strong></td></tr></table>"
             "<table><tbody><tr><th>H</th></tr></tbody></table>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char table_expected[] =
            "<html><head></head><body><p>intro</p>"
            "<table class=\"x\"><tbody><tr><td>one</td>"
            "<td><strong>two</strong></td></tr></tbody></table>"
            "<table><tbody><tr><th>H</th></tr></tbody></table>"
            "</body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(table_expected) - 1
                   && memcmp(sink.data, table_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<table><td>a</td></table>"
             "<table><thead><tr><th>b</th></tr></thead></table>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char implied_expected[] =
            "<html><head></head><body><table><tbody><tr>"
            "<td>a</td></tr></tbody></table><table><thead>"
            "<tr><th>b</th></tr></thead></table></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(implied_expected) - 1
                   && memcmp(sink.data, implied_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<table><div>foo<span>bar</span></div><img src='x'>"
             "<tr><td>cell</td></tr></table>")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char foster_elements_expected[] =
            "<html><head></head><body><div>foo<span>bar</span></div>"
            "<img src=\"x\"><table><tbody><tr><td>cell</td></tr>"
            "</tbody></table></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(foster_elements_expected) - 1
                   && memcmp(sink.data, foster_elements_expected,
                              sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<table><tr><td><table><div>inner</div>"
             "<tr><td>x</td></tr></table></td></tr></table>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char nested_foster_expected[] =
            "<html><head></head><body><table><tbody><tr><td>"
            "<div>inner</div><table><tbody><tr><td>x</td></tr>"
            "</tbody></table></td></tr></tbody></table></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(nested_foster_expected) - 1
                   && memcmp(sink.data, nested_foster_expected,
                              sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<table><tr><td><p>one<th>two<td>three</table>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char cells_expected[] =
            "<html><head></head><body><table><tbody><tr>"
            "<td><p>one</p></td><th>two</th><td>three</td>"
            "</tr></tbody></table></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(cells_expected) - 1
                   && memcmp(sink.data, cells_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<table><tbody><tr><td>one<tr><th>two"
             "<thead><tr><td>head</table>")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char rows_expected[] =
            "<html><head></head><body><table><tbody><tr><td>one</td></tr>"
            "<tr><th>two</th></tr></tbody><thead><tr><td>head</td></tr>"
            "</thead></table></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(rows_expected) - 1
                   && memcmp(sink.data, rows_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("table");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("tr");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("td");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("p");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 6);
    budget.limits.max_nodes = (uint32_t) budget.used[LXA_BUDGET_NODES];
    token.name = span("td");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(nodes->limit_reason == LXA_BUDGET_NODES
               && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 6);
    budget.limits.max_nodes = limits.max_nodes;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("table");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    token.name = span("div");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(nodes->limit_reason == LXA_BUDGET_WORK
               && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 2);
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("table");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_TEXT;
    token.data = span(" ");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.data = span("x");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_END_TAG;
    token.name = span("table");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.kind = LXA_HTML_EOF;
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char split_expected[] =
            "<html><head></head><body> x<table></table></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(split_expected) - 1
                   && memcmp(sink.data, split_expected, sink.length) == 0);
    }
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("table");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    budget.limits.max_token_bytes = 2;
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_TEXT;
    token.data = span("ab");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.data = span("c");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(nodes->limit_reason == LXA_BUDGET_TOKEN_BYTES
               && tree.table_text.length == 2);
    budget.limits.max_token_bytes = limits.max_token_bytes;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("table");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    budget.limits.max_open_elements = 3;
    token.name = span("tr");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(nodes->limit_reason == LXA_BUDGET_OPEN_ELEMENTS
               && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 2);
    budget.limits.max_open_elements = limits.max_open_elements;
    {
        lxa_dom_ref_t table, first;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, tree.body, &table)
                   == LXA_OK && table.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, table, &first)
                   == LXA_OK && first.handle == LXA_HANDLE_NONE);
    }
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<table>  ")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("outside<tr><td>cell</td></tr></table>"
             "<table> \n<tr><td>space</td></tr></table><table>tail"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char foster_expected[] =
            "<html><head></head><body>  outside<table><tbody><tr>"
            "<td>cell</td></tr></tbody></table>"
            "<table> \n<tbody><tr><td>space</td></tr></tbody></table>"
            "tail<table></table></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(foster_expected) - 1
                   && memcmp(sink.data, foster_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("x-note");
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(nodes->limit_reason == LXA_BUDGET_WORK
               && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0);
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("ul");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("li");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("p");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 4);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    token.name = span("li");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(nodes->limit_reason == LXA_BUDGET_WORK
               && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 4);
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<!doctype html><p>first<p>second<h2>heading</h2>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char implicit_expected[] =
            "<!DOCTYPE html><html><head></head><body>"
            "<p>first</p><p>second</p><h2>heading</h2></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(implicit_expected) - 1
                   && memcmp(sink.data, implicit_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<ul><li>one<li>two</ul>")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char list_expected[] =
            "<html><head></head><body><ul><li>one</li><li>two</li>"
            "</ul></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(list_expected) - 1
                   && memcmp(sink.data, list_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<p>intro<div>block</div><p>next<ul><li>x</ul>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char block_expected[] =
            "<html><head></head><body><p>intro</p><div>block</div>"
            "<p>next</p><ul><li>x</li></ul></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(block_expected) - 1
                   && memcmp(sink.data, block_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<ul><li><p>a<li>b<ul><li><p>inner</ul><li><p>last</ul>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char nested_expected[] =
            "<html><head></head><body><ul><li><p>a</p></li>"
            "<li>b<ul><li><p>inner</p></li></ul></li>"
            "<li><p>last</p></li>"
            "</ul></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(nested_expected) - 1
                   && memcmp(sink.data, nested_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    tokenizer = NULL;
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("div");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("span");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.kind = LXA_HTML_END_TAG;
    token.name = span("div");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    TEST_CHECK(lxa_vector_length(&tree.open_elements) == 1);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
        lxa_html_tree_on_token, NULL, &tree, &budget,
        &tokenizer) == LXA_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<!doctype html><html><head><title>A&amp;B</title>"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("<style>x<y</style></head><body><textarea>x&amp;y"))
        == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
        span("</textarea></body>")) == LXA_HTML_OK);
    TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        const char raw_expected[] =
            "<!DOCTYPE html><html><head><title>A&amp;B</title>"
            "<style>x<y</style></head><body>"
            "<textarea>x&amp;y</textarea></body></html>";
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(raw_expected) - 1
                   && memcmp(sink.data, raw_expected, sink.length) == 0);
    }
    lxa_html_tokenizer_destroy(tokenizer);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("table");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("caption");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("span");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    {
        size_t open_before = lxa_vector_length(&tree.open_elements);
        size_t nodes_before = lxa_dom_nodes_length(nodes);
        budget.limits.max_nodes = (uint32_t) nodes_before;
        token.name = span("tr");
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(nodes->limit_reason == LXA_BUDGET_NODES
                   && lxa_vector_length(&tree.open_elements) == open_before
                   && lxa_dom_nodes_length(nodes) == nodes_before);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    }
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    budget.limits.max_nodes = limits.max_nodes;
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("svg");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("g");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("path");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    {
        size_t open_before = lxa_vector_length(&tree.open_elements);
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + 1;
        token.kind = LXA_HTML_END_TAG;
        token.name = span("svg");
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(nodes->limit_reason == LXA_BUDGET_WORK
                   && lxa_vector_length(&tree.open_elements) == open_before);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    }
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    token = (lxa_html_token_t){0};
    token.kind = LXA_HTML_START_TAG;
    token.name = span("svg");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    token.name = span("g");
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_OK);
    {
        size_t open_before = lxa_vector_length(&tree.open_elements);
        size_t nodes_before = lxa_dom_nodes_length(nodes);
        budget.limits.max_nodes = (uint32_t) nodes_before;
        token.name = span("p");
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(nodes->limit_reason == LXA_BUDGET_NODES
                   && lxa_vector_length(&tree.open_elements) == open_before
                   && lxa_dom_nodes_length(nodes) == nodes_before);
        budget.limits.max_nodes = limits.max_nodes;
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    }
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    {
        const uint8_t mixed[] = {'A', 0, 'B', 0, 'C'};
        const uint8_t table_text[] = {'x', 0, 'y'};
        const uint8_t foreign_text[] = {'v', 0, 'w'};
        const char expected_nulls[] =
            "<html><head></head><body>ABCxy<table></table>"
            "<svg>v\xef\xbf\xbdw</svg></body></html>";
        token = (lxa_html_token_t){0};
        token.kind = LXA_HTML_START_TAG;
        token.name = span("body");
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_OK);
        token = (lxa_html_token_t){0};
        token.kind = LXA_HTML_TEXT;
        token.data = (lxa_span_t){mixed, sizeof(mixed)};
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_OK);
        token.kind = LXA_HTML_START_TAG;
        token.data = (lxa_span_t){NULL, 0};
        token.name = span("table");
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_OK);
        token.kind = LXA_HTML_TEXT;
        token.name = (lxa_span_t){NULL, 0};
        token.data = (lxa_span_t){table_text, sizeof(table_text)};
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_OK);
        token.kind = LXA_HTML_END_TAG;
        token.data = (lxa_span_t){NULL, 0};
        token.name = span("table");
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_OK);
        token.kind = LXA_HTML_START_TAG;
        token.name = span("svg");
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_OK);
        token.kind = LXA_HTML_TEXT;
        token.name = (lxa_span_t){NULL, 0};
        token.data = (lxa_span_t){foreign_text, sizeof(foreign_text)};
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_OK);
        token.kind = LXA_HTML_EOF;
        token.data = (lxa_span_t){NULL, 0};
        TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(expected_nulls) - 1
                   && memcmp(sink.data, expected_nulls, sink.length) == 0);
    }
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
    /* A missing parser-owned parent is not unsupported input grammar. */
    tree.document = (lxa_dom_ref_t){0};
    token = (lxa_html_token_t){.kind = LXA_HTML_COMMENT, .data = span("x")};
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_INTERNAL);
    token = (lxa_html_token_t){.kind = LXA_HTML_EOF};
    TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token)
               == LXA_ERROR_INTERNAL);
    lxa_html_tree_destroy(&tree);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
               && budget.used[LXA_BUDGET_MEMORY] == 0);
    return 0;
}
