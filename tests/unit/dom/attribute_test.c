#include "tests/support/test_check.h"
#include <string.h>
#include "dom/node_store_internal.h"

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
    lxa_dom_nodes_t *nodes = NULL, *other = NULL;
    lxa_dom_ref_t first, next, host, class_attr, title, found, other_host;
    lxa_dom_ref_t replacement, middle;
    lxa_dom_record_t record;
    lxa_span_t key, val;
    uint8_t input[] = {'a', 0, 'b'};
    size_t i, before, old_capacity, clear_work, atom_count;
    uint64_t work_before, epoch_before;
    TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("data-x"),
        (lxa_span_t){input, sizeof(input)}, &first) == LXA_OK);
    input[0] = 'X';
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, first, &key, &val) == LXA_OK);
    TEST_CHECK(key.length == 6 && memcmp(key.data, "data-x", 6) == 0
               && val.length == 3 && memcmp(val.data, "a\0b", 3) == 0);
    TEST_CHECK(lxa_dom_nodes_read(nodes, first, &record) == LXA_OK);
    TEST_CHECK(record.kind == LXA_DOM_KIND_ATTRIBUTE
               && record.parent == LXA_ID_NONE);
    for (i = 0; i < 4; ++i) {
        old_capacity = nodes->strings.capacity;
        TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, key, val, &next)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, next, &key, &val) == LXA_OK);
        TEST_CHECK(memcmp(key.data, "data-x", key.length) == 0
                   && memcmp(val.data, "a\0b", val.length) == 0);
        if (i == 0 || i == 2) {
            TEST_CHECK(nodes->strings.capacity > old_capacity);
        }
    }
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    {
        lxa_dom_attr_namespace_t ns;
        lxa_span_t local;
        lxa_dom_ref_t foreign_attr;
        TEST_CHECK(lxa_dom_nodes_append_foreign_attribute(nodes,
            span("xlink:href"), span("#id"), LXA_DOM_ATTR_NS_XML,
            &foreign_attr) == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_append_foreign_attribute(nodes,
            span("xlink:href"), span("#id"), LXA_DOM_ATTR_NS_XLINK,
            &foreign_attr) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attribute_namespace(nodes, foreign_attr, &ns,
            &local) == LXA_OK && ns == LXA_DOM_ATTR_NS_XLINK
            && local.length == 4 && memcmp(local.data, "href", 4) == 0);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    }
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, span("div"), &host)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create(&allocator, &other) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(other, span("div"),
                                                  &other_host) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, other_host, next)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(other, other_host, next)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_find_attribute(other, host, span("class"),
                                             &found) == LXA_ERROR_ARGUMENT);
    lxa_dom_nodes_destroy(other);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, first) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, next)
               == LXA_ERROR_ARGUMENT); /* same (namespace, name) */
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("class"),
        span("some"), &class_attr) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, class_attr) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, class_attr)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("data-x"),
                                             &found) == LXA_OK
               && found.handle == first.handle);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("class"),
                                             &found) == LXA_OK
               && found.handle == class_attr.handle);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("missing"),
                                             &found) == LXA_OK
               && found.handle == LXA_HANDLE_NONE);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("title"),
        span("value"), &title) == LXA_OK);
    budget.limits.max_attributes_per_element = 2;
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, title)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes)
               == LXA_BUDGET_ATTRS_PER_ELEMENT);
    budget.limits.max_attributes_per_element = limits.max_attributes_per_element;
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + 1;
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, title)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, title) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("title"),
                                             &found) == LXA_OK
               && found.handle == title.handle);
    {
        lxa_dom_ref_t cursor = {0}, result;
        TEST_CHECK(lxa_dom_nodes_attribute_next(nodes, host, cursor, &result)
                   == LXA_OK && result.handle == first.handle);
        cursor = result;
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
        TEST_CHECK(lxa_dom_nodes_attribute_next(nodes, host, cursor, &result)
                   == LXA_ERROR_LIMIT && result.handle == LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_attribute_next(nodes, host, cursor, &result)
                   == LXA_OK && result.handle == class_attr.handle);
        cursor = result;
        TEST_CHECK(lxa_dom_nodes_attribute_next(nodes, host, cursor, &result)
                   == LXA_OK && result.handle == title.handle);
        cursor = result;
        TEST_CHECK(lxa_dom_nodes_attribute_next(nodes, host, cursor, &result)
                   == LXA_OK && result.handle == LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_attribute_next(nodes, first, cursor,
                                                 &result) == LXA_ERROR_ARGUMENT);
    }
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("title"),
        span("updated"), &replacement) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("role"),
        span("button"), &middle) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, class_attr,
                                                replacement) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, title, class_attr)
               == LXA_ERROR_ARGUMENT);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + 1;
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, title, replacement)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("title"), &found)
               == LXA_OK && found.handle == title.handle);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, first, middle)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("role"), &found)
               == LXA_OK && found.handle == middle.handle);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, middle, first)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, class_attr, middle)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, middle, class_attr)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, title, replacement)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, title, middle)
               == LXA_ERROR_ARGUMENT); /* title is detached */
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("title"), &found)
               == LXA_OK && found.handle == replacement.handle);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, host, replacement, title)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, first, &key, &val) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, first, val) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, first, &key, &val) == LXA_OK
               && val.length == 3 && memcmp(val.data, "a\0b", 3) == 0);
    budget.limits.max_string_bytes = budget.used[LXA_BUDGET_STRINGS];
    TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, first, span("new"))
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_STRINGS);
    budget.limits.max_string_bytes = limits.max_string_bytes;
    budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
    TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, first,
        span("a replacement value that exceeds the current buffer capacity"))
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_MEMORY);
    budget.limits.max_memory_bytes = limits.max_memory_bytes;
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, first, &key, &val) == LXA_OK
               && val.length == 3 && memcmp(val.data, "a\0b", 3) == 0);
    TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, first, span("new"))
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, first, &key, &val) == LXA_OK
               && val.length == 3 && memcmp(val.data, "new", 3) == 0);
    TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, middle, span(""))
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, middle, &key, &val) == LXA_OK
               && val.length == 0);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("data-x"), &found)
               == LXA_OK && found.handle == first.handle);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    before = lxa_dom_nodes_length(nodes);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("Bad"),
        span("v"), &next) == LXA_ERROR_UNSUPPORTED);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("a\"b"),
        span("v"), &next) == LXA_ERROR_UNSUPPORTED);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("id"),
        (lxa_span_t){NULL, 1}, &next) == LXA_ERROR_ARGUMENT);
    budget.limits.max_name_bytes = 1;
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("id"),
        span("v"), &next) == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_NAME_BYTES);
    budget.limits.max_name_bytes = limits.max_name_bytes;
    budget.limits.max_attributes = (uint32_t) budget.used[LXA_BUDGET_ATTRIBUTES];
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("id"),
        span("v"), &next) == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_ATTRIBUTES);
    budget.limits.max_attributes = limits.max_attributes;
    budget.limits.max_string_bytes = budget.used[LXA_BUDGET_STRINGS];
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("id"),
        span("v"), &next) == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_STRINGS);
    budget.limits.max_string_bytes = limits.max_string_bytes;
    TEST_CHECK(lxa_dom_nodes_length(nodes) == before);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, class_attr, title)
               == LXA_ERROR_ARGUMENT);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + 1;
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, host, class_attr)
               == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK);
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, host, class_attr)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("class"), &found)
               == LXA_OK && found.handle == LXA_HANDLE_NONE);
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, host, class_attr)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, host, first) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, host, title) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, class_attr) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, host, span("class"), &found)
               == LXA_OK && found.handle == class_attr.handle);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_table_clear_cost(nodes->atom_lookup, &clear_work) == LXA_OK);
    atom_count = lxa_vector_length(&nodes->atoms);
    TEST_CHECK(atom_count != 0);
    work_before = budget.used[LXA_BUDGET_WORK];
    epoch_before = nodes->epoch;
    budget.limits.max_work = work_before + clear_work;
    TEST_CHECK(lxa_dom_nodes_reset(nodes) == LXA_ERROR_LIMIT);
    TEST_CHECK(lxa_dom_nodes_limit_reason(nodes) == LXA_BUDGET_WORK
               && budget.used[LXA_BUDGET_WORK] == work_before
               && nodes->epoch == epoch_before
               && lxa_dom_nodes_length(nodes) == before
               && lxa_table_length(nodes->atom_lookup) == atom_count);
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, first, &key, &val) == LXA_OK
               && val.length == 3 && memcmp(val.data, "new", 3) == 0);
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    work_before = budget.used[LXA_BUDGET_WORK];
    TEST_CHECK(lxa_dom_nodes_reset(nodes) == LXA_OK);
    TEST_CHECK(budget.used[LXA_BUDGET_WORK] - work_before
               == clear_work + atom_count);
    TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, first, &key, &val)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, host, class_attr)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, span("id"),
        span("v"), &next) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    return 0;
}
