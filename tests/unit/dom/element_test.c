/* Copyright (C) 2026 Paul Moses */
#include "tests/support/test_check.h"
#include <stdio.h>
#include <string.h>
#include "dom/node_store_internal.h"
#include "liblexa/base.h"

static lxa_span_t
name(const char *text)
{
    lxa_span_t span = {(const uint8_t *) text, strlen(text)};
    return span;
}

static lxa_status_t
fixed_entropy(void *context, uint8_t key[16])
{
    unsigned int i;
    if (context == NULL) {
        return LXA_ERROR_ENTROPY;
    }
    for (i = 0; i != 16; ++i) {
        key[i] = (uint8_t) (i + 1);
    }
    return LXA_OK;
}

int
main(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_dom_nodes_t *nodes = NULL, *other = NULL;
    lxa_dom_ref_t div, next;
    lxa_dom_record_t record;
    lxa_dom_namespace_t ns;
    lxa_budget_t budget;
    lxa_limits_t limits = lxa_limits_default();
    lxa_span_t view;
    char input[] = "custom-tag";
    size_t before, i;
    TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create_with_entropy(&allocator, &budget,
        fixed_entropy, NULL, NULL, &nodes) == LXA_ERROR_ENTROPY);
    TEST_CHECK(nodes == NULL && budget.used[LXA_BUDGET_MEMORY] == 0);
    TEST_CHECK(lxa_dom_nodes_create_with_entropy(&allocator, &budget,
        fixed_entropy, &budget, NULL, &nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name(input), &div)
               == LXA_OK);
    input[0] = 'X';
    TEST_CHECK(lxa_dom_nodes_element_name(nodes, div, &view) == LXA_OK);
    TEST_CHECK(view.length == 10 && memcmp(view.data, "custom-tag", 10) == 0);
    TEST_CHECK(lxa_table_length(nodes->atom_lookup) == 1);
    TEST_CHECK(lxa_dom_nodes_read(nodes, div, &record) == LXA_OK);
    TEST_CHECK(record.kind == LXA_DOM_KIND_ELEMENT
               && record.parent == LXA_ID_NONE
               && record.first_child == LXA_ID_NONE);
    TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, div, &ns) == LXA_OK
               && ns == LXA_DOM_NS_HTML);
    TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("svg"),
        LXA_DOM_NS_SVG, &next) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, next, &ns) == LXA_OK
               && ns == LXA_DOM_NS_SVG);
    TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("math"),
        LXA_DOM_NS_MATHML, &next) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, next, &ns) == LXA_OK
               && ns == LXA_DOM_NS_MATHML);
    before = lxa_dom_nodes_length(nodes);
    TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("svg"),
        0, &next) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("svg"),
        4, &next) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_length(nodes) == before);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    for (i = 0; i != 256; ++i) {
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("div"), &next)
                   == LXA_OK);
    }
    TEST_CHECK(lxa_table_length(nodes->atom_lookup) == 4
               && lxa_vector_length(&nodes->atoms) == 4);
    TEST_CHECK(lxa_dom_nodes_element_name(nodes, div, &view) == LXA_OK);
    TEST_CHECK(view.length == 10 && memcmp(view.data, "custom-tag", 10) == 0);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    before = lxa_dom_nodes_length(nodes);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("Bad"), &next)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("a b"), &next)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name(""), &next)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_length(nodes) == before);

    budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("foo"), &next)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_MEMORY
               && lxa_dom_nodes_length(nodes) == before
               && lxa_table_length(nodes->atom_lookup) == 4);
    budget.limits.max_memory_bytes = limits.max_memory_bytes;
    budget.limits.max_name_bytes = 2;
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("new"), &next)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_NAME_BYTES);
    budget.limits.max_name_bytes = limits.max_name_bytes;
    budget.limits.max_string_bytes = budget.used[LXA_BUDGET_STRINGS];
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("fresh"), &next)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_STRINGS);
    budget.limits.max_string_bytes = limits.max_string_bytes;
    budget.limits.max_nodes = (uint32_t) budget.used[LXA_BUDGET_NODES];
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("div"), &next)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_NODES);
    budget.limits.max_nodes = limits.max_nodes;
    TEST_CHECK(lxa_dom_nodes_length(nodes) == before);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);

    TEST_CHECK(lxa_dom_nodes_create(&allocator, &other) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_element_name(other, div, &view)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_reset(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_element_name(nodes, div, &view)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("span"), &next)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        uint8_t key[16];
        uint64_t occupied, candidate_hash;
        char candidate[20];
        size_t count, before_names;
        TEST_CHECK(fixed_entropy(&budget, key) == LXA_OK);
        TEST_CHECK(lxa_siphash24(key, (const uint8_t *) "span", 4,
                                 &occupied) == LXA_OK);
        for (count = 0; count < 1000; ++count) {
            TEST_CHECK(snprintf(candidate, sizeof(candidate), "a%zu", count)
                       < (int) sizeof(candidate));
            TEST_CHECK(lxa_siphash24(key, name(candidate).data,
                                     name(candidate).length, &candidate_hash)
                       == LXA_OK);
            if ((candidate_hash & 15) == (occupied & 15)) {
                break;
            }
        }
        TEST_CHECK(count < 1000);
        before_names = lxa_table_length(nodes->atom_lookup);
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK]
            + strlen(candidate) + 2; /* validation, hash, first probe */
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name(candidate),
                                                      &div) == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
        TEST_CHECK(lxa_table_length(nodes->atom_lookup) == before_names);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name(candidate),
                                                      &div) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        TEST_CHECK(lxa_table_length(nodes->atom_lookup) == before_names + 1);
    }
    lxa_dom_nodes_destroy(other);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    {
        lxa_dom_ref_t document, html, body, text, comment, another, fragment;
        lxa_dom_ref_t before_html, after_html, doctype, extra_doctype;
        lxa_dom_doctype_data_t declaration, read_doctype;
        lxa_buffer_t content;
        TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_entropy(&allocator, &budget,
            fixed_entropy, &budget, NULL, &nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT,
                                               &document) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("html"),
                                                      &html) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("body"),
                                                      &body) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                   name("hello"), &text) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_COMMENT,
                                                   name("skip"), &comment) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_COMMENT,
                                                   name("before"), &before_html) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_COMMENT,
                                                   name("after"), &after_html) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("extra"),
                                                      &another) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(nodes,
            LXA_DOM_KIND_DOCUMENT_FRAGMENT, &fragment) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_element_name(nodes, html, &view) == LXA_OK);
        memset(&declaration, 0, sizeof(declaration));
        declaration.name = view; /* borrowed from the moving string buffer */
        declaration.public_id = name("public");
        declaration.system_id = name("system");
        declaration.has_public_id = declaration.has_system_id = true;
        declaration.force_quirks = true;
        budget.limits.max_name_bytes = 3;
        TEST_CHECK(lxa_dom_nodes_append_doctype(nodes, declaration, &doctype)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_NAME_BYTES);
        budget.limits.max_name_bytes = limits.max_name_bytes;
        budget.limits.max_string_bytes = budget.used[LXA_BUDGET_STRINGS];
        TEST_CHECK(lxa_dom_nodes_append_doctype(nodes, declaration, &doctype)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_STRINGS
                   && lxa_vector_length(&nodes->doctypes) == 0);
        budget.limits.max_string_bytes = limits.max_string_bytes;
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
        TEST_CHECK(lxa_dom_nodes_append_doctype(nodes, declaration, &doctype)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_append_doctype(nodes, declaration, &doctype)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_doctype_read(nodes, doctype, &read_doctype)
                   == LXA_OK);
        TEST_CHECK(read_doctype.name.length == 4
                   && memcmp(read_doctype.name.data, "html", 4) == 0
                   && read_doctype.has_public_id && read_doctype.has_system_id
                   && read_doctype.force_quirks
                   && read_doctype.public_id.length == 6
                   && memcmp(read_doctype.public_id.data, "public", 6) == 0);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document,
                                                           before_html) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document,
                                                           doctype) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document,
                                                           html) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document,
                                                           after_html) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document,
                                                           another) == LXA_ERROR_ARGUMENT);
        declaration.name = read_doctype.name;
        TEST_CHECK(lxa_dom_nodes_append_doctype(nodes, declaration,
                                                 &extra_doctype) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document,
                                                           extra_doctype)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body,
                                                           extra_doctype)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, fragment,
                                                           another) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, html,
                                                           body) == LXA_OK);
        budget.limits.max_tree_depth = 2;
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, text)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_TREE_DEPTH);
        budget.limits.max_tree_depth = limits.max_tree_depth;
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, text)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, text)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, comment)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, body, text)
                   == LXA_ERROR_ARGUMENT);
        {
            lxa_dom_ref_t second_document, second_root, new_comment, another_root;
            TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT,
                                                   &second_document) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("html"),
                                                          &second_root) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("html"),
                                                          &another_root) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_COMMENT,
                name("before"), &new_comment) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes,
                second_document, second_root) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes,
                second_document, extra_doctype, second_root) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes,
                second_document, another_root, extra_doctype) == LXA_ERROR_ARGUMENT);
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes,
                second_document, new_comment, extra_doctype) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_read(nodes, second_document, &record) == LXA_OK
                       && record.first_child == (lxa_id_t) new_comment.handle
                       && record.last_child == (lxa_id_t) second_root.handle);
            {
                lxa_dom_ref_t third_document, third_doctype;
                TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT,
                    &third_document) == LXA_OK);
                declaration.name = name("html");
                TEST_CHECK(lxa_dom_nodes_append_doctype(nodes, declaration,
                    &third_doctype) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes,
                    third_document, third_doctype) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes,
                    third_document, another_root, third_doctype)
                    == LXA_ERROR_ARGUMENT);
                TEST_CHECK(lxa_dom_nodes_read(nodes, another_root, &record)
                    == LXA_OK && record.parent == LXA_ID_NONE);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes,
                    third_document, another_root) == LXA_OK);
            }
        }
        TEST_CHECK(lxa_dom_nodes_read(nodes, document, &record) == LXA_OK
                   && record.first_child == (lxa_id_t) before_html.handle
                   && record.last_child == (lxa_id_t) after_html.handle);
        TEST_CHECK(lxa_dom_nodes_read(nodes, body, &record) == LXA_OK
                   && record.first_child == (lxa_id_t) text.handle
                   && record.last_child == (lxa_id_t) comment.handle);
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, document, &next)
                   == LXA_ERROR_LIMIT && next.handle == LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, document, &next)
                   == LXA_OK && next.handle == before_html.handle);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, next, &next)
                   == LXA_OK && next.handle == doctype.handle);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, next, &next)
                   == LXA_OK && next.handle == html.handle);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, next, &next)
                   == LXA_OK && next.handle == body.handle);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, next, &next)
                   == LXA_OK && next.handle == text.handle);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, next, &next)
                   == LXA_OK && next.handle == comment.handle);
        TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, next, &next)
                   == LXA_OK && next.handle == LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_first_child(nodes, text, &next)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_last_child(nodes, document, &next)
                   == LXA_OK && next.handle == after_html.handle);
        TEST_CHECK(lxa_dom_nodes_prev_sibling(nodes, next, &next)
                   == LXA_OK && next.handle == html.handle);
        TEST_CHECK(lxa_dom_nodes_parent(nodes, next, &next)
                   == LXA_OK && next.handle == document.handle);
        TEST_CHECK(lxa_dom_nodes_parent(nodes, document, &next)
                   == LXA_OK && next.handle == LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_prev_sibling(nodes, before_html, &next)
                   == LXA_OK && next.handle == LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        TEST_CHECK(lxa_buffer_init(&content, &allocator) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_text_content(nodes, html, &content, 16)
                   == LXA_OK);
        TEST_CHECK(content.length == 5
                   && memcmp(content.data, "hello", 5) == 0);
        lxa_buffer_destroy(&content);
        budget.limits.max_tree_depth = 2;
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(nodes, another, body)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_TREE_DEPTH);
        budget.limits.max_tree_depth = limits.max_tree_depth;
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(nodes, another, body)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_read(nodes, body, &record) == LXA_OK
                   && record.parent == (lxa_id_t) html.handle);
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(nodes, another, body)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(nodes, body, another)
                   == LXA_ERROR_ARGUMENT); /* would form a cycle */
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(nodes, another, comment)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_read(nodes, body, &record) == LXA_OK
                   && record.first_child == record.last_child
                   && record.first_child == (lxa_id_t) text.handle);
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(nodes, another, body)
                   == LXA_OK); /* move past the comment */
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(nodes, another, body)
                   == LXA_OK); /* already at tail */
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_before(nodes, body,
            comment, doctype) == LXA_ERROR_ARGUMENT);
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_before(nodes, body,
            comment, text) == LXA_ERROR_LIMIT);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_before(nodes, body,
            comment, text) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_read(nodes, body, &record) == LXA_OK
                   && record.first_child == (lxa_id_t) comment.handle
                   && record.last_child == (lxa_id_t) text.handle);
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_before(nodes, body,
            text, comment) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_before(nodes, body,
            text, text) == LXA_OK); /* unchanged */
        TEST_CHECK(lxa_dom_nodes_read(nodes, body, &record) == LXA_OK
                   && record.first_child == (lxa_id_t) text.handle
                   && record.last_child == (lxa_id_t) comment.handle);
        {
            lxa_dom_ref_t prefix;
            TEST_CHECK(lxa_dom_nodes_append_character(nodes,
                LXA_DOM_KIND_COMMENT, name("prefix"), &prefix) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes, document,
                prefix, body) == LXA_ERROR_ARGUMENT);
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes, body,
                prefix, doctype) == LXA_ERROR_ARGUMENT);
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes, body,
                prefix, text) == LXA_ERROR_LIMIT);
            budget.limits.max_work = limits.max_work;
            TEST_CHECK(lxa_dom_nodes_read(nodes, prefix, &record) == LXA_OK
                       && record.parent == LXA_ID_NONE);
            TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes, body,
                prefix, text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_read(nodes, body, &record) == LXA_OK
                       && record.first_child == (lxa_id_t) prefix.handle
                       && record.last_child == (lxa_id_t) comment.handle);
        }
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
    return 0;
}
