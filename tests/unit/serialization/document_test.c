#include "tests/support/test_check.h"
#include <string.h>
#include "dom/node_store_internal.h"
#include "liblexa/serialize.h"

typedef struct {
    uint8_t bytes[256];
    size_t length, limit;
    lxa_status_t failure;
} sink_t;

static lxa_write_result_t
write_bytes(void *context, lxa_span_t bytes)
{
    sink_t *sink = context;
    lxa_write_result_t result = {0, LXA_OK};
    if (bytes.length > sink->limit - sink->length) {
        result.status = sink->failure;
        return result;
    }
    memcpy(sink->bytes + sink->length, bytes.data, bytes.length);
    sink->length += bytes.length;
    result.consumed = bytes.length;
    return result;
}

static lxa_span_t
span(const char *text)
{
    lxa_span_t result = {(const uint8_t *) text, strlen(text)};
    return result;
}

int
main(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_budget_t budget;
    lxa_limits_t limits = lxa_limits_default();
    lxa_dom_nodes_t *nodes = NULL;
    lxa_dom_ref_t document, doctype, html, body, link, raw, br, text, comment, attr;
    lxa_dom_doctype_data_t declaration = {0};
    lxa_budget_kind_t reason = LXA_BUDGET_COUNT;
    sink_t sink = {{0}, 0, 256, LXA_ERROR_LIMIT};
    const char expected[] =
        "<!DOCTYPE html><html><body><a href=\"a&amp;&quot;&gt;b\">"
        "x&amp;&lt;&gt;</a><!--c--><br><script>1 < 2 & 3</script></body></html>";
    TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT,
                                           &document) == LXA_OK);
    declaration.name = span("html");
    TEST_CHECK(lxa_dom_nodes_append_doctype(nodes, declaration, &doctype) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("html"), &html) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("body"), &body) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("a"), &link) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("br"), &br) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("script"), &raw) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("href"),
                                                    span("old"), &attr) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, link, attr) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, attr, span("a&\">b")) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                               span("x&<>"), &text) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_COMMENT,
                                               span("c"), &comment) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document, doctype)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document, html) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, html, body) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, link) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, link, text) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, comment) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, br) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, raw) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                               span("1 < 2 & 3"), &text) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, raw, text) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_html_serialize_document(nodes, document, write_bytes,
                                                   &sink, &reason) == LXA_OK);
    TEST_CHECK(sink.length == sizeof(expected) - 1
               && memcmp(sink.bytes, expected, sink.length) == 0);
    sink.length = 0;
    sink.limit = 4;
    TEST_CHECK(lxa_html_serialize_document(nodes, document, write_bytes,
                                                   &sink, &reason) == LXA_ERROR_LIMIT);
    TEST_CHECK(sink.length <= sink.limit);
    sink.failure = LXA_ERROR_NO_MEMORY;
    TEST_CHECK(lxa_html_serialize_document(nodes, document, write_bytes,
                                                   &sink, &reason) == LXA_ERROR_NO_MEMORY);
    TEST_CHECK(sink.length <= sink.limit && reason == LXA_BUDGET_COUNT);
    sink.limit = sizeof(sink.bytes);
    sink.length = 0;
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    TEST_CHECK(lxa_html_serialize_document(nodes, document, write_bytes,
                                                   &sink, &reason) == LXA_ERROR_LIMIT);
    TEST_CHECK(reason == LXA_BUDGET_WORK && sink.length == 0);
    budget.limits.max_work = limits.max_work;
    {
        lxa_dom_record_t *records = (lxa_dom_record_t *) nodes->records.bytes.data;
        lxa_dom_record_t saved = records[(lxa_id_t) br.handle];
        records[(lxa_id_t) br.handle].first_child =
            records[(lxa_id_t) br.handle].last_child = (lxa_id_t) text.handle;
        TEST_CHECK(lxa_html_serialize_document(nodes, document, write_bytes,
                                                       &sink, &reason)
                   == LXA_ERROR_ARGUMENT);
        records[(lxa_id_t) br.handle] = saved;
    }
    {
        lxa_dom_ref_t tmpl, contents, nested, inner, bold, message;
        lxa_dom_ref_t empty, empty_contents;
        const char serialized[] =
            "<!DOCTYPE html><html><body><a href=\"a&amp;&quot;&gt;b\">"
            "x&amp;&lt;&gt;</a><!--c--><br><script>1 < 2 & 3</script>"
            "<template><b>Hi&amp;<template>inner</template>!</b></template>"
            "<template></template>"
            "</body></html>";
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("template"),
                                                      &tmpl) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                               &contents) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_bind_template(nodes, tmpl, contents) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, tmpl)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("b"), &bold)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, contents, bold)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                   span("Hi&"), &message) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, bold, message)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("template"),
                                                      &empty) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                               &empty_contents) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_bind_template(nodes, empty, empty_contents)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, empty)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("template"),
                                                      &nested) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                               &inner) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_bind_template(nodes, nested, inner) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, bold, nested)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                   span("inner"), &message) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, inner, message)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                   span("!"), &message) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, bold, message)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes, document,
            write_bytes, &sink, &reason) == LXA_OK);
        TEST_CHECK(sink.length == sizeof(serialized) - 1
                   && memcmp(sink.bytes, serialized, sink.length) == 0);
        sink.length = 0;
        sink.limit = sizeof(expected) - 1 + 15;
        TEST_CHECK(lxa_html_serialize_document(nodes, document,
            write_bytes, &sink, &reason) == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(sink.length <= sink.limit);
    }
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    return 0;
}
