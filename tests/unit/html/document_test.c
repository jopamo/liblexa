/* Copyright (C) 2026 Paul Moses */
#include "tests/support/test_check.h"
#include "dom/node_store_internal.h"
#include "liblexa/html.h"
#include "liblexa/serialize.h"
#include <string.h>

typedef struct {
    uint8_t data[512];
    size_t length;
} sink_t;

typedef struct {
    size_t calls, fail_at;
    lxa_allocator_t base;
} failing_t;

static void *
fail_allocate(void *context, size_t size)
{
    failing_t *state = context;
    if (state->calls++ == state->fail_at) return NULL;
    return state->base.allocate(state->base.context, size);
}

static void *
fail_resize(void *context, void *ptr, size_t size)
{
    failing_t *state = context;
    if (state->calls++ == state->fail_at) return NULL;
    return state->base.resize(state->base.context, ptr, size);
}

static void
fail_release(void *context, void *ptr)
{
    failing_t *state = context;
    state->base.release(state->base.context, ptr);
}

typedef struct {
    size_t count, offset;
    lxa_status_t response;
} diagnostic_t;

static lxa_status_t
on_malformed(void *context, size_t offset)
{
    diagnostic_t *diagnostic = context;
    diagnostic->count++;
    diagnostic->offset = offset;
    return diagnostic->response;
}

static lxa_span_t
span(const char *text)
{
    lxa_span_t result = {(const uint8_t *) text, strlen(text)};
    return result;
}

static lxa_write_result_t
write_bytes(void *context, lxa_span_t bytes)
{
    sink_t *sink = context;
    lxa_write_result_t result = {0, LXA_ERROR_LIMIT};
    if (bytes.length > sizeof(sink->data) - sink->length) return result;
    memcpy(sink->data + sink->length, bytes.data, bytes.length);
    sink->length += bytes.length;
    result.status = LXA_OK;
    result.consumed = bytes.length;
    return result;
}

int
main(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_html_document_t *document = NULL;
    lxa_html_result_t parse_result;
    lxa_limit_reason_t reason;
    lxa_dom_nodes_t *nodes;
    lxa_dom_ref_t body, anchor, attribute;
    lxa_span_t name, value;
    lxa_buffer_t text;
    lxa_limits_t limits = lxa_limits_default();
    sink_t sink = {{0}, 0};
    const char expected[] =
        "<html><head></head><body><a href=\"new&amp;&quot;&lt;\">A&amp;B"
        "</a></body></html>";
    failing_t failing = {0, 0, allocator};
    diagnostic_t diagnostic = {0, 0, LXA_OK};
    lxa_allocator_t injected = {&failing, fail_allocate, fail_resize,
                                fail_release};
    size_t i;
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<a href='old&amp;value'>A&amp;B</a>"), &parse_result,
        &reason, &document) == LXA_OK);
    TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
    nodes = lxa_html_document_nodes(document);
    body = lxa_html_document_body(document);
    TEST_CHECK(lxa_html_document_head(document).handle != LXA_HANDLE_NONE);
    TEST_CHECK(lxa_dom_nodes_first_child(nodes, body, &anchor) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, anchor, span("href"),
                                             &attribute) == LXA_OK);
    TEST_CHECK(attribute.handle != LXA_HANDLE_NONE);
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, attribute, &name, &value)
               == LXA_OK);
    TEST_CHECK(value.length == 9 && memcmp(value.data, "old&value", 9) == 0);
    TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, attribute,
                                                 span("new&\"<")) == LXA_OK);
    TEST_CHECK(lxa_buffer_init(&text, &allocator) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_text_content(nodes, anchor, &text, 16) == LXA_OK);
    TEST_CHECK(text.length == 3 && memcmp(text.data, "A&B", 3) == 0);
    lxa_buffer_destroy(&text);
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    TEST_CHECK(sink.length == sizeof(expected) - 1
               && memcmp(sink.data, expected, sink.length) == 0);
    lxa_html_document_destroy(document);

    /* The in-frameset stack returns to its parent after an inner end tag. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<frameset id=o><frameset id=i><frame name=x>"
             "</frameset><frame name=y></frameset>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char frames[] =
            "<html><head></head><frameset id=\"o\"><frameset id=\"i\">"
            "<frame name=\"x\"></frameset><frame name=\"y\"></frameset>"
            "</html>";
        TEST_CHECK(sink.length == sizeof(frames) - 1
                   && memcmp(sink.data, frames, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><object>x</object><tr><td>z</td></tr>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char fostered[] =
            "<html><head><template><object>x</object><table><tbody><tr>"
            "<td>z</td></tr></tbody></table></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered) - 1
                   && memcmp(sink.data, fostered, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<frameset><noframes>fallback <b>txt</noframes>"
             "<frame></frameset><noframes>later</noframes>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char frames[] =
            "<html><head></head><frameset><noframes>fallback <b>txt"
            "</noframes><frame></frameset><noframes>later</noframes></html>";
        TEST_CHECK(sink.length == sizeof(frames) - 1
                   && memcmp(sink.data, frames, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<frameset><noframes>unclosed"), &parse_result,
        &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_html_document_body(document).handle == LXA_HANDLE_NONE
               && lxa_dom_nodes_validate(lxa_html_document_nodes(document))
                  == LXA_OK);
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><p><object>v</object>z</p></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char object[] =
            "<html><head><template><p><object>v</object>z</p></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(object) - 1
                   && memcmp(sink.data, object, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><p><b class=x><object id=o>v</object>z"
             "</b></p></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char formatted_object[] =
            "<html><head><template><p><b class=\"x\">"
            "<object id=\"o\">v</object>z</b></p></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(formatted_object) - 1
                   && memcmp(sink.data, formatted_object,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><object id=o>x</object>y"
             "</li></ul><section><object>z</object></section></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char block_objects[] =
            "<html><head><template><ul><li><object id=\"o\">x"
            "</object>y</li></ul><section><object>z</object>"
            "</section></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(block_objects) - 1
                   && memcmp(sink.data, block_objects, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><caption><object>a</object></caption>"
             "<tr><th><object>b</object></th><td><object id=o>c"
             "</object></td></tr></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_objects[] =
            "<html><head><template><table><caption><object>a</object>"
            "</caption><tbody><tr><th><object>b</object></th><td>"
            "<object id=\"o\">c</object></td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_objects) - 1
                   && memcmp(sink.data, table_objects, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    /* In-body </br> is reprocessed as a bare start tag, including when
     * table fostering moves it outside the table. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>A</br id=ignored>B</p><table></br><tr><td>x</td></tr></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char breaks[] =
            "<html><head></head><body><p>A<br>B</p><br>"
            "<table><tbody><tr><td>x</td></tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(breaks) - 1
                   && memcmp(sink.data, breaks, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<b>1<i>2<p>3</b>4"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char pinned_adoption[] =
            "<html><head></head><body><b>1<i>2</i></b>"
            "<i><p><b>3</b>4</p></i></body></html>";
        TEST_CHECK(sink.length == sizeof(pinned_adoption) - 1
                   && memcmp(sink.data, pinned_adoption,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<b class=x>1<i data-y=z>2<p>3<span>more</span>"
             "</b>4"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char adopted_attributes[] =
            "<html><head></head><body><b class=\"x\">1"
            "<i data-y=\"z\">2</i></b><i data-y=\"z\"><p>"
            "<b class=\"x\">3<span>more</span></b>4</p></i>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(adopted_attributes) - 1
                   && memcmp(sink.data, adopted_attributes,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<a class=x><div><style></style><address><a href=/new>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char adopted_anchor[] =
            "<html><head></head><body><a class=\"x\"></a><div>"
            "<a class=\"x\"><style></style></a><address>"
            "<a class=\"x\"></a><a href=\"/new\"></a></address>"
            "</div></body></html>";
        TEST_CHECK(sink.length == sizeof(adopted_anchor) - 1
                   && memcmp(sink.data, adopted_anchor, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><template><a href=/inside>inner</a></template>"
             "<tr><td><a href=/after>after</a></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_template[] =
            "<html><head></head><body><table><template>"
            "<a href=\"/inside\">inner</a></template><tbody><tr>"
            "<td><a href=\"/after\">after</a></td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(table_template) - 1
                   && memcmp(sink.data, table_template,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><template></template></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<nobr>first<nobr>second</nobr><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char repeated_nobr[] =
            "<html><head></head><body><nobr>first</nobr>"
            "<nobr>second</nobr><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(repeated_nobr) - 1
                   && memcmp(sink.data, repeated_nobr, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<nobr><table><marquee></table><nobr>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_marquee[] =
            "<html><head></head><body><nobr><marquee></marquee>"
            "<table></table></nobr><nobr></nobr></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_marquee) - 1
                   && memcmp(sink.data, fostered_marquee, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_open_elements = 2; /* repeated nobr reuses its open slot */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<nobr><nobr></nobr>"), &parse_result, &reason, &document)
        == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);
    limits = lxa_limits_default();
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<nobr><span><nobr>"), &parse_result, &reason, &document)
        == LXA_ERROR_ARGUMENT);
    TEST_CHECK(document == NULL);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><b><i data-x=y>two</b>after</i>"
             "<a href=/next>next</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed[] =
            "<html><head></head><body><b><i data-x=\"y\">two</i></b>"
            "<i data-x=\"y\">after</i><a href=\"/next\">next</a>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(crossed) - 1
                   && memcmp(sink.data, crossed, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><b><i>one</b><p>two</p>three</i>"
             "<a href=/next>next</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char across_block[] =
            "<html><head></head><body><b><i>one</i></b>"
            "<p><i>two</i></p><i>three</i>"
            "<a href=\"/next\">next</a></body></html>";
        TEST_CHECK(sink.length == sizeof(across_block) - 1
                   && memcmp(sink.data, across_block,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><b><i></b></i><p>next</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char closed[] =
            "<html><head></head><body><b><i></i></b><p>next</p>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(closed) - 1
                   && memcmp(sink.data, closed, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><p>before</li>after</p>"
             "<ol><li>first</li></li><li>second</li></ol>"
             "<dl><dt>term</dd></dt></dl>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char list_ends[] =
            "<html><head></head><body><p>beforeafter</p>"
            "<ol><li>first</li><li>second</li></ol>"
            "<dl><dt>term</dt></dl></body></html>";
        TEST_CHECK(sink.length == sizeof(list_ends) - 1
                   && memcmp(sink.data, list_ends, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body></ul></li><ol><li>first<ul><li>inner</ol>"
             "</ul></dt><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_list_end[] =
            "<html><head></head><body><ol><li>first<ul><li>inner</li>"
            "</ul></li></ol><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_list_end) - 1
                   && memcmp(sink.data, nested_list_end, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<ul><li>outer<section><li>inner</ul>"
             "<dl><dt>term<section><dd>definition</dl>"
             "<ol><li>one<article><li>two</ol>"
             "<ul><li>first<div><li>second</ul>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char section_items[] =
            "<html><head></head><body><ul><li>outer<section><li>inner"
            "</li></section></li></ul><dl><dt>term<section><dd>definition"
            "</dd></section></dt></dl><ol><li>one<article><li>two</li>"
            "</article></li></ol><ul><li>first<div></div></li>"
            "<li>second</li></ul></body></html>";
        TEST_CHECK(sink.length == sizeof(section_items) - 1
                   && memcmp(sink.data, section_items, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<ul><li>outer<svg><foreignobject><li>inner</li>"
             "</foreignobject></svg><li>next</ul>"
             "<ul><li>outer<math><mtext><li>inner</li>"
             "</mtext></math><li>next</ul>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_list[] =
            "<html><head></head><body><ul><li>outer<svg><foreignObject>"
            "<li>inner</li></foreignObject></svg></li><li>next</li></ul>"
            "<ul><li>outer<math><mtext><li>inner</li></mtext></math></li>"
            "<li>next</li></ul></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_list) - 1
                   && memcmp(sink.data, foreign_list, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body></div><div>one</div></div><p>a</div>b</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char block_ends[] =
            "<html><head></head><body><div>one</div><p>ab</p></body></html>";
        TEST_CHECK(sink.length == sizeof(block_ends) - 1
                   && memcmp(sink.data, block_ends, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>before</a>after <a href=x>link</a></a></p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char orphan_anchor[] =
            "<html><head></head><body><p>beforeafter "
            "<a href=\"x\">link</a></p></body></html>";
        TEST_CHECK(sink.length == sizeof(orphan_anchor) - 1
                   && memcmp(sink.data, orphan_anchor, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>before</option>after</p><option>value</option>"
             "</option><p>next</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char option_ends[] =
            "<html><head></head><body><p>beforeafter</p>"
            "<option>value</option><p>next</p></body></html>";
        TEST_CHECK(sink.length == sizeof(option_ends) - 1
                   && memcmp(sink.data, option_ends, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>before</span><span><x-inline>inside</span>after</p>"
             "<div><span>outer<p>inner</span>end</p></div>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char span_ends[] =
            "<html><head></head><body><p>before<span><x-inline>inside"
            "</x-inline></span>after</p><div><span>outer"
            "<p>innerend</p></span></div></body></html>";
        TEST_CHECK(sink.length == sizeof(span_ends) - 1
                   && memcmp(sink.data, span_ends, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<x-shell><x-inner>text</x-shell>"
             "<p>before</x-ghost>after</p></x-shell>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char unknown_ends[] =
            "<html><head></head><body><x-shell><x-inner>text</x-inner>"
            "</x-shell><p>beforeafter</p></body></html>";
        TEST_CHECK(sink.length == sizeof(unknown_ends) - 1
                   && memcmp(sink.data, unknown_ends, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    {
        static const uint8_t input[] =
            "<body>A\0B<table>x\0y</table><svg>v\0w</svg>";
        static const char output[] =
            "<html><head></head><body>ABxy<table></table>"
            "<svg>v\xef\xbf\xbdw</svg></body></html>";
        TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){input, sizeof(input) - 1}, &parse_result,
            &reason, &document) == LXA_OK);
        nodes = lxa_html_document_nodes(document);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes,
            lxa_html_document_root(document), write_bytes, &sink, &reason)
            == LXA_OK);
        TEST_CHECK(sink.length == sizeof(output) - 1
                   && memcmp(sink.data, output, sink.length) == 0);
        lxa_html_document_destroy(document);
    }
    {
        static const uint8_t input[] =
            "<math><mi>A\0B</mi><mrow>C\0D</mrow></math>"
            "<svg><title>E\0F</title><g>G\0H</g></svg>";
        static const char output[] =
            "<html><head></head><body><math><mi>AB</mi>"
            "<mrow>C\xef\xbf\xbd" "D</mrow></math>"
            "<svg><title>EF</title><g>G\xef\xbf\xbd" "H</g></svg>"
            "</body></html>";
        TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){input, sizeof(input) - 1}, &parse_result,
            &reason, &document) == LXA_OK);
        nodes = lxa_html_document_nodes(document);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes,
            lxa_html_document_root(document), write_bytes, &sink, &reason)
            == LXA_OK);
        TEST_CHECK(sink.length == sizeof(output) - 1
                   && memcmp(sink.data, output, sink.length) == 0);
        lxa_html_document_destroy(document);
    }

    /* The ambiguous HTML sniff fixture has a disallowed XML target: the
     * pinned HTML tokenizer emits a bogus comment, not a PI token. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("\xef\xbb\xbf <!-- lead --> <?xml version=\"1.0\"?>"
             "<!DOCTYPE html><a href=\"next\">next</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    {
        lxa_dom_ref_t first, pi_comment, link;
        lxa_dom_record_t record;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_root(document), &first) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, first, &pi_comment)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_read(nodes, pi_comment, &record) == LXA_OK
                   && record.kind == LXA_DOM_KIND_COMMENT);
        TEST_CHECK(lxa_dom_nodes_character_read(nodes, pi_comment, &value)
                   == LXA_OK);
        TEST_CHECK(value.length == sizeof("xml version=\"1.0\"?") - 1
                   && memcmp(value.data, "xml version=\"1.0\"?",
                             value.length) == 0);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &link) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, link, span("href"),
                                                 &attribute) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, attribute,
                                                 &name, &value) == LXA_OK);
        TEST_CHECK(value.length == 4 && memcmp(value.data, "next", 4) == 0);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char sniffed[] =
            "<!-- lead --><!--xml version=\"1.0\"?-->"
            "<!DOCTYPE html><html><head></head><body>"
            "<a href=\"next\">next</a></body></html>";
        TEST_CHECK(sink.length == sizeof(sniffed) - 1
                   && memcmp(sink.data, sniffed, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<!--a--><!doctype html><!doctype ignored><p>x</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char only_first[] =
            "<!--a--><!DOCTYPE html><html><head></head><body>"
            "<p>x</p></body></html>";
        TEST_CHECK(sink.length == sizeof(only_first) - 1
                   && memcmp(sink.data, only_first, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    {
        static const struct {
            const char *input, *public_id, *system_id, *serialized;
            bool has_public, has_system, quirks;
        } cases[] = {
            { "<!doctype html public 'pub' 'sys'><p>x",
              "pub", "sys", "<!DOCTYPE html>"
              "<html><head></head><body><p>x</p></body></html>",
              true, true, false },
            { "<!doctype html public '' ''><p>x",
              "", "", "<!DOCTYPE html>"
              "<html><head></head><body><p>x</p></body></html>",
              true, true, false },
            { "<!doctype html system ''><p>x",
              "", "", "<!DOCTYPE html>"
              "<html><head></head><body><p>x</p></body></html>",
              false, true, false },
            { "<!doctype html public 'pub' INVALID><p>x",
              "pub", "", "<!DOCTYPE html>"
              "<html><head></head><body><p>x</p></body></html>",
              true, false, true }
        };
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            lxa_dom_ref_t doctype;
            lxa_dom_doctype_data_t data;
            TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
                span(cases[i].input), &parse_result, &reason, &document)
                == LXA_OK);
            nodes = lxa_html_document_nodes(document);
            TEST_CHECK(lxa_dom_nodes_first_child(nodes,
                lxa_html_document_root(document), &doctype) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_doctype_read(nodes, doctype, &data)
                       == LXA_OK);
            TEST_CHECK(data.name.length == 4
                       && memcmp(data.name.data, "html", 4) == 0
                       && data.has_public_id == cases[i].has_public
                       && data.has_system_id == cases[i].has_system
                       && data.force_quirks == cases[i].quirks
                       && data.public_id.length == strlen(cases[i].public_id)
                       && data.system_id.length == strlen(cases[i].system_id)
                       && (data.public_id.length == 0
                           || memcmp(data.public_id.data, cases[i].public_id,
                                     data.public_id.length) == 0)
                       && (data.system_id.length == 0
                           || memcmp(data.system_id.data, cases[i].system_id,
                                     data.system_id.length) == 0));
            sink.length = 0;
            TEST_CHECK(lxa_html_serialize_document(nodes,
                lxa_html_document_root(document), write_bytes, &sink, &reason)
                == LXA_OK);
            TEST_CHECK(sink.length == strlen(cases[i].serialized)
                       && memcmp(sink.data, cases[i].serialized,
                                 sink.length) == 0);
            lxa_html_document_destroy(document);
        }
    }
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p><?allowed data?></p>"), &parse_result,
        &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char pi[] =
            "<html><head></head><body><p><?allowed data?></p>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(pi) - 1
                   && memcmp(sink.data, pi, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    {
        static const uint8_t input[] = "<p><?custom a\0b?></p>";
        static const uint8_t expected_pi[] =
            "<html><head></head><body><p><?custom a\0b?></p></body></html>";
        lxa_dom_ref_t paragraph, pi;
        lxa_dom_record_t record;
        lxa_span_t target, pi_data;
        TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){input, sizeof(input) - 1}, &parse_result,
            &reason, &document) == LXA_OK);
        nodes = lxa_html_document_nodes(document);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &paragraph) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, paragraph, &pi) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_read(nodes, pi, &record) == LXA_OK
                   && record.kind == LXA_DOM_KIND_PROCESSING_INSTRUCTION);
        TEST_CHECK(lxa_dom_nodes_pi_read(nodes, pi, &target, &pi_data) == LXA_OK);
        TEST_CHECK(target.length == 6
                   && memcmp(target.data, "custom", 6) == 0
                   && pi_data.length == 3
                   && memcmp(pi_data.data, "a\0b", 3) == 0);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        TEST_CHECK(lxa_buffer_init(&text, &allocator) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_text_content(nodes, paragraph, &text, 16)
                   == LXA_OK && text.length == 0);
        lxa_buffer_destroy(&text);
        sink.length = 0;
        TEST_CHECK(lxa_html_serialize_document(nodes,
            lxa_html_document_root(document), write_bytes, &sink, &reason)
            == LXA_OK);
        TEST_CHECK(sink.length == sizeof(expected_pi) - 1
                   && memcmp(sink.data, expected_pi, sink.length) == 0);
        lxa_html_document_destroy(document);
    }
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<?pre x?><!doctype html><p>body</p>"
             "</body></html><?post y?>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char positioned[] =
            "<?pre x?><!DOCTYPE html><html><head></head><body>"
            "<p>body</p></body></html><?post y?>";
        TEST_CHECK(sink.length == sizeof(positioned) - 1
                   && memcmp(sink.data, positioned, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<iframe src=frame><a href=wrong>skip</a></iframe>"
             "<noembed><img src=also-wrong></noembed>"
             "<a href=right>keep</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    {
        lxa_dom_ref_t iframe, noembed, link, raw;
        lxa_dom_record_t record;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &iframe) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, iframe, &raw) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_read(nodes, raw, &record) == LXA_OK
                   && record.kind == LXA_DOM_KIND_TEXT);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, iframe, &noembed) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, noembed, &link) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, link, span("href"),
                                                 &attribute) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, attribute,
                                                 &name, &value) == LXA_OK);
        TEST_CHECK(value.length == 5 && memcmp(value.data, "right", 5) == 0);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char rawtext[] =
            "<html><head></head><body><iframe src=\"frame\">"
            "<a href=wrong>skip</a></iframe><noembed>"
            "<img src=also-wrong></noembed>"
            "<a href=\"right\">keep</a></body></html>";
        TEST_CHECK(sink.length == sizeof(rawtext) - 1
                   && memcmp(sink.data, rawtext, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><noframes><a href=wrong>head</a></noframes></head>"
             "<body><noframes><img src=wrong></noframes>"
             "<a href=right>body</a></body>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    {
        lxa_dom_ref_t ignored, link;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &ignored) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, ignored, &link) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, link, span("href"),
                                                 &attribute) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, attribute,
                                                 &name, &value) == LXA_OK);
        TEST_CHECK(value.length == 5 && memcmp(value.data, "right", 5) == 0);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char frames[] =
            "<html><head><noframes><a href=wrong>head</a></noframes>"
            "</head><body><noframes><img src=wrong></noframes>"
            "<a href=\"right\">body</a></body></html>";
        TEST_CHECK(sink.length == sizeof(frames) - 1
                   && memcmp(sink.data, frames, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>intro<xmp><a href=wrong>raw</a></xmp>"
             "<p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    {
        lxa_dom_ref_t first, raw, next, raw_text;
        lxa_dom_record_t record;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &first) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, first, &raw) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, raw, &raw_text) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_read(nodes, raw_text, &record) == LXA_OK
                   && record.kind == LXA_DOM_KIND_TEXT);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, raw, &next) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_name(nodes, next, &name) == LXA_OK
                   && name.length == 1 && name.data[0] == 'p');
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char rawtext[] =
            "<html><head></head><body><p>intro</p>"
            "<xmp><a href=wrong>raw</a></xmp>"
            "<p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(rawtext) - 1
                   && memcmp(sink.data, rawtext, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p><image SRC=one alt=x></p>"
             "<table><image src=two><tr><td>cell</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    {
        lxa_dom_ref_t paragraph, image;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &paragraph) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, paragraph, &image) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_name(nodes, image, &name) == LXA_OK
                   && name.length == 3
                   && memcmp(name.data, "img", 3) == 0);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, image, span("src"),
                                                 &attribute) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, attribute,
                                                 &name, &value) == LXA_OK);
        TEST_CHECK(value.length == 3 && memcmp(value.data, "one", 3) == 0);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char images[] =
            "<html><head></head><body><p><img src=\"one\" alt=\"x\"></p>"
            "<img src=\"two\"><table><tbody><tr><td>cell</td></tr>"
            "</tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(images) - 1
                   && memcmp(sink.data, images, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td>first</td></tr>"
             "<table><tr><td>second</td></tr></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char adjacent_tables[] =
            "<html><head></head><body><table><tbody><tr><td>first</td></tr>"
            "</tbody></table><table><tbody><tr><td>second</td></tr>"
            "</tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(adjacent_tables) - 1
                   && memcmp(sink.data, adjacent_tables, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><table><tr><td>inner</td></tr></table>"
             "</td></tr></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char nested_table[] =
            "<html><head></head><body><table><tbody><tr><td><table><tbody>"
            "<tr><td>inner</td></tr></tbody></table></td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_table) - 1
                   && memcmp(sink.data, nested_table, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg viewbox='0 0 1 1' xlink:href='#a' xml:lang='en'"
             " xmlns:xlink='urn:x'><lineargradient id=x />"
             "<foreignobject><p>HTML</p>"
             "</foreignobject><g><textpath>SVG</textpath></g></svg>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t svg, gradient, foreign, paragraph, attribute;
        lxa_span_t name, local;
        lxa_dom_namespace_t ns;
        lxa_dom_attr_namespace_t attr_ns;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &svg) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, svg, span("viewBox"),
                                                &attribute) == LXA_OK
                   && attribute.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, svg, span("viewbox"),
                                                &attribute) == LXA_OK
                   && attribute.handle == LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, svg, span("xlink:href"),
                                                &attribute) == LXA_OK
                   && attribute.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_attribute_namespace(nodes, attribute,
                                                     &attr_ns, &local) == LXA_OK
                   && attr_ns == LXA_DOM_ATTR_NS_XLINK
                   && local.length == 4
                   && memcmp(local.data, "href", 4) == 0);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, svg, span("xml:lang"),
                                                &attribute) == LXA_OK
                   && attribute.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_attribute_namespace(nodes, attribute,
                                                     &attr_ns, &local) == LXA_OK
                   && attr_ns == LXA_DOM_ATTR_NS_XML
                   && local.length == 4
                   && memcmp(local.data, "lang", 4) == 0);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, svg, span("xmlns:xlink"),
                                                &attribute) == LXA_OK
                   && attribute.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_attribute_namespace(nodes, attribute,
                                                     &attr_ns, &local) == LXA_OK
                   && attr_ns == LXA_DOM_ATTR_NS_XMLNS
                   && local.length == 5
                   && memcmp(local.data, "xlink", 5) == 0);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, svg, &gradient) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_name(nodes, gradient, &name) == LXA_OK);
        TEST_CHECK(name.length == sizeof("linearGradient") - 1
                   && memcmp(name.data, "linearGradient", name.length) == 0);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, gradient, &foreign)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_name(nodes, foreign, &name) == LXA_OK);
        TEST_CHECK(name.length == sizeof("foreignObject") - 1
                   && memcmp(name.data, "foreignObject", name.length) == 0);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, foreign, &paragraph)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, paragraph, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char canonical_svg[] =
            "<html><head></head><body><svg viewBox=\"0 0 1 1\""
            " xlink:href=\"#a\" xml:lang=\"en\" xmlns:xlink=\"urn:x\">"
            "<linearGradient id=\"x\">"
            "</linearGradient><foreignObject><p>HTML</p></foreignObject>"
            "<g><textPath>SVG</textPath></g></svg></body></html>";
        TEST_CHECK(sink.length == sizeof(canonical_svg) - 1
                   && memcmp(sink.data, canonical_svg, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<math xmlns='urn:m'><mi definitionurl='urn:math'>x</mi></math>"
             "<p definitionurl='html' xml:lang='en'>y</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t math, paragraph, attribute;
        lxa_dom_attr_namespace_t attr_ns;
        lxa_span_t local;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &math) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, math, span("xmlns"),
                                                &attribute) == LXA_OK
                   && attribute.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_attribute_namespace(nodes, attribute,
                                                     &attr_ns, &local) == LXA_OK
                   && attr_ns == LXA_DOM_ATTR_NS_XMLNS
                   && local.length == 5
                   && memcmp(local.data, "xmlns", 5) == 0);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, math, &paragraph)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, paragraph,
            span("xml:lang"), &attribute) == LXA_OK
            && attribute.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_attribute_namespace(nodes, attribute,
                                                     &attr_ns, &local) == LXA_OK
                   && attr_ns == LXA_DOM_ATTR_NS_NONE
                   && local.length == 8
                   && memcmp(local.data, "xml:lang", 8) == 0);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_attribute[] =
            "<html><head></head><body><math xmlns=\"urn:m\">"
            "<mi definitionURL=\"urn:math\">x</mi></math>"
            "<p definitionurl=\"html\" xml:lang=\"en\">y</p></body></html>";
        TEST_CHECK(sink.length == sizeof(math_attribute) - 1
                   && memcmp(sink.data, math_attribute, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g></missing><path>kept</path></g></svg>"
             "<math><mrow></unknown>more</mrow></math>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char ignored_foreign_end[] =
            "<html><head></head><body><svg><g><path>kept</path></g></svg>"
            "<math><mrow>more</mrow></math></body></html>";
        TEST_CHECK(sink.length == sizeof(ignored_foreign_end) - 1
                   && memcmp(sink.data, ignored_foreign_end,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g></span><path>kept</path></g></svg>"
             "<span><svg><g>inside</span><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_span_end[] =
            "<html><head></head><body><svg><g><path>kept</path></g></svg>"
            "<span><svg><g>inside</g></svg></span><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_span_end) - 1
                   && memcmp(sink.data, foreign_span_end, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<section><svg><g>svg</section><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_section_end[] =
            "<html><head></head><body><section><svg><g>svg</g></svg>"
            "</section><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_section_end) - 1
                   && memcmp(sink.data, foreign_section_end,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><image href='x'/><g><![CDATA[<a href='no'>&amp;]]>"
             "</g></svg><math><mrow><![CDATA[x&y]]></mrow></math>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    {
        lxa_dom_ref_t svg, image;
        lxa_dom_namespace_t ns;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &svg) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, svg, &ns) == LXA_OK
                   && ns == LXA_DOM_NS_SVG);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, svg, &image) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_name(nodes, image, &name) == LXA_OK
                   && name.length == 5
                   && memcmp(name.data, "image", 5) == 0);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign[] =
            "<html><head></head><body><svg><image href=\"x\"></image>"
            "<g>&lt;a href='no'&gt;&amp;amp;</g></svg>"
            "<math><mrow>x&amp;y</mrow></math></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign) - 1
                   && memcmp(sink.data, foreign, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><template><g></g></template></svg>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t svg, foreign_template;
        lxa_dom_namespace_t ns;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &svg) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, svg, &foreign_template)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, foreign_template, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_SVG);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_template[] =
            "<html><head></head><body><svg><template><g></g></template>"
            "</svg></body></html>";
        TEST_CHECK(sink.length == sizeof(svg_template) - 1
                   && memcmp(sink.data, svg_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mi>x</mi><mn>2</mn></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math[] =
            "<html><head><template><math><mi>x</mi><mn>2</mn>"
            "</math></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math) - 1
                   && memcmp(sink.data, template_math, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mi><b>x</b></mi><mtext>"
             "<em>y</em></mtext></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_html[] =
            "<html><head><template><math><mi><b>x</b></mi>"
            "<mtext><em>y</em></mtext></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_html) - 1
                   && memcmp(sink.data, template_math_html,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><div id=d>x</div></mtext>"
             "<mn><span>y</span></mn></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_blocks[] =
            "<html><head><template><math><mtext><div id=\"d\">x"
            "</div></mtext><mn><span>y</span></mn></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_blocks) - 1
                   && memcmp(sink.data, template_math_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mi><img src=x></mi><mtext>"
             "<br><hr></mtext></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_voids[] =
            "<html><head><template><math><mi><img src=\"x\"></mi>"
            "<mtext><br><hr></mtext></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_voids) - 1
                   && memcmp(sink.data, template_math_voids,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><select><optgroup label=a>"
             "<option>x</option></optgroup></select></mtext>"
             "</math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_select[] =
            "<html><head><template><math><mtext><select>"
            "<optgroup label=\"a\"><option>x</option></optgroup>"
            "</select></mtext></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_select) - 1
                   && memcmp(sink.data, template_math_select,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><select><option>a"
             "<input type=text>b</select></mtext></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_select_input[] =
            "<html><head><template><math><mtext><select>"
            "<option>a</option></select><input type=\"text\">b"
            "</mtext></math></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_select_input) - 1
                   && memcmp(sink.data, template_math_select_input,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><textarea>a&amp;b</textarea>"
             "<title>t&amp;u</title><style>b{c:d}</style>"
             "</mtext></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_text_modes[] =
            "<html><head><template><math><mtext>"
            "<textarea>a&amp;b</textarea><title>t&amp;u</title>"
            "<style>b{c:d}</style></mtext></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_text_modes) - 1
                   && memcmp(sink.data, template_math_text_modes,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><ruby>x<rt>a</rt></ruby>"
             "</mtext><annotation-xml encoding=text/html>"
             "<ruby>y<rp>(<rt>b<rp>)</ruby></annotation-xml>"
             "</math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_ruby[] =
            "<html><head><template><math><mtext><ruby>x<rt>a</rt>"
            "</ruby></mtext><annotation-xml encoding=\"text/html\">"
            "<ruby>y<rp>(</rp><rt>b</rt><rp>)</rp></ruby>"
            "</annotation-xml></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_ruby) - 1
                   && memcmp(sink.data, template_math_ruby,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><div><math><mrow><p>x</p></mrow>"
             "</math>y</div><math><mrow><mrow><div>z</div>"
             "</mrow></mrow></math><math><mrow><ruby>r</ruby>"
             "</mrow></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_block_breakout[] =
            "<html><head><template><div><math><mrow></mrow></math>"
            "<p>x</p>y</div><math><mrow><mrow></mrow></mrow></math>"
            "<div>z</div><math><mrow></mrow></math><ruby>r</ruby>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_block_breakout) - 1
                   && memcmp(sink.data, template_math_block_breakout,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mrow><span>x</span></mrow></math>"
             "<math><mrow><b>y</b></mrow></math>"
             "<math><mrow><ul><li>z</li></ul></mrow></math>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_inline_list_breakout[] =
            "<html><head><template><math><mrow></mrow></math>"
            "<span>x</span><math><mrow></mrow></math><b>y</b>"
            "<math><mrow></mrow></math><ul><li>z</li></ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_inline_list_breakout) - 1
                   && memcmp(sink.data, template_math_inline_list_breakout,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mrow><section>x</section>"
             "<article>y</article><p>z</p></mrow></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t element, content, math, row, section, article, paragraph;
        lxa_dom_namespace_t ns;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_head(document), &element) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_template_contents(nodes, element,
            &content) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, content, &math)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, math, &row)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, row, &section)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, section, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_MATHML);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, section, &article)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, article, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_MATHML);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, math, &paragraph)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, paragraph, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_foreign_blocks[] =
            "<html><head><template><math><mrow><section>x</section>"
            "<article>y</article></mrow></math><p>z</p></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(math_foreign_blocks) - 1
                   && memcmp(sink.data, math_foreign_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><annotation-xml encoding=application/xml>"
             "<mi>a</mi></annotation-xml><annotation-xml "
             "encoding='TeXt/HtMl'><div>x</div></annotation-xml>"
             "</math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_annotations[] =
            "<html><head><template><math><annotation-xml "
            "encoding=\"application/xml\"><mi>a</mi></annotation-xml>"
            "<annotation-xml encoding=\"TeXt/HtMl\"><div>x</div>"
            "</annotation-xml></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_annotations) - 1
                   && memcmp(sink.data, template_annotations,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><svg><g>x</g></svg></mtext>"
             "<mrow><svg><g>y</g></svg></mrow></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_math_svg[] =
            "<html><head><template><math><mtext><svg><g>x</g>"
            "</svg></mtext><mrow><svg><g>y</g></svg></mrow>"
            "</math></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_math_svg) - 1
                   && memcmp(sink.data, template_math_svg,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><annotation-xml encoding=application/xml>"
             "<svg><g>x</g></svg></annotation-xml></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_annotation_svg[] =
            "<html><head><template><math><annotation-xml "
            "encoding=\"application/xml\"><svg><g>x</g></svg>"
            "</annotation-xml></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_annotation_svg) - 1
                   && memcmp(sink.data, template_annotation_svg,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><p>x</p></g></svg>"
             "<svg><g><g><div>y</div></g></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_breakout[] =
            "<html><head><template><svg><g></g></svg><p>x</p>"
            "<svg><g><g></g></g></svg><div>y</div></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_breakout) - 1
                   && memcmp(sink.data, template_svg_breakout,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><section>x</section>"
             "<p>y</p></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t element, content, svg, group, section, paragraph;
        lxa_dom_namespace_t ns;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_head(document), &element) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_template_contents(nodes, element,
            &content) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, content, &svg)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, svg, &group)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, group, &section)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, section, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_SVG);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, svg, &paragraph)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, paragraph, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_foreign_section[] =
            "<html><head><template><svg><g><section>x</section>"
            "</g></svg><p>y</p></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(svg_foreign_section) - 1
                   && memcmp(sink.data, svg_foreign_section,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><blockquote>x</blockquote></g></svg>"
             "<svg><g><center>y</center></g></svg>"
             "<svg><g><dl><dt>a</dt><dd>b</dd></dl></g></svg>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_definition_breakout[] =
            "<html><head><template><svg><g></g></svg>"
            "<blockquote>x</blockquote><svg><g></g></svg>"
            "<center>y</center><svg><g></g></svg>"
            "<dl><dt>a</dt><dd>b</dd></dl></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_definition_breakout) - 1
                   && memcmp(sink.data, template_svg_definition_breakout,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><b>x</b></g></svg>"
             "<svg><g><span>y</span></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_inline[] =
            "<html><head><template><svg><g></g></svg><b>x</b>"
            "<svg><g></g></svg><span>y</span></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_inline) - 1
                   && memcmp(sink.data, template_svg_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><sub>x</sub></g></svg>"
             "<svg><g><sup>y</sup></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_sub_sup_breakout[] =
            "<html><head><template><svg><g></g></svg><sub>x</sub>"
            "<svg><g></g></svg><sup>y</sup></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_sub_sup_breakout) - 1
                   && memcmp(sink.data, template_svg_sub_sup_breakout,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><big>a</big></g></svg>"
             "<svg><g><small>b</small></g></svg>"
             "<svg><g><s>c</s></g></svg>"
             "<svg><g><strike>d</strike></g></svg>"
             "<svg><g><tt>e</tt></g></svg>"
             "<svg><g><u>f</u></g></svg>"
             "<svg><g><var>g</var></g></svg>"
             "<svg><g><nobr>h</nobr></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_more_inline[] =
            "<html><head><template><svg><g></g></svg><big>a</big>"
            "<svg><g></g></svg><small>b</small>"
            "<svg><g></g></svg><s>c</s>"
            "<svg><g></g></svg><strike>d</strike>"
            "<svg><g></g></svg><tt>e</tt>"
            "<svg><g></g></svg><u>f</u>"
            "<svg><g></g></svg><var>g</var>"
            "<svg><g></g></svg><nobr>h</nobr></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_more_inline) - 1
                   && memcmp(sink.data, template_svg_more_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><embed src=x></g></svg>"
             "<svg><g><meta name=x></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_embed_meta[] =
            "<html><head><template><svg><g></g></svg><embed src=\"x\">"
            "<svg><g></g></svg><meta name=\"x\"></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_embed_meta) - 1
                   && memcmp(sink.data, template_svg_embed_meta,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><menu><li>x</li></menu></g></svg>"
             "<svg><g><listing>\ny</listing></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_menu_listing[] =
            "<html><head><template><svg><g></g></svg>"
            "<menu><li>x</li></menu><svg><g></g></svg>"
            "<listing>y</listing></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_menu_listing) - 1
                   && memcmp(sink.data, template_svg_menu_listing,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><font color=red>x</font></g></svg>"
             "<svg><g><font face=serif>y</font></g></svg>"
             "<svg><g><font>z</font></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_font_breakout[] =
            "<html><head><template><svg><g></g></svg>"
            "<font color=\"red\">x</font><svg><g></g></svg>"
            "<font face=\"serif\">y</font><svg><g><font>z</font>"
            "</g></svg></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_font_breakout) - 1
                   && memcmp(sink.data, template_svg_font_breakout,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><body id=x>text</body>"
             "</g></svg><svg><body>more</body></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_body_breakout[] =
            "<html><head><template><svg><g></g></svg>text"
            "<svg></svg>more</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_body_breakout) - 1
                   && memcmp(sink.data, template_svg_body_breakout,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><ruby><span>x</span></ruby>"
             "</g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_ruby_breakout[] =
            "<html><head><template><svg><g></g></svg>"
            "<ruby><span>x</span></ruby></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_ruby_breakout) - 1
                   && memcmp(sink.data, template_svg_ruby_breakout,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ruby>base<rp>(<rt>a<rp>)</ruby>"
             "<svg><g><ruby>base<rt>a<rt>b</ruby></g></svg>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_ruby_annotations[] =
            "<html><head><template><ruby>base<rp>(</rp>"
            "<rt>a</rt><rp>)</rp></ruby><svg><g></g></svg>"
            "<ruby>base<rt>a</rt><rt>b</rt></ruby></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_ruby_annotations) - 1
                   && memcmp(sink.data, template_ruby_annotations,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ruby><span>x<rt>a<rt>b</ruby>"
             "<ruby><rt>x<span>y<rt>z</ruby></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_ruby_annotations[] =
            "<html><head><template><ruby><span>x<rt>a</rt>"
            "<rt>b</rt></span></ruby><ruby><rt>x<span>y<rt>z</rt>"
            "</span></rt></ruby></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_ruby_annotations) - 1
                   && memcmp(sink.data, nested_ruby_annotations,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><div><svg><g>x</g></svg><math><mi>y</mi>"
             "</math></div></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_nested_foreign[] =
            "<html><head><template><div><svg><g>x</g></svg>"
            "<math><mi>y</mi></math></div></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_nested_foreign) - 1
                   && memcmp(sink.data, template_nested_foreign,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><title><b>x</b></title>"
             "<desc><span>y</span></desc></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_integration[] =
            "<html><head><template><svg><title><b>x</b></title>"
            "<desc><span>y</span></desc></svg></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_integration) - 1
                   && memcmp(sink.data, template_svg_integration,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><foreignObject><p>x</p>"
             "<span>y</span></foreignObject></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_foreign_object[] =
            "<html><head><template><svg><foreignObject>"
            "<p>x</p><span>y</span></foreignObject></svg>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_foreign_object) - 1
                   && memcmp(sink.data, template_foreign_object,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><br><img src=/x><hr></g>"
             "</svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_voids[] =
            "<html><head><template><svg><g></g></svg>"
            "<br><img src=\"/x\"><hr></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_voids) - 1
                   && memcmp(sink.data, template_svg_voids,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><ul><li>x</li></ul></g></svg>"
             "<svg><g><ol><li>y</li></ol></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_lists[] =
            "<html><head><template><svg><g></g></svg><ul><li>x"
            "</li></ul><svg><g></g></svg><ol><li>y</li></ol>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_lists) - 1
                   && memcmp(sink.data, template_svg_lists,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><h2>x</h2></g></svg>"
             "<svg><g><table><td>y</table></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_svg_blocks[] =
            "<html><head><template><svg><g></g></svg><h2>x</h2>"
            "<svg><g></g></svg><table><tbody><tr><td>y</td></tr>"
            "</tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_svg_blocks) - 1
                   && memcmp(sink.data, template_svg_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><![CDATA[one]]></svg><p><![CDATA[two]]></p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char cdata_boundary[] =
            "<html><head></head><body><svg>one</svg>"
            "<p><!--[CDATA[two]]--></p></body></html>";
        TEST_CHECK(sink.length == sizeof(cdata_boundary) - 1
                   && memcmp(sink.data, cdata_boundary, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><path><![CDATA[x&y]]></svg><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char recovered[] =
            "<html><head></head><body><svg><g><path>x&amp;y</path></g>"
            "</svg><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(recovered) - 1
                   && memcmp(sink.data, recovered, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><title><![CDATA[x]]>plain</title>"
             "<desc>description</desc><g><![CDATA[y]]></g></svg>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char integration[] =
            "<html><head></head><body><svg>"
            "<title><!--[CDATA[x]]-->plain</title>"
            "<desc>description</desc><g>y</g></svg></body></html>";
        TEST_CHECK(sink.length == sizeof(integration) - 1
                   && memcmp(sink.data, integration, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><title><b>bold</b><![CDATA[x]]></title>"
             "<desc><span>text</span></desc>"
             "<g><![CDATA[y]]></g></svg>"),
        &parse_result, &reason, &document) == LXA_OK);
    {
        lxa_dom_ref_t svg, title, bold;
        lxa_dom_namespace_t ns;
        nodes = lxa_html_document_nodes(document);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &svg) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, svg, &title) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, title, &bold) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, title, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_SVG);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, bold, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char integration_children[] =
            "<html><head></head><body><svg>"
            "<title><b>bold</b><!--[CDATA[x]]--></title>"
            "<desc><span>text</span></desc><g>y</g>"
            "</svg></body></html>";
        TEST_CHECK(sink.length == sizeof(integration_children) - 1
                   && memcmp(sink.data, integration_children,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<math><annotation-xml encoding='TeXt/HtMl'>"
             "<a href=x>link</a><![CDATA[not cdata]]>"
             "</annotation-xml><annotation-xml encoding='other'>"
             "<![CDATA[raw]]></annotation-xml></math>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t math, annotation, link;
        lxa_dom_namespace_t ns;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &math) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, math, &annotation)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, annotation, &link)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, annotation, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_MATHML);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, link, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char annotations[] =
            "<html><head></head><body><math>"
            "<annotation-xml encoding=\"TeXt/HtMl\"><a href=\"x\">link</a>"
            "<!--[CDATA[not cdata]]--></annotation-xml>"
            "<annotation-xml encoding=\"other\">raw</annotation-xml>"
            "</math></body></html>";
        TEST_CHECK(sink.length == sizeof(annotations) - 1
                   && memcmp(sink.data, annotations, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<math><annotation-xml encoding='APPLICATION/XHTML+XML'>"
             "<span>html</span></annotation-xml>"
             "<annotation-xml><svg><g>svg</g></svg></annotation-xml></math>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char annotations[] =
            "<html><head></head><body><math>"
            "<annotation-xml encoding=\"APPLICATION/XHTML+XML\">"
            "<span>html</span></annotation-xml>"
            "<annotation-xml><svg><g>svg</g></svg></annotation-xml>"
            "</math></body></html>";
        TEST_CHECK(sink.length == sizeof(annotations) - 1
                   && memcmp(sink.data, annotations, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<math><mi><b>bold</b><img src='icon'>"
             "<mglyph/>text</mi><mrow><![CDATA[y]]></mrow></math>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t math, mi, bold, image, glyph;
        lxa_dom_namespace_t ns;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &math) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, math, &mi) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, mi, &bold) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, bold, &image) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, image, &glyph) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, mi, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_MATHML);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, bold, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, image, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, glyph, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_MATHML);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_text[] =
            "<html><head></head><body><math><mi><b>bold</b>"
            "<img src=\"icon\"><mglyph></mglyph>text</mi>"
            "<mrow>y</mrow></math></body></html>";
        TEST_CHECK(sink.length == sizeof(math_text) - 1
                   && memcmp(sink.data, math_text, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><title><a href='next'>Link</a>"
             "<img src='icon'></title><g><![CDATA[x]]></g></svg>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_ref_t svg, title, link, image;
        lxa_dom_namespace_t ns;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &svg) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, svg, &title) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, title, &link) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, link, &image) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, link, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, image, &ns)
                   == LXA_OK && ns == LXA_DOM_NS_HTML);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char integration_links[] =
            "<html><head></head><body><svg><title>"
            "<a href=\"next\">Link</a><img src=\"icon\"></title>"
            "<g>x</g></svg></body></html>";
        TEST_CHECK(sink.length == sizeof(integration_links) - 1
                   && memcmp(sink.data, integration_links, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><p>after<![CDATA[not foreign]]></p></svg>"
             "<div>next</div>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char breakout[] =
            "<html><head></head><body><svg><g></g></svg>"
            "<p>after<!--[CDATA[not foreign]]--></p>"
            "<div>next</div></body></html>";
        TEST_CHECK(sink.length == sizeof(breakout) - 1
                   && memcmp(sink.data, breakout, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><img src='pic'><span>after</span>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char image_breakout[] =
            "<html><head></head><body><svg><g></g></svg>"
            "<img src=\"pic\"><span>after</span></body></html>";
        TEST_CHECK(sink.length == sizeof(image_breakout) - 1
                   && memcmp(sink.data, image_breakout, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><h2>Heading</h2><p>Body</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char heading_breakout[] =
            "<html><head></head><body><svg><g></g></svg>"
            "<h2>Heading</h2><p>Body</p></body></html>";
        TEST_CHECK(sink.length == sizeof(heading_breakout) - 1
                   && memcmp(sink.data, heading_breakout, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><ul><li>one<li>two</ul>"
             "<math><mrow><ol><li>three</ol>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char list_breakout[] =
            "<html><head></head><body><svg><g></g></svg>"
            "<ul><li>one</li><li>two</li></ul>"
            "<math><mrow></mrow></math><ol><li>three</li></ol>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(list_breakout) - 1
                   && memcmp(sink.data, list_breakout, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><blockquote>quoted</blockquote>"
             "<math><mrow><pre>  code</pre><dl><dt>term<dd>value</dl>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char block_breakout[] =
            "<html><head></head><body><svg><g></g></svg>"
            "<blockquote>quoted</blockquote><math><mrow></mrow></math>"
            "<pre>  code</pre><dl><dt>term</dt><dd>value</dd></dl>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(block_breakout) - 1
                   && memcmp(sink.data, block_breakout, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>before</table>after</p>"
             "<table><tr><td>cell</td></tr></table></table><p>next</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char orphan_table_end[] =
            "<html><head></head><body><p>beforeafter</p>"
            "<table><tbody><tr><td>cell</td></tr></tbody></table>"
            "<p>next</p></body></html>";
        TEST_CHECK(sink.length == sizeof(orphan_table_end) - 1
                   && memcmp(sink.data, orphan_table_end, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>before</td>after</p><table></tbody><tr><td>one</th>"
             "two</td></thead></tr></table></tfoot><p>next</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char orphan_table_parts[] =
            "<html><head></head><body><p>beforeafter</p>"
            "<table><tbody><tr><td>onetwo</td></tr></tbody></table>"
            "<p>next</p></body></html>";
        TEST_CHECK(sink.length == sizeof(orphan_table_parts) - 1
                   && memcmp(sink.data, orphan_table_parts, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><table><tr><td>cell</td></tr></table><p>next</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_breakout[] =
            "<html><head></head><body><svg><g></g></svg>"
            "<table><tbody><tr><td>cell</td></tr></tbody></table>"
            "<p>next</p></body></html>";
        TEST_CHECK(sink.length == sizeof(table_breakout) - 1
                   && memcmp(sink.data, table_breakout, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><b>bold</b><em>em</em>"
             "<math><mrow><strong>strong</strong>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char inline_breakout[] =
            "<html><head></head><body><svg><g></g></svg>"
            "<b>bold</b><em>em</em><math><mrow></mrow></math>"
            "<strong>strong</strong></body></html>";
        TEST_CHECK(sink.length == sizeof(inline_breakout) - 1
                   && memcmp(sink.data, inline_breakout, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p><acronym title='W3C'>W3C</acronym> "
             "<var>x</var><sub>1</sub><sup>2</sup></p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char inline_semantic[] =
            "<html><head></head><body><p><acronym title=\"W3C\">W3C"
            "</acronym> <var>x</var><sub>1</sub><sup>2</sup></p>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(inline_semantic) - 1
                   && memcmp(sink.data, inline_semantic, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>outer<svg><title><p>inner</p><div>two</div></title>"
             "<g><![CDATA[after]]></g></svg>tail"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char integrated_blocks[] =
            "<html><head></head><body><p>outer<svg><title><p>inner</p>"
            "<div>two</div></title><g>after</g></svg>tail</p>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(integrated_blocks) - 1
                   && memcmp(sink.data, integrated_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><form action=/x><form action=/ignored>"
             "<tr><form action=/also-ignored><td>cell</td></tr></table></form>"
             "<form action=/later><p>after</p></form>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_form[] =
            "<html><head></head><body><table><form action=\"/x\"></form>"
            "<tbody><tr><td>cell</td></tr></tbody></table>"
            "<form action=\"/later\"><p>after</p></form></body></html>";
        TEST_CHECK(sink.length == sizeof(table_form) - 1
                   && memcmp(sink.data, table_form, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits = lxa_limits_default();
    limits.max_nodes = 5; /* document/html/head/body/table precede form */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><form action=/x><tr><td>cell</td></tr></table>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tbody><tr><form action=/row>"
             "<td>cell</td></tr></tbody></table></form>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char row_form[] =
            "<html><head></head><body><table><tbody><tr>"
            "<form action=\"/row\"></form><td>cell</td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(row_form) - 1
                   && memcmp(sink.data, row_form, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    for (i = 0; i < 256; ++i) {
        lxa_status_t injected_status;
        failing.calls = 0;
        failing.fail_at = i;
        injected_status = lxa_html_document_parse(&injected, NULL,
            span("<table><tbody><tr><form action=/row>"
                 "<td>cell</td></tr></tbody></table></form>"),
            &parse_result, &reason, &document);
        if (injected_status == LXA_OK) {
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(injected_status == LXA_ERROR_NO_MEMORY
                   && document == NULL);
    }
    TEST_CHECK(i < 256);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><caption><span>Heading</span></caption>"
             "<tr><td>cell</td></tr></table>"), &parse_result,
        &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char caption[] =
            "<html><head></head><body><table><caption><span>Heading"
            "</span></caption><tbody><tr><td>cell</td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(caption) - 1
                   && memcmp(sink.data, caption, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p><a href=x>one<hr><p>two"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char split[] =
            "<html><head></head><body><p><a href=\"x\">one</a></p>"
            "<hr><p><a href=\"x\">two</a></p></body></html>";
        TEST_CHECK(sink.length == sizeof(split) - 1
                   && memcmp(sink.data, split, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p><span>intro<hr><p>later</p>"
             "<p><span>before<div>block</div>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char closed[] =
            "<html><head></head><body><p><span>intro</span></p><hr>"
            "<p>later</p><p><span>before</span></p>"
            "<div>block</div></body></html>";
        TEST_CHECK(sink.length == sizeof(closed) - 1
                   && memcmp(sink.data, closed, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p><span>first</p><p>second</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char end_p[] =
            "<html><head></head><body><p><span>first</span></p>"
            "<p>second</p></body></html>";
        TEST_CHECK(sink.length == sizeof(end_p) - 1
                   && memcmp(sink.data, end_p, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<div>x</p>after</div>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char stray[] =
            "<html><head></head><body><div>x<p></p>after</div>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(stray) - 1
                   && memcmp(sink.data, stray, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<section><div><span><b>first</div><p>next</p></section>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char block[] =
            "<html><head></head><body><section><div><span><b>first"
            "</b></span></div><p>next</p></section></body></html>";
        TEST_CHECK(sink.length == sizeof(block) - 1
                   && memcmp(sink.data, block, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<h2><span>Title</h2><p>body</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char heading_end[] =
            "<html><head></head><body><h2><span>Title</span></h2>"
            "<p>body</p></body></html>";
        TEST_CHECK(sink.length == sizeof(heading_end) - 1
                   && memcmp(sink.data, heading_end, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<h2><span>Title</h1></h6><p>body</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char recovered_heading[] =
            "<html><head></head><body><h2><span>Title</span></h2>"
            "<p>body</p></body></html>";
        TEST_CHECK(sink.length == sizeof(recovered_heading) - 1
                   && memcmp(sink.data, recovered_heading,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<h1><span>first<h2>second</h2>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char adjacent_headings[] =
            "<html><head></head><body><h1><span>first</span></h1>"
            "<h2>second</h2></body></html>";
        TEST_CHECK(sink.length == sizeof(adjacent_headings) - 1
                   && memcmp(sink.data, adjacent_headings, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p><i>em</i> <del>gone</del> <s>old</s> "
             "<strike>x</strike> <kbd><samp>key</samp></kbd></p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char inline_markup[] =
            "<html><head></head><body><p><i>em</i> <del>gone</del> "
            "<s>old</s> <strike>x</strike> <kbd><samp>key</samp></kbd>"
            "</p></body></html>";
        TEST_CHECK(sink.length == sizeof(inline_markup) - 1
                   && memcmp(sink.data, inline_markup, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<figure><figcaption>caption</figcaption><p>x</p></figure>"
             "<details><summary>title</summary><p>body</p></details>"
             "<listing>\nline\n</listing>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char blocks[] =
            "<html><head></head><body><figure><figcaption>caption"
            "</figcaption><p>x</p></figure><details><summary>title"
            "</summary><p>body</p></details><listing>line\n</listing>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(blocks) - 1
                   && memcmp(sink.data, blocks, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>before<button><span>skip<button>other</button>"
             "<p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char buttons[] =
            "<html><head></head><body><p>before</p>"
            "<button><span>skip</span></button><button>other</button>"
            "<p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(buttons) - 1
                   && memcmp(sink.data, buttons, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select name=x><option>A<option>B"
             "<optgroup label=g><option>C</optgroup></select>"
             "<p>next</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char options[] =
            "<html><head></head><body><select name=\"x\">"
            "<option>A</option><option>B</option><optgroup label=\"g\">"
            "<option>C</option></optgroup></select><p>next</p>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(options) - 1
                   && memcmp(sink.data, options, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><template>"), &parse_result, &reason, &document)
        == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_template[] =
            "<html><head></head><body><select><template></template>"
            "</select></body></html>";
        TEST_CHECK(sink.length == sizeof(select_template) - 1
                   && memcmp(sink.data, select_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><option><template></template></select>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char option_template[] =
            "<html><head></head><body><select><option><template>"
            "</template></option></select></body></html>";
        TEST_CHECK(sink.length == sizeof(option_template) - 1
                   && memcmp(sink.data, option_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><thead><template><td>x</template></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_template_cell[] =
            "<html><head></head><body><table><thead><template>"
            "<td>x</td></template></thead></table></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_template_cell) - 1
                   && memcmp(sink.data, direct_template_cell, sink.length)
                      == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><template><col></template></colgroup></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_template_col[] =
            "<html><head></head><body><table><colgroup><template>"
            "<col></template></colgroup></table></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_template_col) - 1
                   && memcmp(sink.data, direct_template_col, sink.length)
                      == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><select><table></table></select></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_tables[] =
            "<html><head></head><body><select></select><table></table>"
            "<table></table></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_tables) - 1
                   && memcmp(sink.data, nested_tables, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><table></select>"), &parse_result, &reason, &document)
        == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><option>one</div>two</option>"
             "<option>three</option></select><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char ignored_end[] =
            "<html><head></head><body><select><option>onetwo</option>"
            "<option>three</option></select><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(ignored_end) - 1
                   && memcmp(sink.data, ignored_end, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><optgroup label=g><option>first<hr>"
             "<option>after</option></select><p>next</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_rule[] =
            "<html><head></head><body><select><optgroup label=\"g\">"
            "<option>first</option></optgroup><hr><option>after</option>"
            "</select><p>next</p></body></html>";
        TEST_CHECK(sink.length == sizeof(select_rule) - 1
                   && memcmp(sink.data, select_rule, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><option>first<select><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_select[] =
            "<html><head></head><body><select><option>first</option>"
            "</select><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_select) - 1
                   && memcmp(sink.data, nested_select, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<div><table><svg><foreignObject><select><table><s>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_foreign[] =
            "<html><head></head><body><div><svg><foreignObject>"
            "<select></select></foreignObject></svg><table></table>"
            "<s></s><table></table></div></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_foreign) - 1
                   && memcmp(sink.data, fostered_foreign, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><svg><desc><td>x"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_cell[] =
            "<html><head></head><body><table><tbody><tr><td>"
            "<svg><desc></desc></svg></td><td>x</td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_cell) - 1
                   && memcmp(sink.data, foreign_cell, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><div>d<tr><td>e</table>"
             "<table><section>x<tr><td>y</table></section>"
             "<table><span>a<td>b</table></span>z"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_section_span_rows[] =
            "<html><head></head><body><div>d</div><table><tbody>"
            "<tr><td>e</td></tr></tbody></table>"
            "<section>x</section><table><tbody>"
            "<tr><td>y</td></tr></tbody></table><span>a</span>"
            "<table><tbody><tr><td>b</td></tr></tbody></table>"
            "z</body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_section_span_rows) - 1
                   && memcmp(sink.data, fostered_section_span_rows,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><blockquote>x<tr><td>y</table>"
             "<table><p>a<td>b</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_body_blocks[] =
            "<html><head></head><body><blockquote>x</blockquote>"
            "<table><tbody><tr><td>y</td></tr></tbody></table>"
            "<p>a</p><table><tbody><tr><td>b</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_body_blocks) - 1
                   && memcmp(sink.data, fostered_body_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><select><option>one</td><td>two</td>"
             "</tr></table><select>before</table>after</select>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_table_ends[] =
            "<html><head></head><body><table><tbody><tr>"
            "<td><select><option>one</option></select></td>"
            "<td>two</td></tr></tbody></table>"
            "<select>beforeafter</select></body></html>";
        TEST_CHECK(sink.length == sizeof(select_table_ends) - 1
                   && memcmp(sink.data, select_table_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><select><option>one<table><tr><td>two"
             "</table>three</td></tr></table>"
             "<select><option>before<table><option>after</select>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_table_starts[] =
            "<html><head></head><body><table><tbody><tr><td>"
            "<select><option>one</option></select>"
            "<table><tbody><tr><td>two</td></tr></tbody></table>three"
            "</td></tr></tbody></table>"
            "<select><option>before</option><option>after</option>"
            "</select></body></html>";
        TEST_CHECK(sink.length == sizeof(select_table_starts) - 1
                   && memcmp(sink.data, select_table_starts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><select><option>first<tr><td>second"
             "</table><select><option>before<tr><option>after</select>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_row_start[] =
            "<html><head></head><body><table><tbody>"
            "<tr><td><select><option>first</option></select></td></tr>"
            "<tr><td>second</td></tr></tbody></table>"
            "<select><option>before</option><option>after</option>"
            "</select></body></html>";
        TEST_CHECK(sink.length == sizeof(select_row_start) - 1
                   && memcmp(sink.data, select_row_start,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><select><option>one<th>two"
             "<td>three</table><select><option>before<td>"
             "<option>after</select>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_cell_start[] =
            "<html><head></head><body><table><tbody><tr>"
            "<td><select><option>one</option></select></td>"
            "<th>two</th><td>three</td></tr></tbody></table>"
            "<select><option>before</option><option>after</option>"
            "</select></body></html>";
        TEST_CHECK(sink.length == sizeof(select_cell_start) - 1
                   && memcmp(sink.data, select_cell_start,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><select><option>one<thead>"
             "<tr><th>two</table>"
             "<select><option>before<tbody><option>after</select>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_section_start[] =
            "<html><head></head><body><table>"
            "<tbody><tr><td><select><option>one</option></select>"
            "</td></tr></tbody><thead><tr><th>two</th></tr></thead>"
            "</table><select><option>before</option><option>after</option>"
            "</select></body></html>";
        TEST_CHECK(sink.length == sizeof(select_section_start) - 1
                   && memcmp(sink.data, select_section_start,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><select><option>one<caption>two</caption>"
             "<tr><td>three</table><select><option>before<caption>"
             "<option>after</select>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_caption_start[] =
            "<html><head></head><body><table>"
            "<tbody><tr><td><select><option>one</option></select>"
            "</td></tr></tbody><caption>two</caption>"
            "<tbody><tr><td>three</td></tr></tbody></table>"
            "<select><option>before</option><option>after</option>"
            "</select></body></html>";
        TEST_CHECK(sink.length == sizeof(select_caption_start) - 1
                   && memcmp(sink.data, select_caption_start,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><select><colgroup><col><option>cell"
             "</select></table>"
             "<select><option>before<col><colgroup><option>after</select>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_columns[] =
            "<html><head></head><body><table><tbody><tr><td>"
            "<select><option>cell</option></select>"
            "</td></tr></tbody></table>"
            "<select><option>before</option><option>after</option>"
            "</select></body></html>";
        TEST_CHECK(sink.length == sizeof(select_columns) - 1
                   && memcmp(sink.data, select_columns,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><caption><select><option>one<caption>two</caption>"
             "<tr><td>cell</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_caption_replacement[] =
            "<html><head></head><body><table>"
            "<caption><select><option>one</option></select></caption>"
            "<caption>two</caption><tbody><tr><td>cell</td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_caption_replacement) - 1
                   && memcmp(sink.data, select_caption_replacement,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><select><option>before<tr><td>cell</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_table_row[] =
            "<html><head></head><body><select><option>before</option>"
            "</select><table><tbody><tr><td>cell</td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_table_row) - 1
                   && memcmp(sink.data, select_table_row,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><select><option>one<td>cell</table>"
             "<table><select>two<caption>caption<tr><td>row</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_table_starts[] =
            "<html><head></head><body><select><option>one</option></select>"
            "<table><tbody><tr><td>cell</td></tr></tbody></table>"
            "<select>two</select><table><caption>caption</caption>"
            "<tbody><tr><td>row</td></tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_table_starts) - 1
                   && memcmp(sink.data, select_table_starts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><select>three<thead><tr><td>head</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_table_section[] =
            "<html><head></head><body><select>three</select>"
            "<table><thead><tr><td>head</td></tr></thead></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(select_table_section) - 1
                   && memcmp(sink.data, select_table_section,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tbody><select><option>one<td>cell</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_section_cell[] =
            "<html><head></head><body><select><option>one</option>"
            "</select><table><tbody><tr><td>cell</td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_section_cell) - 1
                   && memcmp(sink.data, select_section_cell,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tbody><select>first<thead><tr><td>second</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_section_section[] =
            "<html><head></head><body><select>first</select><table>"
            "<tbody></tbody><thead><tr><td>second</td></tr></thead>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_section_section) - 1
                   && memcmp(sink.data, select_section_section,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><select><option>one<td>cell</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_row_cell[] =
            "<html><head></head><body><select><option>one</option></select>"
            "<table><tbody><tr><td>cell</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(select_row_cell) - 1
                   && memcmp(sink.data, select_row_cell,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><select>first<thead><tr><td>second</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_row_section[] =
            "<html><head></head><body><select>first</select><table>"
            "<tbody><tr></tr></tbody><thead><tr><td>second</td>"
            "</tr></thead></table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_row_section) - 1
                   && memcmp(sink.data, select_row_section,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><select><option>one<td>cell</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_colgroup_cell[] =
            "<html><head></head><body><select><option>one</option>"
            "</select><table><colgroup></colgroup>"
            "<tbody><tr><td>cell</td></tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_colgroup_cell) - 1
                   && memcmp(sink.data, select_colgroup_cell,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><select>first<tr><td>second</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_colgroup_row[] =
            "<html><head></head><body><select>first</select><table>"
            "<colgroup></colgroup><tbody><tr><td>second</td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_colgroup_row) - 1
                   && memcmp(sink.data, select_colgroup_row,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><caption><select><option>one<td>cell</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_caption_cell[] =
            "<html><head></head><body><table><caption><select>"
            "<option>one</option></select></caption>"
            "<tbody><tr><td>cell</td></tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_caption_cell) - 1
                   && memcmp(sink.data, select_caption_cell,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><caption><select>first<tr><td>second</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_caption_row[] =
            "<html><head></head><body><table><caption>"
            "<select>first</select></caption>"
            "<tbody><tr><td>second</td></tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(select_caption_row) - 1
                   && memcmp(sink.data, select_caption_row,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<html lang=en><p>x<html lang=fr class=one>"
             "<select><html class=two data-x=ok><option>choice</select>"
             "<table><html data-x=no><tr><td>end</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char repeated_html[] =
            "<html lang=\"en\" class=\"one\" data-x=\"ok\"><head></head>"
            "<body><p>x<select><option>choice</option></select></p>"
            "<table><tbody><tr><td>end</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(repeated_html) - 1
                   && memcmp(sink.data, repeated_html, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    /* Duplicate head tags are ignored before or within the body, including
     * when table foster parenting would otherwise reject them. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><title>T</title></head><head id=wrong></head>"
             "<body><p>x<head id=also-wrong></head>y</p>"
             "<table><head id=table></head><tr><td>z</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char ignored_heads[] =
            "<html><head><title>T</title></head><body><p>xy</p>"
            "<table><tbody><tr><td>z</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(ignored_heads) - 1
                   && memcmp(sink.data, ignored_heads, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    /* head is an HTML breakout in foreign content; it must not become an
     * SVG/MathML child or hold subsequent ordinary body content there. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><head id=bad><p>one</p>"
             "<math><mrow><head><p>two</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_head[] =
            "<html><head></head><body><svg><g></g></svg><p>one</p>"
            "<math><mrow></mrow></math><p>two</p></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_head) - 1
                   && memcmp(sink.data, foreign_head, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body id=first><svg><g><body id=second class=shared>"
             "<a href=outside>link</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_body[] =
            "<html><head></head><body id=\"first\" class=\"shared\">"
            "<svg><g></g></svg><a href=\"outside\">link</a></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_body) - 1
                   && memcmp(sink.data, foreign_body, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><meta name=description content=ok>"
             "<a href=outside>link</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_meta[] =
            "<html><head></head><body><svg><g></g></svg>"
            "<meta name=\"description\" content=\"ok\">"
            "<a href=\"outside\">link</a>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_meta) - 1
                   && memcmp(sink.data, foreign_meta, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    /* Foreign </br> breaks out before the in-body rule turns it into a
     * bare br start; a following anchor belongs to the HTML body. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><title></br id=ignored><a href=outside>link</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_break_end[] =
            "<html><head></head><body><svg><title></title></svg><br>"
            "<a href=\"outside\">link</a></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_break_end) - 1
                   && memcmp(sink.data, foreign_break_end, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g></p><a href=outside>link</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_paragraph_end[] =
            "<html><head></head><body><svg><g></g></svg><p></p>"
            "<a href=\"outside\">link</a></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_paragraph_end) - 1
                   && memcmp(sink.data, foreign_paragraph_end,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 6; /* document, roots, SVG and g precede implied p */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<svg><g></p>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><embed src=/asset><math><mrow><listing>\npre</listing>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_embed_listing[] =
            "<html><head></head><body><svg><g></g></svg><embed src=\"/asset\">"
            "<math><mrow></mrow></math><listing>pre</listing></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_embed_listing) - 1
                   && memcmp(sink.data, foreign_embed_listing,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>one<center><a href=inside>middle</a></center>"
             "<svg><g><center>outside</center><a href=outside>link</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char centers[] =
            "<html><head></head><body><p>one</p><center>"
            "<a href=\"inside\">middle</a></center><svg><g></g></svg>"
            "<center>outside</center><a href=\"outside\">link</a>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(centers) - 1
                   && memcmp(sink.data, centers, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<menu><li>one<li>two</menu>"
             "<svg><g><menu><li>three</menu><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char menus[] =
            "<html><head></head><body><menu><li>one</li><li>two</li>"
            "</menu><svg><g></g></svg><menu><li>three</li></menu>"
            "<p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(menus) - 1
                   && memcmp(sink.data, menus, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<small><a href=in>in</a></small>"
             "<svg><g><big><a href=out>out</a></big><p>tail</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char inline_size[] =
            "<html><head></head><body><small><a href=\"in\">in</a>"
            "</small><svg><g></g></svg><big><a href=\"out\">out</a>"
            "</big><p>tail</p></body></html>";
        TEST_CHECK(sink.length == sizeof(inline_size) - 1
                   && memcmp(sink.data, inline_size, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<picture><source src=/alt><img src=/img></picture>"
             "<video src=/clip><source src=/segment></video>"
             "<audio src=/sound>fallback</audio>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char media[] =
            "<html><head></head><body><picture><source src=\"/alt\">"
            "<img src=\"/img\"></picture><video src=\"/clip\">"
            "<source src=\"/segment\"></video><audio src=\"/sound\">"
            "fallback</audio></body></html>";
        TEST_CHECK(sink.length == sizeof(media) - 1
                   && memcmp(sink.data, media, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<map name=nav><area href=/one><a href=/two>two</a></map>"
             "<canvas><a href=/fallback>fallback</a></canvas>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fallback_links[] =
            "<html><head></head><body><map name=\"nav\"><area href=\"/one\">"
            "<a href=\"/two\">two</a></map><canvas>"
            "<a href=\"/fallback\">fallback</a></canvas></body></html>";
        TEST_CHECK(sink.length == sizeof(fallback_links) - 1
                   && memcmp(sink.data, fallback_links,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<form><label for=x>link <a href=/inside>one</a>"
             "<input id=x></label></form><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char label_link[] =
            "<html><head></head><body><form><label for=\"x\">link "
            "<a href=\"/inside\">one</a><input id=\"x\"></label></form>"
            "<p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(label_link) - 1
                   && memcmp(sink.data, label_link, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>before<fieldset><legend><a href=/help>help</a></legend>"
             "<label><input src=/x>text</label></fieldset><p>after"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fieldset[] =
            "<html><head></head><body><p>before</p><fieldset><legend>"
            "<a href=\"/help\">help</a></legend><label><input src=\"/x\">"
            "text</label></fieldset><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(fieldset) - 1
                   && memcmp(sink.data, fieldset, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<fieldset><legend><div>title</fieldset>"
             "<a href=/after>after</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char implied_legend_end[] =
            "<html><head></head><body><fieldset><legend><div>title</div>"
            "</legend></fieldset><a href=\"/after\">after</a></body></html>";
        TEST_CHECK(sink.length == sizeof(implied_legend_end) - 1
                   && memcmp(sink.data, implied_legend_end,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><title>Head</title></head><body><p>before"
             "<title>Late &amp; body</title><a href=/after>after</a></p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char title_in_body[] =
            "<html><head><title>Head</title></head><body><p>before"
            "<title>Late &amp; body</title><a href=\"/after\">after</a>"
            "</p></body></html>";
        TEST_CHECK(sink.length == sizeof(title_in_body) - 1
                   && memcmp(sink.data, title_in_body, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><base href=/head></head><body><p>"
             "<a href=/first>1</a><base href=/late><link href=/style>"
             "<meta name=x><a href=/last>2</a></p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char metadata_in_body[] =
            "<html><head><base href=\"/head\"></head><body><p>"
            "<a href=\"/first\">1</a><base href=\"/late\">"
            "<link href=\"/style\"><meta name=\"x\">"
            "<a href=\"/last\">2</a></p></body></html>";
        TEST_CHECK(sink.length == sizeof(metadata_in_body) - 1
                   && memcmp(sink.data, metadata_in_body,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><base href=/base><link href=/sheet><meta name=x>"
             "<tr><link href=/row><td>cell</td></tr></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_metadata[] =
            "<html><head></head><body><base href=\"/base\">"
            "<link href=\"/sheet\"><meta name=\"x\"><link href=\"/row\">"
            "<table><tbody><tr><td>cell</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(table_metadata) - 1
                   && memcmp(sink.data, table_metadata, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><col><base href=/base><link href=/sheet>"
             "<tr><td>cell</td></tr></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_metadata[] =
            "<html><head></head><body><base href=\"/base\">"
            "<link href=\"/sheet\"><table><colgroup><col></colgroup>"
            "<tbody><tr><td>cell</td></tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_metadata) - 1
                   && memcmp(sink.data, colgroup_metadata,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 7; /* document, roots, table, colgroup and col */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><colgroup><col><base href=/base>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><col><input type=hidden name=x>"
             "<tr><td>one</td></tr></table>"
             "<table><colgroup><col><input src=/asset>"
             "<tr><td>two</td></tr></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_inputs[] =
            "<html><head></head><body><table><colgroup><col></colgroup>"
            "<input type=\"hidden\" name=\"x\"><tbody><tr><td>one</td></tr>"
            "</tbody></table><input src=\"/asset\"><table><colgroup><col>"
            "</colgroup><tbody><tr><td>two</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_inputs) - 1
                   && memcmp(sink.data, colgroup_inputs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    /* A body void closes colgroup before fostering; a later col starts a
     * fresh colgroup rather than reusing the one that the void closed. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><col><img src=/one><col><tr><td>x</table>"
             "<table><colgroup><source src=/two><col></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_voids[] =
            "<html><head></head><body><img src=\"/one\">"
            "<table><colgroup><col></colgroup><colgroup><col></colgroup>"
            "<tbody><tr><td>x</td></tr></tbody></table>"
            "<source src=\"/two\"><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_voids) - 1
                   && memcmp(sink.data, colgroup_voids,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 7; /* document, roots, table, colgroup and col */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><colgroup><col><img src=/one>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><col><div><a href=/outside>link</a></div>"
             "<col><tr><td>x</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_container[] =
            "<html><head></head><body><div><a href=\"/outside\">link</a>"
            "</div><table><colgroup><col></colgroup><colgroup><col>"
            "</colgroup><tbody><tr><td>x</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_container) - 1
                   && memcmp(sink.data, colgroup_container,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 7; /* document, roots, table, colgroup and col */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><colgroup><col><div>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();

    /* Leading whitespace belongs to colgroup; following nonspace text
     * closes it and is fostered outside the table. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><col> \nX<col><tr><td>x</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_text[] =
            "<html><head></head><body>X<table><colgroup><col> \n"
            "</colgroup><colgroup><col></colgroup><tbody><tr><td>x</td>"
            "</tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_text) - 1
                   && memcmp(sink.data, colgroup_text,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 7; /* document, roots, table, colgroup and col */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><colgroup><col> \nX"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><style><a href=hidden></style><colgroup><col>"
             "<script><a href=hidden></script><col><tr><td>x</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_text_modes[] =
            "<html><head></head><body><table>"
            "<style><a href=hidden></style>"
            "<colgroup><col></colgroup><script><a href=hidden></script>"
            "<colgroup><col></colgroup><tbody><tr><td>x</td></tr></tbody>"
            "</table></body></html>";
        TEST_CHECK(sink.length == sizeof(table_text_modes) - 1
                   && memcmp(sink.data, table_text_modes,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 5; /* document, roots and table */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><style>x</style>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();

    /* Ordinary phrasing tags are not specialized element layouts. They
     * still retain nested links and text for document traversal. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p><abbr title=x><cite><data value=y><dfn><mark><q>"
             "<time datetime=z><a href=/item>link</a></time>"
             "</q></mark></dfn></data></cite></abbr></p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char phrasing[] =
            "<html><head></head><body><p><abbr title=\"x\"><cite>"
            "<data value=\"y\"><dfn><mark><q><time datetime=\"z\">"
            "<a href=\"/item\">link</a></time></q></mark></dfn></data>"
            "</cite></abbr></p></body></html>";
        TEST_CHECK(sink.length == sizeof(phrasing) - 1
                   && memcmp(sink.data, phrasing, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>a<address><a href=x>1</a></address>"
             "<p>b<dialog>2</dialog><p>c<dir>3</dir>"
             "<p>d<hgroup>4</hgroup><p>e<search>5</search>"
             "<p>f<summary>6</summary>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char block_starts[] =
            "<html><head></head><body><p>a</p><address><a href=\"x\">1"
            "</a></address><p>b</p><dialog>2</dialog><p>c</p><dir>3"
            "</dir><p>d</p><hgroup>4</hgroup><p>e</p><search>5</search>"
            "<p>f</p><summary>6</summary></body></html>";
        TEST_CHECK(sink.length == sizeof(block_starts) - 1
                   && memcmp(sink.data, block_starts, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    /* With scripting disabled, in-body noscript content is ordinary markup,
     * so links and images remain visible to document traversal. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><noscript><a href=/ok>ok</a><img src=/pic>"
             "</noscript><p>end</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char body_noscript[] =
            "<html><head></head><body><noscript><a href=\"/ok\">ok</a>"
            "<img src=\"/pic\"></noscript><p>end</p></body></html>";
        TEST_CHECK(sink.length == sizeof(body_noscript) - 1
                   && memcmp(sink.data, body_noscript,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><noscript id=x><!--c--> \n<link href=/sheet>"
             "<meta name=x><style>raw</style></noscript><title>t</title>"
             "</head><body><a href=/ok>ok</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char head_noscript[] =
            "<html><head><noscript id=\"x\"><!--c--> \n"
            "<link href=\"/sheet\"><meta name=\"x\"><style>raw</style>"
            "</noscript><title>t</title></head><body>"
            "<a href=\"/ok\">ok</a></body></html>";
        TEST_CHECK(sink.length == sizeof(head_noscript) - 1
                   && memcmp(sink.data, head_noscript,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><noscript> \n<body><a href=/ok>ok</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char reprocessed_noscript[] =
            "<html><head><noscript> \n</noscript></head><body>"
            "<a href=\"/ok\">ok</a></body></html>";
        TEST_CHECK(sink.length == sizeof(reprocessed_noscript) - 1
                   && memcmp(sink.data, reprocessed_noscript,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><noscript><link href=/one></br><link href=/two>"
             "</head><body><a href=/ok>ok</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char noscript_br[] =
            "<html><head><noscript><link href=\"/one\"></noscript>"
            "<link href=\"/two\"></head><body><a href=\"/ok\">ok</a>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(noscript_br) - 1
                   && memcmp(sink.data, noscript_br,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><noscript><link href=/one></br><link href=/two>"
             "</noscript></head><body><a href=/ok>ok</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char recovered[] =
            "<html><head><noscript><link href=\"/one\"></noscript>"
            "<link href=\"/two\"></head><body><a href=\"/ok\">ok</a>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(recovered) - 1
                   && memcmp(sink.data, recovered, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head></orphan><link href=/sheet></title></head>"
             "<body><a href=/ok>ok</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char recovered[] =
            "<html><head><link href=\"/sheet\"></head>"
            "<body><a href=\"/ok\">ok</a></body></html>";
        TEST_CHECK(sink.length == sizeof(recovered) - 1
                   && memcmp(sink.data, recovered, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><link href=/sheet></br><a href=/ok>ok</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char recovered[] =
            "<html><head><link href=\"/sheet\"></head>"
            "<body><br><a href=\"/ok\">ok</a></body></html>";
        TEST_CHECK(sink.length == sizeof(recovered) - 1
                   && memcmp(sink.data, recovered, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head></body></html>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char closed[] = "<html><head></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(closed) - 1
                   && memcmp(sink.data, closed, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 4; /* document, html, head and body; br needs a fifth */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head></br>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template>Hello</template><div>after</div>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char implied_head_template[] =
            "<html><head><template>Hello</template></head>"
            "<body><div>after</div></body></html>";
        TEST_CHECK(sink.length == sizeof(implied_head_template) - 1
                   && memcmp(sink.data, implied_head_template, sink.length)
                      == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 2; /* the implicit head cannot be constructed */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template>Hello</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template id=x></template><link href=/ok>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char empty_template[] =
            "<html><head><template id=\"x\"></template>"
            "<link href=\"/ok\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(empty_template) - 1
                   && memcmp(sink.data, empty_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template id=x>A&amp;B</template><link href=/ok>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_first_child(nodes, lxa_html_document_head(document),
                                         &anchor) == LXA_OK);
    {
        lxa_dom_record_t template_record;
        lxa_dom_ref_t contents, first;
        TEST_CHECK(lxa_dom_nodes_read(nodes, anchor, &template_record) == LXA_OK
                   && template_record.first_child == LXA_ID_NONE);
        TEST_CHECK(lxa_dom_nodes_template_contents(nodes, anchor, &contents)
                   == LXA_OK && contents.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, contents, &first) == LXA_OK
                   && first.handle != LXA_HANDLE_NONE);
    }
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char text_template[] =
            "<html><head><template id=\"x\">A&amp;B</template>"
            "<link href=\"/ok\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(text_template) - 1
                   && memcmp(sink.data, text_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template>a<!--c--><link href=/hidden><meta name=x>"
             "<!doctype html>b</template>"
             "<link href=/ok>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char mixed_template[] =
            "<html><head><template>a<!--c--><link href=\"/hidden\">"
            "<meta name=\"x\">b</template>"
            "<link href=\"/ok\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(mixed_template) - 1
                   && memcmp(sink.data, mixed_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><title>T&amp;B</title><style>x<y</style>"
             "<script>var x = '<a href=/bad>';</script>"
             "<noframes><img src=/bad></noframes></template>"
             "<link href=/ok>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char raw_template[] =
            "<html><head><template><title>T&amp;B</title><style>x<y</style>"
            "<script>var x = '<a href=/bad>';</script>"
            "<noframes><img src=/bad></noframes></template>"
            "<link href=\"/ok\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(raw_template) - 1
                   && memcmp(sink.data, raw_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template>before<image src=/one><br><input value=x>"
             "after</template><link href=/ok>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char void_template[] =
            "<html><head><template>before<img src=\"/one\"><br>"
            "<input value=\"x\">after</template><link href=\"/ok\">"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(void_template) - 1
                   && memcmp(sink.data, void_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>A<span>B<a href=/in>C</a></span>D</p>"
             "<template><div><a href=/nested>N</a></div></template>"
             "<img src=/end>E</template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char elements_template[] =
            "<html><head><template><p>A<span>B<a href=\"/in\">C</a>"
            "</span>D</p><template><div><a href=\"/nested\">N</a>"
            "</div></template><img src=\"/end\">E</template>"
            "<link href=\"/out\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(elements_template) - 1
                   && memcmp(sink.data, elements_template,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>one<span>two<p>three</p>"
             "<p>four<div>five</div></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char adjacent_paragraphs[] =
            "<html><head><template><p>one<span>two</span></p>"
            "<p>three</p><p>four</p><div>five</div></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(adjacent_paragraphs) - 1
                   && memcmp(sink.data, adjacent_paragraphs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>outer<template></p><p>inner</p>"
             "</template>tail</p></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char scoped_paragraphs[] =
            "<html><head><template><p>outer<template><p>inner</p>"
            "</template>tail</p></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(scoped_paragraphs) - 1
                   && memcmp(sink.data, scoped_paragraphs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>one<span>two<hr>three</template>"
             "<link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char rule_after_paragraph[] =
            "<html><head><template><p>one<span>two</span></p><hr>three"
            "</template><link href=\"/out\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(rule_after_paragraph) - 1
                   && memcmp(sink.data, rule_after_paragraph,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>before<span>inline<section><article>"
             "<a href=/in>inside</a></article></section><aside>after</aside>"
             "</template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_sectioning[] =
            "<html><head><template><p>before<span>inline</span></p>"
            "<section><article><a href=\"/in\">inside</a></article>"
            "</section><aside>after</aside></template><link href=\"/out\">"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_sectioning) - 1
                   && memcmp(sink.data, template_sectioning,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>text<custom-box data-x=y>"
             "<a href=/nested>link</a></custom-box><picture>"
             "<source src=/src><img src=/im></picture></p></template>"
             "<link href=/ok>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char ordinary_template[] =
            "<html><head><template><p>text<custom-box data-x=\"y\">"
            "<a href=\"/nested\">link</a></custom-box><picture>"
            "<source src=\"/src\"><img src=\"/im\"></picture></p>"
            "</template><link href=\"/ok\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(ordinary_template) - 1
                   && memcmp(sink.data, ordinary_template,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p><b>A<em>B</em>C</b><strong>D</strong>"
             "</p></template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char formatted_template[] =
            "<html><head><template><p><b>A<em>B</em>C</b>"
            "<strong>D</strong></p></template><link href=\"/out\">"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(formatted_template) - 1
                   && memcmp(sink.data, formatted_template,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p><b>x</p>y</b></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(document == NULL && parse_result == LXA_HTML_UNSUPPORTED);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><b><em>x</b>y</em></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(document == NULL && parse_result == LXA_HTML_UNSUPPORTED);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><ul><li><p>one<li>two<ul><li>inner"
             "<li><a href=/in>next</a></ul><li>last</ul></template>"
             "<link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_list[] =
            "<html><head><template><ul><li><p>one</p></li><li>two"
            "<ul><li>inner</li><li><a href=\"/in\">next</a></li></ul>"
            "</li><li>last</li></ul></template><link href=\"/out\">"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_list) - 1
                   && memcmp(sink.data, template_list, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><ul><li>outer<template><li>inner</li>"
             "</template><li>next</ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char scoped_list[] =
            "<html><head><template><ul><li>outer<template><li>inner</li>"
            "</template></li><li>next</li></ul></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(scoped_list) - 1
                   && memcmp(sink.data, scoped_list, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><ul><li>x<section><li>later</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_list_item[] =
            "<html><head><template><ul><li>x<section><li>later</li>"
            "</section></li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_list_item) - 1
                   && memcmp(sink.data, nested_list_item, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><ol><li>outer<article><li>inner"
             "<li>next</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char article_items[] =
            "<html><head><template><ol><li>outer<article><li>inner</li>"
            "<li>next</li></article></li></ol></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(article_items) - 1
                   && memcmp(sink.data, article_items, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><ul><li>one<address>contact"
             "<li>two</ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char address_items[] =
            "<html><head><template><ul><li>one<address>contact</address>"
            "</li><li>two</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(address_items) - 1
                   && memcmp(sink.data, address_items, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><ul><li>one<dialog>details"
             "<li>two</ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char dialog_items[] =
            "<html><head><template><ul><li>one<dialog>details</dialog>"
            "</li><li>two</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(dialog_items) - 1
                   && memcmp(sink.data, dialog_items, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><ul><li>x<section><b>t<li>later</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char formatted_section_items[] =
            "<html><head><template><ul><li>x<section><b>t<li>later</li>"
            "</b></section></li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(formatted_section_items) - 1
                   && memcmp(sink.data, formatted_section_items,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><ul><li>x<section><b>t<li>later</b>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(document == NULL && parse_result == LXA_HTML_UNSUPPORTED);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>before<h1>A<h2>B</h3>"
             "<h4><p>C<h5>D</h5></template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_headings[] =
            "<html><head><template><p>before</p><h1>A</h1><h2>B</h2>"
            "<h4><p>C</p></h4><h5>D</h5></template><link href=\"/out\">"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_headings) - 1
                   && memcmp(sink.data, template_headings,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><div><template><a href=/inner>hidden</a>"
             "<template><p>nested</p></template>tail</template>"
             "<a href=/after>visible</a></div>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char body_template[] =
            "<html><head></head><body><div><template>"
            "<a href=\"/inner\">hidden</a><template><p>nested</p>"
            "</template>tail</template><a href=\"/after\">visible</a>"
            "</div></body></html>";
        TEST_CHECK(sink.length == sizeof(body_template) - 1
                   && memcmp(sink.data, body_template,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><template><textarea>\nA&amp;B</textarea>"
             "<a href=/after>next</a></template><a href=/out>out</a>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_textarea[] =
            "<html><head></head><body><template><textarea>A&amp;B"
            "</textarea><a href=\"/after\">next</a></template>"
            "<a href=\"/out\">out</a></body></html>";
        TEST_CHECK(sink.length == sizeof(template_textarea) - 1
                   && memcmp(sink.data, template_textarea,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>before<pre>\nA&amp;B</pre>"
             "<listing>\nC</listing><a href=/after>after</a></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_pre[] =
            "<html><head><template><p>before</p><pre>A&amp;B</pre>"
            "<listing>C</listing><a href=\"/after\">after</a></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_pre) - 1
                   && memcmp(sink.data, template_pre, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><noscript><a href=/fallback>use</a>"
             "</noscript><a href=/next>next</a></template>"
             "<link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_noscript[] =
            "<html><head><template><noscript><a href=\"/fallback\">"
            "use</a></noscript><a href=\"/next\">next</a></template>"
            "<link href=\"/out\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_noscript) - 1
                   && memcmp(sink.data, template_noscript,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><iframe src=/frame><a href=/fake>"
             "</iframe><noembed><img src=/fake></noembed>"
             "<a href=/real>real</a></template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char raw_template[] =
            "<html><head><template><iframe src=\"/frame\"><a href=/fake>"
            "</iframe><noembed><img src=/fake></noembed>"
            "<a href=\"/real\">real</a></template><link href=\"/out\">"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(raw_template) - 1
                   && memcmp(sink.data, raw_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>before<xmp><a href=/fake></xmp>"
             "<a href=/real>real</a></template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char xmp_template[] =
            "<html><head><template><p>before</p><xmp><a href=/fake>"
            "</xmp><a href=\"/real\">real</a></template>"
            "<link href=\"/out\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(xmp_template) - 1
                   && memcmp(sink.data, xmp_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><plaintext>literal<a href=/fake>"
             "</template><a href=/also-fake>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char plaintext_template[] =
            "<html><head><template><plaintext>literal<a href=/fake>"
            "</template><a href=/also-fake></plaintext></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(plaintext_template) - 1
                   && memcmp(sink.data, plaintext_template,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>before<select><option>one"
             "<a href=/fake>text<option>two<optgroup label=x>"
             "<option>three</optgroup><option>four<hr>"
             "<option>five</select><a href=/real>real</a>"
             "</template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_select[] =
            "<html><head><template><p>before<select>"
            "<option>onetext</option><option>two</option>"
            "<optgroup label=\"x\"><option>three</option></optgroup>"
            "<option>four</option><hr><option>five</option></select>"
            "<a href=\"/real\">real</a></p></template><link href=\"/out\">"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_select) - 1
                   && memcmp(sink.data, template_select,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><p><object><select><option>x</select>"
             "y</object>z</p></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_object_select[] =
            "<html><head><template><p><object><select><option>x"
            "</option></select>y</object>z</p></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_object_select) - 1
                   && memcmp(sink.data, template_object_select,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><select><option>outer<template>"
             "<option>inner</option></template><option>next</select>"
             "</template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char scoped_select[] =
            "<html><head><template><select><option>outer<template>"
            "<option>inner</option></template></option><option>next"
            "</option></select></template><link href=\"/out\">"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(scoped_select) - 1
                   && memcmp(sink.data, scoped_select,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><select><option>pick<select>"
             "<a href=/after>after</a></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_select[] =
            "<html><head><template><select><option>pick</option></select>"
            "<a href=\"/after\">after</a></template></head><body></body>"
            "</html>";
        TEST_CHECK(sink.length == sizeof(nested_select) - 1
                   && memcmp(sink.data, nested_select,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><select><option>one<input src=/input>"
             "<select><option>two<textarea>\nA&amp;B</textarea>"
             "<a href=/after>after</a></template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_reprocessing[] =
            "<html><head><template><select><option>one</option></select>"
            "<input src=\"/input\"><select><option>two</option></select>"
            "<textarea>A&amp;B</textarea><a href=\"/after\">after</a>"
            "</template><link href=\"/out\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(select_reprocessing) - 1
                   && memcmp(sink.data, select_reprocessing,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>before<table> \n<!--c-->"
             "<script>var s='<a href=/fake>'</script>"
             "<link href=/in></table><a href=/after>after</a>"
             "</template><link href=/out>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_table[] =
            "<html><head><template><p>before<link href=\"/in\">"
            "<table> \n<!--c--><script>var s='<a href=/fake>'</script>"
            "</table><a href=\"/after\">after</a></p></template>"
            "<link href=\"/out\"></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_table) - 1
                   && memcmp(sink.data, template_table,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><div><p>before<table>abc<td>x</table>"
             "after</p></div></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char paragraph_table[] =
            "<html><head><template><div><p>beforeabc<table>"
            "<tbody><tr><td>x</td></tr></tbody></table>after</p>"
            "</div></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(paragraph_table) - 1
                   && memcmp(sink.data, paragraph_table,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><div><table> A&amp;B<tr><td>cell"
             "</table><a href=/after>after</a></div></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered[] =
            "<html><head><template><div> A&amp;B<table><tbody><tr>"
            "<td>cell</td></tr></tbody></table><a href=\"/after\">"
            "after</a></div></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered) - 1
                   && memcmp(sink.data, fostered, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table>tail"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foster_eof[] =
            "<html><head><template>tail<table></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(foster_eof) - 1
                   && memcmp(sink.data, foster_eof,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><h2>x</h2><col></table>"
             "<table><colgroup><menu><li>y</li></menu><col>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_fostered_heading[] =
            "<html><head><template><h2>x</h2><table>"
            "<colgroup></colgroup><colgroup><col></colgroup></table>"
            "<menu><li>y</li></menu><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_fostered_heading) - 1
                   && memcmp(sink.data, colgroup_fostered_heading,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><pre>\nx</pre><col></table>"
             "<table><colgroup><listing>\ny</listing><col>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_fostered_pre[] =
            "<html><head><template><pre>x</pre><table>"
            "<colgroup></colgroup><colgroup><col></colgroup></table>"
            "<listing>y</listing><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_fostered_pre) - 1
                   && memcmp(sink.data, colgroup_fostered_pre,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><style>x</style><col>"
             "</table><table><colgroup><script>y</script>"
             "</colgroup><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_text_modes[] =
            "<html><head><template><table><colgroup></colgroup>"
            "<style>x</style><colgroup><col></colgroup></table>"
            "<table><colgroup></colgroup><script>y</script>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_text_modes) - 1
                   && memcmp(sink.data, colgroup_text_modes,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><base href=/a><col></table>"
             "<table><colgroup><link href=/b><meta name=x><col>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_metadata[] =
            "<html><head><template><base href=\"/a\"><table>"
            "<colgroup><col></colgroup></table><link href=\"/b\">"
            "<meta name=\"x\"><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_metadata) - 1
                   && memcmp(sink.data, fostered_table_metadata,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><title>a&amp;b</title><col></table>"
             "<table><colgroup><title>c&amp;d</title><col></table>"
             "<table><tr><title>e&amp;f</title><td>z</td></tr>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_titles[] =
            "<html><head><template><title>a&amp;b</title><table>"
            "<colgroup><col></colgroup></table><title>c&amp;d</title>"
            "<table><colgroup></colgroup><colgroup><col></colgroup></table>"
            "<title>e&amp;f</title><table><tbody><tr><td>z</td>"
            "</tr></tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_titles) - 1
                   && memcmp(sink.data, fostered_table_titles,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><colgroup> \nA&amp;B</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char column_text[] =
            "<html><head><template>A&amp;B<table><colgroup> \n"
            "</colgroup></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(column_text) - 1
                   && memcmp(sink.data, column_text,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><tr><td>x</td></tr></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char rows[] =
            "<html><head><template><table><tbody><tr><td>x</td></tr>"
            "</tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(rows) - 1
                   && memcmp(sink.data, rows, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><td><a href=/first>one</a>"
             "<th>two<tr><td>three<thead><tr><th>four</table>"
             "<a href=/after>after</a></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char rows[] =
            "<html><head><template><table><tbody><tr><td>"
            "<a href=\"/first\">one</a></td><th>two</th></tr>"
            "<tr><td>three</td></tr></tbody><thead><tr><th>four"
            "</th></tr></thead></table><a href=\"/after\">after</a>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(rows) - 1
                   && memcmp(sink.data, rows, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><caption><a href=/caption>"
             "Title</a></caption><tr><td>Cell</table>"
             "<a href=/after>next</a></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char caption[] =
            "<html><head><template><table><caption>"
            "<a href=\"/caption\">Title</a></caption>"
            "<tbody><tr><td>Cell</td></tr></tbody></table>"
            "<a href=\"/after\">next</a></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(caption) - 1
                   && memcmp(sink.data, caption, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><caption>Title<tr><td>Cell"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char implicit_caption[] =
            "<html><head><template><table><caption>Title</caption>"
            "<tbody><tr><td>Cell</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(implicit_caption) - 1
                   && memcmp(sink.data, implicit_caption, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><caption><p><a href=/title>Title"
             "<tr><td><a href=/cell>Cell</a></table>"
             "<a href=/after>After</a></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_caption[] =
            "<html><head><template><table><caption><p>"
            "<a href=\"/title\">Title</a></p></caption>"
            "<tbody><tr><td><a href=\"/cell\">Cell</a></td></tr></tbody>"
            "</table><a href=\"/after\">After</a></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_caption) - 1
                   && memcmp(sink.data, nested_caption,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><td><p><a href=/first>one"
             "<td>two<tr><td><p>three<thead><tr><th>four</table>"
             "<a href=/after>after</a></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char cell_descendants[] =
            "<html><head><template><table><tbody><tr><td><p>"
            "<a href=\"/first\">one</a></p></td><td>two</td></tr>"
            "<tr><td><p>three</p></td></tr></tbody><thead><tr><th>"
            "four</th></tr></thead></table><a href=\"/after\">after</a>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(cell_descendants) - 1
                   && memcmp(sink.data, cell_descendants,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><td><p>first<caption>"
             "<a href=/title>Title</a></caption><tbody><tr>"
             "<td>last</table><a href=/after>After</a></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char caption_after_cell[] =
            "<html><head><template><table><tbody><tr><td>"
            "<p>first</p></td></tr></tbody><caption>"
            "<a href=\"/title\">Title</a></caption>"
            "<tbody><tr><td>last</td></tr></tbody></table>"
            "<a href=\"/after\">After</a></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(caption_after_cell) - 1
                   && memcmp(sink.data, caption_after_cell,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><col span=2><colgroup><col>"
             "<tr><td><a href=/cell>cell</a></table>"
             "<a href=/after>after</a></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char columns[] =
            "<html><head><template><table><colgroup><col span=\"2\">"
            "</colgroup><colgroup><col></colgroup><tbody><tr><td>"
            "<a href=\"/cell\">cell</a></td></tr></tbody></table>"
            "<a href=\"/after\">after</a></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(columns) - 1
                   && memcmp(sink.data, columns, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><colgroup>wrong</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char column_foster[] =
            "<html><head><template>wrong<table><colgroup></colgroup>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(column_foster) - 1
                   && memcmp(sink.data, column_foster, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><tbody>outside<td>inside</table>"
             "</template>"), &parse_result, &reason, &document)
        == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char section_foster[] =
            "<html><head><template>outside<table><tbody><tr>"
            "<td>inside</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(section_foster) - 1
                   && memcmp(sink.data, section_foster, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>first<span>second</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char implicit_close[] =
            "<html><head><template><p>first<span>second</span></p>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(implicit_close) - 1
                   && memcmp(sink.data, implicit_close, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template>outer<template><link href=/inner>inner"
             "</template>tail</template><link href=/ok>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_template[] =
            "<html><head><template>outer<template><link href=\"/inner\">"
            "inner</template>tail</template><link href=\"/ok\"></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_template) - 1
                   && memcmp(sink.data, nested_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><template><td></td><template></template>"
             "<caption></caption><td></td></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_row_mode[] =
            "<html><head></head><body><template><td></td>"
            "<template></template><td></td></template></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_row_mode) - 1
                   && memcmp(sink.data, nested_row_mode, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><template><td><td><caption><td>last</td>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_cells[] =
            "<html><head></head><body><template><td></td><td></td>"
            "<td>last</td></template></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_cells) - 1
                   && memcmp(sink.data, direct_cells, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><template><col>A \tB\n<template><p>nested</p>"
             "</template>C<col></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_columns[] =
            "<html><head></head><body><template><col> \t\n"
            "<template><p>nested</p></template><col></template>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(direct_columns) - 1
                   && memcmp(sink.data, direct_columns, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><template><image src=/x><template></template>"
             "<tr><table></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char void_body_mode[] =
            "<html><head></head><body><template><img src=\"/x\">"
            "<template></template><table></table></template>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(void_body_mode) - 1
                   && memcmp(sink.data, void_body_mode, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><template><textarea>x</textarea><template></template>"
             "<tr><table></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char text_body_mode[] =
            "<html><head></head><body><template><textarea>x</textarea>"
            "<template></template><table></table></template>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(text_body_mode) - 1
                   && memcmp(sink.data, text_body_mode, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><i class=x><menu>Foo</i>Bar</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_menu[] =
            "<html><head><template><i class=\"x\"></i>"
            "<menu><i class=\"x\">Foo</i>Bar</menu></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_menu) - 1
                   && memcmp(sink.data, crossed_menu, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><em class=x><ul>Foo</em>Bar</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list[] =
            "<html><head><template><em class=\"x\"></em>"
            "<ul><em class=\"x\">Foo</em>Bar</ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list) - 1
                   && memcmp(sink.data, crossed_list, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    /* parse5 8.0.1: closing a crossed formatting tag leaves later list
     * text outside its clone, even when that clone encloses a closed item. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li>x</li></b>y</ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char closed_item[] =
            "<html><head><template><b></b><ul><b><li>x</li></b>y</ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(closed_item) - 1
                   && memcmp(sink.data, closed_item, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li>x</b>y</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char open_item[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\">x</b>y</li></ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(open_item) - 1
                   && memcmp(sink.data, open_item, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li>one</li><li>two</li>"
             "<li>three</b>four</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char multi_item[] =
            "<html><head><template><b></b><ul><b><li>one</li>"
            "<li>two</li></b><li><b>three</b>four</li></ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(multi_item) - 1
                   && memcmp(sink.data, multi_item, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li>one</li>prefix<!--c--><li>x</b>y</li>"
             "</ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char prior_text[] =
            "<html><head><template><b></b><ul><b><li>one</li>"
            "prefix<!--c--></b>"
            "<li><b>x</b>y</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(prior_text) - 1
                   && memcmp(sink.data, prior_text, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li><em id=z>x</b>y</em>"
             "</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char reconstructed_item[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\"><em id=\"z\">x</em>"
            "</b><em id=\"z\">y</em></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(reconstructed_item) - 1
                   && memcmp(sink.data, reconstructed_item, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li>prefix<div id=d>"
             "x</b>y</div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_div[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\">prefix</b>"
            "<div id=\"d\"><b class=\"x\">x</b>y</div></li></ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_div) - 1
                   && memcmp(sink.data, crossed_div, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li><div id=d><span title=t>"
             "x</b>y</span></div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_div_span[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\"></b>"
            "<div id=\"d\"><b class=\"x\"><span title=\"t\">x"
            "</span></b>y</div></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_div_span) - 1
                   && memcmp(sink.data, crossed_div_span,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><b class=x><em id=y>text</em>"
             "</b><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_formatting[] =
            "<html><head><template><b class=\"x\"><em id=\"y\">"
            "text</em></b><table><colgroup></colgroup><colgroup>"
            "<col></colgroup></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_formatting) - 1
                   && memcmp(sink.data, colgroup_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li><div id=d>"
             "<section title=s>x</b>y</section></div>"
             "</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_nested_block[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\"></b>"
            "<div id=\"d\"><b class=\"x\"></b><section title=\"s\">"
            "<b class=\"x\">x</b>y</section></div></li></ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_nested_block) - 1
                   && memcmp(sink.data, crossed_nested_block,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><section><div>x</b>y"
             "</div></section></li></ul></template>"
             "<template><b><ul><li><div><p>x</b>y</p>"
             "</div></li></ul></template>"
             "<template><b><ul><li><section><section>x</b>y"
             "</section></section></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_two_blocks[] =
            "<html><head><template><b></b><ul><b></b><li><b></b>"
            "<section><b></b><div><b>x</b>y</div></section>"
            "</li></ul></template><template><b></b><ul><b></b>"
            "<li><b></b><div><b></b><p><b>x</b>y</p></div>"
            "</li></ul></template><template><b></b><ul><b></b>"
            "<li><b></b><section><b></b><section><b>x</b>y"
            "</section></section></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_two_blocks) - 1
                   && memcmp(sink.data, crossed_two_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><div><section><div>x</b>y"
             "</div></section></div></li></ul></template>"
             "<template><b><ul><li><div><section><p>x</b>y"
             "</p></section></div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_three_blocks[] =
            "<html><head><template><b></b><ul><b></b><li><b></b>"
            "<div><b></b><section><b></b><div><b>x</b>y"
            "</div></section></div></li></ul></template>"
            "<template><b></b><ul><b></b><li><b></b>"
            "<div><b></b><section><b></b><p><b>x</b>y"
            "</p></section></div></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_three_blocks) - 1
                   && memcmp(sink.data, crossed_three_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><div><div><div><div>x</b>y"
             "</div></div></div></div></li></ul></template>"
             "<template><b><ul><li><div><div><div><div><div>x"
             "</b>y</div></div></div></div></div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_deep_blocks[] =
            "<html><head><template><b></b><ul><b></b><li><b></b>"
            "<div><b></b><div><b></b><div><b></b><div><b>x</b>y"
            "</div></div></div></div></li></ul></template>"
            "<template><b></b><ul><b></b><li><b></b>"
            "<div><b></b><div><b></b><div><b></b><div><b></b>"
            "<div><b>x</b>y</div></div></div></div></div>"
            "</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_deep_blocks) - 1
                   && memcmp(sink.data, crossed_deep_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><div><section><span>x"
             "</b>y</span></section></div></li></ul></template>"
             "<template><b><ul><li><div><section><label>"
             "<span>x</b>y</span></label></section></div></li>"
             "</ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_nested_inline[] =
            "<html><head><template><b></b><ul><b></b><li><b></b>"
            "<div><b></b><section><b><span>x</span></b>y"
            "</section></div></li></ul></template>"
            "<template><b></b><ul><b></b><li><b></b>"
            "<div><b></b><section><b><label><span>x"
            "</span></label></b>y</section></div></li></ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_nested_inline) - 1
                   && memcmp(sink.data, crossed_nested_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><span><div><section>x"
             "</b>y</section></div></span></li></ul></template>"
             "<template><b><ul><li><label><span><div><section>"
             "<span>x</b>y</span></section></div></span></label>"
             "</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_surrounding_inline[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b><span></span></b><div><b></b><section><b>x"
            "</b>y</section></div></li></ul></template>"
            "<template><b></b><ul><b></b><li>"
            "<b><label><span></span></label></b><div><b></b>"
            "<section><b><span>x</span></b>y</section></div>"
            "</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_surrounding_inline) - 1
                   && memcmp(sink.data, crossed_surrounding_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><div><section><em>x</b>y"
             "</em></section></div></li></ul></template>"
             "<template><b><ul><li><span><div><section>"
             "<em><i>x</b>y</i></em></section></div></span>"
             "</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_nested_formatting[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b></b><div><b></b><section><b><em>x</em></b>"
            "<em>y</em></section></div></li></ul></template>"
            "<template><b></b><ul><b></b><li>"
            "<b><span></span></b><div><b></b><section>"
            "<b><em><i>x</i></em></b><em><i>y</i></em>"
            "</section></div></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_nested_formatting) - 1
                   && memcmp(sink.data, crossed_nested_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><svg><g>x</g></svg>"
             "<col></table></template>"
             "<template><table><colgroup><math><mi>y</mi>"
             "</math><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char colgroup_foreign[] =
            "<html><head><template><svg><g>x</g></svg>"
            "<table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template>"
            "<template><math><mi>y</mi></math>"
            "<table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(colgroup_foreign) - 1
                   && memcmp(sink.data, colgroup_foreign,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><svg><g>x</g></svg>"
             "<tr><td>y</table></template>"
             "<template><table><tbody><math><mi>x</mi></math>"
             "<tr><td>y</table></template>"
             "<template><table><tbody><tr><svg><g>x</g></svg>"
             "<td>y</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_foreign[] =
            "<html><head><template><svg><g>x</g></svg>"
            "<table><tbody><tr><td>y</td></tr></tbody></table>"
            "</template><template><math><mi>x</mi></math>"
            "<table><tbody><tr><td>y</td></tr></tbody></table>"
            "</template><template><svg><g>x</g></svg>"
            "<table><tbody><tr><td>y</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_foreign) - 1
                   && memcmp(sink.data, table_foreign,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><rt>x<rp>y</template>"
             "<template><table><colgroup><rt>x</rt><col>"
             "</table></template>"
             "<template><table><tbody><tr><rp>x</rp><td>y"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char orphan_ruby_annotations[] =
            "<html><head><template><rt>x<rp>y</rp></rt></template>"
            "<template><rt>x</rt><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "<template><rp>x</rp><table><tbody><tr><td>y</td></tr>"
            "</tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(orphan_ruby_annotations) - 1
                   && memcmp(sink.data, orphan_ruby_annotations,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><ruby>x<rt>y"
             "</ruby><col></table></template>"
             "<template><table><q>x</q><tr><td>y</table>"
             "</template><template><table><tbody><tr>"
             "<cite>x</cite><td>y</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_inline[] =
            "<html><head><template><ruby>x<rt>y</rt></ruby>"
            "<table><colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template><template><q>x</q>"
            "<table><tbody><tr><td>y</td></tr></tbody></table>"
            "</template><template><cite>x</cite>"
            "<table><tbody><tr><td>y</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_inline) - 1
                   && memcmp(sink.data, table_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><button>a"
             "<button>b</button><col></table></template>"
             "<template><div><button>a<button>b</button>"
             "</div></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_buttons[] =
            "<html><head><template><button>a</button>"
            "<button>b</button><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "<template><div><button>a</button><button>b"
            "</button></div></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_buttons) - 1
                   && memcmp(sink.data, template_buttons,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><select><option>a"
             "<select><option>b</select><col></table></template>"
             "<template><table><optgroup><option>a<option>b"
             "<optgroup><option>c<tr><td>d</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_orphan_options[] =
            "<html><head><template><select><option>a</option>"
            "</select><option>b</option><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table>"
            "</template><template><optgroup><option>a</option>"
            "<option>b</option><optgroup><option>c</option>"
            "</optgroup></optgroup><table><tbody><tr><td>d"
            "</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_orphan_options) - 1
                   && memcmp(sink.data, table_orphan_options,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><foreignObject><button>x</button>"
             "</foreignObject><g><button>y</button></g>"
             "<title><button>z</button></title>"
             "<desc><button>a<button>b</button></desc>"
             "</svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_buttons[] =
            "<html><head><template><svg><foreignObject><button>x"
            "</button></foreignObject><g><button>y</button></g>"
            "<title><button>z</button></title><desc><button>a"
            "</button><button>b</button></desc></svg></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(svg_buttons) - 1
                   && memcmp(sink.data, svg_buttons,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><button>a<button>b"
             "</button></mtext><mrow><button>c</button>"
             "</mrow><annotation-xml encoding=text/html>"
             "<button>d</button></annotation-xml></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_buttons[] =
            "<html><head><template><math><mtext><button>a</button>"
            "<button>b</button></mtext><mrow><button>c</button>"
            "</mrow><annotation-xml encoding=\"text/html\">"
            "<button>d</button></annotation-xml></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(math_buttons) - 1
                   && memcmp(sink.data, math_buttons,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><foreignObject><ul><li>a<li>b"
             "</ul></foreignObject><title><ol><li>x</li>"
             "</ol></title><desc><section>y</section>"
             "</desc></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_integration_blocks[] =
            "<html><head><template><svg><foreignObject><ul>"
            "<li>a</li><li>b</li></ul></foreignObject><title>"
            "<ol><li>x</li></ol></title><desc><section>y"
            "</section></desc></svg></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(svg_integration_blocks) - 1
                   && memcmp(sink.data, svg_integration_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><ul><li>a<li>b</ul>"
             "<section>c</section></mtext><annotation-xml "
             "encoding=text/html><ol><li>d</li></ol>"
             "</annotation-xml></math><math><mrow><ol>"
             "<li>e</li></ol></mrow></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_integration_lists[] =
            "<html><head><template><math><mtext><ul><li>a"
            "</li><li>b</li></ul><section>c</section></mtext>"
            "<annotation-xml encoding=\"text/html\"><ol><li>d"
            "</li></ol></annotation-xml></math><math><mrow>"
            "</mrow></math><ol><li>e</li></ol></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(math_integration_lists) - 1
                   && memcmp(sink.data, math_integration_lists,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><article>x</article></g>"
             "<foreignObject><article>y</article>"
             "</foreignObject></svg><math><mtext>"
             "<article>z</article></mtext></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_articles[] =
            "<html><head><template><svg><g><article>x</article>"
            "</g><foreignObject><article>y</article>"
            "</foreignObject></svg><math><mtext><article>z"
            "</article></mtext></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_articles) - 1
                   && memcmp(sink.data, foreign_articles,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><aside>x</aside><main>y"
             "</main></g><foreignObject><nav>z</nav>"
             "</foreignObject></svg><math><mrow><header>a"
             "</header></mrow><mtext><footer>b</footer>"
             "</mtext></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_sectioning[] =
            "<html><head><template><svg><g><aside>x</aside>"
            "<main>y</main></g><foreignObject><nav>z</nav>"
            "</foreignObject></svg><math><mrow><header>a"
            "</header></mrow><mtext><footer>b</footer>"
            "</mtext></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_sectioning) - 1
                   && memcmp(sink.data, foreign_sectioning,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><div><span><section>"
             "<div>x</b>y</div></section></span></div>"
             "</li></ul></template>"
             "<template><b><ul><li><div><label><section>"
             "<em>x</b>y</em></section></label></div>"
             "</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_interposed_inline[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b></b><div><b><span></span></b><section><b></b>"
            "<div><b>x</b>y</div></section></div></li></ul>"
            "</template><template><b></b><ul><b></b><li>"
            "<b></b><div><b><label></label></b><section>"
            "<b><em>x</em></b><em>y</em></section></div>"
            "</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_interposed_inline) - 1
                   && memcmp(sink.data, crossed_interposed_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><div><span><section>"
             "<span>x</b>y</span></section></span></div>"
             "</li></ul></template>"
             "<template><b><ul><li><span><div><label>"
             "<section><span><em>x</b>y</em></span>"
             "</section></label></div></span></li></ul>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_both_inline[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b></b><div><b><span></span></b><section>"
            "<b><span>x</span></b>y</section></div>"
            "</li></ul></template><template><b></b><ul>"
            "<b></b><li><b><span></span></b><div>"
            "<b><label></label></b><section>"
            "<b><span><em>x</em></span></b><em>y</em>"
            "</section></div></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_both_inline) - 1
                   && memcmp(sink.data, crossed_both_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><div><section>"
             "<span><div>x</b>y</div></span></section>"
             "</div></li></ul></template>"
             "<template><b><ul><li><div><label><section>"
             "<span><div><em>x</b>y</em></div></span>"
             "</section></label></div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_nested_gap[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b></b><div><b></b><section><b><span></span>"
            "</b><div><b>x</b>y</div></section></div>"
            "</li></ul></template><template><b></b><ul>"
            "<b></b><li><b></b><div><b><label></label>"
            "</b><section><b><span></span></b><div>"
            "<b><em>x</em></b><em>y</em></div></section>"
            "</div></li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_nested_gap) - 1
                   && memcmp(sink.data, crossed_nested_gap,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><div><section><span>"
             "<div><label><section>x</b>y</section>"
             "</label></div></span></section></div>"
             "</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_two_gaps[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b></b><div><b></b><section><b><span></span>"
            "</b><div><b><label></label></b><section>"
            "<b>x</b>y</section></div></section></div>"
            "</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_two_gaps) - 1
                   && memcmp(sink.data, crossed_two_gaps,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><dialog>x"
             "</dialog><col></table></template>"
             "<template><table><tbody><tr><dialog>"
             "<p>y</p></dialog><td>z</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_dialogs[] =
            "<html><head><template><dialog>x</dialog>"
            "<table><colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template><template><dialog><p>y</p>"
            "</dialog><table><tbody><tr><td>z</td></tr>"
            "</tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_dialogs) - 1
                   && memcmp(sink.data, table_dialogs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><label>x</label><cite>y</cite>"
             "</g><foreignObject><label>a</label><q>b</q>"
             "</foreignObject></svg><math><mrow><abbr>x"
             "</abbr><q>y</q></mrow><mtext><label>a</label>"
             "<cite>b</cite></mtext></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_simple_inline[] =
            "<html><head><template><svg><g><label>x</label>"
            "<cite>y</cite></g><foreignObject><label>a</label>"
            "<q>b</q></foreignObject></svg><math><mrow>"
            "<abbr>x</abbr><q>y</q></mrow><mtext>"
            "<label>a</label><cite>b</cite></mtext></math>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_simple_inline) - 1
                   && memcmp(sink.data, foreign_simple_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><object><object>x</object></object>"
             "</template><template><table><object><select>"
             "<option>a</select><object>b</object></object>"
             "<tr><td>c</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_objects[] =
            "<html><head><template><object><object>x</object>"
            "</object></template><template><object><select>"
            "<option>a</option></select><object>b</object>"
            "</object><table><tbody><tr><td>c</td></tr>"
            "</tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_objects) - 1
                   && memcmp(sink.data, nested_objects,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><details><summary>x</summary>"
             "<p>y</p></details></template>"
             "<template><table><colgroup><details>"
             "<summary>a</summary></details><col></table>"
             "</template><template><table><tbody><tr>"
             "<summary>b</summary><td>c</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_details[] =
            "<html><head><template><details><summary>x</summary>"
            "<p>y</p></details></template><template><details>"
            "<summary>a</summary></details><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table>"
            "</template><template><summary>b</summary>"
            "<table><tbody><tr><td>c</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_details) - 1
                   && memcmp(sink.data, template_details,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><fieldset><legend>x</legend>"
             "<p>y</p></fieldset></template>"
             "<template><table><colgroup><fieldset>"
             "<legend>a</legend></fieldset><col></table>"
             "</template><template><table><tbody><tr>"
             "<legend>b</legend><td>c</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_fieldsets[] =
            "<html><head><template><fieldset><legend>x</legend>"
            "<p>y</p></fieldset></template><template><fieldset>"
            "<legend>a</legend></fieldset><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table>"
            "</template><template><legend>b</legend>"
            "<table><tbody><tr><td>c</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_fieldsets) - 1
                   && memcmp(sink.data, template_fieldsets,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><figure><figcaption>x</figcaption>"
             "<p>y</p></figure></template><template><table>"
             "<colgroup><figcaption>a</figcaption><col>"
             "</table></template><template><table><tbody><tr>"
             "<figure><figcaption>b</figcaption></figure>"
             "<td>c</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_figcaptions[] =
            "<html><head><template><figure><figcaption>x"
            "</figcaption><p>y</p></figure></template>"
            "<template><figcaption>a</figcaption><table>"
            "<colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template><template><figure><figcaption>b"
            "</figcaption></figure><table><tbody><tr><td>c"
            "</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_figcaptions) - 1
                   && memcmp(sink.data, template_figcaptions,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><hgroup><h2>x</h2>"
             "</hgroup><col></table></template>"
             "<template><table><tbody><tr><search><p>y</p>"
             "</search><td>z</table></template>"
             "<template><table><colgroup><dir><li>a<li>b"
             "</dir><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_more_blocks[] =
            "<html><head><template><hgroup><h2>x</h2></hgroup>"
            "<table><colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template><template><search><p>y</p>"
            "</search><table><tbody><tr><td>z</td></tr>"
            "</tbody></table></template><template><dir><li>a"
            "</li><li>b</li></dir><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_more_blocks) - 1
                   && memcmp(sink.data, template_more_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><noscript><b>x</b></noscript></template>"
             "<template><table><colgroup><noscript><b>x</b>"
             "</noscript><col></table></template>"
             "<template><table><tbody><tr><noscript><b>x</b>"
             "</noscript><td>y</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_noscript[] =
            "<html><head><template><noscript><b>x</b></noscript>"
            "</template><template><noscript><b>x</b></noscript>"
            "<table><colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template><template><noscript><b>x</b>"
            "</noscript><table><tbody><tr><td>y</td></tr></tbody>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_noscript) - 1
                   && memcmp(sink.data, template_noscript,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><picture><source src=x>"
             "<img src=y></picture><col></table></template>"
             "<template><table><tbody><tr><video><source src=x>v"
             "</video><td>y</table></template>"
             "<template><table><colgroup><canvas><span>x</span>"
             "</canvas><col></table></template>"
             "<template><table><colgroup><audio><source src=x>a"
             "</audio><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_media[] =
            "<html><head><template><picture><source src=\"x\">"
            "<img src=\"y\"></picture><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "<template><video><source src=\"x\">v</video><table>"
            "<tbody><tr><td>y</td></tr></tbody></table></template>"
            "<template><canvas><span>x</span></canvas><table>"
            "<colgroup></colgroup><colgroup><col></colgroup></table>"
            "</template><template><audio><source src=\"x\">a</audio>"
            "<table><colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_media) - 1
                   && memcmp(sink.data, template_media, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><data value=a><dfn>x</dfn><mark>y</mark>"
             "<time datetime=z>t</time></data></template>"
             "<template><table><colgroup><del><samp>a</samp>"
             "<kbd>b</kbd></del><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_phrasing[] =
            "<html><head><template><data value=\"a\"><dfn>x</dfn>"
            "<mark>y</mark><time datetime=\"z\">t</time></data>"
            "</template><template><del><samp>a</samp><kbd>b</kbd>"
            "</del><table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_phrasing) - 1
                   && memcmp(sink.data, template_phrasing,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><output name=x><span>a</span></output>"
             "</template><template><table><colgroup><progress"
             " value=1><span>b</span></progress><col></table>"
             "</template><template><table><tbody><tr><meter"
             " value=2><span>c</span></meter><td>d</table>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_values[] =
            "<html><head><template><output name=\"x\"><span>a</span>"
            "</output></template><template><progress value=\"1\">"
            "<span>b</span></progress><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "<template><meter value=\"2\"><span>c</span></meter>"
            "<table><tbody><tr><td>d</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_values) - 1
                   && memcmp(sink.data, template_values,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><span><div><span><section>"
             "<span><div><span><p>x</b>y</p></div></section>"
             "</div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_long_gaps[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b><span></span></b><div><b><span></span></b>"
            "<section><b><span></span></b><div><b><span></span>"
            "</b><p><b>x</b>y</p></div></section></div></li>"
            "</ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_long_gaps) - 1
                   && memcmp(sink.data, crossed_long_gaps,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><map name=m><area href=/x><span>a</span>"
             "</map></template><template><table><colgroup>"
             "<map name=m><area href=/x><span>a</span></map>"
             "<col></table></template><template><table><tbody>"
             "<tr><map name=m><area href=/x>x</map><td>y"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_maps[] =
            "<html><head><template><map name=\"m\"><area href=\"/x\">"
            "<span>a</span></map></template><template><map name=\"m\">"
            "<area href=\"/x\"><span>a</span></map><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table></template>"
            "<template><map name=\"m\"><area href=\"/x\">x</map>"
            "<table><tbody><tr><td>y</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_maps) - 1
                   && memcmp(sink.data, template_maps, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><datalist id=d><option>a<option>b"
             "</datalist></template><template><table><colgroup>"
             "<datalist id=d><option>a<option>b</datalist><col>"
             "</table></template><template><table><tbody><tr>"
             "<datalist id=d><option>a<option>b</datalist>"
             "<td>y</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_datalists[] =
            "<html><head><template><datalist id=\"d\"><option>a"
            "</option><option>b</option></datalist></template>"
            "<template><datalist id=\"d\"><option>a</option>"
            "<option>b</option></datalist><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table></template>"
            "<template><datalist id=\"d\"><option>a</option>"
            "<option>b</option></datalist><table><tbody><tr>"
            "<td>y</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_datalists) - 1
                   && memcmp(sink.data, template_datalists,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><slot id=s><video>x</video>"
             "</slot><select><option>a</option></select>"
             "<circle/></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_descendants[] =
            "<html><head><template><svg><g><slot id=\"s\">"
            "<video>x</video></slot><select><option>a</option>"
            "</select><circle></circle></g></svg></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(svg_descendants) - 1
                   && memcmp(sink.data, svg_descendants,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mrow><slot>x</slot><video>y"
             "</video></mrow></math></template>"
             "<template><math><mrow><strong>a</strong>"
             "<font color=red>b</font></mrow></math></template>"
             "<template><math><mrow><table>c</table></mrow>"
             "</math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_breakouts[] =
            "<html><head><template><math><mrow><slot>x</slot>"
            "<video>y</video></mrow></math></template>"
            "<template><math><mrow></mrow></math><strong>a</strong>"
            "<font color=\"red\">b</font></template>"
            "<template><math><mrow></mrow></math>c<table></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(math_breakouts) - 1
                   && memcmp(sink.data, math_breakouts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mrow><select><option>x</option>"
             "</select><slot>y</slot></mrow></math></template>"
             "<template><math><mrow><slot><strong>z</strong>"
             "</slot></mrow></math></template>"
             "<template><math><msup><table>x</table></msup>"
             "</math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_nested_foreign[] =
            "<html><head><template><math><mrow><select><option>x"
            "</option></select><slot>y</slot></mrow></math></template>"
            "<template><math><mrow><slot></slot></mrow></math>"
            "<strong>z</strong></template><template><math><msup>"
            "</msup></math>x<table></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(math_nested_foreign) - 1
                   && memcmp(sink.data, math_nested_foreign,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><g><slot><p>x</p></slot></g>"
             "</svg></template><template><svg><g><slot>"
             "<font color=red>y</font></slot></g></svg>"
             "</template><template><svg><g><slot><svg>"
             "<g><p>z</p></g></svg></slot></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_nested_breakouts[] =
            "<html><head><template><svg><g><slot></slot></g></svg>"
            "<p>x</p></template><template><svg><g><slot></slot>"
            "</g></svg><font color=\"red\">y</font></template>"
            "<template><svg><g><slot><svg><g></g></svg></slot>"
            "</g></svg><p>z</p></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(svg_nested_breakouts) - 1
                   && memcmp(sink.data, svg_nested_breakouts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><foreignObject><g><p>x</p></g>"
             "<video><span>y</span></video></foreignObject>"
             "<desc><custom><b>z</b></custom></desc>"
             "</svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_integrated_containers[] =
            "<html><head><template><svg><foreignObject><g>"
            "<p>x</p></g><video><span>y</span></video>"
            "</foreignObject><desc><custom><b>z</b></custom>"
            "</desc></svg></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(svg_integrated_containers) - 1
                   && memcmp(sink.data, svg_integrated_containers,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><foreignObject><title><b>x</b>"
             "</title><script><b>y</b></script></foreignObject>"
             "<desc><textarea>\n<b>z</b></textarea></desc>"
             "<title><xmp><b>w</b></xmp></title></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_integrated_text[] =
            "<html><head><template><svg><foreignObject><title>"
            "&lt;b&gt;x&lt;/b&gt;</title><script><b>y</b></script>"
            "</foreignObject><desc><textarea>&lt;b&gt;z&lt;/b&gt;"
            "</textarea></desc><title><xmp><b>w</b></xmp></title>"
            "</svg></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(svg_integrated_text) - 1
                   && memcmp(sink.data, svg_integrated_text,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><script><b>x</b></script>"
             "<noscript><span>y</span></noscript><custom>"
             "<b>z</b></custom></mtext><annotation-xml"
             " encoding=text/html><iframe><b>w</b></iframe>"
             "</annotation-xml></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_integrated_content[] =
            "<html><head><template><math><mtext><script><b>x</b>"
            "</script><noscript><span>y</span></noscript>"
            "<custom><b>z</b></custom></mtext><annotation-xml"
            " encoding=\"text/html\"><iframe><b>w</b></iframe>"
            "</annotation-xml></math></template></head><body></body>"
            "</html>";
        TEST_CHECK(sink.length == sizeof(math_integrated_content) - 1
                   && memcmp(sink.data, math_integrated_content,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><svg><g><p>x</p></g>"
             "</svg></mtext></math></template><template><math>"
             "<mrow><svg><g><p>y</p></g></svg></mrow>"
             "</math></template><template><math><annotation-xml"
             " encoding=text/html><svg><g><p>z</p></g></svg>"
             "</annotation-xml></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char svg_math_breakouts[] =
            "<html><head><template><math><mtext><svg><g></g></svg>"
            "<p>x</p></mtext></math></template><template><math>"
            "<mrow><svg><g></g></svg></mrow></math><p>y</p>"
            "</template><template><math><annotation-xml"
            " encoding=\"text/html\"><svg><g></g></svg><p>z</p>"
            "</annotation-xml></math></template></head><body></body>"
            "</html>";
        TEST_CHECK(sink.length == sizeof(svg_math_breakouts) - 1
                   && memcmp(sink.data, svg_math_breakouts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ins><span>x</span></ins><slot><b>y"
             "</b></slot></template><template><table><colgroup>"
             "<slot><ins><span>x</span></ins></slot><col>"
             "</table></template><template><table><tbody><tr>"
             "<ins><slot>x</slot></ins><td>y</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_transparent[] =
            "<html><head><template><ins><span>x</span></ins>"
            "<slot><b>y</b></slot></template><template><slot>"
            "<ins><span>x</span></ins></slot><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table></template>"
            "<template><ins><slot>x</slot></ins><table><tbody>"
            "<tr><td>y</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_transparent) - 1
                   && memcmp(sink.data, template_transparent,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><my-widget a=1><b>x"
             "</b></my-widget><col></table></template>"
             "<template><table><tbody><tr><custom-box>x"
             "</custom-box><td>y</table></template>"
             "<template><table><unknown>x</unknown><tr>"
             "<td>z</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_custom_foster[] =
            "<html><head><template><my-widget a=\"1\"><b>x</b>"
            "</my-widget><table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template><template><custom-box>x"
            "</custom-box><table><tbody><tr><td>y</td></tr></tbody>"
            "</table></template><template><unknown>x</unknown>"
            "<table><tbody><tr><td>z</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_custom_foster) - 1
                   && memcmp(sink.data, template_custom_foster,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><strong><ul><li>x"
             "</strong>y</li></ul><col></table></template>"
             "<template><table><tbody><tr><b><ol><li>"
             "<div>a</b>z</div></li></ol><td>q</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_foster_order[] =
            "<html><head><template><strong></strong><ul><strong>"
            "</strong><li><strong>x</strong>y</li></ul>"
            "<table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template><template><b></b>"
            "<ol><b></b><li><b></b><div><b>a</b>z</div></li>"
            "</ol><table><tbody><tr><td>q</td></tr></tbody>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_foster_order) - 1
                   && memcmp(sink.data, crossed_foster_order,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em class=x><div>a</b>b"
             "</template><template><strong><ol><li><i title=t>"
             "<section>c</strong>d</template><template><b><ul>"
             "<li><em><div><span><q>e</b>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_formatted_block[] =
            "<html><head><template><b></b><ul><b></b><li><b>"
            "<em class=\"x\"></em></b><em class=\"x\"><div>"
            "<b>a</b>b</div></em></li></ul></template>"
            "<template><strong></strong><ol><strong></strong><li>"
            "<strong><i title=\"t\"></i></strong><i title=\"t\">"
            "<section><strong>c</strong>d</section></i></li></ol>"
            "</template><template><b></b><ul><b></b><li><b>"
            "<em></em></b><em><div><b><span><q>e</q></span>"
            "</b>f</div></em></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_formatted_block) - 1
                   && memcmp(sink.data, crossed_list_formatted_block,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em class=x><strong title=t>"
             "<i data-q=v><div><span>g</b>h</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_formatting_chain[] =
            "<html><head><template><b></b><ul><b></b><li><b>"
            "<em class=\"x\"><strong title=\"t\"><i data-q=\"v\"></i>"
            "</strong></em></b><em class=\"x\"><strong title=\"t\">"
            "<i data-q=\"v\"><div><b><span>g</span></b>h</div></i>"
            "</strong></em></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_formatting_chain) - 1
                   && memcmp(sink.data, crossed_list_formatting_chain,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em><strong><i><code>"
             "<em class=x><div><span>a</b>b</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_long_formatting_chain[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b><em><strong><i><code><em class=\"x\"></em>"
            "</code></i></strong></em></b><i><code><em class=\"x\">"
            "<div><b><span>a</span></b>b</div></em></code></i></li>"
            "</ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_long_formatting_chain) - 1
                   && memcmp(sink.data, crossed_list_long_formatting_chain,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em class=x><strong title=t>"
             "<i id=z><code class=c><q><div>a</b>b</template>"
             "<template><b><ul><li><em><span><q><label>"
             "<div>c</b>d</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_preblock_inline[] =
            "<html><head><template><b></b><ul><b></b><li><b>"
            "<em class=\"x\"><strong title=\"t\"><i id=\"z\">"
            "<code class=\"c\"><q></q></code></i></strong></em></b>"
            "<i id=\"z\"><code class=\"c\"><div><b>a</b>b</div>"
            "</code></i></li></ul></template><template><b></b>"
            "<ul><b></b><li><b><em><span><q><label></label>"
            "</q></span></em></b><div><b>c</b>d</div></li></ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_preblock_inline) - 1
                   && memcmp(sink.data, crossed_list_preblock_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em class=x><span id=s>"
             "<strong title=t><div>a</b>b</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_interposed_inline[] =
            "<html><head><template><b></b><ul><b></b><li><b>"
            "<em class=\"x\"><span id=\"s\"><strong title=\"t\">"
            "</strong></span></em></b><em class=\"x\">"
            "<strong title=\"t\"><div><b>a</b>b</div></strong></em>"
            "</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_interposed_inline) - 1
                   && memcmp(sink.data, crossed_list_interposed_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em class=x><div>a</b>b"
             "</ul>c</em>d</template><template><ul><li><em>x"
             "</ul><template>q</template>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_closure[] =
            "<html><head><template><b></b><ul><b></b><li><b>"
            "<em class=\"x\"></em></b><em class=\"x\"><div>"
            "<b>a</b>b</div></em></li></ul><em class=\"x\">c</em>d"
            "</template><template><ul><li><em>x</em></li></ul>"
            "<template>q</template><em>y</em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_closure) - 1
                   && memcmp(sink.data, crossed_list_closure,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><p>b</template>"
             "<template><ol><li><strong>c</ol><div>d</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_blocks[] =
            "<html><head><template><ul><li><em class=\"x\">a</em>"
            "</li></ul><p><em class=\"x\">b</em></p></template>"
            "<template><ol><li><strong>c</strong></li></ol>"
            "<div><strong>d</strong></div></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_blocks) - 1
                   && memcmp(sink.data, following_list_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><p>b</p>c"
             "</template><template><ol><li><strong>d</ol>"
             "<div>e</div>f</template><template><ul><li>"
             "<em>g</ul><p></p>h</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char closed_list_blocks[] =
            "<html><head><template><ul><li><em class=\"x\">a</em>"
            "</li></ul><p><em class=\"x\">b</em></p><em class=\"x\">c"
            "</em></template><template><ol><li><strong>d</strong>"
            "</li></ol><div><strong>e</strong></div><strong>f"
            "</strong></template><template><ul><li><em>g</em>"
            "</li></ul><p></p><em>h</em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(closed_list_blocks) - 1
                   && memcmp(sink.data, closed_list_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<section>b</section>c</template>"
             "<template><ol><li><strong>d</ol>"
             "<article>e</article>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_sections[] =
            "<html><head><template><ul><li><em class=\"x\">a</em>"
            "</li></ul><section><em class=\"x\">b</em></section>"
            "<em class=\"x\">c</em></template><template><ol>"
            "<li><strong>d</strong></li></ol><article>"
            "<strong>e</strong></article><strong>f</strong>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_sections) - 1
                   && memcmp(sink.data, following_list_sections,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em>a</ul><h1>b</h1>c"
             "</template><template><ol><li><strong>d</ol>"
             "<h6>e</h6>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_headings[] =
            "<html><head><template><ul><li><em>a</em></li></ul>"
            "<h1><em>b</em></h1><em>c</em></template><template>"
            "<ol><li><strong>d</strong></li></ol><h6><strong>e"
            "</strong></h6><strong>f</strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_headings) - 1
                   && memcmp(sink.data, following_list_headings,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><br>b</em>c"
             "</template><template><ol><li><strong>d</ol>"
             "<img src=x>e</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_voids[] =
            "<html><head><template><ul><li><em class=\"x\">a</em>"
            "</li></ul><em class=\"x\"><br>b</em>c</template>"
            "<template><ol><li><strong>d</strong></li></ol>"
            "<strong><img src=\"x\">e</strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_voids) - 1
                   && memcmp(sink.data, following_list_voids,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em>a</ul><br><img src=x>"
             "<wbr>b</em>c</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_void_chain[] =
            "<html><head><template><ul><li><em>a</em></li></ul>"
            "<em><br><img src=\"x\"><wbr>b</em>c</template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_void_chain) - 1
                   && memcmp(sink.data, following_list_void_chain,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em>a</ul><hr><hr>b"
             "</template><template><ol><li><strong>c</ol>"
             "<p>d</p><hr>e</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_rules[] =
            "<html><head><template><ul><li><em>a</em></li></ul>"
            "<hr><hr><em>b</em></template><template><ol>"
            "<li><strong>c</strong></li></ol><p><strong>d"
            "</strong></p><hr><strong>e</strong></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_rules) - 1
                   && memcmp(sink.data, following_list_rules,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<ol><li>b</ol>c</template>"
             "<template><ol><li><strong>d</ol>"
             "<ul><li></ul>e</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_items[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><ol><li><em class=\"x\">b</em>"
            "</li></ol><em class=\"x\">c</em></template>"
            "<template><ol><li><strong>d</strong></li></ol>"
            "<ul><li></li></ul><strong>e</strong></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_items) - 1
                   && memcmp(sink.data, following_list_items,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em>a</ul><ol><li>b<li>c"
             "</ol>d</template><template><ol><li><strong>e"
             "</ol><ul><li>f</li><li>g</li></ul>h</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_siblings[] =
            "<html><head><template><ul><li><em>a</em></li></ul>"
            "<ol><li><em>b</em></li><li><em>c</em></li></ol>"
            "<em>d</em></template><template><ol><li><strong>"
            "e</strong></li></ol><ul><li><strong>f</strong></li>"
            "<li><strong>g</strong></li></ul><strong>h</strong>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_siblings) - 1
                   && memcmp(sink.data, following_list_siblings,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><div>"
             "<ol><li>b<li>c</ol>d</div></template>"
             "<template><ol><li><strong>e</ol><div>"
             "<ul><li>f</li><li>g</li></ul>h</div></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_list_continuation[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><div><ol><li><em class=\"x\">b</em>"
            "</li><li><em class=\"x\">c</em></li></ol>"
            "<em class=\"x\">d</em></div></template><template>"
            "<ol><li><strong>e</strong></li></ol><div><ul>"
            "<li><strong>f</strong></li><li><strong>g</strong>"
            "</li></ul><strong>h</strong></div></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_list_continuation) - 1
                   && memcmp(sink.data, nested_list_continuation,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<span>b</span><span>c</span>d</em>e</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list_spans[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><span>b</span>"
            "<span>c</span>d</em>e</template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list_spans) - 1
                   && memcmp(sink.data, following_list_spans,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<span>b</em>c</template><template><ol><li>"
             "<strong>d</ol><span>e</strong></span>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_following_span[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><span>b</span></em>"
            "c</template><template><ol><li><strong>d</strong>"
            "</li></ol><strong><span>e</span></strong>f"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_following_span) - 1
                   && memcmp(sink.data, crossed_following_span,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<span><br><img src=x><wbr>b</span>c</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_span_voids[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><span><br>"
            "<img src=\"x\"><wbr>b</span>c</em></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_span_voids) - 1
                   && memcmp(sink.data, following_span_voids,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em>a</ul><br><hr>b"
             "</template><template><ol><li><strong>c</ol>"
             "<span>d</span><hr>e</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char rule_after_formatting[] =
            "<html><head><template><ul><li><em>a</em></li></ul>"
            "<em><br><hr>b</em></template><template><ol>"
            "<li><strong>c</strong></li></ol><strong><span>d"
            "</span><hr>e</strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(rule_after_formatting) - 1
                   && memcmp(sink.data, rule_after_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em>a</ul><br><p>b</p>c"
             "</template><template><ol><li><strong>d</ol>"
             "<span>e</span><div>f</div>g</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char blocks_inside_formatting[] =
            "<html><head><template><ul><li><em>a</em></li></ul>"
            "<em><br><p>b</p>c</em></template><template><ol>"
            "<li><strong>d</strong></li></ol><strong><span>e"
            "</span><div>f</div>g</strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(blocks_inside_formatting) - 1
                   && memcmp(sink.data, blocks_inside_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<span><span><span>b</span>c</span>d</span>e"
             "</template><template><ol><li><strong>f</ol>"
             "<span><span>g</strong>h</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_formatting_spans[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><span><span>"
            "<span>b</span>c</span>d</span>e</em></template>"
            "<template><ol><li><strong>f</strong></li></ol>"
            "<strong><span><span>g</span></span></strong>h"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_formatting_spans) - 1
                   && memcmp(sink.data, nested_formatting_spans,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em>a</ul><span><span>"
             "b<hr>c</span></span></template><template><ol>"
             "<li><strong>d</ol><span><span>e<br>"
             "<img src=x><wbr>f</span></span></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_span_voids[] =
            "<html><head><template><ul><li><em>a</em></li></ul>"
            "<em><span><span>b<hr>c</span></span></em></template>"
            "<template><ol><li><strong>d</strong></li></ol>"
            "<strong><span><span>e<br><img src=\"x\"><wbr>f"
            "</span></span></strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_span_voids) - 1
                   && memcmp(sink.data, nested_span_voids,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><span>"
             "<span>b<div>c</div>d</span></span>e</template>"
             "<template><ol><li><strong>f</ol><span>"
             "<span>g<section>h</section>i</span></span>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char blocks_in_nested_spans[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><span><span>b"
            "<div>c</div>d</span></span>e</em></template>"
            "<template><ol><li><strong>f</strong></li></ol>"
            "<strong><span><span>g<section>h</section>i"
            "</span></span></strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(blocks_in_nested_spans) - 1
                   && memcmp(sink.data, blocks_in_nested_spans,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><div><div>"
             "<div><ol><li>b<li>c</ol>d</div>e</div>f"
             "</div>g</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char deep_list_continuation[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><div><div><div><ol><li><em class=\"x\">"
            "b</em></li><li><em class=\"x\">c</em></li></ol>"
            "<em class=\"x\">d</em></div><em class=\"x\">e</em>"
            "</div><em class=\"x\">f</em></div><em class=\"x\">g"
            "</em></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(deep_list_continuation) - 1
                   && memcmp(sink.data, deep_list_continuation,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<address>b</address>c<br><blockquote>d"
             "</blockquote>e<span><figure>f</figure>g"
             "</span></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_special_blocks[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><address><em class=\"x\">b</em>"
            "</address><em class=\"x\">c<br><blockquote>d"
            "</blockquote>e<span><figure>f</figure>g</span>"
            "</em></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_special_blocks) - 1
                   && memcmp(sink.data, following_special_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<object data=q>b</object>c</template>"
             "<template><ol><li><strong>d</ol>"
             "<br><object>e</object>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char objects_after_lists[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><object data=\"q\">"
            "b</object>c</em></template><template><ol>"
            "<li><strong>d</strong></li></ol><strong><br>"
            "<object>e</object>f</strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(objects_after_lists) - 1
                   && memcmp(sink.data, objects_after_lists,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<ol><li><ul><li><ol><li>b</ol>c"
             "</ul>d</ol>e</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_following_lists[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><ol><li><ul><li><ol><li>"
            "<em class=\"x\">b</em></li></ol><em class=\"x\">c"
            "</em></li></ul><em class=\"x\">d</em></li></ol>"
            "<em class=\"x\">e</em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_following_lists) - 1
                   && memcmp(sink.data, nested_following_lists,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><ol>"
             "<li>b<ul><li>c<li>d</ul>e</ol>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char formatted_nested_list[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><ol><li><em class=\"x\">b<ul>"
            "<li>c</li><li>d</li></ul>e</em></li></ol>"
            "<em class=\"x\">f</em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(formatted_nested_list) - 1
                   && memcmp(sink.data, formatted_nested_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<ol><li><div><div><ul><li>b</ul>c"
             "</div>d</div>e</ol>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char list_div_descendants[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><ol><li><div><div><ul><li>"
            "<em class=\"x\">b</em></li></ul><em class=\"x\">c"
            "</em></div><em class=\"x\">d</em></div>"
            "<em class=\"x\">e</em></li></ol><em class=\"x\">f"
            "</em></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(list_div_descendants) - 1
                   && memcmp(sink.data, list_div_descendants,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><ol>"
             "<li>b<div><div><ul><li>c</ul>d"
             "</div>e</div>f</ol>g</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char formatted_list_divs[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><ol><li><em class=\"x\">b"
            "<div><div><ul><li>c</li></ul>d</div>e</div>f"
            "</em></li></ol><em class=\"x\">g</em></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(formatted_list_divs) - 1
                   && memcmp(sink.data, formatted_list_divs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul><ol>"
             "<li>b<section><blockquote><ul><li>c</ul>d"
             "</blockquote>e</section>f</ol>g</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char formatted_list_blocks[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><ol><li><em class=\"x\">b"
            "<section><blockquote><ul><li>c</li></ul>d"
            "</blockquote>e</section>f</em></li></ol>"
            "<em class=\"x\">g</em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(formatted_list_blocks) - 1
                   && memcmp(sink.data, formatted_list_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<marquee>b</marquee>c<br><applet>d</applet>e"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char markers_after_lists[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><marquee>b"
            "</marquee>c<br><applet>d</applet>e</em>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(markers_after_lists) - 1
                   && memcmp(sink.data, markers_after_lists,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<title>b&amp;c</title><textarea>d&amp;e</textarea>"
             "<script>f()</script><style>g{}</style>h</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char text_modes_after_list[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><title>b&amp;c</title>"
            "<textarea>d&amp;e</textarea><script>f()</script>"
            "<style>g{}</style><em class=\"x\">h</em>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(text_modes_after_list) - 1
                   && memcmp(sink.data, text_modes_after_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<xmp>b</xmp>c</template><template><ol><li>"
             "<strong>d</ol><iframe>e</iframe>"
             "<noembed>f</noembed><noframes>g</noframes>h"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char raw_modes_after_list[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><xmp>b</xmp>c"
            "</em></template><template><ol><li><strong>d"
            "</strong></li></ol><iframe>e</iframe><noembed>"
            "f</noembed><noframes>g</noframes><strong>h"
            "</strong></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(raw_modes_after_list) - 1
                   && memcmp(sink.data, raw_modes_after_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<plaintext>b"), &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char plaintext_after_list[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><plaintext><em class=\"x\">b"
            "</em></plaintext></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(plaintext_after_list) - 1
                   && memcmp(sink.data, plaintext_after_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em>a</ul><br><plaintext>b"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
               == LXA_OK);
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<pre>\nb</pre>c</template><template><ol>"
             "<li><strong>d</ol><br><listing>\ne"
             "</listing>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_preformatted[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><pre><em class=\"x\">b</em></pre>"
            "<em class=\"x\">c</em></template><template><ol>"
            "<li><strong>d</strong></li></ol><strong><br>"
            "<listing>e</listing>f</strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_preformatted) - 1
                   && memcmp(sink.data, following_preformatted,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<svg><circle/></svg>b</template>"
             "<template><ol><li><strong>c</ol>"
             "<math><mi>d</mi></math>e</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_after_list[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><svg><circle></circle>"
            "</svg>b</em></template><template><ol><li><strong>"
            "c</strong></li></ol><strong><math><mi>d</mi></math>"
            "e</strong></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_after_list) - 1
                   && memcmp(sink.data, foreign_after_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<br><svg><circle/></svg>b</template>"
             "<template><ol><li><strong>c</ol><span>"
             "<math><mi>d</mi></math>e</span></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_under_formatting[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><br><svg>"
            "<circle></circle></svg>b</em></template><template>"
            "<ol><li><strong>c</strong></li></ol><strong>"
            "<span><math><mi>d</mi></math>e</span></strong>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_under_formatting) - 1
                   && memcmp(sink.data, foreign_under_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x>a</ul>"
             "<svg><g><path/></g></svg>b</template>"
             "<template><ol><li><strong>c</ol>"
             "<math><mo>+</mo><mn>2</mn></math>d</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_descendants_after_list[] =
            "<html><head><template><ul><li><em class=\"x\">a"
            "</em></li></ul><em class=\"x\"><svg><g><path>"
            "</path></g></svg>b</em></template><template>"
            "<ol><li><strong>c</strong></li></ol><strong>"
            "<math><mo>+</mo><mn>2</mn></math>d</strong>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_descendants_after_list) - 1
                   && memcmp(sink.data, foreign_descendants_after_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong title=t>"
             "<i>a</ul><template>q</template>b</template>"
             "<template><ol><li><b><em>c</ol>d</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char multiple_list_formatting[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong title=\"t\"><i>a</i></strong></em></li>"
            "</ul><template>q</template><em class=\"x\">"
            "<strong title=\"t\"><i>b</i></strong></em></template>"
            "<template><ol><li><b><em>c</em></b></li></ol>"
            "<b><em>d</em></b></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(multiple_list_formatting) - 1
                   && memcmp(sink.data, multiple_list_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong title=t>x</ul>"
             "<hr><span><span><img src=a>y</span>z</span><br>w"
             "<span>q</span><wbr><hr></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char multiple_formatting_spans[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong title=\"t\">x</strong></em></li></ul><hr>"
            "<em class=\"x\"><strong title=\"t\"><span><span>"
            "<img src=\"a\">y</span>z</span><br>w<span>q</span>"
            "<wbr><hr></strong></em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(multiple_formatting_spans) - 1
                   && memcmp(sink.data, multiple_formatting_spans,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong title=t>x</ul>"
             "<p>a<section>y<blockquote>z</blockquote>q</section>"
             "<div>v</div>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char multiple_formatting_div[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong title=\"t\">x</strong></em></li></ul>"
            "<p><em class=\"x\"><strong title=\"t\">a</strong></em></p>"
            "<section><em class=\"x\"><strong title=\"t\">y"
            "<blockquote>z</blockquote>q</strong></em></section>"
            "<div><em class=\"x\"><strong title=\"t\">v</strong></em></div>"
            "<em class=\"x\"><strong title=\"t\">w"
            "</strong></em></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(multiple_formatting_div) - 1
                   && memcmp(sink.data, multiple_formatting_div,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong title=t><i>a</ul>"
             "b</i>c</strong>d</em>e</template>"
             "<template><ol><li><em><strong>x</ol></strong>y</template>"
             "<template><ul><li><em><strong>x</ul>"
             "<div>y</strong>z</div>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char multiple_formatting_ends[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong title=\"t\"><i>a</i></strong></em></li></ul>"
            "<em class=\"x\"><strong title=\"t\"><i>b</i>c</strong>d"
            "</em>e</template><template><ol><li><em><strong>x"
            "</strong></em></li></ol><em>y</em></template>"
            "<template><ul><li><em><strong>x</strong></em></li></ul>"
            "<div><em><strong>y</strong>z</em></div><em>w</em>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(multiple_formatting_ends) - 1
                   && memcmp(sink.data, multiple_formatting_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong><i>x</ul>"
             "y</em>z</i>q</strong>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_early_formatting[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong><i>x</i></strong></em></li></ul><em class=\"x\">"
            "<strong><i>y</i></strong></em><strong><i>z</i>q"
            "</strong>w</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_early_formatting) - 1
                   && memcmp(sink.data, crossed_early_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong title=t>x</ul>"
             "<svg><g><path/></g></svg>y<math><mi>q</mi></math>z"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char multiple_formatting_foreign[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong title=\"t\">x</strong></em></li></ul>"
            "<em class=\"x\"><strong title=\"t\"><svg><g><path></path>"
            "</g></svg>y<math><mi>q</mi></math>z</strong></em>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(multiple_formatting_foreign) - 1
                   && memcmp(sink.data, multiple_formatting_foreign,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong title=t>x</ul>"
             "<p>y<p>z<hr>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char multiple_formatting_paragraphs[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong title=\"t\">x</strong></em></li></ul>"
            "<p><em class=\"x\"><strong title=\"t\">y</strong></em></p>"
            "<p><em class=\"x\"><strong title=\"t\">z</strong></em></p>"
            "<hr>"
            "<em class=\"x\"><strong title=\"t\">w</strong></em>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(multiple_formatting_paragraphs) - 1
                   && memcmp(sink.data, multiple_formatting_paragraphs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em><strong><i>x</ul>"
             "y</strong>z</i>w</em>q</template>"
             "<template><ol><li><em><em><strong>x</ol>"
             "y</em>z</strong>w</em>q</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_middle_formatting[] =
            "<html><head><template><ul><li><em><strong><i>x</i>"
            "</strong></em></li></ul><em><strong><i>y</i></strong>"
            "<i>z</i>w</em>q</template><template><ol><li><em><em>"
            "<strong>x</strong></em></em></li></ol><em><em>"
            "<strong>y</strong></em><strong>z</strong>w</em>q"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_middle_formatting) - 1
                   && memcmp(sink.data, crossed_middle_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong>x</ul>"
             "<span>y</em>z</span>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_span_formatting[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong>x</strong></em></li></ul><em class=\"x\">"
            "<strong><span>y</span></strong></em><strong>zw</strong>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_span_formatting) - 1
                   && memcmp(sink.data, crossed_span_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong><i>x</ul>"
             "<div>y</em>z</div>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_block_formatting[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong><i>x</i></strong></em></li></ul><div>"
            "<em class=\"x\"><strong><i>y</i></strong></em>"
            "<strong><i>z</i></strong></div><strong><i>w</i>"
            "</strong></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_block_formatting) - 1
                   && memcmp(sink.data, crossed_block_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong>x</ul>"
             "<div><span>y</em>z</span></div>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_block_span_formatting[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong>x</strong></em></li></ul><div><em class=\"x\">"
            "<strong><span>y</span></strong></em><strong>z"
            "</strong></div><strong>w</strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_block_span_formatting) - 1
                   && memcmp(sink.data, crossed_block_span_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong><i>x</ul>"
             "<div><section><blockquote>y</em>z</blockquote>"
             "</section></div>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_nested_blocks[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong><i>x</i></strong></em></li></ul><div><section>"
            "<blockquote><em class=\"x\"><strong><i>y</i></strong>"
            "</em><strong><i>z</i></strong></blockquote></section>"
            "</div><strong><i>w</i></strong></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_nested_blocks) - 1
                   && memcmp(sink.data, crossed_nested_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong title=t>x</ul>"
             "<p>a<article>y<div>z</div>q</article>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_article[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong title=\"t\">x</strong></em></li></ul>"
            "<p><em class=\"x\"><strong title=\"t\">a</strong></em></p>"
            "<article><em class=\"x\"><strong title=\"t\">y<div>z</div>"
            "q</strong></em></article><em class=\"x\"><strong title=\"t\">w"
            "</strong></em></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_article) - 1
                   && memcmp(sink.data, following_article, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em><strong>x</ul><p>a<address>b"
             "</address><figure>c</figure><pre>\nd</pre>"
             "<listing>\ne</listing>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_direct_blocks[] =
            "<html><head><template><ul><li><em><strong>x</strong>"
            "</em></li></ul><p><em><strong>a</strong></em></p>"
            "<address><em><strong>b</strong></em></address>"
            "<figure><em><strong>c</strong></em></figure>"
            "<pre><em><strong>d</strong></em></pre>"
            "<listing><em><strong>e</strong></em></listing>"
            "<em><strong>f</strong></em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_direct_blocks) - 1
                   && memcmp(sink.data, following_direct_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em><strong>x</ul>"
             "<p>a<h2>y</h3>z<h4>q</h4>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_headings[] =
            "<html><head><template><ul><li><em><strong>x</strong>"
            "</em></li></ul><p><em><strong>a</strong></em></p>"
            "<h2><em><strong>y</strong></em></h2><em><strong>z"
            "<h4>q</h4>w</strong></em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_headings) - 1
                   && memcmp(sink.data, following_headings,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong>x</ul>"
             "<ol><li>y</li><li>z</ol>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_list[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong>x</strong></em></li></ul><ol><li><em class=\"x\">"
            "<strong>y</strong></em></li><li><em class=\"x\"><strong>"
            "z</strong></em></li></ol><em class=\"x\"><strong>w"
            "</strong></em></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_list) - 1
                   && memcmp(sink.data, following_list, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em class=x><strong>x</ul>"
             "<ol><li>y<ul><li>z</ul>w</li></ol>q</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char following_nested_list[] =
            "<html><head><template><ul><li><em class=\"x\">"
            "<strong>x</strong></em></li></ul><ol><li><em class=\"x\">"
            "<strong>y<ul><li>z</li></ul>w</strong></em></li></ol>"
            "<em class=\"x\"><strong>q</strong></em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(following_nested_list) - 1
                   && memcmp(sink.data, following_nested_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><em><strong>x</ul>"
             "<ol><li>y<ul><li>z</ol>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char closed_outer_list[] =
            "<html><head><template><ul><li><em><strong>x</strong>"
            "</em></li></ul><ol><li><em><strong>y<ul><li>z"
            "</li></ul></strong></em></li></ol><em><strong>w"
            "</strong></em></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(closed_outer_list) - 1
                   && memcmp(sink.data, closed_outer_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><b><em>x<ul><li>y</li></ul>"
             "</li></ul>z</template>"
             "<template><ol><li><b><em>x<ul><li>y</li></ul>"
             "</em>z</li></ol>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_formatted_item_ends[] =
            "<html><head><template><ul><li><b><em>x<ul><li>y"
            "</li></ul></em></b></li></ul><b><em>z</em></b>"
            "</template><template><ol><li><b><em>x<ul><li>y"
            "</li></ul></em>z</b></li></ol><b>w</b></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_formatted_item_ends) - 1
                   && memcmp(sink.data, nested_formatted_item_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><b><em><label><i><ruby><span>x"
             "<ul><li>y</li></ul></b>z</span></li></ul>w"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_crossed_formatting[] =
            "<html><head><template><ul><li><b><em><label><i>"
            "<ruby><span>x<ul><li>y</li></ul></span></ruby>"
            "</i></label></em></b>"
            "<em><i>z</i></em></li></ul>"
            "<em><i>w</i></em>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_crossed_formatting) - 1
                   && memcmp(sink.data, nested_crossed_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em class=x><span>a</b>b"
             "</template><template><b><ol><li><span><em>"
             "<q>c</b>d</template><template><b><ul><li>"
             "<em class=x><span><strong title=t>e</b>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_trailing_inline[] =
            "<html><head><template><b></b><ul><b></b><li><b>"
            "<em class=\"x\"><span>a</span></em></b>"
            "<em class=\"x\">b</em></li></ul></template>"
            "<template><b></b><ol><b></b><li><b><span>"
            "<em><q>c</q></em></span></b><em>d</em></li>"
            "</ol></template><template><b></b><ul><b></b><li>"
            "<b><em class=\"x\"><span><strong title=\"t\">e"
            "</strong></span></em></b><em class=\"x\">"
            "<strong title=\"t\">f</strong></em></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_trailing_inline) - 1
                   && memcmp(sink.data, crossed_list_trailing_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><p>x</b>y</p></template>"
             "<template><b>a<div>x</b>y</div>z</template>"
             "<template><table><colgroup><strong><section>x"
             "</strong>y</section><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_direct_blocks[] =
            "<html><head><template><b></b><p><b>x</b>y</p>"
            "</template><template><b>a</b><div><b>x</b>y</div>z"
            "</template><template><strong></strong><section>"
            "<strong>x</strong>y</section><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_direct_blocks) - 1
                   && memcmp(sink.data, crossed_direct_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><strong><article>x</strong>y</article>"
             "</template><template><table><colgroup><b><nav>x"
             "</b>y</nav><col></table></template>"
             "<template><em><aside>x</em>y</aside></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_sectioning[] =
            "<html><head><template><strong></strong><article>"
            "<strong>x</strong>y</article></template><template>"
            "<b></b><nav><b>x</b>y</nav><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table>"
            "</template><template><em></em><aside><em>x</em>y"
            "</aside></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_sectioning) - 1
                   && memcmp(sink.data, crossed_sectioning,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><blockquote>x</b>y</blockquote>"
             "</template><template><table><colgroup><strong>"
             "<fieldset>x</strong>y</fieldset><col></table>"
             "</template><template><em><figure>x</em>y</figure>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_special_blocks[] =
            "<html><head><template><b></b><blockquote><b>x</b>y"
            "</blockquote></template><template><strong></strong>"
            "<fieldset><strong>x</strong>y</fieldset><table>"
            "<colgroup></colgroup><colgroup><col></colgroup></table>"
            "</template><template><em></em><figure><em>x</em>y"
            "</figure></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_special_blocks) - 1
                   && memcmp(sink.data, crossed_special_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><dialog>x</b>y</dialog></template>"
             "<template><table><colgroup><strong><search>a"
             "</strong>b</search><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_ordinary_blocks[] =
            "<html><head><template><b><dialog>x</dialog></b>y"
            "</template><template><strong><search>a</search>"
            "</strong>b<table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_ordinary_blocks) - 1
                   && memcmp(sink.data, crossed_ordinary_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><h1>x</b>y</h1></template>"
             "<template><table><colgroup><strong><h6>a"
             "</strong>b</h6><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_headings[] =
            "<html><head><template><b></b><h1><b>x</b>y</h1>"
            "</template><template><strong></strong><h6>"
            "<strong>a</strong>b</h6><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_headings) - 1
                   && memcmp(sink.data, crossed_headings,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><p><span>x</b>y</span></p>"
             "</template><template><table><colgroup>"
             "<strong><div><q>a</strong>b</q></div><col>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_block_inline[] =
            "<html><head><template><b></b><p><b><span>x</span>"
            "</b>y</p></template><template><strong></strong>"
            "<div><strong><q>a</q></strong>b</div><table>"
            "<colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_block_inline) - 1
                   && memcmp(sink.data, crossed_block_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><div><em class=x><span><q>a</b>b"
             "</template><template><b><div><span><em><q>c"
             "</b>d</template><template><b><div><em class=x>"
             "<span><strong title=t>e</b>f</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_formatting_trailing_inline[] =
            "<html><head><template><b></b><div><b><em class=\"x\">"
            "<span><q>a</q></span></em></b><em class=\"x\">b</em>"
            "</div></template><template><b></b><div><b><span>"
            "<em><q>c</q></em></span></b><em>d</em></div></template>"
            "<template><b></b><div><b><em class=\"x\"><span>"
            "<strong title=\"t\">e</strong></span></em></b>"
            "<em class=\"x\"><strong title=\"t\">f</strong></em>"
            "</div></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_formatting_trailing_inline) - 1
                   && memcmp(sink.data, crossed_formatting_trailing_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><p><strong>x</b>y</strong>z</p>"
             "</template><template><table><colgroup><b><div>"
             "<em class=x>a</b>b</em></div><col></table>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_block_formatting[] =
            "<html><head><template><b></b><p><b><strong>x"
            "</strong></b><strong>y</strong>z</p></template>"
            "<template><b></b><div><b><em class=\"x\">a</em></b>"
            "<em class=\"x\">b</em></div><table><colgroup>"
            "</colgroup><colgroup><col></colgroup></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_block_formatting) - 1
                   && memcmp(sink.data, crossed_block_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><div><em class=x><strong title=t>a"
             "</b>b</strong></em></div></template>"
             "<template><table><colgroup><b><section><em>"
             "<strong>x</b>y</strong></em></section><col>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_block_formatting_pair[] =
            "<html><head><template><b></b><div><b><em class=\"x\">"
            "<strong title=\"t\">a</strong></em></b><em class=\"x\">"
            "<strong title=\"t\">b</strong></em></div></template>"
            "<template><b></b><section><b><em><strong>x</strong>"
            "</em></b><em><strong>y</strong></em></section><table>"
            "<colgroup></colgroup><colgroup><col></colgroup></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_block_formatting_pair) - 1
                   && memcmp(sink.data, crossed_block_formatting_pair,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><div><em><strong><code>x</b>"
             "y</code></strong></em></div></template>"
             "<template><table><tbody><tr><b><section>"
             "<em><strong><i>a</b>b</i></strong></em>"
             "</section><td>c</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_block_three_formatting[] =
            "<html><head><template><b></b><div><b><em><strong>"
            "<code>x</code></strong></em></b><em><strong><code>y"
            "</code></strong></em></div></template><template><b>"
            "</b><section><b><em><strong><i>a</i></strong></em>"
            "</b><em><strong><i>b</i></strong></em></section>"
            "<table><tbody><tr><td>c</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length
                   == sizeof(crossed_block_three_formatting) - 1
                   && memcmp(sink.data, crossed_block_three_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><p><span><em><strong>x</b>"
             "y</strong></em></span></p></template>"
             "<template><table><colgroup><b><section><q>"
             "<em>a</b>b</em></q></section><col></table>"
             "</template><template><b><div><span><q><em>"
             "<strong><code>x</b>y</code></strong></em>"
             "</q></span></div></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_block_inline_gap[] =
            "<html><head><template><b></b><p><b><span><em>"
            "<strong>x</strong></em></span></b><em><strong>y"
            "</strong></em></p></template><template><b></b>"
            "<section><b><q><em>a</em></q></b><em>b</em>"
            "</section><table><colgroup></colgroup><colgroup>"
            "<col></colgroup></table></template><template><b></b>"
            "<div><b><span><q><em><strong><code>x</code></strong>"
            "</em></q></span></b><em><strong><code>y</code>"
            "</strong></em></div></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_block_inline_gap) - 1
                   && memcmp(sink.data, crossed_block_inline_gap,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><svg><foreignObject><template><div>x"
             "</div></template><p>y</p></foreignObject><desc>"
             "<template><b>z</b></template></desc></svg>"
             "</template><template><math><mtext><template>"
             "<ul><li>a</ul></template></mtext></math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char integrated_templates[] =
            "<html><head><template><svg><foreignObject><template>"
            "<div>x</div></template><p>y</p></foreignObject>"
            "<desc><template><b>z</b></template></desc></svg>"
            "</template><template><math><mtext><template><ul>"
            "<li>a</li></ul></template></mtext></math></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(integrated_templates) - 1
                   && memcmp(sink.data, integrated_templates,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><frameset>x"
             "</frameset><col></table></template>"
             "<template><table><tbody><tr><frameset>a"
             "</frameset><td>y</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char ignored_template_framesets[] =
            "<html><head><template>x<table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template><template>"
            "a<table><tbody><tr><td>y</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(ignored_template_framesets) - 1
                   && memcmp(sink.data, ignored_template_framesets,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li><span title=t>"
             "<div id=d>x</b>y</div></span></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_span_div[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\"><span title=\"t\">"
            "</span></b><div id=\"d\"><b class=\"x\">x</b>y</div>"
            "</li></ul></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_span_div) - 1
                   && memcmp(sink.data, crossed_span_div,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><label>x</b>y</label>"
             "</li></ul></template>"
             "<template><b><ul><li><div><ruby>x</b>y</ruby>"
             "</div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_inline_containers[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b><label>x</label></b>y</li></ul></template>"
            "<template><b></b><ul><b></b><li><b></b><div>"
            "<b><ruby>x</ruby></b>y</div></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_inline_containers) - 1
                   && memcmp(sink.data, crossed_inline_containers,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><q>x</b>y</q></li>"
             "</ul></template><template><b><ul><li>"
             "<cite><abbr>x</b>y</abbr></cite></li></ul></template>"
             "<template><b><ul><li><div><q>x</b>y</q>"
             "</div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_quote_containers[] =
            "<html><head><template><b></b><ul><b></b><li>"
            "<b><q>x</q></b>y</li></ul></template>"
            "<template><b></b><ul><b></b><li><b>"
            "<cite><abbr>x</abbr></cite></b>y</li></ul></template>"
            "<template><b></b><ul><b></b><li><b></b><div>"
            "<b><q>x</q></b>y</div></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_quote_containers) - 1
                   && memcmp(sink.data, crossed_quote_containers,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li><div id=d><span>"
             "<em id=e>x</b>y</em></span></div></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_div_formatting[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\"></b>"
            "<div id=\"d\"><b class=\"x\"><span><em id=\"e\">x"
            "</em></span></b><em id=\"e\">y</em></div></li></ul>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_div_formatting) - 1
                   && memcmp(sink.data, crossed_div_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><p id=x>before</b>after</p>"
             "</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_paragraph[] =
            "<html><head><template><b></b><ul><b></b><li><b></b>"
            "<p id=\"x\"><b>before</b>after</p></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_paragraph) - 1
                   && memcmp(sink.data, crossed_paragraph,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><section>x</b>y</section>"
             "</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_section[] =
            "<html><head><template><b></b><ul><b></b><li><b></b>"
            "<section><b>x</b>y</section></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_section) - 1
                   && memcmp(sink.data, crossed_section,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li><span title=t>"
             "x</b>y</span></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_span[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\"><span title=\"t\">"
            "x</span></b>y</li></ul></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_span) - 1
                   && memcmp(sink.data, crossed_span, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li><span title=t><span>"
             "<em id=e>x</b>y</em></span></span></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_nested_spans[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\"><span title=\"t\">"
            "<span><em id=\"e\">x</em></span></span></b>"
            "<em id=\"e\">y</em></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_nested_spans) - 1
                   && memcmp(sink.data, crossed_nested_spans,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b class=x><ul><li><em id=e><strong id=s>"
             "x</b>y</strong></em></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_continuation[] =
            "<html><head><template><b class=\"x\"></b><ul>"
            "<b class=\"x\"></b><li><b class=\"x\"><em id=\"e\">"
            "<strong id=\"s\">x</strong></em></b><em id=\"e\">"
            "<strong id=\"s\">y</strong></em></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_continuation) - 1
                   && memcmp(sink.data, nested_continuation,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><tr> a</tr><td>b</td></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_text[] =
            "<html><head><template><tr></tr> a<tr><td>b</td></tr>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_text) - 1
                   && memcmp(sink.data, direct_row_text, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><template><tr><div><span>y<td>x</td>"
             "</tr></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_row_cell[] =
            "<html><head></head><body><template><tr><td>x</td></tr>"
            "<div><span>y</span></div></template></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_row_cell) - 1
                   && memcmp(sink.data, fostered_row_cell, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("</template><div></template></div>"
             "<table></template></table><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char stray_template[] =
            "<html><head></head><body><div></div><table></table>"
            "<p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(stray_template) - 1
                   && memcmp(sink.data, stray_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template>one</span>two<template>inner</br>tail"
             "</template>end</template><link href=/ok>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char ignored_ends[] =
            "<html><head><template>onetwo<template>innertail"
            "</template>end</template><link href=\"/ok\"></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(ignored_ends) - 1
                   && memcmp(sink.data, ignored_ends, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template>outer<template>inner"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(lxa_html_document_nodes(document),
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_eof[] =
            "<html><head><template>outer<template>inner</template>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_eof) - 1
                   && memcmp(sink.data, nested_eof, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p>outer<template><div>inner"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char eof_elements[] =
            "<html><head><template><p>outer<template><div>inner</div>"
            "</template></p></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(eof_elements) - 1
                   && memcmp(sink.data, eof_elements, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template>A&amp;B"), &parse_result, &reason, &document)
        == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(lxa_html_document_nodes(document),
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char eof_template[] =
            "<html><head><template>A&amp;B</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(eof_template) - 1
                   && memcmp(sink.data, eof_template, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><p><a href=/old>x<p>overlap</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(document == NULL && parse_result == LXA_HTML_UNSUPPORTED);
    limits.max_nodes = 4; /* document/html/head/template; fragment needs one */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head></head><template></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 5; /* text inside fragment needs a sixth node */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template>text</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 6; /* comment follows template text in fragment */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template>a<!--c--></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 7; /* col and implicit body fit; whitespace needs one more */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><col>A \tB</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><col>AB</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    lxa_html_document_destroy(document);
    limits.max_nodes = 5; /* a template title requires an element slot */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><title>x</title></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><textarea>x</textarea></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><noscript>x</noscript></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><iframe>x</iframe></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><xmp>x</xmp></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><plaintext>x</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table></table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><select><option>x</select></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><math><mi>x</mi></math></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 7; /* math and mi fit; HTML b needs one more slot */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><math><mi><b>x</b></mi></math></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><svg><g><p>x</p></g></svg></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 6; /* fragment and div fit; nested svg needs a slot */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><div><svg><g>x</g></svg></div></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 5;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><image src=/bad></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>x</p></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = lxa_limits_default().max_nodes;
    limits.max_tree_depth = 5; /* nested fragment exceeds logical depth */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><template></template></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_TREE_DEPTH);
    limits.max_tree_depth = lxa_limits_default().max_tree_depth;
    limits.max_open_elements = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><template></template></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits.max_open_elements = 3; /* template, anchor and table fit */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><a><table><a>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits.max_open_elements = 2; /* template and table fit; fostered object does not */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><table><object>x</object>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits.max_open_elements = 0; /* frameset is the document's open root */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<frameset><frame></frameset>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits.max_open_elements = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<frameset><frameset><frame></frameset></frameset>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>x</p></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits.max_open_elements = 2; /* HTML template and SVG root fit */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><svg><template></template></svg>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits.max_open_elements = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><object></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits.max_open_elements = 2; /* template plus one open paragraph */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>a<p>b</p></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    lxa_html_document_destroy(document);
    limits = lxa_limits_default();
    limits.max_open_elements = 3; /* two templates and tbody fit, select does not */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><template><tbody><select>"
             "</template></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits = lxa_limits_default();
    limits.max_tree_depth = 4;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>x</p></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_TREE_DEPTH);
    limits = lxa_limits_default();
    limits.max_tree_depth = 5; /* body/div/template fragment; p exceeds it */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<body><div><template><p>x</p></template></div>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_TREE_DEPTH);
    limits = lxa_limits_default();
    limits.max_nodes = 7; /* reject next paragraph before closing the first */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>a<p>b</p></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><div></div><tr></tr></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><div><tr></tr></div></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><div></div><table></table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><tr> a</tr></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 8; /* cloned i after direct menu needs ninth node */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><tr><div><td></td></tr></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<body><template><i><menu>Foo</i></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 7;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>x<hr>after</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>x<section>after</section></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>x<h1>heading</h1></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><tr></tr><td></td></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><tr><div></div></tr></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><thead></thead><tr></tr></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><p>x<pre>\ny</pre></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 8; /* first list text fits; next item is rejected */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><ul><li>x<li>y</ul></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><select><option>x<option>y"
             "</select></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><select><option>x<input src=/real>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><select><option>x<textarea>y"
             "</textarea></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 8; /* document/html/head/template/fragment/table + 3 */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><td>x</table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_open_elements = 4; /* template, table, tbody, tr */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><td>x</table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits = lxa_limits_default();
    limits.max_nodes = 6; /* caption follows document/html/head/template/fragment/table */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><caption></caption></table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 8; /* do not close caption before implied tbody/tr fit */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><caption>x<tr></table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 9; /* nested p fits; implied tbody/tr does not */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><caption><p>x<tr>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 11; /* open cell/p/text fit, next tr does not */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><td><p>x<tr>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><td><p>x<caption>"
             "</caption></table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 7; /* col needs implicit colgroup and its own slot */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><col></table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_open_elements = 2; /* template and table, not colgroup */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><col></table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits = lxa_limits_default();
    limits.max_nodes = 6; /* fostered text needs a seventh slot */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table>outside</table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><input type=HIDDEN name=x>"
             "<col><input type=hidden name=y><tr>"
             "<input type=hidden name=z><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char hidden_table_inputs[] =
            "<html><head><template><table><input type=\"HIDDEN\" name=\"x\">"
            "<colgroup><col></colgroup><input type=\"hidden\" name=\"y\">"
            "<tbody><tr><input type=\"hidden\" name=\"z\">"
            "<td>cell</td></tr></tbody></table></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(hidden_table_inputs) - 1
                   && memcmp(sink.data, hidden_table_inputs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><input type=text name=a>"
             "<col><input name=b><tr><input type=radio name=c>"
             "<td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_inputs[] =
            "<html><head><template><input type=\"text\" name=\"a\">"
            "<input name=\"b\"><input type=\"radio\" name=\"c\">"
            "<table><colgroup><col></colgroup><tbody><tr>"
            "<td>cell</td></tr></tbody></table></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_inputs) - 1
                   && memcmp(sink.data, fostered_table_inputs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><img src=/a><col>"
             "<image src=/alias><br>"
             "<tr><hr><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_voids[] =
            "<html><head><template><img src=\"/a\">"
            "<img src=\"/alias\"><br><hr>"
            "<table><colgroup><col></colgroup><tbody><tr>"
            "<td>cell</td></tr></tbody></table></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_voids) - 1
                   && memcmp(sink.data, fostered_table_voids,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><span class=a>one</span><col>"
             "<span class=b>two</span><tr><span>three</span>"
             "<td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_spans[] =
            "<html><head><template><span class=\"a\">one</span>"
            "<span class=\"b\">two</span><span>three</span>"
            "<table><colgroup><col></colgroup><tbody><tr>"
            "<td>cell</td></tr></tbody></table></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_spans) - 1
                   && memcmp(sink.data, fostered_table_spans,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><col><div id=a>one</div>"
             "<tr><div id=b>two</div><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_divs[] =
            "<html><head><template><div id=\"a\">one</div>"
            "<div id=\"b\">two</div><table><colgroup><col></colgroup>"
            "<tbody><tr><td>cell</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_divs) - 1
                   && memcmp(sink.data, fostered_table_divs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><col><section>one</section>"
             "<tr><article>two</article><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_sections[] =
            "<html><head><template><section>one</section>"
            "<article>two</article><table><colgroup><col></colgroup>"
            "<tbody><tr><td>cell</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_sections) - 1
                   && memcmp(sink.data, fostered_table_sections,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><p>one</p><col>"
             "<p>two</p><tr><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_paragraphs[] =
            "<html><head><template><p>one</p><p>two</p>"
            "<table><colgroup><col></colgroup><tbody><tr>"
            "<td>cell</td></tr></tbody></table></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_paragraphs) - 1
                   && memcmp(sink.data, fostered_table_paragraphs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><ul><li>one</ul><col>"
             "<ol><li>two</ol><tr><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_lists[] =
            "<html><head><template><ul><li>one</li></ul>"
            "<ol><li>two</li></ol><table><colgroup><col></colgroup>"
            "<tbody><tr><td>cell</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_lists) - 1
                   && memcmp(sink.data, fostered_table_lists,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><form id=a></form><col>"
             "</form><form id=b><tr><form id=c><td>cell"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_table_forms[] =
            "<html><head><template><table>"
            "<colgroup><col></colgroup>"
            "<tbody><tr><td>cell</td></tr>"
            "</tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_table_forms) - 1
                   && memcmp(sink.data, template_table_forms,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><form id=ignored><col>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_colgroup_form[] =
            "<html><head><template><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_colgroup_form) - 1
                   && memcmp(sink.data, template_colgroup_form,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><form id=outer><template><table><form id=inner>"
             "</table></template></form>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_template_form[] =
            "<html><head></head><body><form id=\"outer\"><template>"
            "<table></table></template>"
            "</form></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_template_form) - 1
                   && memcmp(sink.data, nested_template_form,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table id=first><col>"
             "<table id=second><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_adjacent_tables[] =
            "<html><head><template><table id=\"first\">"
            "<colgroup><col></colgroup></table><table id=\"second\">"
            "<tbody><tr><td>cell</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_adjacent_tables) - 1
                   && memcmp(sink.data, template_adjacent_tables,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><tr><td>one</tr>"
             "<table><tr><td>two</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_adjacent_rows[] =
            "<html><head><template><table><tbody><tr><td>one</td>"
            "</tr></tbody></table><table><tbody><tr><td>two</td>"
            "</tr></tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_adjacent_rows) - 1
                   && memcmp(sink.data, template_adjacent_rows,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><select id=a><option>one</select>"
             "<col><select id=b><option>two</select>"
             "<tr><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_selects[] =
            "<html><head><template><select id=\"a\"><option>one</option>"
            "</select><select id=\"b\"><option>two</option></select>"
            "<table><colgroup><col></colgroup><tbody><tr>"
            "<td>cell</td></tr></tbody></table></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_selects) - 1
                   && memcmp(sink.data, fostered_table_selects,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><caption><select><option>x<table>y"
             "</template><template><caption><select><optgroup>"
             "<option>a<table>b</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char caption_select_table[] =
            "<html><head><template><caption><select><option>x</option>"
            "</select>y<table></table></caption></template>"
            "<template><caption><select><optgroup><option>a</option>"
            "</optgroup></select>b<table></table></caption></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(caption_select_table) - 1
                   && memcmp(sink.data, caption_select_table,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><select><option>x<td>y</template>"
             "<template><tbody><select><option>x<tr><td>y</template>"
             "<template><td><select><option>x<td>y</template>"
             "<template><table><tr><select><option>x<col>y"
             "</table></template>"
             "<template><tr><select>x<table>y</template>"
             "<template><tr><table><td>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_table_select[] =
            "<html><head><template><tr><td>y</td></tr>"
            "<select><option>x</option></select></template>"
            "<template><tbody><tr><td>y</td></tr></tbody>"
            "<select><option>x</option></select></template>"
            "<template><td><select><option>x</option></select></td>"
            "<td>y</td></template>"
            "<template><select><option>xy</option></select>"
            "<table><tbody><tr></tr></tbody></table></template>"
            "<template><tr></tr><select>x</select>y</template>"
            "<template><tr><td>z</td></tr></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_table_select) - 1
                   && memcmp(sink.data, direct_table_select,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><select><option>A"
             "<tr><td>B</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_select_row[] =
            "<html><head><template><select><option>A</option></select>"
            "<table><tbody><tr><td>B</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_select_row) - 1
                   && memcmp(sink.data, table_select_row, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><select><option>A"
             "</table><table><td>B</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_select_close[] =
            "<html><head><template><select><option>A</option></select>"
            "<table></table><table><tbody><tr><td>B</td></tr></tbody>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_select_close) - 1
                   && memcmp(sink.data, table_select_close, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><select><table>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_select_table[] =
            "<html><head><template><select></select><table></table>"
            "<table></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_select_table) - 1
                   && memcmp(sink.data, table_select_table, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><select><option>a<input type=text>y"
             "</select><tr><td>x</td></tr></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_input_closed[] =
            "<html><head><template><select><option>a</option></select>"
            "<input type=\"text\">y<table><tbody><tr><td>x</td></tr>"
            "</tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(select_input_closed) - 1
                   && memcmp(sink.data, select_input_closed,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><select><option>x<keygen>y"
             "<tr><td>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_keygen_template[] =
            "<html><head><template><select><option>x</option></select>"
            "<keygen>y<table><tbody><tr><td>z</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(select_keygen_template) - 1
                   && memcmp(sink.data, select_keygen_template,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><div>x<table><td>y</template>"
             "<template><table><tr><td><div>x<table><td>z</template>"
             "<template><tbody><div>x<table><td>y</template>"
             "<template><tr><div>x<table>x</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_start[] =
            "<html><head><template><div>x</div><table><tbody></tbody></table>"
            "<table><tbody><tr><td>y</td></tr></tbody></table></template>"
            "<template><table><tbody><tr><td><div>x<table><tbody><tr>"
            "<td>z</td></tr></tbody></table></div></td></tr></tbody></table>"
            "</template><template><tbody><tr><td>y</td></tr></tbody>"
            "<div>x</div></template><template><tr></tr><div>xx</div>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_start) - 1
                   && memcmp(sink.data, fostered_table_start,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g><math><mi>x</mi></math></g></svg>"
             "<math><mrow><svg><g>z</g></svg></mrow></math>"
             "<template><svg><foreignObject><svg><g>y</g></svg>"
             "</foreignObject></svg></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char cross_foreign_roots[] =
            "<html><head></head><body><svg><g><math><mi>x</mi></math>"
            "</g></svg><math><mrow><svg><g>z</g></svg></mrow></math>"
            "<template><svg><foreignObject><svg><g>y</g>"
            "</svg></foreignObject></svg></template></body></html>";
        TEST_CHECK(sink.length == sizeof(cross_foreign_roots) - 1
                   && memcmp(sink.data, cross_foreign_roots,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><math><mi>x</mi></math>"
             "<mglyph>y</mglyph><malignmark>z</malignmark></mtext>"
             "</math></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_math_text[] =
            "<html><head><template><math><mtext><math><mi>x</mi></math>"
            "<mglyph>y</mglyph><malignmark>z</malignmark></mtext>"
            "</math></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_math_text) - 1
                   && memcmp(sink.data, nested_math_text,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><math><mtext><object>x</object></mtext></math>"
             "<svg><foreignObject><object>y</object></foreignObject></svg>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char integration_objects[] =
            "<html><head><template><math><mtext><object>x</object>"
            "</mtext></math><svg><foreignObject><object>y</object>"
            "</foreignObject></svg></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(integration_objects) - 1
                   && memcmp(sink.data, integration_objects,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><colgroup><div>x<table><td>y</template>"
             "<template><caption></caption><object>x<table><td>z"
             "</template><template><caption><div>x<table><td>w"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_table_start[] =
            "<html><head><template><colgroup></colgroup><div>x</div>"
            "<tbody><tr><td>y</td></tr></tbody></template>"
            "<template><caption></caption><object>x</object>"
            "<tbody><tr><td>z</td></tr></tbody></template>"
            "<template><caption><div>x<table><tbody><tr><td>w</td></tr>"
            "</tbody></table></div></caption></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_table_start) - 1
                   && memcmp(sink.data, direct_table_start,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><colgroup><div>x<td>y</template>"
             "<template><caption><object>x<tr><td>z</template>"
             "<template><tbody><div>x<td>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_part_after_fostered[] =
            "<html><head><template><colgroup></colgroup><div>x</div>"
            "<tbody><tr><td>y</td></tr></tbody></template>"
            "<template><caption><object>x</object></caption>"
            "<tbody><tr><td>z</td></tr></tbody></template>"
            "<template><tbody><tr><td>w</td></tr></tbody><div>x</div>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_part_after_fostered) - 1
                   && memcmp(sink.data, direct_part_after_fostered,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><colgroup><div>x<col>y</template>"
             "<template><tbody><object>x<col>y</template>"
             "<template><table></table><div>x<col>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char column_after_fostered[] =
            "<html><head><template><colgroup></colgroup><div>x</div>"
            "<colgroup><col></colgroup>y</template>"
            "<template><tbody></tbody><object>x</object>"
            "<colgroup><col></colgroup>y</template>"
            "<template><table></table><div>xy</div></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(column_after_fostered) - 1
                   && memcmp(sink.data, column_after_fostered,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><colgroup><noframes>x</noframes><col>"
             "<tr><td>y</template><template><colgroup><object>x</object>"
             "<col><tr><td>z</template>"
             "<template><colgroup><body><col></template>"
             "<template><table><colgroup><frame><col></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_group_content[] =
            "<html><head><template><colgroup></colgroup>"
            "<noframes>x</noframes><colgroup><col></colgroup>"
            "<tbody><tr><td>y</td></tr></tbody></template>"
            "<template><colgroup></colgroup><object>x</object>"
            "<colgroup><col></colgroup><tbody><tr><td>z</td></tr>"
            "</tbody></template><template><colgroup></colgroup>"
            "<colgroup><col></colgroup></template>"
            "<template><table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_group_content) - 1
                   && memcmp(sink.data, direct_group_content,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><select><option>A</td>B"
             "</select><tr><td>C</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char unmatched_select_cell[] =
            "<html><head><template><select><option>AB</option></select>"
            "<table><tbody><tr><td>C</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(unmatched_select_cell) - 1
                   && memcmp(sink.data, unmatched_select_cell,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><td><select><option>A</td>"
             "<td>B</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char closed_select_cell[] =
            "<html><head><template><table><tbody><tr><td><select>"
            "<option>A</option></select></td><td>B</td></tr></tbody>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(closed_select_cell) - 1
                   && memcmp(sink.data, closed_select_cell,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><select><option>A"
             "<input type=text name=x><select><option>B"
             "<input type=HIDDEN name=y><tr><td>C"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_select_inputs[] =
            "<html><head><template><select><option>A</option></select>"
            "<input type=\"text\" name=\"x\">"
            "<select><option>B</option></select><table>"
            "<input type=\"HIDDEN\" name=\"y\"><tbody><tr><td>C</td>"
            "</tr></tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_select_inputs) - 1
                   && memcmp(sink.data, table_select_inputs,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><col><textarea>\nA&amp;B"
             "</textarea><select><option>X<textarea>\nC&amp;D"
             "</textarea><tr><td>E</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_textareas[] =
            "<html><head><template><textarea>A&amp;B</textarea>"
            "<select><option>X</option></select>"
            "<textarea>C&amp;D</textarea><table><colgroup><col>"
            "</colgroup><tbody><tr><td>E</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_textareas) - 1
                   && memcmp(sink.data, fostered_table_textareas,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><xmp><b>raw</b></xmp><col>"
             "<iframe src=/f><p>frame</p></iframe><tr><td>cell"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_rawtext[] =
            "<html><head><template><xmp><b>raw</b></xmp>"
            "<iframe src=\"/f\"><p>frame</p></iframe>"
            "<table><colgroup><col></colgroup><tbody><tr>"
            "<td>cell</td></tr></tbody></table></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_rawtext) - 1
                   && memcmp(sink.data, fostered_table_rawtext,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><tr><noembed><img src=/raw>"
             "</noembed><td>cell</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_noembed[] =
            "<html><head><template><noembed><img src=/raw></noembed>"
            "<table><tbody><tr><td>cell</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_noembed) - 1
                   && memcmp(sink.data, fostered_table_noembed,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><plaintext>literal"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_table_plaintext[] =
            "<html><head><template><plaintext>literal</table>"
            "</template></plaintext><table></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_table_plaintext) - 1
                   && memcmp(sink.data, fostered_table_plaintext,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table></br><col></br>"
             "<tr></br><td></br></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_end_breaks[] =
            "<html><head><template><br><br><br><table>"
            "<colgroup><col></colgroup><tbody><tr><td><br></td>"
            "</tr></tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_end_breaks) - 1
                   && memcmp(sink.data, table_end_breaks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><li>one<li>two</li><col>"
             "<li>three</li><tr><li>four</li><td>cell"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_direct_items[] =
            "<html><head><template><li>one</li><li>two</li>"
            "<li>three</li><li>four</li><table>"
            "<colgroup><col></colgroup><tbody><tr><td>cell</td>"
            "</tr></tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_direct_items) - 1
                   && memcmp(sink.data, table_direct_items,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_open_elements = 3; /* template, table, one direct item */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><li>one<li>two</li>"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
               == LXA_OK);
    lxa_html_document_destroy(document);
    limits = lxa_limits_default();
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><li><b>one<li>two"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(document == NULL && parse_result == LXA_HTML_UNSUPPORTED);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><li>one<tr><td>A</td>"
             "<span>two<tr><td>B</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char closed_fostered_items[] =
            "<html><head><template><li>one</li><span>two</span>"
            "<table><tbody><tr><td>A</td></tr><tr><td>B</td></tr>"
            "</tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(closed_fostered_items) - 1
                   && memcmp(sink.data, closed_fostered_items,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><blockquote><span>x<td>y</td></template>"
             "<template><tr><dl><dt>a<td>z</td></template>"
             "<template><tr><legend>x<td>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_fostered_blocks[] =
            "<html><head><template><tr><td>y</td></tr>"
            "<blockquote><span>x</span></blockquote></template>"
            "<template><tr><td>z</td></tr><dl><dt>a</dt></dl>"
            "</template><template><tr><td>y</td></tr><legend>x</legend>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_fostered_blocks) - 1
                   && memcmp(sink.data, direct_row_fostered_blocks,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><address>x<td>y</td></template>"
             "<template><tr><figure>x<tr><td>y</td></template>"
             "<template><tr><article>x<td>y</template>"
             "<template><tr><h1>x<tr><td>y</template>"
             "<template><tr><p>x<td>y</template>"
             "<template><tr><output>x<td>y</template>"
             "<template><tr><ruby>x<tr><td>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_fostered_containers[] =
            "<html><head><template><tr><td>y</td></tr>"
            "<address>x</address></template><template><tr></tr>"
            "<figure>x</figure><tr><td>y</td></tr></template>"
            "<template><tr><td>y</td></tr><article>x</article></template>"
            "<template><tr></tr><h1>x</h1><tr><td>y</td></tr></template>"
            "<template><tr><td>y</td></tr><p>x</p></template>"
            "<template><tr><td>y</td></tr><output>x</output></template>"
            "<template><tr></tr><ruby>x</ruby><tr><td>y</td></tr>"
            "</template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_fostered_containers) - 1
                   && memcmp(sink.data, direct_row_fostered_containers,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><colgroup><col><td>x</template>"
             "<template><tr><col><td>y</template>"
             "<template><tr></tr><col><td>z</template>"
             "<template><tr><caption><td>w</template>"
             "<template><tr><tbody><td>q</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_ignored_parts[] =
            "<html><head><template><tr></tr><tr><td>x</td></tr>"
            "</template><template><tr></tr><tr><td>y</td></tr>"
            "</template><template><tr></tr><tr><td>z</td></tr>"
            "</template><template><tr></tr><tr><td>w</td></tr>"
            "</template><template><tr></tr><tr><td>q</td></tr>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_ignored_parts) - 1
                   && memcmp(sink.data, direct_row_ignored_parts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><map>x<td>y</template>"
             "<template><tr><picture><img src=a><td>y</template>"
             "<template><tr><video>x<tr><td>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_fostered_media[] =
            "<html><head><template><tr><td>y</td></tr><map>x</map>"
            "</template><template><tr><td>y</td></tr>"
            "<picture><img src=\"a\"></picture></template>"
            "<template><tr></tr><video>x</video><tr><td>y</td></tr>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_fostered_media) - 1
                   && memcmp(sink.data, direct_row_fostered_media,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><object>x<td>y</template>"
             "<template><tr><noscript>x<tr><td>y</template>"
             "<template><tr><option>x<td>y</template>"
             "<template><tr><optgroup><option>x<tr><td>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_fostered_special[] =
            "<html><head><template><tr><td>y</td></tr><object>x"
            "</object></template><template><tr></tr><noscript>x"
            "</noscript><tr><td>y</td></tr></template><template>"
            "<tr><td>y</td></tr><option>x</option></template>"
            "<template><tr></tr><optgroup><option>x</option></optgroup>"
            "<tr><td>y</td></tr></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_fostered_special) - 1
                   && memcmp(sink.data, direct_row_fostered_special,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><ins><slot>x<td>y</template>"
             "<template><tr><mark>x<tr><td>y</template>"
             "<template><tr><data><dfn>x<td>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_fostered_inline[] =
            "<html><head><template><tr><td>y</td></tr><ins><slot>x"
            "</slot></ins></template><template><tr></tr><mark>x</mark>"
            "<tr><td>y</td></tr></template><template><tr><td>y</td></tr>"
            "<data><dfn>x</dfn></data></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_fostered_inline) - 1
                   && memcmp(sink.data, direct_row_fostered_inline,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><b>x<td>y</template>"
             "<template><tr><em><b>x<tr><td>y</template>"
             "<template><tr><a href=/a>x<td>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_fostered_formatting[] =
            "<html><head><template><tr><td>y</td></tr><b>x</b>"
            "</template><template><tr></tr><em><b>x</b></em>"
            "<tr><td>y</td></tr></template><template><tr><td>y</td>"
            "</tr><a href=\"/a\">x</a></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_fostered_formatting) - 1
                   && memcmp(sink.data, direct_row_fostered_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><article>x<tr><td>y</template>"
             "<template><tbody><div><span>x<td>y</template>"
             "<template><tbody><tr><tr><td>y</template>"
             "<template><tbody><tr><td>x<td>y</template>"
             "<template><tbody><tr><th>x<tr><td>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_section_fostered_rows[] =
            "<html><head><template><tbody><tr><td>y</td></tr></tbody>"
            "<article>x</article></template><template><tbody>"
            "<tr><td>y</td></tr></tbody><div><span>x</span></div>"
            "</template><template><tbody><tr></tr><tr><td>y</td></tr>"
            "</tbody></template>"
            "<template><tbody><tr><td>x</td><td>y</td></tr></tbody>"
            "</template><template><tbody><tr><th>x</th></tr>"
            "<tr><td>y</td></tr></tbody></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_section_fostered_rows) - 1
                   && memcmp(sink.data, direct_section_fostered_rows,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><tr><thead><tr><td>y</template>"
             "<template><tbody><tr><td>x<thead><tr><td>y</template>"
             "<template><tbody><tr><td>x<caption>y</template>"
             "<template><tbody><tr><td>x<colgroup><col>"
             "<tr><td>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_section_parts[] =
            "<html><head><template><tbody><tr></tr></tbody>"
            "<thead><tr><td>y</td></tr></thead></template>"
            "<template><tbody><tr><td>x</td></tr></tbody>"
            "<thead><tr><td>y</td></tr></thead></template>"
            "<template><tbody><tr><td>x</td></tr></tbody>"
            "<caption>y</caption></template>"
            "<template><tbody><tr><td>x</td></tr></tbody>"
            "<colgroup><col></colgroup><tbody><tr><td>y</td>"
            "</tr></tbody></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_section_parts) - 1
                   && memcmp(sink.data, direct_section_parts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><tr><td><div><span>x<tr><td>y"
             "</template>"
             "<template><tbody><tr><td><b>x<td>y</template>"
             "<template><tbody><tr><td><select><option>x<td>y"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_section_cell_descendants[] =
            "<html><head><template><tbody><tr><td><div><span>x"
            "</span></div></td></tr><tr><td>y</td></tr></tbody>"
            "</template><template><tbody><tr><td><b>x</b></td>"
            "<td>y</td></tr></tbody></template><template><tbody>"
            "<tr><td><select><option>x</option></select></td>"
            "<td>y</td></tr></tbody></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_section_cell_descendants) - 1
                   && memcmp(sink.data, direct_section_cell_descendants,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><tr><td><div><span>x"
             "<thead><tr><td>y</template>"
             "<template><tbody><tr><td><b>x<caption>y</template>"
             "<template><tbody><tr><td><span>x<colgroup><col>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_section_cell_parts[] =
            "<html><head><template><tbody><tr><td><div><span>x"
            "</span></div></td></tr></tbody><thead><tr><td>y</td>"
            "</tr></thead></template><template><tbody><tr><td><b>x"
            "</b></td></tr></tbody><caption>y</caption></template>"
            "<template><tbody><tr><td><span>x</span></td></tr></tbody>"
            "<colgroup><col></colgroup></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_section_cell_parts) - 1
                   && memcmp(sink.data, direct_section_cell_parts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><article>x<thead><tr><td>y</template>"
             "<template><tbody><div><span>x<colgroup><col></template>"
             "<template><tbody><article>x<caption>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char section_fostered_parts[] =
            "<html><head><template><tbody></tbody><article>x</article>"
            "<thead><tr><td>y</td></tr></thead></template>"
            "<template><tbody></tbody><div><span>x</span></div>"
            "<colgroup><col></colgroup></template>"
            "<template><tbody></tbody><article>x</article>"
            "<caption>y</caption></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(section_fostered_parts) - 1
                   && memcmp(sink.data, section_fostered_parts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><tr><div>x<thead><tr><td>y</template>"
             "<template><tbody><tr><article>x<td>y<tr><td>z</template>"
             "<template><tbody><tr><span>x<colgroup><col></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char section_row_fostered[] =
            "<html><head><template><tbody><tr></tr></tbody>"
            "<div>x</div><thead><tr><td>y</td></tr></thead></template>"
            "<template><tbody><tr><td>y</td></tr><tr><td>z</td>"
            "</tr></tbody><article>x</article></template>"
            "<template><tbody><tr></tr></tbody><span>x</span>"
            "<colgroup><col></colgroup></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(section_row_fostered) - 1
                   && memcmp(sink.data, section_row_fostered,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><tr><td>a</td><tr><blockquote>"
             "<span>x<tr><td>b</td></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_transition[] =
            "<html><head><template><tr></tr><tr><td>a</td></tr>"
            "<tr></tr><blockquote><span>x</span></blockquote>"
            "<tr><td>b</td></tr></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_transition) - 1
                   && memcmp(sink.data, direct_row_transition,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><blockquote>x</blockquote>"
             "<tr><td>y</td></tr></tbody></table>"
             "<table><dl><dt>a</dt><dd>b</dd></dl>"
             "<tr><td>z</td></tr></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_definition[] =
            "<html><head><template><blockquote>x</blockquote>"
            "<table><tbody><tr><td>y</td></tr></tbody></table>"
            "<dl><dt>a</dt><dd>b</dd></dl>"
            "<table><tbody><tr><td>z</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_definition) - 1
                   && memcmp(sink.data, fostered_definition,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><address>x</address><tr><td>y"
             "</td></tr></table><table><tbody><figure>z</figure>"
             "<tr><td>w</td></tr></tbody></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_address_figure[] =
            "<html><head><template><address>x</address><table>"
            "<tbody><tr><td>y</td></tr></tbody></table><figure>z</figure>"
            "<table><tbody><tr><td>w</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_address_figure) - 1
                   && memcmp(sink.data, fostered_address_figure,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><div>before<td>cell"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char implied_after_foster[] =
            "<html><head><template><div>before</div><table>"
            "<tbody><tr><td>cell</td></tr></tbody></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(implied_after_foster) - 1
                   && memcmp(sink.data, implied_after_foster,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table></caption><tr><td>x</table></template>"
             "<template><table><colgroup></caption><col>"
             "</table></template>"
             "<template><table><tbody><tr></caption><td>y"
             "</table></template>"
             "<template><table></h1><col></table></template>"
             "<template><table><colgroup></h2><col></table></template>"
             "<template><table><colgroup></dialog><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char unmatched_table_ends[] =
            "<html><head><template><table><tbody><tr><td>x</td></tr>"
            "</tbody></table></template><template><table>"
            "<colgroup></colgroup><colgroup><col></colgroup></table>"
            "</template><template><table><tbody><tr><td>y</td></tr>"
            "</tbody></table></template>"
            "<template><table><colgroup><col></colgroup></table>"
            "</template><template><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "<template><table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(unmatched_table_ends) - 1
                   && memcmp(sink.data, unmatched_table_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table></option><tr><td>x"
             "</table></template>"
             "<template><table><colgroup></optgroup><col>"
             "</table></template>"
             "<template><table><tbody><tr></option><td>y"
             "</table></template>"
             "<template><div><table></div><tr><td>z"
             "</table></template>"
             "<template><table><colgroup></button><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char unmatched_table_containers[] =
            "<html><head><template><table><tbody><tr><td>x</td>"
            "</tr></tbody></table></template><template><table>"
            "<colgroup></colgroup><colgroup><col></colgroup></table>"
            "</template><template><table><tbody><tr><td>y</td></tr>"
            "</tbody></table></template><template><div><table><tbody>"
            "<tr><td>z</td></tr></tbody></table></div></template>"
            "<template><table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(unmatched_table_containers) - 1
                   && memcmp(sink.data, unmatched_table_containers,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table></ruby><col></table></template>"
             "<template><table><colgroup></rt><col></table></template>"
             "<template><table><tbody><tr></rp><td>x"
             "</table></template>"
             "<template><ruby><table></ruby><tr><td>y"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char unmatched_table_ruby[] =
            "<html><head><template><table><colgroup><col></colgroup>"
            "</table></template><template><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "<template><table><tbody><tr><td>x</td></tr></tbody>"
            "</table></template><template><ruby><table><tbody><tr>"
            "<td>y</td></tr></tbody></table></ruby></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(unmatched_table_ruby) - 1
                   && memcmp(sink.data, unmatched_table_ruby,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><colgroup><col><tr><td>x</template>"
             "<template><colgroup><col><td>y</template>"
             "<template><tbody><colgroup><thead><tr><td>z</template>"
             "<template><colgroup><caption>x</template>"
             "<template><tbody></tbody><col><tr><td>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_column_group_transitions[] =
            "<html><head><template><tbody></tbody><colgroup><col>"
            "</colgroup><tbody><tr><td>x</td></tr></tbody></template>"
            "<template><colgroup><col></colgroup><tbody><tr><td>y</td>"
            "</tr></tbody></template><template><tbody></tbody>"
            "<colgroup></colgroup><thead><tr><td>z</td></tr></thead>"
            "</template><template><colgroup></colgroup>"
            "<caption>x</caption></template>"
            "<template><tbody></tbody><colgroup><col></colgroup>"
            "<tbody><tr><td>w</td></tr></tbody></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_column_group_transitions) - 1
                   && memcmp(sink.data, direct_column_group_transitions,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><tr><td>A</td>"
             "<colgroup><col><tr><td>B</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_late_columns[] =
            "<html><head><template><table><tbody><tr><td>A</td></tr>"
            "</tbody><colgroup><col></colgroup><tbody><tr><td>B</td>"
            "</tr></tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_late_columns) - 1
                   && memcmp(sink.data, table_late_columns,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup></col><col></table></template>"
             "<template><table><colgroup></thead><col></table></template>"
             "<template><table><colgroup></select><col></table></template>"
             "<template><table><tbody><tr></td><td>x"
             "</table></template>"
             "<template><table></body><tr><td>y</table></template>"
             "<template><table><colgroup></html><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char unmatched_table_parts[] =
            "<html><head><template><table><colgroup><col></colgroup>"
            "</table></template><template><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "<template><table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template><template><table><tbody>"
            "<tr><td>x</td></tr></tbody></table></template>"
            "<template><table><tbody><tr><td>y</td></tr></tbody>"
            "</table></template><template><table><colgroup></colgroup>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(unmatched_table_parts) - 1
                   && memcmp(sink.data, unmatched_table_parts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table></p><col></table></template>"
             "<template><table><colgroup></p><col></table></template>"
             "<template><table><tbody><tr></p><td>x</table></template>"
             "<template><tbody></p><tr><td>x</template>"
             "<template><table><colgroup></li><col></table></template>"
             "<template><table><tr></dt><td>y</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_missing_ends[] =
            "<html><head><template><p></p><table><colgroup><col>"
            "</colgroup></table></template><template><p></p>"
            "<table><colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template><template><p></p><table><tbody><tr>"
            "<td>x</td></tr></tbody></table></template><template>"
            "<tbody><tr><td>x</td></tr></tbody><p></p></template>"
            "<template><table><colgroup></colgroup><colgroup><col>"
            "</colgroup></table></template><template><table><tbody><tr>"
            "<td>y</td></tr></tbody></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_missing_ends) - 1
                   && memcmp(sink.data, table_missing_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><span>X<colgroup><col>"
             "<caption>Y<colgroup><col></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_fostered_columns[] =
            "<html><head><template><span>X</span><table>"
            "<colgroup><col></colgroup><caption>Y</caption>"
            "<colgroup><col></colgroup></table></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_fostered_columns) - 1
                   && memcmp(sink.data, table_fostered_columns,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><tr><td><div>x<colgroup><col>"
             "</template><template><table><caption><div>y<col>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char column_after_descendants[] =
            "<html><head><template><table><tbody><tr><td><div>x"
            "</div></td></tr></tbody><colgroup><col></colgroup>"
            "</table></template><template><table><caption><div>y"
            "</div></caption><colgroup><col></colgroup></table>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(column_after_descendants) - 1
                   && memcmp(sink.data, column_after_descendants,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><colgroup><math><mi>x<col></template>"
             "<template><table><tbody><math><mi>y<col></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_column[] =
            "<html><head><template><math><mi>x</mi></math>"
            "<table><colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template><template><math><mi>y</mi></math>"
            "<table><tbody></tbody><colgroup><col></colgroup>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(math_column) - 1
                   && memcmp(sink.data, math_column, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><tr><td><math><mi>x<col>"
             "</template><template><table><caption><math><mi>y<col>"
             "</template><template><tbody><tr><th><svg><g>z"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_table_cells[] =
            "<html><head><template><table><tbody><tr><td><math>"
            "<mi>x</mi></math></td></tr></tbody><colgroup><col>"
            "</colgroup></table></template><template><table><caption>"
            "<math><mi>y</mi></math></caption><colgroup><col>"
            "</colgroup></table></template><template><tbody><tr><th>"
            "<svg><g>z</g></svg></th></tr></tbody></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_table_cells) - 1
                   && memcmp(sink.data, foreign_table_cells,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><tr><td><math><mi>x<tr><td>y"
             "</template><template><table><caption><math><mi>z"
             "<thead><tr><td>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char math_table_parts[] =
            "<html><head><template><table><tbody><tr><td><math>"
            "<mi>x</mi></math></td></tr><tr><td>y</td></tr></tbody>"
            "</table></template><template><table><caption><math>"
            "<mi>z</mi></math></caption><thead><tr><td>w</td></tr>"
            "</thead></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(math_table_parts) - 1
                   && memcmp(sink.data, math_table_parts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><math><mi>x<col></template>"
             "<template><tbody><tr><math><mi>y<td>z</template>"
             "<template><tbody><math><mi>w<thead><tr><td>q"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_section_math[] =
            "<html><head><template><tbody></tbody><math><mi>x"
            "</mi></math><colgroup><col></colgroup></template>"
            "<template><tbody><tr><td>z</td></tr></tbody><math>"
            "<mi>y</mi></math></template><template><tbody></tbody>"
            "<math><mi>w</mi></math><thead><tr><td>q</td></tr>"
            "</thead></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_section_math) - 1
                   && memcmp(sink.data, direct_section_math,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><math><mi>x<td>y</template>"
             "<template><tr><math><mi>z<tr><td>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_math[] =
            "<html><head><template><tr><td>y</td></tr><math>"
            "<mi>x</mi></math></template><template><tr></tr>"
            "<math><mi>z</mi></math><tr><td>w</td></tr></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_math) - 1
                   && memcmp(sink.data, direct_row_math,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><th>x<tr><td>y</template>"
             "<template><tr><td>a<td>b</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_cells[] =
            "<html><head><template><tr><th>x</th></tr>"
            "<tr><td>y</td></tr></template><template><tr><td>a</td>"
            "<td>b</td></tr></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_cells) - 1
                   && memcmp(sink.data, direct_row_cells,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><td><div><span>x<tr><td>y</template>"
             "<template><tr><th><b>z<td>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_cell_descendants[] =
            "<html><head><template><tr><td><div><span>x</span>"
            "</div></td></tr><tr><td>y</td></tr></template>"
            "<template><tr><th><b>z</b></th><td>w</td></tr>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_cell_descendants) - 1
                   && memcmp(sink.data, direct_row_cell_descendants,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><math><mi>x<col><td>y</template>"
             "<template><tr><math><mi>z<thead><tr><td>w"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_math_parts[] =
            "<html><head><template><tr></tr><math><mi>x</mi></math>"
            "<tr><td>y</td></tr></template><template><tr></tr>"
            "<math><mi>z</mi></math><tr><td>w</td></tr></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_math_parts) - 1
                   && memcmp(sink.data, direct_row_math_parts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><math><annotation-xml "
             "encoding=text/html>x<col></template>"
             "<template><tbody><tr><math><annotation-xml "
             "encoding=text/html>y<td>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_section_annotation[] =
            "<html><head><template><tbody></tbody><math>"
            "<annotation-xml encoding=\"text/html\">x</annotation-xml>"
            "</math><colgroup><col></colgroup></template><template>"
            "<tbody><tr><td>z</td></tr></tbody><math>"
            "<annotation-xml encoding=\"text/html\">y</annotation-xml>"
            "</math></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_section_annotation) - 1
                   && memcmp(sink.data, direct_section_annotation,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><tr><td><form><span>x</form>"
             "y<td>z</template><template><table><caption><form>a"
             "<colgroup><col></template><template><tbody><tr><td>"
             "<form>b</form><td>c</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_cell_forms[] =
            "<html><head><template><table><tbody><tr><td><form>"
            "<span>x</span></form>y</td><td>z</td></tr></tbody>"
            "</table></template><template><table><caption><form>"
            "a</form></caption><colgroup><col></colgroup></table>"
            "</template><template><tbody><tr><td><form>b</form>"
            "</td><td>c</td></tr></tbody></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_cell_forms) - 1
                   && memcmp(sink.data, template_cell_forms,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><tr><td><object>x</object><td>y"
             "</template><template><table><caption><div><object>"
             "z</object></div><tr><td>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char template_cell_objects[] =
            "<html><head><template><tbody><tr><td><object>x"
            "</object></td><td>y</td></tr></tbody></template>"
            "<template><table><caption><div><object>z</object>"
            "</div></caption><tbody><tr><td>w</td></tr></tbody>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(template_cell_objects) - 1
                   && memcmp(sink.data, template_cell_objects,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><article>x<object>y</object>"
             "<tr><td>z</template><template><tr><article>a<object>"
             "b</object><td>c</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_objects[] =
            "<html><head><template><article>x<object>y</object>"
            "</article><table><tbody><tr><td>z</td></tr></tbody>"
            "</table></template><template><tr><td>c</td></tr>"
            "<article>a<object>b</object></article></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_objects) - 1
                   && memcmp(sink.data, fostered_objects,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><article>x<marquee>y</marquee><td>z"
             "</template><template><tbody><tr><applet>a<tr><td>b"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_marquee_applet[] =
            "<html><head><template><tr><td>z</td></tr><article>"
            "x<marquee>y</marquee></article></template><template>"
            "<tbody><tr></tr><tr><td>b</td></tr></tbody>"
            "<applet>a</applet></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_marquee_applet) - 1
                   && memcmp(sink.data, fostered_marquee_applet,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><marquee>x<colgroup><col></template>"
             "<template><tr><td><div>y<caption>z</template>"
             "<template><tr><ins>w<thead><tr><td>q</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_ignored_parts[] =
            "<html><head><template><tr></tr><marquee>x</marquee>"
            "</template><template><tr><td><div>y</div></td></tr>"
            "z</template><template><tr></tr><ins>w</ins><tr>"
            "<td>q</td></tr></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_ignored_parts) - 1
                   && memcmp(sink.data, direct_row_ignored_parts,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><td>x</colgroup><col><td>y"
             "</template><template><tr><article>a<col><td>b"
             "</template><template><tbody><tr><td>c<col>"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_row_columns[] =
            "<html><head><template><tr><td>x</td></tr>"
            "<tr><td>y</td></tr></template><template><tr></tr>"
            "<article>a</article><tr><td>b</td></tr></template>"
            "<template><tbody><tr><td>c</td></tr></tbody>"
            "<colgroup><col></colgroup></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_row_columns) - 1
                   && memcmp(sink.data, direct_row_columns,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><div>x</table><td>y</template>"
             "<template><tbody><tr><article>a</table><td>b"
             "</template><template><tr><div>c</tfoot><td>d"
             "</template><template><tr><td>e</table><td>f"
             "</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_table_ends[] =
            "<html><head><template><tr></tr><div>x</div><tr>"
            "<td>y</td></tr></template><template><tbody><tr></tr>"
            "</tbody><article>a</article><tbody><tr><td>b</td></tr>"
            "</tbody></template><template><tr></tr><div>c</div>"
            "<tr><td>d</td></tr></template><template><tr><td>e"
            "</td><td>f</td></tr></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_table_ends) - 1
                   && memcmp(sink.data, direct_table_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tbody><tr><div>x</thead><td>y</template>"
             "<template><thead><tr><marquee>z</tfoot><td>w"
             "</template><template><tbody><tr><td>a</thead>"
             "<td>b</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char direct_section_mismatched_ends[] =
            "<html><head><template><tbody><tr></tr><tr><td>y</td>"
            "</tr></tbody><div>x</div></template><template><thead>"
            "<tr></tr><tr><td>w</td></tr></thead><marquee>z</marquee>"
            "</template><template><tbody><tr><td>a</td><td>b</td>"
            "</tr></tbody></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(direct_section_mismatched_ends) - 1
                   && memcmp(sink.data, direct_section_mismatched_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><tr><div>x</thead><td>y"
             "</template><template><table><thead><tr><marquee>"
             "z</tfoot><td>w</template><template><table><tbody>"
             "<tr><td>a</thead><td>b</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_mismatched_ends[] =
            "<html><head><template><div>x</div><table><tbody>"
            "<tr></tr><tr><td>y</td></tr></tbody></table></template>"
            "<template><marquee>z</marquee><table><thead><tr></tr>"
            "<tr><td>w</td></tr></thead></table></template><template>"
            "<table><tbody><tr><td>a</td><td>b</td></tr></tbody>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_mismatched_ends) - 1
                   && memcmp(sink.data, table_mismatched_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><div><table><tbody><tr><div>x</thead>"
             "<td>y</template><template><p><table><thead><tr>"
             "<marquee>z</tfoot><td>w</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_table_mismatched_ends[] =
            "<html><head><template><div><div>x</div><table><tbody>"
            "<tr></tr><tr><td>y</td></tr></tbody></table></div>"
            "</template><template><p><marquee>z</marquee><table>"
            "<thead><tr></tr><tr><td>w</td></tr></thead></table>"
            "</p></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_table_mismatched_ends) - 1
                   && memcmp(sink.data, nested_table_mismatched_ends,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><tr><noframes>x&amp;<b>y</noframes>"
             "<td>z</template><template><table><colgroup>"
             "<noframes>w</noframes><col></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_noframes[] =
            "<html><head><template><tr><td>z</td></tr>"
            "<noframes>x&amp;<b>y</noframes></template>"
            "<template><noframes>w</noframes><table>"
            "<colgroup></colgroup><colgroup><col></colgroup>"
            "</table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(table_noframes) - 1
                   && memcmp(sink.data, table_noframes,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><menuitem>x<span>y</span></menuitem>"
             "<multicol>z</multicol><nextid>a</nextid>"
             "<template><table><spacer>b<tr><td>c</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char legacy_containers[] =
            "<html><head></head><body><menuitem>x<span>y</span>"
            "</menuitem><multicol>z</multicol><nextid>a</nextid>"
            "<template><spacer>b</spacer><table><tbody><tr><td>c</td>"
            "</tr></tbody></table></template></body></html>";
        TEST_CHECK(sink.length == sizeof(legacy_containers) - 1
                   && memcmp(sink.data, legacy_containers,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><noscript><basefont>x</noscript><p>y</p>"
             "<noscript>z</noscript></noscript>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char closed_head_noscript[] =
            "<html><head><noscript><basefont></noscript></head>"
            "<body>x<p>y</p><noscript>z</noscript></body></html>";
        TEST_CHECK(sink.length == sizeof(closed_head_noscript) - 1
                   && memcmp(sink.data, closed_head_noscript,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><basefont></head><body><bgsound>x"
             "<template><table><basefont>a<bgsound>b<tr><td>c</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char legacy_void_head_table[] =
            "<html><head><basefont></head><body><bgsound>x"
            "<template><basefont>a<bgsound>b<table><tbody><tr><td>c</td>"
            "</tr></tbody></table></template></body></html>";
        TEST_CHECK(sink.length == sizeof(legacy_void_head_table) - 1
                   && memcmp(sink.data, legacy_void_head_table,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><isindex>x</isindex><template><table>"
             "<isindex>a<tr><td>b</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char isindex_container[] =
            "<html><head></head><body><isindex>x</isindex>"
            "<template><isindex>a</isindex><table><tbody><tr><td>b</td>"
            "</tr></tbody></table></template></body></html>";
        TEST_CHECK(sink.length == sizeof(isindex_container) - 1
                   && memcmp(sink.data, isindex_container,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><keygen>x<template><tr><keygen>y<td>z"
             "</template><template><table><colgroup><keygen>w"
             "<col></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char keygen_void[] =
            "<html><head></head><body><keygen>x<template><tr>"
            "<td>z</td></tr><keygen>y</template><template>"
            "<keygen>w<table><colgroup></colgroup><colgroup>"
            "<col></colgroup></table></template></body></html>";
        TEST_CHECK(sink.length == sizeof(keygen_void) - 1
                   && memcmp(sink.data, keygen_void, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><tr><td>A</td><col>"
             "<tr><td>B</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char implicit_late_column[] =
            "<html><head><template><table><tbody><tr><td>A</td>"
            "</tr></tbody><colgroup><col></colgroup><tbody><tr><td>B</td>"
            "</tr></tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(implicit_late_column) - 1
                   && memcmp(sink.data, implicit_late_column,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><table><span>X<col><tr><td>Y"
             "</table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char implicit_fostered_column[] =
            "<html><head><template><span>X</span><table>"
            "<colgroup><col></colgroup><tbody><tr><td>Y</td>"
            "</tr></tbody></table></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(implicit_fostered_column) - 1
                   && memcmp(sink.data, implicit_fostered_column,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><b>x<colgroup><col></colgroup></table>"
             "y</b>z</template><template><table><em>x<col></table>"
             "y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_column_formatting[] =
            "<html><head><template><b>x</b><table><colgroup><col>"
            "</colgroup></table><b>y</b>z</template><template><em>x</em>"
            "<table><colgroup><col></colgroup></table><em>y</em>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_column_formatting) - 1
                   && memcmp(sink.data, fostered_column_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><span><b>x<colgroup><col></colgroup>"
             "</table>y</b>z</template><template><table><div><em>x"
             "<col></table>y</em></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char nested_fostered_column[] =
            "<html><head><template><span><b>x</b></span><table>"
            "<colgroup><col></colgroup></table><b>y</b>z</template>"
            "<template><div><em>x</em></div><table><colgroup><col>"
            "</colgroup></table><em>y</em></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(nested_fostered_column) - 1
                   && memcmp(sink.data, nested_fostered_column,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><div><span><b>x<colgroup><col>"
             "</colgroup></table>y</template><template><table>"
             "<span><div><em>x<col></table>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char deeper_fostered_column[] =
            "<html><head><template><div><span><b>x</b></span></div>"
            "<table><colgroup><col></colgroup></table><b>y</b>"
            "</template><template><span><div><em>x</em></div></span>"
            "<table><colgroup><col></colgroup></table><em>y</em>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(deeper_fostered_column) - 1
                   && memcmp(sink.data, deeper_fostered_column,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><b><em>x<colgroup><col></colgroup>"
             "</table>y</em>z</b>q</template><template><table>"
             "<span><b><em>x<col></table>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char multiple_fostered_column_formatting[] =
            "<html><head><template><b><em>x</em></b><table>"
            "<colgroup><col></colgroup></table><b><em>y</em>z</b>q"
            "</template><template><span><b><em>x</em></b></span>"
            "<table><colgroup><col></colgroup></table><b><em>y"
            "</em></b></template></head><body></body></html>";
        TEST_CHECK(sink.length
                   == sizeof(multiple_fostered_column_formatting) - 1
                   && memcmp(sink.data, multiple_fostered_column_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><div><b>x<tr></table>z</b>q"
             "</template><template><table><tbody><b><em>x<tr>"
             "</table>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_row_formatting[] =
            "<html><head><template><div><b>x</b></div><table>"
            "<tbody><tr></tr></tbody></table><b>z</b>q</template>"
            "<template><b><em>x</em></b><table><tbody><tr></tr>"
            "</tbody></table><b><em>z</em></b></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_row_formatting) - 1
                   && memcmp(sink.data, fostered_row_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><b>x<tr><th>y</table>z</b>q"
             "</template><template><table><tbody><b><em>x<tr><td>y"
             "</table>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_cell_formatting[] =
            "<html><head><template><b>x</b><table><tbody><tr><th>y"
            "</th></tr></tbody></table><b>z</b>q</template>"
            "<template><b><em>x</em></b><table><tbody><tr><td>y"
            "</td></tr></tbody></table><b><em>z</em></b></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_cell_formatting) - 1
                   && memcmp(sink.data, fostered_cell_formatting,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><b>x<tr><td>y</td></table>z"
             "</template><template><table><tbody><b><em>x<tr><th>y"
             "</th></table>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_explicit_cell_end[] =
            "<html><head><template><b>x</b><table><tbody><tr><td>y"
            "</td></tr></tbody></table><b>z</b></template>"
            "<template><b><em>x</em></b><table><tbody><tr><th>y"
            "</th></tr></tbody></table><b><em>z</em></b></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_explicit_cell_end) - 1
                   && memcmp(sink.data, fostered_explicit_cell_end,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><thead><b>x</thead></table>z</b>q"
             "</template><template><table><tbody><b><em>x</tbody>"
             "</table>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char fostered_section_end[] =
            "<html><head><template><b>x</b><table><thead></thead>"
            "</table><b>z</b>q</template><template><b><em>x</em></b>"
            "<table><tbody></tbody></table><b><em>z</em></b>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(fostered_section_end) - 1
                   && memcmp(sink.data, fostered_section_end,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><b>x</tbody><tfoot></table>z"
             "</template><template><table><tbody><b><em>x</tbody>"
             "<thead></table>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char next_fostered_section[] =
            "<html><head><template><b>x</b><table><tbody></tbody>"
            "<tfoot></tfoot></table><b>z</b></template>"
            "<template><b><em>x</em></b><table><tbody></tbody>"
            "<thead></thead></table><b><em>z</em></b></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(next_fostered_section) - 1
                   && memcmp(sink.data, next_fostered_section,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tbody><b>x</tbody><tfoot><tr>"
             "</table>z</template><template><table><tbody><b><em>x"
             "</tbody><tfoot><tr><td>y</table>z</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char next_fostered_section_row[] =
            "<html><head><template><b>x</b><table><tbody></tbody>"
            "<tfoot><tr></tr></tfoot></table><b>z</b></template>"
            "<template><b><em>x</em></b><table><tbody></tbody>"
            "<tfoot><tr><td>y</td></tr></tfoot></table>"
            "<b><em>z</em></b></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(next_fostered_section_row) - 1
                   && memcmp(sink.data, next_fostered_section_row,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em><strong><main>x</b>y"
             "</main></li></ul></template><template><i><ol><li>"
             "<em><article>a</i>b</article></li></ol></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_main_article[] =
            "<html><head><template><b></b><ul><b></b><li><b><em>"
            "<strong></strong></em></b><em><strong><main><b>x"
            "</b>y</main></strong></em></li></ul></template>"
            "<template><i></i><ol><i></i><li><i><em></em></i>"
            "<em><article><i>a</i>b</article></em></li></ol>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_main_article) - 1
                   && memcmp(sink.data, crossed_list_main_article,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em><strong><aside>x</b>y"
             "</aside></li></ul></template><template><i><ol><li>"
             "<em><nav>a</i>b</nav></li></ol></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_aside_nav[] =
            "<html><head><template><b></b><ul><b></b><li><b><em>"
            "<strong></strong></em></b><em><strong><aside><b>x"
            "</b>y</aside></strong></em></li></ul></template>"
            "<template><i></i><ol><i></i><li><i><em></em></i>"
            "<em><nav><i>a</i>b</nav></em></li></ol>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_aside_nav) - 1
                   && memcmp(sink.data, crossed_list_aside_nav,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em><header>x</b>y</header>"
             "</li></ul></template><template><i><ol><li><strong>"
             "<pre>\na</i>b</pre></li></ol></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_header_pre[] =
            "<html><head><template><b></b><ul><b></b><li><b><em>"
            "</em></b><em><header><b>x</b>y</header></em></li></ul>"
            "</template><template><i></i><ol><i></i><li><i>"
            "<strong></strong></i><strong><pre><i>a</i>b</pre>"
            "</strong></li></ol></template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_header_pre) - 1
                   && memcmp(sink.data, crossed_list_header_pre,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em><strong><details>x</b>y"
             "</details></li></ul></template><template><i><ol><li>"
             "<em><fieldset>a</i>b</fieldset></li></ol></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_details_fieldset[] =
            "<html><head><template><b></b><ul><b></b><li><b><em>"
            "<strong></strong></em></b><em><strong><details><b>x"
            "</b>y</details></strong></em></li></ul></template>"
            "<template><i></i><ol><i></i><li><i><em></em></i>"
            "<em><fieldset><i>a</i>b</fieldset></em></li></ol>"
            "</template></head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_details_fieldset) - 1
                   && memcmp(sink.data, crossed_list_details_fieldset,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><b><ul><li><em><ol><li>x</li></b>y"
             "</ol></li></ul></template><template><i><ul><li>"
             "<strong><menu>x</i>y</menu></li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_nested_list[] =
            "<html><head><template><b></b><ul><b></b><li><b><em>"
            "</em></b><em><ol><b><li>x</li></b>y</ol></em></li>"
            "</ul></template><template><i></i><ul><i></i><li>"
            "<i><strong></strong></i><strong><menu><i>x</i>y"
            "</menu></strong></li></ul></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_nested_list) - 1
                   && memcmp(sink.data, crossed_list_nested_list,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><b><p>x</b>y<div>z</div></li>"
             "</ul></template><template><ol><li><em><main>a"
             "</em>b<section>c</section></li></ol></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_direct_block[] =
            "<html><head><template><ul><li><b></b><p><b>x</b>y"
            "</p><div>z</div></li></ul></template>"
            "<template><ol><li><em></em><main><em>a</em>b"
            "<section>c</section></main></li></ol></template>"
            "</head><body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_direct_block) - 1
                   && memcmp(sink.data, crossed_list_direct_block,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><ul><li><b><p><span>x</b>y</p></li></ul>"
             "</template><template><ul><li><b><p><em><strong>x"
             "</b>y</p>z</li></ul></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char crossed_list_block_suffix[] =
            "<html><head><template><ul><li><b></b><p><b><span>x"
            "</span></b>y</p></li></ul></template><template><ul><li>"
            "<b></b><p><b><em><strong>x</strong></em></b>"
            "<em><strong>y</strong></em></p><em><strong>z"
            "</strong></em></li></ul></template></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(crossed_list_block_suffix) - 1
                   && memcmp(sink.data, crossed_list_block_suffix,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_active_formatting = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><table><tbody><b><em>x</tbody></table>z"
             "</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    limits = lxa_limits_default();
    limits.max_active_formatting = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><table><tbody><b><em>x<tr></table>z</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    limits = lxa_limits_default();
    limits.max_active_formatting = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><table><b><em>x<col></table>y</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    limits = lxa_limits_default();
    limits.max_active_formatting = 0;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><table><b>x<colgroup><col></table>y</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    limits = lxa_limits_default();
    limits.max_open_elements = 3; /* row needs tbody/tr after closing li */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><li>one<tr></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits.max_open_elements = 4;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><li>one<tr></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
               == LXA_OK);
    lxa_html_document_destroy(document);
    limits = lxa_limits_default();
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><template><select><option>A<tr>"
             "</select></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_html_document_destroy(document);
    limits.max_nodes = 6; /* document, html, head, template, fragment, first table */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><table></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 6; /* document, html, head, template, fragment, table */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><input type=hidden></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><input type=text></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><textarea>x</textarea></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_open_elements = 2; /* template/table fit; fostered span doesn't */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><table><span>x</span></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_OPEN_ELEMENTS);
    limits = lxa_limits_default();
    limits.max_nodes = 5; /* custom element follows the template fragment */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><template><custom-box>x</custom-box></template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    for (i = 0; i < 128; ++i) {
        lxa_status_t status;
        failing.calls = 0;
        failing.fail_at = i;
        status = lxa_html_document_parse(&injected, NULL,
            span("<head><template id=x><custom-box data-x=y>"
                 "<p><b>A&amp;B</b><!--c--></p></custom-box>"
                 "<ul><li>x<li>y</ul><template>"
                 "<a href=/hidden>X</a></template>"
                 "<noscript><a href=/fallback>use</a></noscript>"
                 "<textarea>\nT&amp;B</textarea>"
                 "<iframe><a href=/fake></iframe>"
                 "<xmp><a href=/fake></xmp>"
                 "<select><option>one<input src=/x>"
                 "<select><option>two<textarea>next</textarea>"
                 "<table>foster&amp;run<col span=2><caption><p>title"
                 "<tr><td><p>cell<caption>"
                 "other</caption><tr><td>next</table>"
                 "</template><link href=/ok>"),
            &parse_result, &reason, &document);
        if (status == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(status == LXA_ERROR_NO_MEMORY && document == NULL
                   && parse_result == LXA_HTML_NO_MEMORY);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t status;
        failing.calls = 0;
        failing.fail_at = i;
        status = lxa_html_document_parse(&injected, NULL,
            span("<body><div><template><a href=/inner>hidden</a>"
                 "</template><a href=/after>visible</a></div>"),
            &parse_result, &reason, &document);
        if (status == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(status == LXA_ERROR_NO_MEMORY && document == NULL
                   && parse_result == LXA_HTML_NO_MEMORY);
    }
    TEST_CHECK(i < 128);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<head><noscript> \nA"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char split_noscript_text[] =
            "<html><head><noscript> \n</noscript></head><body>A</body></html>";
        TEST_CHECK(sink.length == sizeof(split_noscript_text) - 1
                   && memcmp(sink.data, split_noscript_text,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 3; /* document, html and head */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<head><noscript><link href=/sheet></noscript>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    for (i = 0; i < 128; ++i) {
        lxa_status_t status;
        failing.calls = 0;
        failing.fail_at = i;
        status = lxa_html_document_parse(&injected, NULL,
            span("<head><noscript><link href=/sheet></noscript></head>"),
            &parse_result, &reason, &document);
        if (status == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(status == LXA_ERROR_NO_MEMORY && document == NULL
                   && parse_result == LXA_HTML_NO_MEMORY);
    }
    TEST_CHECK(i < 128);

    /* A foreign font stays foreign unless color/face/size requests HTML
     * breakout; the latter preserves the link outside the SVG subtree. */
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><font title=x><a href=/foreign>F</a></font></svg>"
             "<svg><font face=roman><a href=/html>H</a></font></svg>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char font_boundary[] =
            "<html><head></head><body><svg><font title=\"x\">"
            "<a href=\"/foreign\">F</a></font></svg><svg></svg>"
            "<font face=\"roman\"><a href=\"/html\">H</a></font>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(font_boundary) - 1
                   && memcmp(sink.data, font_boundary,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    limits.max_nodes = 5; /* document, roots and svg before font */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<svg><font color=red>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body id=first><table><body id=second class=shared>"
             "<tr><td>row</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char repeated_body[] =
            "<html><head></head><body id=\"first\" class=\"shared\">"
            "<table><tbody><tr><td>row</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(repeated_body) - 1
                   && memcmp(sink.data, repeated_body, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><option>one<script><a href=hidden></script>"
             "two</option></select><p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_script[] =
            "<html><head></head><body><select><option>one"
            "<script><a href=hidden></script>two</option></select>"
            "<p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(select_script) - 1
                   && memcmp(sink.data, select_script, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><option>first<textarea>\nvalue &amp; more</textarea>"
             "<p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_textarea[] =
            "<html><head></head><body><select><option>first</option>"
            "</select><textarea>value &amp; more</textarea>"
            "<p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(select_textarea) - 1
                   && memcmp(sink.data, select_textarea, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><div><a href=ignored>text</a><img src=ignored>"
             "<option>choice</option></div></select><p>next</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char ignored_starts[] =
            "<html><head></head><body><select>text<option>choice"
            "</option></select><p>next</p></body></html>";
        TEST_CHECK(sink.length == sizeof(ignored_starts) - 1
                   && memcmp(sink.data, ignored_starts, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><option>first<input src=after-select>"
             "<p>after</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_input[] =
            "<html><head></head><body><select><option>first</option>"
            "</select><input src=\"after-select\"><p>after</p></body></html>";
        TEST_CHECK(sink.length == sizeof(select_input) - 1
                   && memcmp(sink.data, select_input, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><option>first<keygen>after"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char select_keygen[] =
            "<html><head></head><body><select><option>first</option>"
            "</select><keygen>after</body></html>";
        TEST_CHECK(sink.length == sizeof(select_keygen) - 1
                   && memcmp(sink.data, select_keygen, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<select><option>a<select><optgroup><option>b<p>c"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char orphan_option_body[] =
            "<html><head></head><body><select><option>a</option></select>"
            "<optgroup><option>b<p>c</p></option></optgroup></body></html>";
        TEST_CHECK(sink.length == sizeof(orphan_option_body) - 1
                   && memcmp(sink.data, orphan_option_body,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><select><option>x<textarea>\ny&amp;z"
             "</textarea><tr><td>q"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_select_textarea[] =
            "<html><head></head><body><select><option>x</option></select>"
            "<textarea>y&amp;z</textarea><table><colgroup></colgroup>"
            "<tbody><tr><td>q</td></tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(table_select_textarea) - 1
                   && memcmp(sink.data, table_select_textarea,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><colgroup><title>A&amp;B</title><tr><td>x</table>"
             "<table><tbody><xmp><b>raw</b></xmp><tr><td>y</table>"
             "<table><iframe>Z</iframe><noembed>Q</noembed>"
             "<tr><td>z</table><table><colgroup><noframes><b>raw</b>"
             "</noframes><tr><td>w</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char table_fostered_text_modes[] =
            "<html><head></head><body><title>A&amp;B</title>"
            "<table><colgroup></colgroup><tbody><tr><td>x</td></tr>"
            "</tbody></table><xmp><b>raw</b></xmp><table><tbody><tr>"
            "<td>y</td></tr></tbody></table><iframe>Z</iframe>"
            "<noembed>Q</noembed><table><tbody><tr><td>z</td></tr>"
            "</tbody></table><noframes><b>raw</b></noframes>"
            "<table><colgroup></colgroup><tbody><tr><td>w</td></tr>"
            "</tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(table_fostered_text_modes) - 1
                   && memcmp(sink.data, table_fostered_text_modes,
                             sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><tr><td><style>.noise{}</style>"
             "<script>noise()</script><span>kept</span></td></tr></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char cell[] =
            "<html><head></head><body><table><tbody><tr><td>"
            "<style>.noise{}</style><script>noise()</script>"
            "<span>kept</span></td></tr></tbody></table></body></html>";
        TEST_CHECK(sink.length == sizeof(cell) - 1
                   && memcmp(sink.data, cell, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>Be sure your reply has a <b>Subject:</b> header</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char bold[] =
            "<html><head></head><body><p>Be sure your reply has a "
            "<b>Subject:</b> header</p></body></html>";
        TEST_CHECK(sink.length == sizeof(bold) - 1
                   && memcmp(sink.data, bold, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>first<aside><p>chrome</p></aside><p>kept</p>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char aside[] =
            "<html><head></head><body><p>first</p><aside><p>chrome</p>"
            "</aside><p>kept</p></body></html>";
        TEST_CHECK(sink.length == sizeof(aside) - 1
                   && memcmp(sink.data, aside, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<p>before<form><p>search</p></form><p>after</p>"
             "<form><pre>archive search</pre></form>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char forms[] =
            "<html><head></head><body><p>before</p><form><p>search</p>"
            "</form><p>after</p><form><pre>archive search</pre></form>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(forms) - 1
                   && memcmp(sink.data, forms, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<html><body><div><a href=x>link</body></html>"
             "<!--after-->"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char unclosed[] =
            "<html><head></head><body><div><a href=\"x\">link</a>"
            "</div></body></html><!--after-->";
        TEST_CHECK(sink.length == sizeof(unclosed) - 1
                   && memcmp(sink.data, unclosed, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><col span=2><tr><td>one</table>"
             "<table><colgroup><col><col></colgroup><tr><td>two</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char columns[] =
            "<html><head></head><body>"
            "<table><colgroup><col span=\"2\"></colgroup>"
            "<tbody><tr><td>one</td></tr></tbody></table>"
            "<table><colgroup><col><col></colgroup>"
            "<tbody><tr><td>two</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(columns) - 1
                   && memcmp(sink.data, columns, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<html><head></head><body></body><!--after-body-->"
             "</html><!--after-html-->"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char comments[] =
            "<html><head></head><body></body><!--after-body-->"
            "</html><!--after-html-->";
        TEST_CHECK(sink.length == sizeof(comments) - 1
                   && memcmp(sink.data, comments, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<html><head><link href='page.html'><base target='_self'>"
             "<base href='/docs/'><base href='/ignored/'></head></html>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char head[] =
            "<html><head><link href=\"page.html\"><base target=\"_self\">"
            "<base href=\"/docs/\"><base href=\"/ignored/\"></head>"
            "<body></body></html>";
        TEST_CHECK(sink.length == sizeof(head) - 1
                   && memcmp(sink.data, head, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<html><head><BASE HREF='/docs/'></head><body>"
             "<a href='page.html'>page</a></body></html>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char body[] =
            "<html><head><base href=\"/docs/\"></head><body>"
            "<a href=\"page.html\">page</a></body></html>";
        TEST_CHECK(sink.length == sizeof(body) - 1
                   && memcmp(sink.data, body, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><input type=HIDDEN name=x>"
             "<input type=text name=y><tr><td>cell</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char inputs[] =
            "<html><head></head><body><input type=\"text\" name=\"y\">"
            "<table><input type=\"HIDDEN\" name=\"x\">"
            "<tbody><tr><td>cell</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(inputs) - 1
                   && memcmp(sink.data, inputs, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<ul><li><a href='one&amp;two'>one<li><img src='pic.png'>"
             "<a HREF='last'>last<div data-href='ignored'>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char recovered[] =
            "<html><head></head><body><ul><li><a href=\"one&amp;two\">"
            "one</a></li><li><a href=\"one&amp;two\"><img src=\"pic.png\">"
            "</a><a href=\"last\">last<div data-href=\"ignored\"></div>"
            "</a></li></ul></body></html>";
        TEST_CHECK(sink.length == sizeof(recovered) - 1
                   && memcmp(sink.data, recovered, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><caption>first<caption>second</caption></table>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char adjacent[] =
            "<html><head></head><body><table><caption>first</caption>"
            "<caption>second</caption></table></body></html>";
        TEST_CHECK(sink.length == sizeof(adjacent) - 1
                   && memcmp(sink.data, adjacent, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<table><caption><span>one<tr><td>two</table>"),
        &parse_result, &reason, &document) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(
        lxa_html_document_nodes(document), lxa_html_document_root(document),
        write_bytes, &sink, &reason) == LXA_OK);
    {
        const char implicit[] =
            "<html><head></head><body><table><caption><span>one</span>"
            "</caption><tbody><tr><td>two</td></tr></tbody></table>"
            "</body></html>";
        TEST_CHECK(sink.length == sizeof(implicit) - 1
                   && memcmp(sink.data, implicit, sink.length) == 0);
    }
    lxa_html_document_destroy(document);

    document = (lxa_html_document_t *) &sink;
    limits.max_input_bytes = 3;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits, span("<p>x</p>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_INPUT
               && parse_result == LXA_HTML_LIMIT);
    {
        const uint8_t broken[] = {0xef, 0xbb, 0xbf, '<','p','>',0xff,'<','/','p','>'};
        lxa_dom_ref_t paragraph, text_node;
        TEST_CHECK(lxa_html_document_parse_report(&allocator, NULL,
            (lxa_span_t){broken, sizeof(broken)}, on_malformed, &diagnostic,
            &parse_result, &reason, &document) == LXA_OK);
        TEST_CHECK(diagnostic.count == 1 && diagnostic.offset == 6);
        nodes = lxa_html_document_nodes(document);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &paragraph) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, paragraph, &text_node)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_character_read(nodes, text_node, &value)
                   == LXA_OK);
        TEST_CHECK(value.length == 3
                   && memcmp(value.data, "\xef\xbf\xbd", 3) == 0);
        lxa_html_document_destroy(document);
        diagnostic.count = 0;
        diagnostic.response = LXA_ERROR_NO_MEMORY;
        TEST_CHECK(lxa_html_document_parse_report(&allocator, NULL,
            (lxa_span_t){broken, sizeof(broken)}, on_malformed, &diagnostic,
            &parse_result, &reason, &document) == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(document == NULL && diagnostic.count == 1
                   && diagnostic.offset == 6);
    }
    limits = lxa_limits_default();
    limits.initial_work_credit = 0;
    limits.work_per_input_byte = 0;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits, span("<p>x</p>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_WORK);
    limits = lxa_limits_default();
    limits.max_nodes = 5; /* document/html/head/body/p precede the PI */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<p><?custom data?></p>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 5; /* the option exceeds document/html/head/body/select */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<select><option>x</option></select>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<select><hr></select>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<select><input src=x>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<select><keygen>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<select><textarea>x</textarea>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 6; /* document, roots, div and text precede implied p */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<div>x</p>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 5; /* caption would be the sixth node */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><caption>x</caption></table>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 9; /* roots, table, implied tbody, tr, td, select */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><tr><td><select><table>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 9; /* roots, table, tbody, tr, td, select */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><tr><td><select><tr>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><tr><td><select><th>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><tr><td><select><thead>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><tr><td><select><caption>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 7; /* roots, table, caption, select */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><caption><select><caption>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><select><tr>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 8; /* a direct-table td also needs tbody and tr */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><select><td>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><tbody><select><td>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><tr><select><td>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><colgroup><select><tr>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><caption><select><tr>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 9; /* colgroup-select td requires three new slots */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><colgroup><select><td>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><caption><select><td>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 7; /* direct section needs colgroup and col together */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><tbody></tbody><col>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 6; /* implicit colgroup and col need two free slots */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><col span=2>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 5; /* a second table cannot allocate after the first */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><table>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 4; /* document/html/head/body precede svg */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<svg><![CDATA[x]]></svg>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 5; /* roots and svg precede adjusted foreignObject */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<svg><foreignobject><p>x</p></foreignobject></svg>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_attributes_per_element = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<html lang=en><select><html class=one>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ATTRS_PER_ELEMENT);
    limits = lxa_limits_default();
    limits.max_nodes = 6; /* p breakout needs a node after svg and g */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<svg><g><p>x"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_active_formatting = 0;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<ul><li><a href='x'>x<li><img src='y'>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><ul><li><em>x</ul>y</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    limits.max_active_formatting = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><ul><li><em>x</ul>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><ul><li><em><strong>x</ul>y</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    limits.max_active_formatting = 2;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><ul><li><em><strong>x</ul>y</template>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><ul><li><em><strong>x</ul>"
             "<template><ol><li><i>q</ol></template>y</template>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    limits.max_active_formatting = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<a>1<b>2</a>3</b>"), &parse_result, &reason, &document)
        == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);
    limits.max_active_formatting = 0;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<b><i>x</b>after</i>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_ACTIVE_FORMATTING);
    limits = lxa_limits_default();
    limits.max_nodes = 7; /* crossed i needs another element before text */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<body><b><i>x</b>after</i>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 5; /* repeated nobr needs a second element slot */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<nobr><nobr>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 6; /* fostered marquee follows nobr and table */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<nobr><table><marquee></table><nobr>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 9; /* two cloned anchors need slots before reparenting */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<a><div><style></style><address><a>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 5; /* document roots, anchor and p; clone needs one more */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<a><p></a>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_open_elements = 4; /* adoption replaces rather than grows stack */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<a><div><style></style><address><a>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);
    limits = lxa_limits_default();
    limits.max_nodes = 8; /* fragment exists, but a direct cell needs a slot */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><thead><template><td>x</template></table>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 10; /* open list item adoption needs two clones */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li>x</b>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 12; /* direct formatting continuation needs a third clone */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><em>x</b>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 12; /* direct div requires one clone within the block */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><div>x</b>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 14; /* nested section needs a fourth formatting clone */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><div><section>x</b>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 7; /* template table and colgroup fit; b does not */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><table><colgroup><b>x</b>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><p><b><object>x</object></b></p>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><ul><li><object>x</object></li></ul>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 9; /* table, implied section/row and cell precede object */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><table><tr><td><object>x</object>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 13; /* leading span and direct block need three clones */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><span><div>x</b>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 13; /* span stays under the cloned block formatting */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><div><span>x</b>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 15; /* block span plus formatting need four clones */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><div><span><em>x</b>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 11; /* span remains under two crossed-list clones */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><span>x</b>"), &parse_result,
        &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 14; /* nested spans keep one formatting continuation */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><span><span><em>x</b>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 14; /* nested continuation needs four clones */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<template><b><ul><li><em><strong>x</b>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 5; /* select plus template and fragment need two slots */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<select><template>"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 9; /* the second table follows foreign select */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<div><table><svg><foreignObject><select><table><s>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_open_elements = 6; /* foreign select is the stack high-water */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<div><table><svg><foreignObject><select><table><s>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);
    limits = lxa_limits_default();
    limits.max_nodes = 10; /* second cell follows the SVG desc */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><tr><td><svg><desc><td>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    limits.max_nodes = 6; /* table template and fragment require two slots */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<table><template></template></table>"),
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits.max_nodes = 11; /* pinned paragraph adoption needs two clones */
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        span("<b>1<i>2<p>3</b>4"), &parse_result, &reason, &document)
        == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_NODES);
    limits = lxa_limits_default();
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<body><b><i class=x>one</b><p>two</p>three</i>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);
    for (i = 0; i < 64; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<body><b><i class=x>one</b><p>two</p>three</i>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX; /* validation allocates scratch */
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 64);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<nobr><table><marquee></table><nobr>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<template>Hello</template><div>after</div>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<table><thead><template><td>x</template></table>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<div><table><svg><foreignObject><select><table><s>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<table><tr><td><svg><desc><td>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<select><option><template></template></select>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<a class=x><div><style></style><address><a href=/new>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<b class=x>1<i data-y=z>2<p>3<span>more</span>"
                 "</b>4"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t result;
        failing.calls = 0;
        failing.fail_at = i;
        result = lxa_html_document_parse(&injected, NULL,
            span("<table><template><a href=/inside>inner</a></template>"
                 "<tr><td>after</table>"),
            &parse_result, &reason, &document);
        if (result == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            failing.fail_at = SIZE_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(
                lxa_html_document_nodes(document)) == LXA_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(result == LXA_ERROR_NO_MEMORY && document == NULL);
    }
    TEST_CHECK(i < 128);
    {
        const uint8_t latin1[] = {
            '<','m','e','t','a',' ','c','h','a','r','s','e','t','=','w','i','n',
            'd','o','w','s','-','1','2','5','2','>','<','p','>',0x80,'<','/','p','>'
        };
        lxa_dom_ref_t paragraph, text_node;
        limits = lxa_limits_default();
        limits.max_input_bytes = sizeof(latin1);
        TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
            (lxa_span_t){latin1, sizeof(latin1)}, &parse_result,
            &reason, &document) == LXA_OK);
        nodes = lxa_html_document_nodes(document);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes,
            lxa_html_document_body(document), &paragraph) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, paragraph, &text_node)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_character_read(nodes, text_node, &value)
                   == LXA_OK);
        TEST_CHECK(value.length == 3 && memcmp(value.data, "\342\202\254", 3) == 0);
        lxa_html_document_destroy(document);
    }
    {
        const uint8_t bom[] = {0xef, 0xbb, 0xbf, '<','p','>','x','<','/','p','>'};
        TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){bom, sizeof(bom)}, &parse_result, &reason,
            &document) == LXA_OK);
        lxa_html_document_destroy(document);
    }
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<meta charset='unknown-encoding'><p>x</p>"), &parse_result,
        &reason, &document) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(document == NULL && parse_result == LXA_HTML_BAD_ARGUMENT);
    {
        uint8_t late[1150];
        const char unknown[] = "<meta charset='not-a-real-encoding'><p>x</p>";
        const char known[] = "<meta charset='windows-1252'><p>\x80</p>";
        memset(late, ' ', 1025);
        memcpy(late + 1025, unknown, sizeof(unknown) - 1);
        TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){late, 1025 + sizeof(unknown) - 1},
            &parse_result, &reason, &document) == LXA_ERROR_ARGUMENT);
        TEST_CHECK(document == NULL && parse_result == LXA_HTML_BAD_ARGUMENT);
        memcpy(late + 1025, known, sizeof(known) - 1);
        diagnostic.count = 0;
        diagnostic.response = LXA_OK;
        TEST_CHECK(lxa_html_document_parse_report(&allocator, NULL,
            (lxa_span_t){late, 1025 + sizeof(known) - 1},
            on_malformed, &diagnostic, &parse_result, &reason, &document)
            == LXA_OK);
        TEST_CHECK(diagnostic.count == 0 && parse_result == LXA_HTML_OK);
        {
            lxa_dom_ref_t paragraph, text;
            nodes = lxa_html_document_nodes(document);
            TEST_CHECK(lxa_dom_nodes_first_child(nodes,
                lxa_html_document_body(document), &paragraph) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_first_child(nodes, paragraph, &text)
                       == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_character_read(nodes, text, &value)
                       == LXA_OK);
            TEST_CHECK(value.length == 3
                       && memcmp(value.data, "\342\202\254", 3) == 0);
        }
        lxa_html_document_destroy(document);
        {
            static const char malformed[] =
                "<meta charset='euc-jp'><p>\x8f\xa2Z</p>";
            const size_t bad_offset = 1025
                + sizeof("<meta charset='euc-jp'><p>") - 1;
            lxa_dom_ref_t paragraph, text;
            memcpy(late + 1025, malformed, sizeof(malformed) - 1);
            diagnostic.count = 0;
            TEST_CHECK(lxa_html_document_parse_report(&allocator, NULL,
                (lxa_span_t){late, 1025 + sizeof(malformed) - 1},
                on_malformed, &diagnostic, &parse_result, &reason, &document)
                == LXA_OK);
            TEST_CHECK(parse_result == LXA_HTML_OK && diagnostic.count == 1
                       && diagnostic.offset == bad_offset);
            nodes = lxa_html_document_nodes(document);
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_first_child(nodes,
                lxa_html_document_body(document), &paragraph) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_first_child(nodes, paragraph, &text)
                       == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_character_read(nodes, text, &value)
                       == LXA_OK);
            TEST_CHECK(value.length == 4
                       && memcmp(value.data, "\xef\xbf\xbdZ", 4) == 0);
            lxa_html_document_destroy(document);
            {
                const lxa_status_t failures[] = {
                    LXA_ERROR_ARGUMENT, LXA_ERROR_NO_MEMORY, LXA_ERROR_LIMIT,
                    LXA_ERROR_OVERFLOW, LXA_ERROR_ENTROPY
                };
                size_t failure;
                for (failure = 0; failure < sizeof(failures) / sizeof(*failures);
                     ++failure) {
                    diagnostic.count = 0;
                    diagnostic.response = failures[failure];
                    TEST_CHECK(lxa_html_document_parse_report(&allocator, NULL,
                        (lxa_span_t){late, 1025 + sizeof(malformed) - 1},
                        on_malformed, &diagnostic, &parse_result, &reason,
                        &document) == failures[failure]);
                    TEST_CHECK(document == NULL && diagnostic.count == 1
                               && diagnostic.offset == bad_offset
                               && parse_result == LXA_HTML_CALLBACK_FAILURE
                               && reason == LXA_BUDGET_COUNT);
                }
            }
            diagnostic.response = LXA_OK;
            memcpy(late + 1025, known, sizeof(known) - 1);
        }
        limits = lxa_limits_default();
        limits.max_nodes = 9; /* eight tentative slots plus preflight margin */
        TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
            (lxa_span_t){late, 1025 + sizeof(known) - 1},
            &parse_result, &reason, &document) == LXA_OK);
        lxa_html_document_destroy(document);
        limits = lxa_limits_default();
        for (i = 0; i < 256; ++i) {
            lxa_status_t injected_status;
            failing.calls = 0;
            failing.fail_at = i;
            injected_status = lxa_html_document_parse(&injected, NULL,
                (lxa_span_t){late, 1025 + sizeof(known) - 1},
                &parse_result, &reason, &document);
            if (injected_status == LXA_OK) {
                lxa_html_document_destroy(document);
                break;
            }
            TEST_CHECK(injected_status == LXA_ERROR_NO_MEMORY
                       && parse_result == LXA_HTML_NO_MEMORY
                       && document == NULL);
        }
        TEST_CHECK(i < 256);
        {
            const char pragma[] =
                "<meta http-equiv=CONTENT-TYPE "
                "content='text/html; charset=windows-1252'><p>\x80</p>";
            lxa_dom_ref_t paragraph, text;
            memcpy(late + 1025, pragma, sizeof(pragma) - 1);
            diagnostic.count = 0;
            TEST_CHECK(lxa_html_document_parse_report(&allocator, NULL,
                (lxa_span_t){late, 1025 + sizeof(pragma) - 1},
                on_malformed, &diagnostic, &parse_result, &reason, &document)
                == LXA_OK);
            TEST_CHECK(diagnostic.count == 0);
            nodes = lxa_html_document_nodes(document);
            TEST_CHECK(lxa_dom_nodes_first_child(nodes,
                lxa_html_document_body(document), &paragraph) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_first_child(nodes, paragraph, &text)
                       == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_character_read(nodes, text, &value)
                       == LXA_OK);
            TEST_CHECK(value.length == 3
                       && memcmp(value.data, "\342\202\254", 3) == 0);
            lxa_html_document_destroy(document);
        }
        {
            const char pragma[] =
                "<meta http-equiv=content-type "
                "content='text/html; charset=not-a-real-encoding'><p>x</p>";
            memcpy(late + 1025, pragma, sizeof(pragma) - 1);
            TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
                (lxa_span_t){late, 1025 + sizeof(pragma) - 1},
                &parse_result, &reason, &document) == LXA_ERROR_ARGUMENT);
            TEST_CHECK(document == NULL && parse_result == LXA_HTML_BAD_ARGUMENT);
        }
        {
            const char irrelevant[] =
                "<meta http-equiv=refresh content='charset=invalid'>"
                "<meta http-equiv=content-type content='text/html'>"
                "<p>x</p>";
            memcpy(late + 1025, irrelevant, sizeof(irrelevant) - 1);
            TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
                (lxa_span_t){late, 1025 + sizeof(irrelevant) - 1},
                &parse_result, &reason, &document) == LXA_OK);
            lxa_html_document_destroy(document);
        }
        {
            const char comment[] =
                "<!--<meta charset='not-a-real-encoding'>--><p>x</p>";
            memcpy(late + 1025, comment, sizeof(comment) - 1);
            TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
                (lxa_span_t){late, 1025 + sizeof(comment) - 1},
                &parse_result, &reason, &document) == LXA_OK);
            lxa_html_document_destroy(document);
        }
        {
            const char script[] =
                "<script><meta charset='not-a-real-encoding'></script><p>x</p>";
            memcpy(late + 1025, script, sizeof(script) - 1);
            TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
                (lxa_span_t){late, 1025 + sizeof(script) - 1},
                &parse_result, &reason, &document) == LXA_OK);
            lxa_html_document_destroy(document);
        }
        memcpy(late, "\xef\xbb\xbf", 3);
        memset(late + 3, ' ', 1025);
        memcpy(late + 1028, unknown, sizeof(unknown) - 1);
        TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){late, 1028 + sizeof(unknown) - 1},
            &parse_result, &reason, &document) == LXA_OK);
        lxa_html_document_destroy(document);
    }
    limits = lxa_limits_default();
    limits.max_input_bytes = 1;
    TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
        (lxa_span_t){(const uint8_t *) "xx", 2},
        &parse_result, &reason, &document) == LXA_ERROR_LIMIT);
    TEST_CHECK(document == NULL && reason == LXA_BUDGET_INPUT
               && parse_result == LXA_HTML_LIMIT);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        (lxa_span_t){NULL, 1}, &parse_result, &reason, &document)
        == LXA_ERROR_ARGUMENT && document == NULL);
    TEST_CHECK(parse_result == LXA_HTML_BAD_ARGUMENT);
    {
        const lxa_limit_reason_t reasons[] = {
            LXA_BUDGET_MEMORY, LXA_BUDGET_NODES, LXA_BUDGET_WORK
        };
        size_t limit;
        for (limit = 0; limit < sizeof(reasons) / sizeof(*reasons); ++limit) {
            limits = lxa_limits_default();
            if (reasons[limit] == LXA_BUDGET_MEMORY) limits.max_memory_bytes = 0;
            if (reasons[limit] == LXA_BUDGET_NODES) limits.max_nodes = 0;
            if (reasons[limit] == LXA_BUDGET_WORK) limits.max_work = 0;
            TEST_CHECK(lxa_html_document_parse(&allocator, &limits,
                span("<p>x</p>"), &parse_result, &reason, &document)
                == LXA_ERROR_LIMIT);
            TEST_CHECK(document == NULL && parse_result == LXA_HTML_LIMIT
                       && reason == reasons[limit]);
        }
    }
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<template><table><tr><td>x</td></tr></table></template>"),
        &parse_result, &reason, &document) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(lxa_html_document_nodes(document))
        == LXA_OK);
    lxa_html_document_destroy(document);
    TEST_CHECK(lxa_html_document_parse(&allocator, NULL,
        span("<svg><g></math><path>kept</path></g></svg>"
             "<math><mrow></svg>text</mrow></math>"),
        &parse_result, &reason, &document) == LXA_OK);
    nodes = lxa_html_document_nodes(document);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    sink.length = 0;
    TEST_CHECK(lxa_html_serialize_document(nodes,
        lxa_html_document_root(document), write_bytes, &sink, &reason)
        == LXA_OK);
    {
        const char foreign_ends[] =
            "<html><head></head><body><svg><g><path>kept</path></g></svg>"
            "<math><mrow>text</mrow></math></body></html>";
        TEST_CHECK(sink.length == sizeof(foreign_ends) - 1
                   && memcmp(sink.data, foreign_ends, sink.length) == 0);
    }
    lxa_html_document_destroy(document);
    for (i = 0; i < 128; ++i) {
        lxa_status_t status;
        failing.calls = 0;
        failing.fail_at = i;
        status = lxa_html_document_parse(&injected, NULL,
            span("<p><?custom data?></p>"), &parse_result,
            &reason, &document);
        if (status == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(status == LXA_ERROR_NO_MEMORY && document == NULL
                   && parse_result == LXA_HTML_NO_MEMORY);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t status;
        failing.calls = 0;
        failing.fail_at = i;
        status = lxa_html_document_parse(&injected, NULL,
            span("<svg viewbox='0 0 1 1'><image href='x'/>"
                 "<g><![CDATA[x&y]]><p>after"),
            &parse_result, &reason, &document);
        if (status == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(status == LXA_ERROR_NO_MEMORY && document == NULL
                   && parse_result == LXA_HTML_NO_MEMORY);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t status;
        failing.calls = 0;
        failing.fail_at = i;
        status = lxa_html_document_parse(&injected, NULL,
            span("<p>ok</p>"), &parse_result, &reason, &document);
        if (status == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(status == LXA_ERROR_NO_MEMORY && document == NULL
                   && parse_result == LXA_HTML_NO_MEMORY);
    }
    TEST_CHECK(i < 128);
    for (i = 0; i < 128; ++i) {
        lxa_status_t status;
        failing.calls = 0;
        failing.fail_at = i;
        status = lxa_html_document_parse(&injected, NULL,
            span("<ul><li><a href='x'>x<li><img src='y'>"),
            &parse_result, &reason, &document);
        if (status == LXA_OK) {
            TEST_CHECK(document != NULL && parse_result == LXA_HTML_OK);
            lxa_html_document_destroy(document);
            break;
        }
        TEST_CHECK(status == LXA_ERROR_NO_MEMORY && document == NULL
                   && parse_result == LXA_HTML_NO_MEMORY);
    }
    TEST_CHECK(i < 128);
    lxa_html_document_destroy(NULL);
    return 0;
}
