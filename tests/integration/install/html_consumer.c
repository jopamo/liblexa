/* Copyright (C) 2026 Paul Moses
 * Compile only against the privately installed candidate headers/libraries.
 */
#include <string.h>
#include "liblexa/html.h"
#include "liblexa/serialize.h"

typedef struct {
    unsigned char bytes[256];
    size_t length;
} output_t;

static lxa_write_result_t
write_bytes(void *context, lxa_span_t bytes)
{
    output_t *output = context;
    lxa_write_result_t result = {0, LXA_ERROR_LIMIT};
    if (bytes.length > sizeof(output->bytes) - output->length)
        return result;
    memcpy(output->bytes + output->length, bytes.data, bytes.length);
    output->length += bytes.length;
    result.consumed = bytes.length;
    result.status = LXA_OK;
    return result;
}

int
main(void)
{
    static const unsigned char html[] = "<a href='old&amp;x'>A&amp;B</a>";
    static const char expected[] =
        "<html><head></head><body><a href=\"new&amp;&quot;&lt;\">A&amp;B"
        "</a></body></html>";
    static const unsigned char legacy[] =
        "<meta charset=windows-1252><p>\200</p>";
    static const char decoded[] =
        "<html><head><meta charset=\"windows-1252\"></head><body><p>"
        "\342\202\254</p></body></html>";
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_html_document_t *document = NULL;
    lxa_html_result_t parser_result;
    lxa_limit_reason_t reason;
    lxa_dom_nodes_t *nodes;
    lxa_dom_ref_t anchor, href;
    lxa_dom_kind_t kind;
    lxa_span_t name, value;
    output_t output = {{0}, 0};
    lxa_limits_t limits = lxa_limits_default();
    int failed;

    if (lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){html, sizeof(html) - 1}, &parser_result,
            &reason, &document) != LXA_OK || document == NULL
        || parser_result != LXA_HTML_OK) {
        lxa_html_document_destroy(document);
        return 1;
    }
    nodes = lxa_html_document_nodes(document);
    failed = lxa_dom_nodes_first_child(nodes,
                 lxa_html_document_body(document), &anchor) != LXA_OK
        || lxa_dom_nodes_kind(nodes, anchor, &kind) != LXA_OK
        || kind != LXA_DOM_KIND_ELEMENT
        || lxa_dom_nodes_find_attribute(nodes, anchor,
             (lxa_span_t){(const unsigned char *)"href", 4}, &href) != LXA_OK
        || href.handle == LXA_HANDLE_NONE
        || lxa_dom_nodes_attribute_read(nodes, href, &name, &value) != LXA_OK
        || value.length != 5 || memcmp(value.data, "old&x", 5) != 0
        || lxa_dom_nodes_attribute_set_value(nodes, href,
             (lxa_span_t){(const unsigned char *)"new&\"<", 6}) != LXA_OK
        || lxa_html_serialize_document(nodes,
             lxa_html_document_root(document), write_bytes, &output,
             &reason) != LXA_OK
        || output.length != sizeof(expected) - 1
        || memcmp(output.bytes, expected, output.length) != 0;
    lxa_html_document_destroy(document);
    if (failed)
        return 2;

    document = NULL;
    limits.max_input_bytes = sizeof(html) - 2;
    if (lxa_html_document_parse(&allocator, &limits,
            (lxa_span_t){html, sizeof(html) - 1}, &parser_result,
            &reason, &document) != LXA_ERROR_LIMIT
        || reason != LXA_BUDGET_INPUT || document != NULL)
        return 3;

    output.length = 0;
    if (lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){legacy, sizeof(legacy) - 1}, &parser_result,
            &reason, &document) != LXA_OK || document == NULL
        || parser_result != LXA_HTML_OK) {
        lxa_html_document_destroy(document);
        return 4;
    }
    failed = lxa_html_serialize_document(
                 lxa_html_document_nodes(document),
                 lxa_html_document_root(document), write_bytes, &output,
                 &reason) != LXA_OK
        || output.length != sizeof(decoded) - 1
        || memcmp(output.bytes, decoded, output.length) != 0;
    lxa_html_document_destroy(document);
    if (failed)
        return 5;
    return 0;
}
