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

static void
check_foreign_after_body_namespace(void)
{
    static const struct {
        const char *input;
        lxa_dom_namespace_t ns;
    } cases[] = {
        {"<svg></body><circle/>", LXA_DOM_NS_SVG},
        {"<math></html><mrow/>", LXA_DOM_NS_MATHML},
        {"<svg></html><foreignObject><p>x", LXA_DOM_NS_HTML}
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        lxa_dom_nodes_t *nodes;
        lxa_html_tree_t tree;
        lxa_html_tokenizer_t *tokenizer;
        lxa_dom_ref_t node;
        lxa_dom_namespace_t ns;
        TEST_CHECK(lxa_dom_nodes_create(&allocator, &nodes) == LXA_OK);
        TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
        TEST_CHECK(lxa_html_tokenizer_create(&allocator,
            lxa_html_tree_on_token, NULL, &tree, &tokenizer) == LXA_OK);
        TEST_CHECK(lxa_html_tokenizer_feed(tokenizer, span(cases[i].input))
                   == LXA_HTML_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, tree.body, &node)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, node, &node) == LXA_OK);
        if (i == 2)
            TEST_CHECK(lxa_dom_nodes_first_child(nodes, node, &node)
                       == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, node, &ns)
                   == LXA_OK && ns == cases[i].ns);
        TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_html_tokenizer_destroy(tokenizer);
        lxa_html_tree_destroy(&tree);
        lxa_dom_nodes_destroy(nodes);
    }
}


static void
check_template_column_admission_limits(void)
{
    static const char *inputs[] = {
        "<template><td><tr><div>",
        "<template><tr><caption><div>",
        "<template><caption><div>",
        "<template><table><tr><td><div>"
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        for (uint32_t capacity = 0; capacity < 3; ++capacity) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_html_tree_t tree;
            lxa_html_tokenizer_t *tokenizer;
            lxa_html_token_t column = {.kind = LXA_HTML_START_TAG,
                                       .name = span("col")};
            size_t count, open;
            lxa_html_template_mode_t mode;
            bool ignored = i < 2, limited = !ignored && capacity < 2;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                NULL, &nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
                lxa_html_tree_on_token, NULL, &tree, &budget,
                &tokenizer) == LXA_OK);
            TEST_CHECK(lxa_html_tokenizer_feed(tokenizer, span(inputs[i]))
                       == LXA_HTML_OK);
            count = lxa_dom_nodes_length(nodes);
            open = lxa_vector_length(&tree.template_elements);
            mode = tree.template_insertion_mode;
            budget.limits.max_nodes = (uint32_t) count + capacity;
            TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &column)
                       == (limited ? LXA_ERROR_LIMIT : LXA_OK));
            TEST_CHECK(lxa_dom_nodes_length(nodes)
                       == count + (ignored || limited ? 0 : 2));
            if (ignored || limited)
                TEST_CHECK(lxa_vector_length(&tree.template_elements) == open
                           && tree.template_insertion_mode == mode);
            if (limited) TEST_CHECK(nodes->limit_reason == LXA_BUDGET_NODES);
            budget.limits = lxa_limits_default();
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_html_tokenizer_destroy(tokenizer);
            lxa_html_tree_destroy(&tree);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                       && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
                       && budget.used[LXA_BUDGET_ACTIVE_FORMATTING] == 0);
        }
    }
}


static void
check_table_transitions(void)
{
    static const struct {
        const char *input, *output;
    } cases[] = {
        {"<html lang=old><div></body><html lang=new dir=rtl><!--c-->"
         "<?x y?><html title=t>",
         "<html lang=\"old\" dir=\"rtl\" title=\"t\"><head></head><body>"
         "<div></div></body><!--c--><?x y?></html>"},
        {"<div></body></html><html lang=x><!--c--><?x y?>",
         "<html lang=\"x\"><head></head><body><div></div></body></html>"
         "<!--c--><?x y?>"},
        {"<div></body><html><!--a--><script><b></script><!--b-->",
         "<html><head></head><body><div><script><b></script><!--b-->"
         "</div></body><!--a--></html>"},
        {"<div></body></html><html><!--a--><script><b></script><!--b-->",
         "<html><head></head><body><div><script><b></script><!--b-->"
         "</div></body></html><!--a-->"},
        {"<table><colgroup><head href=x> <script><a href=x>",
         "<html><head></head><body><table><colgroup></colgroup> "
         "<script><a href=x></script></table></body></html>"},
        {"<template><caption>x<caption>y</template>",
         "<html><head><template><caption>x</caption><caption>y</caption>"
         "</template></head><body></body></html>"},
        {"<template><caption>x<colgroup>y</template>",
         "<html><head><template><caption>x</caption><colgroup></colgroup>"
         "y</template></head><body></body></html>"},
        {"<template><caption>x<tbody>y</template>",
         "<html><head><template><caption>x</caption><tbody></tbody>"
         "y</template></head><body></body></html>"},
        {"<template><caption>x<tr>y</template>",
         "<html><head><template><caption>x</caption><tbody><tr></tr>"
         "</tbody>y</template></head><body></body></html>"},
        {"<template><caption>x<td>y</template>",
         "<html><head><template><caption>x</caption><tbody><tr><td>y</td>"
         "</tr></tbody></template></head><body></body></html>"},
        {"<table><colgroup><math><mi>x</mi></math> y<col></table>",
         "<html><head></head><body><math><mi>x</mi></math> y<table>"
         "<colgroup></colgroup><colgroup><col></colgroup></table>"
         "</body></html>"},
        {"<table><colgroup><svg><g>x</g></svg> y<col></table>",
         "<html><head></head><body><svg><g>x</g></svg> y<table>"
         "<colgroup></colgroup><colgroup><col></colgroup></table>"
         "</body></html>"},
        {"<template><td><tr><div><tfoot>y</div></template>",
         "<html><head><template><td></td><div>y</div></template></head>"
         "<body></body></html>"},
        {"<template><tr><caption><div><tbody>y</div></template>",
         "<html><head><template><tr></tr><div>y</div></template></head>"
         "<body></body></html>"},
        {"<template><td><tr><div><td>y</td>z</template>",
         "<html><head><template><td></td><div></div><td>y</td>z</template>"
         "</head><body></body></html>"},
        {"<template><tr><caption><div><td>y</td>z</template>",
         "<html><head><template><tr></tr><div></div><tr><td>y</td></tr>"
         "z</template></head><body></body></html>"},
        {"<template><thead><th><form>x<colgroup><col>y</template>",
         "<html><head><template><thead><tr><th><form>x</form></th></tr>"
         "</thead><colgroup><col></colgroup>y</template></head>"
         "<body></body></html>"},
        {"<form><template><tr><td><form>x<tfoot>y</template></form><form>z",
         "<html><head></head><body><form><template><tr><td><form>x</form>"
         "</td></tr>y</template></form><form>z</form></body></html>"},
        {"<template><html><tr><td>x</template>"
         "<template><head><tr><td>x</template>"
         "<template><body><tr><td>x</template>"
         "<template><frameset><tr><td>x</template>"
         "<template><frame><tr><td>x</template>",
         "<html><head><template>x</template><template>x</template>"
         "<template>x</template><template>x</template><template>x</template>"
         "</head><body></body></html>"},
        {"<template><table><template><frame><tr><td>x</template>"
         "<tr><td>y</template>",
         "<html><head><template><table><template>x</template><tbody><tr>"
         "<td>y</td></tr></tbody></table></template></head><body></body></html>"},
        {"<template><colgroup><html lang=x><col></template>",
         "<html><head><template><colgroup><col></colgroup></template>"
         "</head><body></body></html>"},
        {"<svg></body><circle/><g/><!--x--><?x y?><![CDATA[z]]>",
         "<html><head></head><body><svg><circle></circle><g></g>"
         "<!--x--><?x y?>z</svg></body></html>"},
        {"<math></html><mrow/><!--x-->",
         "<html><head></head><body><math><mrow></mrow><!--x-->"
         "</math></body></html>"},
        {"<svg><g></body></g><circle/>",
         "<html><head></head><body><svg><g></g><circle></circle>"
         "</svg></body></html>"},
        {"<svg></html><foreignObject><p>x",
         "<html><head></head><body><svg><foreignObject><p>x</p>"
         "</foreignObject></svg></body></html>"},
        {"<svg></body><p>x",
         "<html><head></head><body><svg></svg><p>x</p></body></html>"},
        {"<svg></body></html><!--x-->",
         "<html><head></head><body><svg><!--x--></svg></body></html>"},
        {"<template><tr><caption><div><col span=2>x<td>y",
         "<html><head><template><tr></tr><div>x</div><tr><td>y</td></tr>"
         "</template></head><body></body></html>"},
        {"<template><td><tr><div><col>x<td>y",
         "<html><head><template><td></td><div>x</div><td>y</td></template>"
         "</head><body></body></html>"},
        {"<template><th><caption><b><col>x",
         "<html><head><template><th></th><b>x</b></template></head>"
         "<body></body></html>"},
        {"<template><caption>x<col span=2>y",
         "<html><head><template><caption>x</caption><colgroup>"
         "<col span=\"2\"></colgroup>y</template></head><body></body></html>"},
        {"<table><thead><math><mtext><table>x<tr><td>y",
         "<html><head></head><body><math><mtext></mtext></math><table>"
         "<thead></thead></table>x<table><tbody><tr><td>y</td></tr></tbody>"
         "</table></body></html>"},
        {"<table><tbody><svg><foreignObject><table>x<tr><td>y",
         "<html><head></head><body><svg><foreignObject></foreignObject>"
         "</svg><table><tbody></tbody></table>x<table><tbody><tr><td>y</td>"
         "</tr></tbody></table></body></html>"},
        {"<table><td><math><mtext><table>x<tr><td>y",
         "<html><head></head><body><table><tbody><tr><td><math><mtext>x"
         "<table><tbody><tr><td>y</td></tr></tbody></table></mtext></math>"
         "</td></tr></tbody></table></body></html>"},
        {"<table><caption><svg><foreignObject><table>x",
         "<html><head></head><body><table><caption><svg><foreignObject>x"
         "<table></table></foreignObject></svg></caption></table></body></html>"},
        {"<template><svg><colgroup></dl><!--c--><?x y?>",
         "<html><head><template><svg><colgroup><!--c--><?x y?>"
         "</colgroup></svg></template></head><body></body></html>"},
        {"<template><math><colgroup></div><!--c-->",
         "<html><head><template><math><colgroup><!--c--></colgroup>"
         "</math></template></head><body></body></html>"},
        {"<template><svg><colgroup></colgroup><!--c-->",
         "<html><head><template><svg><colgroup></colgroup><!--c-->"
         "</svg></template></head><body></body></html>"},
        {"<template><table><colgroup></dl><!--c-->",
         "<html><head><template><table><colgroup></colgroup><!--c-->"
         "</table></template></head><body></body></html>"},
        {"<table><colgroup> <head> \n<col>",
         "<html><head></head><body><table><colgroup> </colgroup> \n"
         "<colgroup><col></colgroup></table></body></html>"},
        {"<table><colgroup><body class=x> <col>",
         "<html><head></head><body class=\"x\"><table><colgroup></colgroup> "
         "<colgroup><col></colgroup></table></body></html>"},
        {"<table><colgroup><frameset> <col><frame> <col>",
         "<html><head></head><body><table><colgroup></colgroup> "
         "<colgroup><col></colgroup> <colgroup><col></colgroup></table>"
         "</body></html>"},
        {"<table><colgroup><html lang=x> <col>",
         "<html lang=\"x\"><head></head><body><table><colgroup> <col>"
         "</colgroup></table></body></html>"}
    };
    static const size_t chunks[] = {SIZE_MAX, 1, 7};
    lxa_allocator_t allocator = lxa_allocator_default();
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (size_t j = 0; j < sizeof(chunks) / sizeof(chunks[0]); ++j) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_html_tree_t tree;
            lxa_html_tokenizer_t *tokenizer;
            lxa_limit_reason_t reason;
            sink_t sink = {{0}, 0};
            lxa_span_t input = span(cases[i].input);
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                NULL, &nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
                lxa_html_tree_on_token, NULL, &tree, &budget,
                &tokenizer) == LXA_OK);
            for (size_t offset = 0; offset < input.length;) {
                size_t length = input.length - offset;
                if (length > chunks[j]) length = chunks[j];
                TEST_CHECK(lxa_html_tokenizer_feed(tokenizer, (lxa_span_t) {
                    input.data + offset, length
                }) == LXA_HTML_OK);
                offset += length;
            }
            TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
                write_bytes, &sink, &reason) == LXA_OK);
            TEST_CHECK(sink.length == strlen(cases[i].output)
                && memcmp(sink.data, cases[i].output, sink.length) == 0);
            lxa_html_tokenizer_destroy(tokenizer);
            lxa_html_tree_destroy(&tree);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
                && budget.used[LXA_BUDGET_ACTIVE_FORMATTING] == 0);
        }
    }
}

static void
check_option_insert_state(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < 4; ++path) {
        bool templated = (path & 1) != 0, explicit_second = (path & 2) != 0;
        lxa_budget_t budget;
        lxa_dom_nodes_t *nodes;
        lxa_html_tree_t tree;
        lxa_html_tokenizer_t *tokenizer;
        lxa_dom_ref_t second, select, first;
        lxa_dom_record_t record;
        lxa_html_result_t (*feed)(lxa_html_tokenizer_t *, lxa_span_t) =
            lxa_html_tokenizer_feed;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                    &nodes) == LXA_OK);
        TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
        TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
            lxa_html_tree_on_token, NULL, &tree, &budget, &tokenizer) == LXA_OK);
        if (templated) TEST_CHECK(feed(tokenizer, span("<template>")) == LXA_HTML_OK);
        TEST_CHECK(feed(tokenizer, span("<select><option>a")) == LXA_HTML_OK);
        TEST_CHECK(feed(tokenizer, span(explicit_second ? "<option selected>b"
                                                       : "<option>b")) == LXA_HTML_OK);
        {
            const lxa_vector_t *open = templated ? &tree.template_elements
                                                : &tree.open_elements;
            second = ((const lxa_dom_ref_t *) open->bytes.data)
                [lxa_vector_length(open) - 1];
        }
        TEST_CHECK(lxa_dom_nodes_parent(nodes, second, &select) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, select, &first) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_read(nodes, first, &record) == LXA_OK
                   && record.flags == (explicit_second ? 0 : LXA_DOM_OPTION_SELECTED));
        TEST_CHECK(lxa_dom_nodes_read(nodes, second, &record) == LXA_OK
                   && record.flags == (explicit_second ? LXA_DOM_OPTION_SELECTED : 0));
        TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_html_tokenizer_destroy(tokenizer);
        lxa_html_tree_destroy(&tree);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
}

static void
check_foster_content_limits(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    bool success = false, published_failure = false;
    for (size_t allowance = 0; allowance < 512 && !success; ++allowance) {
        lxa_budget_t budget;
        lxa_dom_nodes_t *nodes;
        lxa_html_tree_t tree;
        lxa_html_tokenizer_t *tokenizer;
        lxa_dom_ref_t table, content;
        lxa_dom_record_t record;
        lxa_html_token_t token = {.kind = LXA_HTML_START_TAG,
            .name = span("selectedcontent")};
        lxa_status_t status;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL, &nodes) == LXA_OK);
        TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
        TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
            lxa_html_tree_on_token, NULL, &tree, &budget, &tokenizer) == LXA_OK);
        TEST_CHECK(lxa_html_tokenizer_feed(tokenizer, span("<option><table>")) == LXA_HTML_OK);
        TEST_CHECK(lxa_vector_length(&tree.open_elements) == 3);
        table = ((const lxa_dom_ref_t *) tree.open_elements.bytes.data)[2];
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
        status = lxa_html_tree_on_token(&tree, NULL, &token);
        budget.limits.max_work = lxa_limits_default().max_work;
        if (status != LXA_OK)
            TEST_CHECK(status == LXA_ERROR_LIMIT && tree.failure == status
                && nodes->limit_reason == LXA_BUDGET_WORK);
        TEST_CHECK(lxa_dom_nodes_prev_sibling(nodes, table, &content) == LXA_OK);
        if (status == LXA_OK) {
            success = true;
            TEST_CHECK(lxa_dom_nodes_read(nodes, content, &record) == LXA_OK
                && record.flags == LXA_DOM_SELECTEDCONTENT_DISABLED);
        }
        else {
            if (content.handle != LXA_HANDLE_NONE) {
                published_failure = true;
                TEST_CHECK(lxa_vector_length(&tree.open_elements) == 3);
            }
        }
        TEST_CHECK(lxa_html_tokenizer_finish(tokenizer)
            == (success ? LXA_HTML_OK : LXA_HTML_CALLBACK_FAILURE));
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_html_tokenizer_destroy(tokenizer);
        lxa_html_tree_destroy(&tree);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
            && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0);
    }
    TEST_CHECK(success && published_failure);
}

static void
check_selectedcontent_disabled_state(void)
{
    static const struct {
        const char *input;
        bool disabled;
    } cases[] = {
        {"<selectedcontent><selectedcontent>", true},
        {"<option><selectedcontent>", true},
        {"<option><table><selectedcontent>", true},
        {"<selectedcontent>", false},
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    for (size_t path = 0; path < sizeof(cases) / sizeof(cases[0]); ++path) {
        lxa_budget_t budget;
        lxa_dom_nodes_t *nodes;
        lxa_html_tree_t tree;
        lxa_html_tokenizer_t *tokenizer;
        lxa_dom_ref_t element;
        lxa_dom_record_t record;
        lxa_span_t tag;
        const lxa_vector_t *open;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                    &nodes) == LXA_OK);
        TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
        TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
            lxa_html_tree_on_token, NULL, &tree, &budget, &tokenizer) == LXA_OK);
        TEST_CHECK(lxa_html_tokenizer_feed(tokenizer, span(cases[path].input))
                   == LXA_HTML_OK);
        open = &tree.open_elements;
        element = ((const lxa_dom_ref_t *) open->bytes.data)[lxa_vector_length(open) - 1];
        TEST_CHECK(lxa_dom_nodes_element_name(nodes, element, &tag) == LXA_OK
                   && tag.length == strlen("selectedcontent")
                   && memcmp(tag.data, "selectedcontent", tag.length) == 0);
        TEST_CHECK(lxa_dom_nodes_read(nodes, element, &record) == LXA_OK
                   && record.flags == (cases[path].disabled
                        ? LXA_DOM_SELECTEDCONTENT_DISABLED : 0));
        TEST_CHECK(lxa_html_tokenizer_finish(tokenizer) == LXA_HTML_OK);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_html_tokenizer_destroy(tokenizer);
        lxa_html_tree_destroy(&tree);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                   && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0);
    }
}

static void
check_option_pop_reentrant_removal(void)
{
    static const size_t chunks[] = {SIZE_MAX, 1, 7};
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_span_t input = span("<template><select><option selected>x<!---->");
    for (unsigned engine = 0; engine < 1; ++engine) {
        for (size_t part = 0; part < sizeof(chunks) / sizeof(chunks[0]); ++part) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_html_tree_t tree;
            lxa_html_tokenizer_t *tokenizer;
            lxa_dom_ref_t option, select, target, content, text;
            lxa_dom_record_t record;
            lxa_html_result_t (*feed)(lxa_html_tokenizer_t *, lxa_span_t) =
                lxa_html_tokenizer_feed;
            lxa_html_result_t (*finish)(lxa_html_tokenizer_t *) =
                lxa_html_tokenizer_finish;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL, &nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
                lxa_html_tree_on_token, NULL, &tree, &budget, &tokenizer) == LXA_OK);
            for (size_t offset = 0; offset < input.length;) {
                size_t size = input.length - offset;
                if (size > chunks[part]) size = chunks[part];
                TEST_CHECK(feed(tokenizer, (lxa_span_t){input.data + offset, size}) == LXA_HTML_OK);
                offset += size;
            }
            option = ((const lxa_dom_ref_t *) tree.template_elements.bytes.data)
                [lxa_vector_length(&tree.template_elements) - 1];
            TEST_CHECK(lxa_dom_nodes_parent(nodes, option, &select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("selectedcontent"), &target) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes, select, target, option) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("selectedcontent"), &content) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, target, content) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, span("old"), &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, content, text) == LXA_OK);
            size_t count = lxa_dom_nodes_length(nodes);
            TEST_CHECK(feed(tokenizer, span("</template>")) == LXA_HTML_OK);
            TEST_CHECK(lxa_dom_nodes_length(nodes) == count + 6);
            TEST_CHECK(finish(tokenizer) == LXA_HTML_OK);
            TEST_CHECK(tree.mode == LXA_TREE_DONE && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0);
            TEST_CHECK(lxa_dom_nodes_read(nodes, content, &record) == LXA_OK
                && record.parent == LXA_ID_NONE);
            TEST_CHECK(lxa_dom_nodes_first_child(nodes, target, &text) == LXA_OK);
            lxa_span_t bytes;
            TEST_CHECK(lxa_dom_nodes_character_read(nodes, text, &bytes) == LXA_OK
                && bytes.length == 1 && bytes.data[0] == 'x');
            TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, text, &text) == LXA_OK
                && lxa_dom_nodes_read(nodes, text, &record) == LXA_OK
                && record.kind == LXA_DOM_KIND_COMMENT);
            TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, text, &text) == LXA_OK
                && text.handle == LXA_HANDLE_NONE);
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_html_tokenizer_destroy(tokenizer);
            lxa_html_tree_destroy(&tree);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
                && budget.used[LXA_BUDGET_ACTIVE_FORMATTING] == 0);
        }
    }
}

static void
check_option_pop_failure(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < 5; ++path) {
        bool success = false;
        for (size_t allowance = 0; allowance < 1024 && !success; ++allowance) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_html_tree_t tree;
            lxa_html_tokenizer_t *tokenizer;
            lxa_html_token_t token = {0};
            lxa_dom_ref_t option, select, target, text;
            lxa_dom_record_t before[32], record;
            lxa_dom_ref_t open[8];
            lxa_vector_t *elements;
            lxa_html_root_mode_t mode;
            size_t count, length, quota;
            lxa_status_t status;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                        &nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
                lxa_html_tree_on_token, NULL, &tree, &budget, &tokenizer) == LXA_OK);
            TEST_CHECK(lxa_html_tokenizer_feed(tokenizer,
                span(path < 3 ? "<body><template><select><option selected>x<!---->"
                              : "<body><select><option selected>x<!---->")) == LXA_HTML_OK);
            elements = path < 3 ? &tree.template_elements : &tree.open_elements;
            length = lxa_vector_length(elements);
            option = ((const lxa_dom_ref_t *) elements->bytes.data)[length - 1];
            TEST_CHECK(lxa_dom_nodes_parent(nodes, option, &select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("selectedcontent"),
                                                         &target) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes, select, target, option)
                       == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, span("old"),
                                                      &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, target, text) == LXA_OK);
            count = lxa_dom_nodes_length(nodes);
            quota = budget.used[LXA_BUDGET_OPEN_ELEMENTS];
            mode = tree.mode;
            TEST_CHECK(count <= sizeof(before) / sizeof(before[0])
                       && length <= sizeof(open) / sizeof(open[0]));
            memcpy(before, nodes->records.bytes.data, count * sizeof(before[0]));
            memcpy(open, elements->bytes.data, length * sizeof(open[0]));
            token.kind = path == 2 || path == 4 ? LXA_HTML_EOF : LXA_HTML_END_TAG;
            token.name = token.kind == LXA_HTML_EOF ? (lxa_span_t){0}
                : span(path == 1 ? "template" : "option");
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_html_tree_on_token(&tree, NULL, &token);
            success = status == LXA_OK;
            budget.limits.max_work = lxa_limits_default().max_work;
            if (!success) {
                TEST_CHECK(status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK);
                TEST_CHECK(lxa_vector_length(elements) == length
                    && memcmp(open, elements->bytes.data,
                              length * sizeof(open[0])) == 0
                    && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == quota
                    && tree.mode == mode);
                for (size_t i = 0; i < count; ++i) {
                    lxa_dom_ref_t ref = {.document = nodes, .epoch = nodes->epoch};
                    TEST_CHECK(lxa_handle_pack((lxa_id_t) i, nodes->generation, &ref.handle) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_read(nodes, ref, &record) == LXA_OK
                               && memcmp(&record, &before[i], sizeof(record)) == 0);
                }
                TEST_CHECK(lxa_html_tree_on_token(&tree, NULL, &token) == status);
            }
            else if (token.kind == LXA_HTML_EOF) {
                TEST_CHECK(tree.mode == LXA_TREE_DONE
                    && lxa_vector_length(&tree.open_elements) == 0
                    && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0);
            }
            if (success) {
                lxa_dom_ref_t clone;
                lxa_span_t bytes;
                TEST_CHECK(lxa_dom_nodes_first_child(nodes, target, &clone) == LXA_OK
                    && clone.handle != text.handle
                    && lxa_dom_nodes_character_read(nodes, clone, &bytes) == LXA_OK
                    && bytes.length == 1 && bytes.data[0] == 'x');
                TEST_CHECK(lxa_dom_nodes_read(nodes, text, &record) == LXA_OK
                           && record.parent == LXA_ID_NONE);
            }
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_html_tokenizer_destroy(tokenizer);
            lxa_html_tree_destroy(&tree);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
                && budget.used[LXA_BUDGET_ACTIVE_FORMATTING] == 0);
        }
        TEST_CHECK(success);
    }
}



static void
check_adoption_removal_limits(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned pass = 0; pass < 1; ++pass) {
        bool in_template = pass >= 2;
        const char *expected = in_template
            ? "<html><head><template><div><b></b><div><b></b></div></div>"
              "</template></head><body></body></html>"
            : "<html><head></head><body><div><a></a><div><a></a></div>"
              "</div></body></html>";
        bool success = false, detached_failure = false;
        for (size_t allowance = 0; allowance < 512 && !success; ++allowance) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_html_tree_t tree;
            lxa_html_tokenizer_t *tokenizer;
            lxa_dom_ref_t common, formatting, furthest;
            lxa_dom_record_t record;
            lxa_html_token_t token = {.kind = LXA_HTML_END_TAG,
                .name = span(in_template ? "b" : "a")};
            lxa_html_result_t (*feed)(lxa_html_tokenizer_t *, lxa_span_t) =
                lxa_html_tokenizer_feed;
            lxa_html_result_t (*finish)(lxa_html_tokenizer_t *) =
                lxa_html_tokenizer_finish;
            lxa_status_t status;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                NULL, &nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
                lxa_html_tree_on_token, NULL, &tree, &budget, &tokenizer) == LXA_OK);
            TEST_CHECK(feed(tokenizer, span(in_template
                ? "<template><div><b><div>" : "<div><a><div>")) == LXA_HTML_OK);
            const lxa_vector_t *elements = in_template
                ? &tree.template_elements : &tree.open_elements;
            size_t count = lxa_vector_length(elements);
            TEST_CHECK(count == (in_template ? 3 : 4));
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) elements->bytes.data;
            common = open[count - 3];
            formatting = open[count - 2];
            furthest = open[count - 1];
            TEST_CHECK(lxa_dom_nodes_read(nodes, furthest, &record) == LXA_OK
                && record.parent == (lxa_id_t) formatting.handle);
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_html_tree_on_token(&tree, NULL, &token);
            budget.limits.max_work = lxa_limits_default().max_work;
            TEST_CHECK(lxa_dom_nodes_read(nodes, furthest, &record) == LXA_OK);
            if (status == LXA_OK) {
                sink_t sink = {{0}, 0};
                lxa_limit_reason_t reason;
                success = true;
                TEST_CHECK(record.parent == (lxa_id_t) common.handle);
                TEST_CHECK(finish(tokenizer) == LXA_HTML_OK);
                TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
                    write_bytes, &sink, &reason) == LXA_OK);
                TEST_CHECK(sink.length == strlen(expected)
                    && memcmp(sink.data, expected, sink.length) == 0);
            }
            else {
                TEST_CHECK(status == LXA_ERROR_LIMIT && tree.failure == status
                    && nodes->limit_reason == LXA_BUDGET_WORK);
                if (record.parent == LXA_ID_NONE) {
                    detached_failure = true;
                    TEST_CHECK(record.prev == LXA_ID_NONE && record.next == LXA_ID_NONE);
                }
                else TEST_CHECK(record.parent == (lxa_id_t) formatting.handle
                    || record.parent == (lxa_id_t) common.handle);
                TEST_CHECK(finish(tokenizer) == LXA_HTML_CALLBACK_FAILURE);
            }
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_html_tokenizer_destroy(tokenizer);
            lxa_html_tree_destroy(&tree);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0
                && budget.used[LXA_BUDGET_ACTIVE_FORMATTING] == 0);
        }
        TEST_CHECK(success && detached_failure);
    }
}

static void
check_detached_table_fostering(void)
{
    static const struct {
        const char *input, *output;
    } cases[] = {
        {"<div>foster</div>", "<div>foster</div>"},
        {"foster", "foster"},
        {"<svg><text>foster</text></svg>", "<svg><text>foster</text></svg>"},
        {"<textarea>foster</textarea>", "<textarea>foster</textarea>"}
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned driver = 0; driver < 1; ++driver) {
        for (size_t path = 0; path < sizeof(cases) / sizeof(cases[0]); ++path) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_html_tree_t tree;
            lxa_html_tokenizer_t *tokenizer;
            lxa_dom_ref_t table;
            lxa_dom_record_t record;
            sink_t sink = {{0}, 0};
            lxa_limit_reason_t reason;
            lxa_html_result_t (*feed)(lxa_html_tokenizer_t *, lxa_span_t) =
                lxa_html_tokenizer_feed;
            lxa_html_result_t (*finish)(lxa_html_tokenizer_t *) =
                lxa_html_tokenizer_finish;
            const char *prefix = "<html><head></head><body>";
            const char *suffix = "</body></html>";
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL, &nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tree_init(&tree, nodes) == LXA_OK);
            TEST_CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
                lxa_html_tree_on_token, NULL, &tree, &budget, &tokenizer) == LXA_OK);
            TEST_CHECK(feed(tokenizer, span("<table>")) == LXA_HTML_OK);
            TEST_CHECK(lxa_vector_length(&tree.open_elements) == 2);
            table = ((const lxa_dom_ref_t *) tree.open_elements.bytes.data)[1];
            TEST_CHECK(lxa_dom_nodes_detach_constructed_element(nodes, table) == LXA_OK);
            TEST_CHECK(feed(tokenizer, span(cases[path].input)) == LXA_HTML_OK);
            TEST_CHECK(finish(tokenizer) == LXA_HTML_OK);
            TEST_CHECK(lxa_dom_nodes_read(nodes, table, &record) == LXA_OK
                && record.parent == LXA_ID_NONE);
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            TEST_CHECK(lxa_html_serialize_document(nodes, tree.document,
                write_bytes, &sink, &reason) == LXA_OK);
            TEST_CHECK(sink.length == strlen(prefix) + strlen(cases[path].output) + strlen(suffix)
                && memcmp(sink.data, prefix, strlen(prefix)) == 0
                && memcmp(sink.data + strlen(prefix), cases[path].output,
                          strlen(cases[path].output)) == 0
                && memcmp(sink.data + sink.length - strlen(suffix), suffix, strlen(suffix)) == 0);
            lxa_html_tokenizer_destroy(tokenizer);
            lxa_html_tree_destroy(&tree);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0
                && budget.used[LXA_BUDGET_OPEN_ELEMENTS] == 0);
        }
    }
}

int
main(void)
{
    check_table_transitions();
    check_foreign_after_body_namespace();
    check_template_column_admission_limits();
    check_detached_table_fostering();
    check_adoption_removal_limits();
    check_option_pop_failure();
    check_option_pop_reentrant_removal();
    check_option_insert_state();
    check_foster_content_limits();
    check_selectedcontent_disabled_state();
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
