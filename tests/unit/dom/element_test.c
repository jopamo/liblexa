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

static void
check_option_state(lxa_dom_nodes_t *nodes, lxa_dom_ref_t option, uint16_t flags)
{
    lxa_dom_record_t record;
    TEST_CHECK(lxa_dom_nodes_read(nodes, option, &record) == LXA_OK
               && record.flags == flags);
}

static void
check_option_selected_attributes(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_budget_t budget;
    lxa_dom_nodes_t *nodes;
    lxa_dom_ref_t option, selected, replacement, title, clone, found;
    lxa_dom_record_t *records;
    uint64_t max_work;
    TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
    max_work = budget.limits.max_work;
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                               &nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &option)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("selected"),
        name("false"), &selected) == LXA_OK);
    check_option_state(nodes, option, 0);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, option, selected)
               == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK);
    check_option_state(nodes, option, 0);
    budget.limits.max_work = max_work;
    TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, option, name("selected"),
        &found) == LXA_OK && found.handle == LXA_HANDLE_NONE);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, option, selected) == LXA_OK);
    check_option_state(nodes, option, LXA_DOM_OPTION_SELECTED);
    TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, selected, name(""))
               == LXA_OK);
    check_option_state(nodes, option, LXA_DOM_OPTION_SELECTED);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("selected"),
        name("other"), &replacement) == LXA_OK);
    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    records[(lxa_id_t) option.handle].flags = 0; /* Model select reset. */
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, option, selected,
                                               replacement) == LXA_OK);
    check_option_state(nodes, option, 0);
    TEST_CHECK(lxa_dom_nodes_clone_element(nodes, option, &clone) == LXA_OK);
    check_option_state(nodes, clone, LXA_DOM_OPTION_SELECTED);
    TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("title"),
        name("text"), &title) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, option, replacement, title)
               == LXA_OK);
    check_option_state(nodes, option, 0);
    TEST_CHECK(lxa_dom_nodes_replace_attribute(nodes, option, title, selected)
               == LXA_OK);
    check_option_state(nodes, option, LXA_DOM_OPTION_SELECTED);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, option, selected)
               == LXA_ERROR_LIMIT);
    check_option_state(nodes, option, LXA_DOM_OPTION_SELECTED);
    budget.limits.max_work = max_work;
    TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, option, selected) == LXA_OK);
    check_option_state(nodes, option, 0);
    for (unsigned selectedness = 0; selectedness < 2; ++selectedness) {
        uint16_t flags = LXA_DOM_OPTION_DIRTY
            | (selectedness ? LXA_DOM_OPTION_SELECTED : 0);
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        records[(lxa_id_t) option.handle].flags = flags;
        TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, option, selected)
                   == LXA_OK);
        check_option_state(nodes, option, flags);
        TEST_CHECK(lxa_dom_nodes_clone_element(nodes, option, &clone) == LXA_OK);
        check_option_state(nodes, clone, LXA_DOM_OPTION_SELECTED);
        TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, option, selected)
                   == LXA_OK);
        check_option_state(nodes, option, flags);
    }
    for (unsigned foreign = 0; foreign < 2; ++foreign) {
        lxa_dom_ref_t host;
        TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes,
            name(foreign ? "option" : "div"),
            foreign ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML, &host) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, host, selected)
                   == LXA_OK);
        check_option_state(nodes, host, 0);
        TEST_CHECK(lxa_dom_nodes_detach_attribute(nodes, host, selected)
                   == LXA_OK);
    }
    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    records[(lxa_id_t) option.handle].flags = 0;
    TEST_CHECK(lxa_dom_nodes_append_foreign_attribute(nodes, name("xml:selected"),
        name(""), LXA_DOM_ATTR_NS_XML, &selected) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, option, selected) == LXA_OK);
    check_option_state(nodes, option, 0);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    records[(lxa_id_t) option.handle].flags = UINT16_MAX;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_ERROR_ARGUMENT);
    records[(lxa_id_t) option.handle].flags = 0;
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
}

static void
check_option_select_reset(void)
{
    static const struct {
        const char *size;
        bool fallback;
    } cases[] = {
        {NULL, true}, {"1", true}, {" \t+0001tail", true},
        {"invalid", true}, {"-1", true}, {"-0", false},
        {"0", false}, {"2", false}, {"999999999999999999999999", false}
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        lxa_budget_t budget;
        lxa_dom_nodes_t *nodes;
        lxa_dom_ref_t select, group, div, datalist, first, second, excluded, attr, text;
        lxa_dom_record_t *records;
        lxa_limits_t limits;
        size_t retained;
        bool success = false;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        limits = budget.limits;
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                   &nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                      &select) == LXA_OK);
        if (cases[c].size != NULL) {
            TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("size"),
                name(cases[c].size), &attr) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, select, attr)
                       == LXA_OK);
        }
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("optgroup"),
                                                      &group) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("disabled"),
            name("false"), &attr) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, group, attr) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, group)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &first) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, group, first)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("div"), &div)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, div)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
            name("text"), &text) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, div, text)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &second) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, div, second)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("datalist"),
                                                      &datalist) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, datalist)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &excluded) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, datalist,
                                                          excluded) == LXA_OK);
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        records[(lxa_id_t) excluded.handle].flags = LXA_DOM_OPTION_SELECTED;
        retained = budget.used[LXA_BUDGET_MEMORY];
        records[(lxa_id_t) first.handle].flags =
            records[(lxa_id_t) second.handle].flags = LXA_DOM_OPTION_SELECTED;
        budget.limits.max_memory_bytes = retained;
        TEST_CHECK(lxa_dom_nodes_reset_option_select(nodes, second)
                   == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_MEMORY);
        check_option_state(nodes, first, LXA_DOM_OPTION_SELECTED);
        check_option_state(nodes, second, LXA_DOM_OPTION_SELECTED);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
        budget.limits = limits;
        records[(lxa_id_t) first.handle].flags =
            records[(lxa_id_t) second.handle].flags = 0;
        for (size_t allowance = 0; allowance < 256; ++allowance) {
            lxa_status_t status;
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_dom_nodes_reset_option_select(nodes, second);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
            if (status == LXA_OK) { success = true; break; }
            TEST_CHECK(status == LXA_ERROR_LIMIT
                       && nodes->limit_reason == LXA_BUDGET_WORK);
            check_option_state(nodes, first, 0);
            check_option_state(nodes, second, 0);
            check_option_state(nodes, excluded, LXA_DOM_OPTION_SELECTED);
        }
        TEST_CHECK(success);
        budget.limits = limits;
        check_option_state(nodes, second,
                           cases[c].fallback ? LXA_DOM_OPTION_SELECTED : 0);
        TEST_CHECK(lxa_dom_nodes_reset_option_select(nodes, excluded) == LXA_OK);
        check_option_state(nodes, excluded, LXA_DOM_OPTION_SELECTED);
        records[(lxa_id_t) first.handle].flags =
            records[(lxa_id_t) second.handle].flags =
                LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
        TEST_CHECK(lxa_dom_nodes_reset_option_select(nodes, second) == LXA_OK);
        check_option_state(nodes, first, LXA_DOM_OPTION_DIRTY);
        check_option_state(nodes, second,
                           LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY);
        TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("multiple"),
            name("false"), &attr) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, select, attr) == LXA_OK);
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        records[(lxa_id_t) first.handle].flags |= LXA_DOM_OPTION_SELECTED;
        TEST_CHECK(lxa_dom_nodes_reset_option_select(nodes, first) == LXA_OK);
        check_option_state(nodes, first,
                           LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
    for (unsigned barrier = 0; barrier < 5; ++barrier) {
        static const char *tags[] = {"option", "hr", "datalist", "optgroup",
                                     "datalist"};
        lxa_dom_nodes_t *nodes;
        lxa_dom_ref_t select, parent, outer, option;
        TEST_CHECK(lxa_dom_nodes_create(&allocator, &nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                      &select) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes,
            name(tags[barrier]), barrier == 4 ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML,
            &parent) == LXA_OK);
        if (barrier == 3) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("optgroup"),
                                                          &outer) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select,
                                                               outer) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, outer,
                                                               parent) == LXA_OK);
        }
        else TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select,
                                                                parent) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &option) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, parent, option)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_reset_option_select(nodes, option) == LXA_OK);
        check_option_state(nodes, option, barrier == 4 ? LXA_DOM_OPTION_SELECTED : 0);
        TEST_CHECK(lxa_dom_nodes_reset_option_select(nodes, select)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_dom_nodes_destroy(nodes);
    }
}

static void
check_option_detach(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < 7; ++path) {
        lxa_budget_t budget;
        lxa_dom_nodes_t *nodes;
        lxa_dom_ref_t select, host, first, second, third, attr;
        lxa_dom_record_t before[5], after;
        lxa_dom_ref_t refs[5];
        lxa_dom_record_t *records;
        lxa_limits_t limits;
        uint16_t old_flags = path == 4 || path == 6 ? 0
            : LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
        uint16_t second_flags = path == 1 || path == 6 ? LXA_DOM_OPTION_SELECTED : 0;
        uint16_t third_flags = path == 1 ? LXA_DOM_OPTION_SELECTED : 0;
        size_t retained;
        bool success = false;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        limits = budget.limits;
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                   &nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                      &select) == LXA_OK);
        if (path == 2 || path == 3) {
            TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes,
                name(path == 2 ? "multiple" : "size"), name("0"), &attr) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, select, attr) == LXA_OK);
        }
        host = select;
        if (path == 5) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("datalist"),
                                                          &host) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, host)
                       == LXA_OK);
        }
        TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("option"),
            path == 4 ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML, &first) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, host, first)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &second) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, second)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &third) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, third)
                   == LXA_OK);
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        records[(lxa_id_t) first.handle].flags = old_flags;
        records[(lxa_id_t) second.handle].flags = second_flags;
        records[(lxa_id_t) third.handle].flags = third_flags;
        refs[0] = select; refs[1] = host; refs[2] = first;
        refs[3] = second; refs[4] = third;
        for (size_t i = 0; i < 5; ++i)
            TEST_CHECK(lxa_dom_nodes_read(nodes, refs[i], &before[i]) == LXA_OK);
        retained = budget.used[LXA_BUDGET_MEMORY];
        if (path == 1) {
            budget.limits.max_memory_bytes = retained;
            TEST_CHECK(lxa_dom_nodes_detach_constructed_element(nodes, first)
                       == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_MEMORY);
            for (size_t i = 0; i < 5; ++i)
                TEST_CHECK(lxa_dom_nodes_read(nodes, refs[i], &after) == LXA_OK
                           && memcmp(&after, &before[i], sizeof(after)) == 0);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
            budget.limits = limits;
        }
        for (size_t allowance = 0; allowance < 256; ++allowance) {
            lxa_status_t status;
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_dom_nodes_detach_constructed_element(nodes, first);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
            if (status == LXA_OK) { success = true; break; }
            TEST_CHECK(status == LXA_ERROR_LIMIT
                       && nodes->limit_reason == LXA_BUDGET_WORK);
            for (size_t i = 0; i < 5; ++i)
                TEST_CHECK(lxa_dom_nodes_read(nodes, refs[i], &after) == LXA_OK
                           && memcmp(&after, &before[i], sizeof(after)) == 0);
        }
        TEST_CHECK(success);
        budget.limits = limits;
        check_option_state(nodes, first, old_flags);
        check_option_state(nodes, second,
            path == 0 || path == 6 ? LXA_DOM_OPTION_SELECTED : 0);
        check_option_state(nodes, third, third_flags);
        TEST_CHECK(lxa_dom_nodes_read(nodes, first, &after) == LXA_OK
                   && after.parent == LXA_ID_NONE && after.prev == LXA_ID_NONE
                   && after.next == LXA_ID_NONE);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
}

static void
check_option_move(void)
{
    enum {
        APPEND, BEFORE, NESTED_BEFORE, NESTED_AFTER, DISABLED_GROUP,
        SAME_OWNER, BLOCKED, FOREIGN, UNOWNED, TEXT_BEFORE, MULTIPLE, CASE_COUNT
    };
    const uint16_t chosen = LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < CASE_COUNT; ++path) {
        lxa_budget_t budget;
        lxa_dom_nodes_t *nodes;
        lxa_dom_ref_t old_select, new_select, old_host, parent, moved, remaining;
        lxa_dom_ref_t existing, text, attribute, before_ref = {0};
        lxa_dom_record_t before[16], *records;
        lxa_limits_t limits;
        uint16_t moved_flags = path == FOREIGN ? 0
            : path == DISABLED_GROUP || path == UNOWNED || path == TEXT_BEFORE
                ? LXA_DOM_OPTION_DIRTY : chosen;
        uint16_t old_flags = path == SAME_OWNER || path == DISABLED_GROUP
            || path == TEXT_BEFORE ? chosen : 0;
        uint16_t new_flags = path == FOREIGN || path == DISABLED_GROUP
            || path == UNOWNED || path == TEXT_BEFORE ? 0 : chosen;
        size_t retained, count;
        bool success = false;
        bool coalescing = path <= NESTED_AFTER;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        limits = budget.limits;
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                   &nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                      &old_select) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                      &new_select) == LXA_OK);
        old_host = old_select;
        if (path == UNOWNED) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("datalist"),
                                                          &old_host) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, old_select,
                                                               old_host) == LXA_OK);
        }
        TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("option"),
            path == FOREIGN ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML, &moved) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, old_host, moved)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &remaining) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, old_select,
                                                          remaining) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &existing) == LXA_OK);
        if (path == NESTED_AFTER)
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, new_select,
                                                              existing) == LXA_OK);
        parent = new_select;
        if (path == NESTED_BEFORE || path == NESTED_AFTER || path == SAME_OWNER
            || path == DISABLED_GROUP || path == BLOCKED) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes,
                name(path == DISABLED_GROUP ? "optgroup" : path == BLOCKED
                     ? "datalist" : "div"), &parent) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes,
                path == SAME_OWNER ? old_select : new_select, parent) == LXA_OK);
        }
        TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
            name("text"), &text) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, parent, text)
                   == LXA_OK);
        if (path != NESTED_AFTER && path != UNOWNED)
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, new_select,
                                                              existing) == LXA_OK);
        if (path == DISABLED_GROUP || path == MULTIPLE) {
            TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes,
                name(path == MULTIPLE ? "multiple" : "disabled"), name("false"),
                &attribute) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, parent, attribute)
                       == LXA_OK);
        }
        if (path == BEFORE) before_ref = existing;
        if (path == TEXT_BEFORE) before_ref = text;
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        records[(lxa_id_t) moved.handle].flags = moved_flags;
        records[(lxa_id_t) remaining.handle].flags = old_flags;
        records[(lxa_id_t) existing.handle].flags = new_flags;
        count = lxa_dom_nodes_length(nodes);
        TEST_CHECK(count <= sizeof(before) / sizeof(before[0]));
        memcpy(before, records, count * sizeof(before[0]));
        retained = budget.used[LXA_BUDGET_MEMORY];
        if (coalescing) {
            lxa_status_t status;
            budget.limits.max_memory_bytes = retained;
            status = before_ref.handle == LXA_HANDLE_NONE
                ? lxa_dom_nodes_reparent_constructed_child(nodes, parent, moved)
                : lxa_dom_nodes_reparent_constructed_before(nodes, parent, moved,
                                                             before_ref);
            TEST_CHECK(status == LXA_ERROR_LIMIT
                       && nodes->limit_reason == LXA_BUDGET_MEMORY
                       && memcmp(records, before, count * sizeof(before[0])) == 0);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
            budget.limits = limits;
        }
        for (size_t allowance = 0; allowance < 512; ++allowance) {
            lxa_status_t status;
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = before_ref.handle == LXA_HANDLE_NONE
                ? lxa_dom_nodes_reparent_constructed_child(nodes, parent, moved)
                : lxa_dom_nodes_reparent_constructed_before(nodes, parent, moved,
                                                             before_ref);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
            if (status == LXA_OK) { success = true; break; }
            TEST_CHECK(status == LXA_ERROR_LIMIT
                       && nodes->limit_reason == LXA_BUDGET_WORK
                       && memcmp(records, before, count * sizeof(before[0])) == 0);
        }
        TEST_CHECK(success);
        budget.limits = limits;
        check_option_state(nodes, moved, path == BEFORE || path == NESTED_BEFORE
            ? LXA_DOM_OPTION_DIRTY : path == UNOWNED || path == TEXT_BEFORE
                ? chosen : moved_flags);
        check_option_state(nodes, remaining, path == SAME_OWNER
            || path == DISABLED_GROUP || path == TEXT_BEFORE ? old_flags
                : path == FOREIGN || path == UNOWNED ? 0 : LXA_DOM_OPTION_SELECTED);
        check_option_state(nodes, existing, path == APPEND || path == NESTED_AFTER
            ? LXA_DOM_OPTION_DIRTY : path == DISABLED_GROUP
                ? LXA_DOM_OPTION_SELECTED : new_flags);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
}

static void
check_container_option_detach(void)
{
    static const struct {
        const char *root, *inner;
        bool outer_group, foreign_root, foreign_option, reset, empty, multiple;
    } cases[] = {
        {"div", NULL, false, false, false, true, false, false},
        {"div", "span", false, false, false, true, false, false},
        {"optgroup", NULL, false, false, false, true, false, false},
        {"div", NULL, true, false, false, true, false, false},
        {"optgroup", NULL, true, false, false, false, false, false},
        {"div", "optgroup", false, false, false, true, false, false},
        {"div", "optgroup", true, false, false, false, false, false},
        {"datalist", NULL, false, false, false, false, false, false},
        {"hr", NULL, false, false, false, false, false, false},
        {"option", NULL, false, false, false, true, false, false},
        {"select", NULL, false, false, false, false, false, false},
        {"div", "select", false, false, false, false, false, false},
        {"div", "datalist", false, false, false, false, false, false},
        {"div", NULL, false, false, true, false, false, false},
        {"datalist", NULL, false, true, false, true, false, false},
        {"div", NULL, false, false, false, false, true, false},
        {"div", NULL, false, false, false, true, false, true}
    };
    const uint16_t chosen = LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
    for (size_t path = 0; path < sizeof(cases) / sizeof(cases[0]); ++path) {
        lxa_allocator_t allocator = lxa_allocator_default();
        lxa_budget_t budget;
        lxa_limits_t limits;
        lxa_dom_nodes_t *nodes;
        lxa_dom_ref_t select, root, host, inside[2], remaining[2], attr;
        lxa_dom_record_t before[16], *records, detached;
        bool coalescing = path == 0 || !cases[path].reset, success = false;
        size_t count, retained;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        limits = budget.limits;
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                    &nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                       &select) == LXA_OK);
        host = select;
        if (cases[path].outer_group) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("optgroup"),
                                                          &host) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, host)
                       == LXA_OK);
        }
        TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes,
            name(cases[path].root), cases[path].foreign_root
                ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML, &root) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, host, root) == LXA_OK);
        host = root;
        if (cases[path].inner != NULL) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes,
                name(cases[path].inner), &host) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, root, host)
                       == LXA_OK);
        }
        for (size_t i = 0; i < 2; ++i) {
            TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("option"),
                cases[path].foreign_option ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML,
                &inside[i]) == LXA_OK);
            if (!cases[path].empty)
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, host,
                                                                  inside[i]) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                         &remaining[i]) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select,
                                                              remaining[i]) == LXA_OK);
        }
        if (cases[path].multiple) {
            TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("multiple"),
                                                          name(""), &attr) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, select, attr) == LXA_OK);
        }
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        for (size_t i = 0; i < 2; ++i) {
            records[(lxa_id_t) inside[i].handle].flags =
                cases[path].foreign_option ? 0 : chosen;
            records[(lxa_id_t) remaining[i].handle].flags = coalescing ? chosen : 0;
        }
        count = lxa_dom_nodes_length(nodes);
        TEST_CHECK(count <= sizeof(before) / sizeof(before[0]));
        memcpy(before, records, count * sizeof(before[0]));
        retained = budget.used[LXA_BUDGET_MEMORY];
        budget.limits.max_memory_bytes = retained;
        if (coalescing && cases[path].reset) {
            TEST_CHECK(lxa_dom_nodes_detach_constructed_element(nodes, root)
                       == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_MEMORY);
            TEST_CHECK(memcmp(records, before, count * sizeof(before[0])) == 0
                       && budget.used[LXA_BUDGET_MEMORY] == retained);
            budget.limits = limits;
        }
        for (size_t allowance = 0; allowance < 512; ++allowance) {
            lxa_status_t status;
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_dom_nodes_detach_constructed_element(nodes, root);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
            if (status == LXA_OK) { success = true; break; }
            TEST_CHECK(status == LXA_ERROR_LIMIT
                       && nodes->limit_reason == LXA_BUDGET_WORK
                       && memcmp(records, before, count * sizeof(before[0])) == 0);
        }
        TEST_CHECK(success);
        budget.limits = limits;
        for (size_t i = 0; i < 2; ++i) {
            check_option_state(nodes, inside[i], cases[path].foreign_option ? 0 : chosen);
            check_option_state(nodes, remaining[i], !cases[path].reset
                ? chosen : cases[path].multiple ? 0 : coalescing
                    ? (i == 0 ? LXA_DOM_OPTION_DIRTY : chosen)
                    : (i == 0 ? LXA_DOM_OPTION_SELECTED : 0));
        }
        TEST_CHECK(lxa_dom_nodes_read(nodes, root, &detached) == LXA_OK
                   && detached.parent == LXA_ID_NONE && detached.prev == LXA_ID_NONE
                   && detached.next == LXA_ID_NONE);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
}

static void
check_container_option_move(void)
{
    enum {
        APPEND, BEFORE, OLD_BLOCKED, NEW_BLOCKED, SAME_OWNER, GAIN_GROUP,
        LOSE_GROUP, INNER_SELECT, FOREIGN_ROOT, DISABLED_DEST, DUAL_COALESCE,
        UNCHANGED_GROUP, NESTED_DEST, CASE_COUNT
    };
    const uint16_t chosen = LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
    for (unsigned path = 0; path < CASE_COUNT; ++path) {
        lxa_allocator_t allocator = lxa_allocator_default();
        lxa_budget_t budget;
        lxa_limits_t limits;
        lxa_dom_nodes_t *nodes;
        lxa_dom_ref_t old_select, new_select, old_host, new_host, root, inner;
        lxa_dom_ref_t options[2], remaining[2], existing, attribute;
        lxa_dom_record_t before[20], *records, moved;
        bool same_select = path == SAME_OWNER || path == GAIN_GROUP
            || path == LOSE_GROUP || path == UNCHANGED_GROUP;
        bool success = false;
        size_t count, retained;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        limits = budget.limits;
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                    &nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                      &old_select) == LXA_OK);
        new_select = old_select;
        if (!same_select)
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                          &new_select) == LXA_OK);
        old_host = old_select;
        if (path == OLD_BLOCKED || path == GAIN_GROUP) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes,
                name(path == OLD_BLOCKED ? "datalist" : "optgroup"), &old_host) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, old_select,
                                                              old_host) == LXA_OK);
        }
        new_host = new_select;
        if (path == NEW_BLOCKED || path == LOSE_GROUP || path == DISABLED_DEST
            || path == UNCHANGED_GROUP || path == NESTED_DEST) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes,
                name(path == NEW_BLOCKED ? "datalist" : path == NESTED_DEST
                    ? "div" : "optgroup"), &new_host) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, new_select,
                                                              new_host) == LXA_OK);
        }
        if (path == DISABLED_DEST) {
            TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("disabled"),
                                                           name(""), &attribute) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, new_host, attribute)
                       == LXA_OK);
        }
        TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes,
            name(path == FOREIGN_ROOT ? "datalist" : "div"),
            path == FOREIGN_ROOT ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML, &root) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, old_host, root) == LXA_OK);
        inner = root;
        if (path == GAIN_GROUP || path == LOSE_GROUP || path == INNER_SELECT) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes,
                name(path == INNER_SELECT ? "select" : "optgroup"), &inner) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, root, inner) == LXA_OK);
        }
        for (size_t i = 0; i < 2; ++i) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                         &options[i]) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, inner,
                                                              options[i]) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                         &remaining[i]) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, old_select,
                                                              remaining[i]) == LXA_OK);
        }
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                      &existing) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, new_select,
                                                          existing) == LXA_OK);
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        for (size_t i = 0; i < 2; ++i) {
            records[(lxa_id_t) options[i].handle].flags =
                path == DISABLED_DEST ? LXA_DOM_OPTION_DIRTY : chosen;
            records[(lxa_id_t) remaining[i].handle].flags =
                path == DUAL_COALESCE ? chosen : 0;
        }
        records[(lxa_id_t) existing.handle].flags = path == DISABLED_DEST ? 0 : chosen;
        count = lxa_dom_nodes_length(nodes);
        TEST_CHECK(count <= sizeof(before) / sizeof(before[0]));
        memcpy(before, records, count * sizeof(before[0]));
        retained = budget.used[LXA_BUDGET_MEMORY];
        if (path == DUAL_COALESCE) {
            lxa_vector_t candidate;
            lxa_id_t id = (lxa_id_t) existing.handle;
            size_t candidate_memory;
            TEST_CHECK(lxa_vector_init(&candidate, &allocator, sizeof(id)) == LXA_OK);
            TEST_CHECK(lxa_vector_bind_budget(&candidate, &budget) == LXA_OK);
            TEST_CHECK(lxa_vector_push(&candidate, &id) == LXA_OK
                       && lxa_vector_push(&candidate, &id) == LXA_OK);
            candidate_memory = budget.used[LXA_BUDGET_MEMORY] - retained;
            TEST_CHECK(candidate_memory != 0);
            lxa_vector_destroy(&candidate);
            for (size_t i = 0; i < 2; ++i) {
                budget.limits.max_memory_bytes = retained + i * candidate_memory;
                TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(nodes, new_host, root)
                           == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_MEMORY);
                TEST_CHECK(memcmp(records, before, count * sizeof(before[0])) == 0
                           && budget.used[LXA_BUDGET_MEMORY] == retained);
            }
            budget.limits = limits;
        }
        for (size_t allowance = 0; allowance < 1024; ++allowance) {
            lxa_status_t status;
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = path == BEFORE
                ? lxa_dom_nodes_reparent_constructed_before(nodes, new_host, root, existing)
                : lxa_dom_nodes_reparent_constructed_child(nodes, new_host, root);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
            if (status == LXA_OK) { success = true; break; }
            TEST_CHECK(status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK
                       && memcmp(records, before, count * sizeof(before[0])) == 0);
        }
        TEST_CHECK(success);
        budget.limits = limits;
        for (size_t i = 0; i < 2; ++i) {
            bool coalesce_new = path == APPEND || path == BEFORE || path == OLD_BLOCKED
                || path == GAIN_GROUP || path == FOREIGN_ROOT || path == DUAL_COALESCE
                || path == NESTED_DEST;
            check_option_state(nodes, options[i], path == DISABLED_DEST
                ? LXA_DOM_OPTION_DIRTY : coalesce_new
                    && (path == BEFORE || path == NESTED_DEST || i == 0)
                    ? LXA_DOM_OPTION_DIRTY : chosen);
            check_option_state(nodes, remaining[i], path == DUAL_COALESCE
                ? (i == 0 ? LXA_DOM_OPTION_DIRTY : chosen)
                : path == APPEND || path == BEFORE || path == NEW_BLOCKED
                    || path == FOREIGN_ROOT || path == DISABLED_DEST || path == NESTED_DEST
                        ? (i == 0 ? LXA_DOM_OPTION_SELECTED : 0) : 0);
        }
        check_option_state(nodes, existing, path == APPEND || path == OLD_BLOCKED
            || path == GAIN_GROUP || path == FOREIGN_ROOT || path == DUAL_COALESCE
                ? LXA_DOM_OPTION_DIRTY
                : path == DISABLED_DEST ? LXA_DOM_OPTION_SELECTED : chosen);
        TEST_CHECK(lxa_dom_nodes_read(nodes, root, &moved) == LXA_OK
                   && moved.parent == (lxa_id_t) new_host.handle);
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
}

static void
check_selectedcontent_connection(void)
{
    static const struct {
        const char *outer, *inner;
        lxa_dom_namespace_t ns;
        bool multiple, disabled, primary;
    } cases[] = {
        {"div", NULL, LXA_DOM_NS_HTML, false, false, false},
        {"option", NULL, LXA_DOM_NS_HTML, false, true, false},
        {"selectedcontent", NULL, LXA_DOM_NS_HTML, false, true, false},
        {"select", "select", LXA_DOM_NS_HTML, false, true, false},
        {"option", NULL, LXA_DOM_NS_SVG, false, false, false},
        {"selectedcontent", NULL, LXA_DOM_NS_SVG, false, false, false},
        {"select", NULL, LXA_DOM_NS_SVG, false, false, false},
        {"select", NULL, LXA_DOM_NS_HTML, true, false, false},
        {"select", NULL, LXA_DOM_NS_HTML, false, false, true},
        {"option", "select", LXA_DOM_NS_HTML, false, true, false}
    };
    for (size_t path = 0; path < sizeof(cases) / sizeof(cases[0]); ++path) {
        lxa_allocator_t allocator = lxa_allocator_default();
        lxa_budget_t budget;
        lxa_limits_t limits;
        lxa_dom_nodes_t *nodes;
        lxa_dom_ref_t parent, host, element, clone, attribute;
        lxa_dom_record_t before, after, *records;
        bool success = false, disabled;
        size_t retained;
        TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        limits = budget.limits;
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                    &nodes) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name(cases[path].outer),
                                                          cases[path].ns, &parent) == LXA_OK);
        host = parent;
        if (cases[path].inner != NULL) {
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name(cases[path].inner),
                                                        &host) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, parent, host) == LXA_OK);
        }
        if (cases[path].multiple) {
            TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("multiple"),
                                                        name("false"), &attribute) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, parent, attribute) == LXA_OK);
        }
        TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"),
                                                    &element) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, host, element) == LXA_OK);
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        records[(lxa_id_t) element.handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
        TEST_CHECK(lxa_dom_nodes_read(nodes, element, &before) == LXA_OK);
        retained = budget.used[LXA_BUDGET_MEMORY];
        budget.limits.max_memory_bytes = retained;
        for (size_t allowance = 0; allowance < 256; ++allowance) {
            lxa_status_t status;
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_dom_nodes_connect_selectedcontent(nodes, element);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
            if (status == LXA_OK) {
                success = true;
                break;
            }
            TEST_CHECK(status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK);
            TEST_CHECK(lxa_dom_nodes_read(nodes, element, &after) == LXA_OK);
            if (cases[path].primary) {
                TEST_CHECK(after.flags == 0 || after.flags == before.flags);
                after.flags = before.flags;
            }
            TEST_CHECK(memcmp(&before, &after, sizeof(before)) == 0);
        }
        TEST_CHECK(success);
        budget.limits = limits;
        check_option_state(nodes, element, cases[path].disabled
            ? LXA_DOM_SELECTEDCONTENT_DISABLED : 0);
        if (cases[path].primary)
            TEST_CHECK(lxa_dom_nodes_prepare_selectedcontent(nodes, host, &disabled)
                == LXA_ERROR_UNSUPPORTED && !disabled);
        TEST_CHECK(lxa_dom_nodes_clone_element(nodes, element, &clone) == LXA_OK);
        check_option_state(nodes, clone, 0);
        TEST_CHECK(lxa_dom_nodes_connect_selectedcontent(nodes, host)
            == (path == 2 ? LXA_OK : LXA_ERROR_ARGUMENT));
        TEST_CHECK(lxa_dom_nodes_prepare_selectedcontent(nodes, host, NULL)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_prepare_selectedcontent(nodes, (lxa_dom_ref_t){0},
                                                         &disabled) == LXA_OK && !disabled);
        records = (lxa_dom_record_t *) nodes->records.bytes.data;
        records[(lxa_id_t) clone.handle].flags = LXA_DOM_OPTION_SELECTED;
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_connect_selectedcontent(nodes, clone) == LXA_ERROR_ARGUMENT);
        records[(lxa_id_t) clone.handle].flags = 0;
        TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
        lxa_dom_nodes_destroy(nodes);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
}

static void
check_option_pop_clone(void)
{
    enum {
        CONNECTED, INERT, EMPTY, UNSELECTED, MULTIPLE, FIRST_DISABLED,
        BARRIER, FOREIGN_OPTION, NON_OPTION, NO_TARGET, FIRST_IN_OPTION, CASE_COUNT
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < CASE_COUNT; ++path) {
        bool success = false;
        for (size_t allowance = 0; allowance < 1024 && !success; ++allowance) {
            lxa_budget_t budget;
            lxa_limits_t limits;
            lxa_dom_nodes_t *nodes;
            lxa_dom_ref_t document, container, select, parent, option, target, old;
            lxa_dom_ref_t text = {0}, nested = {0}, foreign = {0}, templated = {0};
            lxa_dom_ref_t fragment, child, other, other_text, attribute, first;
            lxa_dom_record_t before[64], record, *records;
            size_t count;
            bool copies = path == CONNECTED || path == INERT || path == EMPTY;
            lxa_status_t status;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            limits = budget.limits;
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                        &nodes) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT,
                                                &document) == LXA_OK);
            container = document;
            if (path == INERT) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("template"),
                                                            &child) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document, child)
                           == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                                     &container) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_bind_template(nodes, child, container) == LXA_OK);
            }
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                        &select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, container, select)
                       == LXA_OK);
            if (path == MULTIPLE) {
                TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("multiple"),
                                                             name("false"), &attribute) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, select, attribute) == LXA_OK);
            }
            if (path == FIRST_IN_OPTION) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &option)
                           == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, option)
                           == LXA_OK);
            }
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"),
                                                        &target) == LXA_OK);
            if (path != NO_TARGET)
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, target)
                           == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                      name("old"), &old) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, target, old) == LXA_OK);
            parent = select;
            if (path == BARRIER) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("datalist"),
                                                            &parent) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, parent)
                           == LXA_OK);
            }
            if (path != FIRST_IN_OPTION) {
                TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes,
                    name(path == NON_OPTION ? "div" : "option"),
                    path == FOREIGN_OPTION ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML, &option) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, parent, option) == LXA_OK);
            }
            if (path != EMPTY) {
                TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                         name("x"), &text) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, option, text) == LXA_OK);
            }
            if (path == CONNECTED || path == INERT || path == FIRST_IN_OPTION) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"),
                                                            &nested) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, option, nested)
                           == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                         name("nested"), &child) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, nested, child)
                           == LXA_OK);
                if (path != INERT)
                    TEST_CHECK(lxa_dom_nodes_connect_selectedcontent(nodes, nested) == LXA_OK);
            }
            if (path == CONNECTED || path == INERT) {
                TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("selectedcontent"),
                                                                  LXA_DOM_NS_SVG, &foreign) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, option, foreign)
                           == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("template"),
                                                            &templated) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, option, templated)
                           == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                                     &fragment) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_bind_template(nodes, templated, fragment) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"),
                                                            &child) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, fragment, child)
                           == LXA_OK);
            }
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"),
                                                        &other) == LXA_OK);
            if (path != NO_TARGET)
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, other) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                      name("other"), &other_text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, other, other_text) == LXA_OK);
            records = (lxa_dom_record_t *) nodes->records.bytes.data;
            if (path != FOREIGN_OPTION && path != NON_OPTION)
                records[(lxa_id_t) option.handle].flags = LXA_DOM_OPTION_DIRTY
                    | (path == UNSELECTED ? 0 : LXA_DOM_OPTION_SELECTED);
            if (path == FIRST_DISABLED)
                records[(lxa_id_t) target.handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
            count = lxa_dom_nodes_length(nodes);
            TEST_CHECK(count <= sizeof(before) / sizeof(before[0]));
            memcpy(before, records, count * sizeof(before[0]));
            if (allowance == 0 && path != EMPTY) {
                budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
                status = lxa_dom_nodes_maybe_clone_selected_option(nodes, option);
                TEST_CHECK(status == (copies ? LXA_ERROR_LIMIT : LXA_OK));
                if (copies)
                    TEST_CHECK(nodes->limit_reason == LXA_BUDGET_MEMORY);
                budget.limits = limits;
            }
            if (!success) {
                budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
                status = lxa_dom_nodes_maybe_clone_selected_option(nodes, option);
                success = status == LXA_OK;
                if (!success)
                    TEST_CHECK(status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK);
                budget.limits = limits;
            }
            for (size_t i = 0; i < count; ++i) {
                lxa_dom_ref_t ref = {.document = nodes, .epoch = nodes->epoch};
                if (success && copies
                    && (i == (lxa_id_t) target.handle || i == (lxa_id_t) old.handle))
                    continue;
                TEST_CHECK(lxa_handle_pack((lxa_id_t) i, nodes->generation,
                                           &ref.handle) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_read(nodes, ref, &record) == LXA_OK
                           && memcmp(&record, &before[i], sizeof(record)) == 0);
            }
            if (success && copies) {
                TEST_CHECK(lxa_dom_nodes_read(nodes, old, &record) == LXA_OK
                           && record.parent == LXA_ID_NONE && record.next == LXA_ID_NONE
                           && record.prev == LXA_ID_NONE);
                TEST_CHECK(lxa_dom_nodes_first_child(nodes, target, &first) == LXA_OK);
                if (path == EMPTY) TEST_CHECK(first.handle == LXA_HANDLE_NONE);
                else {
                    lxa_span_t bytes;
                    lxa_dom_ref_t clone;
                    TEST_CHECK(first.handle != text.handle
                        && lxa_dom_nodes_character_read(nodes, first, &bytes) == LXA_OK
                        && bytes.length == 1 && bytes.data[0] == 'x');
                    TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, first, &clone) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_read(nodes, clone, &record) == LXA_OK
                        && record.flags == (path == CONNECTED ? LXA_DOM_SELECTEDCONTENT_DISABLED : 0));
                    TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, clone, &clone) == LXA_OK);
                    check_option_state(nodes, clone, 0);
                    TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, clone, &clone) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_template_contents(nodes, clone, &fragment) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_first_child(nodes, fragment, &clone) == LXA_OK);
                    check_option_state(nodes, clone, 0);
                }
                TEST_CHECK(lxa_dom_nodes_first_child(nodes, other, &first) == LXA_OK
                           && first.handle == other_text.handle);
            }
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        }
        TEST_CHECK(success);
    }
}

static void
check_option_clone_select_resets(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < 6; ++path) {
        bool success = false;
        for (size_t allowance = 0; allowance < 4096 && !success; ++allowance) {
            lxa_budget_t budget;
            lxa_limits_t limits;
            lxa_dom_nodes_t *nodes;
            lxa_dom_ref_t document, select, owner, option, target, old = {0};
            lxa_dom_ref_t retained = {0}, child = {0}, nested = {0}, first, second;
            lxa_dom_ref_t extra = {0}, leading = {0}, trailing = {0};
            lxa_dom_ref_t attribute, clone;
            lxa_dom_record_t before[48], record, *records;
            lxa_status_t status;
            size_t count;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            limits = budget.limits;
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                        &nodes) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT,
                                                 &document) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                        &select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document, select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                        &option) == LXA_OK);
            owner = select;
            if (path == 0) {
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, option) == LXA_OK);
            }
            else {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"),
                                                             &owner) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, owner) == LXA_OK);
            }
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"),
                                                         &target) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, owner, target) == LXA_OK);
            if (path != 0) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &old) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, target, old) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"),
                                                             &retained) == LXA_OK);
                TEST_CHECK((path == 3 || path == 5
                    ? lxa_dom_nodes_insert_constructed_before(nodes, owner, retained, target)
                    : lxa_dom_nodes_append_constructed_child(nodes, owner, retained)) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, option) == LXA_OK);
            }
            if (path >= 3) {
                if (path == 5)
                    TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT,
                                                             name("old"), &leading) == LXA_OK);
                else TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &leading) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes, target, leading, old) == LXA_OK);
                if (path == 4) {
                    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &trailing) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, target, trailing) == LXA_OK);
                }
                else {
                    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &extra) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_insert_constructed_before(nodes, owner, extra, target) == LXA_OK);
                }
            }
            if (path < 2) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &child) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, option, child) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("selected"),
                                                              name("false"), &attribute) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, child, attribute) == LXA_OK);
            }
            if (path == 0) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"), &nested) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, option, nested) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &first) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, nested, first) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("selected"),
                                                              name(""), &attribute) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, first, attribute) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &second) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, nested, second) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("selected"),
                                                              name(""), &attribute) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, second, attribute) == LXA_OK);
                records = (lxa_dom_record_t *) nodes->records.bytes.data;
                records[(lxa_id_t) first.handle].flags = LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
                records[(lxa_id_t) second.handle].flags = LXA_DOM_OPTION_DIRTY;
            }
            records = (lxa_dom_record_t *) nodes->records.bytes.data;
            records[(lxa_id_t) option.handle].flags = LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
            if (path != 0) records[(lxa_id_t) old.handle].flags = LXA_DOM_OPTION_SELECTED;
            if (path == 3 || path == 5) {
                records[(lxa_id_t) retained.handle].flags = LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
                records[(lxa_id_t) extra.handle].flags = LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY;
            }
            count = lxa_dom_nodes_length(nodes);
            TEST_CHECK(count <= sizeof(before) / sizeof(before[0]));
            memcpy(before, records, count * sizeof(before[0]));
            if (allowance == 0 && path != 2) {
                budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
                TEST_CHECK(lxa_dom_nodes_maybe_clone_selected_option(nodes, option) == LXA_ERROR_LIMIT
                           && nodes->limit_reason == LXA_BUDGET_MEMORY);
                budget.limits = limits;
            }
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_dom_nodes_maybe_clone_selected_option(nodes, option);
            success = status == LXA_OK;
            if (!success)
                TEST_CHECK(status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK);
            budget.limits = limits;
            for (size_t i = 0; i < count; ++i) {
                lxa_dom_ref_t ref = {.document = nodes, .epoch = nodes->epoch};
                if (success && (i == (lxa_id_t) target.handle
                    || (path == 0 && i == (lxa_id_t) option.handle)
                    || (path != 0 && (i == (lxa_id_t) old.handle
                                     || i == (lxa_id_t) retained.handle))
                    || (leading.handle != LXA_HANDLE_NONE && i == (lxa_id_t) leading.handle)
                    || (trailing.handle != LXA_HANDLE_NONE && i == (lxa_id_t) trailing.handle)
                    || (extra.handle != LXA_HANDLE_NONE && i == (lxa_id_t) extra.handle))) continue;
                TEST_CHECK(lxa_handle_pack((lxa_id_t) i, nodes->generation, &ref.handle) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_read(nodes, ref, &record) == LXA_OK
                           && memcmp(&record, &before[i], sizeof(record)) == 0);
            }
            if (success) {
                check_option_state(nodes, option, LXA_DOM_OPTION_DIRTY
                    | (path == 0 ? 0 : LXA_DOM_OPTION_SELECTED));
                TEST_CHECK(lxa_dom_nodes_first_child(nodes, target, &clone) == LXA_OK);
                if (path >= 2) TEST_CHECK(clone.handle == LXA_HANDLE_NONE);
                else check_option_state(nodes, clone, path == 0 ? LXA_DOM_OPTION_SELECTED : 0);
                if (path == 0) {
                    TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, clone, &clone) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_first_child(nodes, clone, &clone) == LXA_OK);
                    check_option_state(nodes, clone, 0);
                    TEST_CHECK(lxa_dom_nodes_next_sibling(nodes, clone, &clone) == LXA_OK);
                    check_option_state(nodes, clone, LXA_DOM_OPTION_SELECTED);
                }
                else {
                    check_option_state(nodes, retained,
                        (path == 3 || path == 5 ? LXA_DOM_OPTION_DIRTY : 0)
                        | (path == 5 ? 0 : LXA_DOM_OPTION_SELECTED));
                    TEST_CHECK(lxa_dom_nodes_read(nodes, old, &record) == LXA_OK
                        && record.flags == LXA_DOM_OPTION_SELECTED && record.parent == LXA_ID_NONE
                        && record.prev == LXA_ID_NONE && record.next == LXA_ID_NONE);
                    if (extra.handle != LXA_HANDLE_NONE)
                        check_option_state(nodes, extra, LXA_DOM_OPTION_DIRTY
                            | (path == 5 ? LXA_DOM_OPTION_SELECTED : 0));
                    if (leading.handle != LXA_HANDLE_NONE)
                        TEST_CHECK(lxa_dom_nodes_read(nodes, leading, &record) == LXA_OK
                            && record.flags == 0 && record.parent == LXA_ID_NONE
                            && record.prev == LXA_ID_NONE && record.next == LXA_ID_NONE);
                    if (trailing.handle != LXA_HANDLE_NONE)
                        TEST_CHECK(lxa_dom_nodes_read(nodes, trailing, &record) == LXA_OK
                            && record.flags == LXA_DOM_OPTION_SELECTED && record.parent == LXA_ID_NONE
                            && record.prev == LXA_ID_NONE && record.next == LXA_ID_NONE);
                }
            }
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        }
        TEST_CHECK(success);
    }
}

static void
check_selectedcontent_update(void)
{
    enum { SECOND, FIRST, TWO_SELECTED, CLEAR, MULTIPLE, DISABLED, FILTERED,
           FOREIGN_SELECT, NO_TARGET, REMOVED, REMOVED_ANCESTOR,
           REMOVED_DISABLED, REMOVED_RETAINED_SELECT, REMOVED_NO_SELECT,
           REMOVED_FOREIGN_SELECT, REMOVED_FOREIGN_NODE, CASE_COUNT };
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < CASE_COUNT; ++path) {
        bool success = false;
        for (size_t allowance = 0; allowance < 1024 && !success; ++allowance) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_dom_ref_t document, select, parent, target, old, other, other_text;
            lxa_dom_ref_t first, second, text, attribute, clone;
            lxa_dom_ref_t removed = {0}, old_parent = {0};
            lxa_dom_record_t before[48], record, *records;
            size_t count;
            lxa_status_t status;
            bool changes = path == SECOND || path == FIRST || path == TWO_SELECTED
                || path == CLEAR || path == FILTERED || path == REMOVED
                || path == REMOVED_ANCESTOR;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL, &nodes) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT, &document) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("select"),
                path == FOREIGN_SELECT || path == REMOVED_FOREIGN_SELECT
                    ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML, &select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document, select) == LXA_OK);
            parent = select;
            if (path == FILTERED) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("datalist"), &parent) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, parent) == LXA_OK);
            }
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"), &target) == LXA_OK);
            if (path != NO_TARGET)
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, parent, target) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("old"), &old) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, target, old) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &first) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, parent, first) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("selected"), name(""), &attribute) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, first, attribute) == LXA_OK);
            if (path == FIRST) {
                TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("disabled"),
                                                               name(""), &attribute) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, first, attribute) == LXA_OK);
            }
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("a"), &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, first, text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &second) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, second) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("b"), &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, second, text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"), &other) == LXA_OK);
            if (path != NO_TARGET)
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, other) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("other"), &other_text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, other, other_text) == LXA_OK);
            if (path == MULTIPLE) {
                TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("multiple"), name("false"), &attribute) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, select, attribute) == LXA_OK);
            }
            if (path >= REMOVED) {
                TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("selectedcontent"),
                    path == REMOVED_FOREIGN_NODE ? LXA_DOM_NS_SVG : LXA_DOM_NS_HTML, &removed) == LXA_OK);
                old_parent = path == REMOVED_NO_SELECT ? document : select;
                if (path == REMOVED_ANCESTOR) {
                    TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("select"),
                        LXA_DOM_NS_SVG, &old_parent) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, old_parent) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_detach_constructed_element(nodes, select) == LXA_OK);
                }
                if (path == REMOVED_RETAINED_SELECT) {
                    lxa_dom_ref_t detached;
                    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"), &detached) == LXA_OK);
                    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, detached, removed) == LXA_OK);
                }
            }
            records = (lxa_dom_record_t *) nodes->records.bytes.data;
            records[(lxa_id_t) first.handle].flags = LXA_DOM_OPTION_DIRTY
                | (path == FIRST || path == TWO_SELECTED || path == FILTERED ? LXA_DOM_OPTION_SELECTED : 0);
            records[(lxa_id_t) second.handle].flags = LXA_DOM_OPTION_DIRTY
                | (path == FIRST || path == CLEAR ? 0 : LXA_DOM_OPTION_SELECTED);
            if (path == DISABLED) records[(lxa_id_t) target.handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
            if (path == REMOVED_DISABLED)
                records[(lxa_id_t) removed.handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
            count = lxa_dom_nodes_length(nodes);
            TEST_CHECK(count <= sizeof(before) / sizeof(before[0]));
            memcpy(before, records, count * sizeof(before[0]));
            if (path == REMOVED && allowance == 0) {
                budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
                TEST_CHECK(lxa_dom_nodes_removed_selectedcontent(nodes, removed, old_parent)
                    == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_MEMORY);
                budget.limits.max_memory_bytes = lxa_limits_default().max_memory_bytes;
                TEST_CHECK(memcmp(before, nodes->records.bytes.data, count * sizeof(before[0])) == 0);
            }
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = path >= REMOVED
                ? lxa_dom_nodes_removed_selectedcontent(nodes, removed, old_parent)
                : lxa_dom_nodes_update_selectedcontent(nodes, select);
            success = status == (path == FOREIGN_SELECT || path == REMOVED_FOREIGN_NODE
                ? LXA_ERROR_ARGUMENT : LXA_OK);
            if (!success)
                TEST_CHECK(status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK);
            budget.limits.max_work = lxa_limits_default().max_work;
            for (size_t i = 0; i < count; ++i) {
                lxa_dom_ref_t ref = {.document = nodes, .epoch = nodes->epoch};
                if (success && changes && (i == (lxa_id_t) target.handle || i == (lxa_id_t) old.handle)) continue;
                TEST_CHECK(lxa_handle_pack((lxa_id_t) i, nodes->generation, &ref.handle) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_read(nodes, ref, &record) == LXA_OK
                    && memcmp(&record, &before[i], sizeof(record)) == 0);
            }
            if (success && changes) {
                TEST_CHECK(lxa_dom_nodes_read(nodes, old, &record) == LXA_OK && record.parent == LXA_ID_NONE);
                TEST_CHECK(lxa_dom_nodes_first_child(nodes, target, &clone) == LXA_OK);
                if (path == CLEAR) TEST_CHECK(clone.handle == LXA_HANDLE_NONE);
                else {
                    lxa_span_t bytes;
                    TEST_CHECK(lxa_dom_nodes_character_read(nodes, clone, &bytes) == LXA_OK
                        && bytes.length == 1 && bytes.data[0] == (path == FIRST || path == TWO_SELECTED ? 'a' : 'b'));
                }
                TEST_CHECK(lxa_dom_nodes_first_child(nodes, other, &clone) == LXA_OK && clone.handle == other_text.handle);
            }
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        }
        TEST_CHECK(success);
    }
}

static void
check_selectedcontent_postconnection(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < 3; ++path) {
        bool success = false, partial = false;
        for (size_t allowance = 0; allowance < 1024 && !success; ++allowance) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_dom_ref_t document, select, primary, other, old, old_other;
            lxa_dom_ref_t option, text, element, clone;
            lxa_dom_record_t before[16], record, *records;
            size_t count;
            bool primary_changed, other_changed;
            lxa_status_t status;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL, &nodes) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT, &document) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"), &select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document, select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"), &primary) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, primary) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("old"), &old) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, primary, old) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"), &other) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, other) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("other"), &old_other) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, other, old_other) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &option) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, option) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("new"), &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, option, text) == LXA_OK);
            element = path == 0 ? primary : other;
            records = (lxa_dom_record_t *) nodes->records.bytes.data;
            records[(lxa_id_t) element.handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
            records[(lxa_id_t) option.handle].flags = LXA_DOM_OPTION_DIRTY
                | (path == 2 ? 0 : LXA_DOM_OPTION_SELECTED);
            count = lxa_dom_nodes_length(nodes);
            TEST_CHECK(count <= sizeof(before) / sizeof(before[0]));
            memcpy(before, records, count * sizeof(before[0]));
            if (path == 0 && allowance == 0) {
                budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
                TEST_CHECK(lxa_dom_nodes_connect_selectedcontent(nodes, element)
                    == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_MEMORY);
                budget.limits.max_memory_bytes = lxa_limits_default().max_memory_bytes;
                records = (lxa_dom_record_t *) nodes->records.bytes.data;
                TEST_CHECK(records[(lxa_id_t) element.handle].flags == 0);
                records[(lxa_id_t) element.handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
                TEST_CHECK(memcmp(before, records, count * sizeof(before[0])) == 0);
            }
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_dom_nodes_connect_selectedcontent(nodes, element);
            success = status == LXA_OK;
            if (!success)
                TEST_CHECK(status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK);
            budget.limits.max_work = lxa_limits_default().max_work;
            TEST_CHECK(lxa_dom_nodes_read(nodes, old, &record) == LXA_OK);
            primary_changed = record.parent == LXA_ID_NONE;
            TEST_CHECK(lxa_dom_nodes_read(nodes, old_other, &record) == LXA_OK);
            other_changed = record.parent == LXA_ID_NONE;
            if (primary_changed)
                check_option_state(nodes, element, 0);
            if (!success && primary_changed) partial = true;
            TEST_CHECK(!other_changed || primary_changed);
            for (size_t i = 0; i < count; ++i) {
                lxa_dom_ref_t ref = {.document = nodes, .epoch = nodes->epoch};
                if (primary_changed && (i == (lxa_id_t) primary.handle || i == (lxa_id_t) old.handle)) continue;
                if (other_changed && (i == (lxa_id_t) other.handle || i == (lxa_id_t) old_other.handle)) continue;
                TEST_CHECK(lxa_handle_pack((lxa_id_t) i, nodes->generation, &ref.handle) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_read(nodes, ref, &record) == LXA_OK);
                if (i == (lxa_id_t) element.handle) {
                    TEST_CHECK(record.flags == 0 || record.flags == LXA_DOM_SELECTEDCONTENT_DISABLED);
                    record.flags = before[i].flags;
                }
                TEST_CHECK(memcmp(&record, &before[i], sizeof(record)) == 0);
            }
            if (primary_changed) {
                lxa_span_t bytes;
                TEST_CHECK(lxa_dom_nodes_read(nodes, old, &record) == LXA_OK
                    && record.parent == LXA_ID_NONE && record.prev == LXA_ID_NONE && record.next == LXA_ID_NONE);
                TEST_CHECK(lxa_dom_nodes_first_child(nodes, primary, &clone) == LXA_OK);
                if (path == 2) TEST_CHECK(clone.handle == LXA_HANDLE_NONE);
                else TEST_CHECK(lxa_dom_nodes_character_read(nodes, clone, &bytes) == LXA_OK
                    && bytes.length == 3 && memcmp(bytes.data, "new", 3) == 0);
            }
            if (other_changed)
                TEST_CHECK(lxa_dom_nodes_read(nodes, other, &record) == LXA_OK
                    && record.first_child == LXA_ID_NONE && record.last_child == LXA_ID_NONE);
            if (success) {
                TEST_CHECK(primary_changed && other_changed);
                check_option_state(nodes, element, 0);
            }
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        }
        TEST_CHECK(success && partial);
    }
}

static void
check_nonprimary_selectedcontent_clear(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    for (unsigned path = 0; path < 3; ++path) {
        bool success = false;
        for (size_t allowance = 0; allowance < 2048 && !success; ++allowance) {
            lxa_budget_t budget;
            lxa_dom_nodes_t *nodes;
            lxa_dom_ref_t document, select, primary, foreign, text, attribute;
            lxa_dom_ref_t targets[4], parents[4], old[4], templated, fragment, inert;
            lxa_dom_ref_t removed = {0}, retained = {0};
            lxa_dom_record_t before[64], record, *records;
            size_t count;
            lxa_status_t status;
            bool changed = false;
            TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL, &nodes) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT, &document) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("select"), &select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, document, select) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("selectedcontent"),
                                                              LXA_DOM_NS_SVG, &foreign) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, foreign) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("foreign"), &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, foreign, text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"), &primary) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, primary) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("primary"), &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, primary, text) == LXA_OK);
            parents[0] = primary;
            parents[1] = parents[3] = select;
            for (size_t i = 0; i < 4; ++i) {
                if (i == 2) parents[i] = targets[1];
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"), &targets[i]) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, parents[i], targets[i]) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("old"), &old[i]) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, targets[i], old[i]) == LXA_OK);
            }
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("template"), &templated) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, primary, templated) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT_FRAGMENT, &fragment) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_bind_template(nodes, templated, fragment) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("selectedcontent"), &inert) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, fragment, inert) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, name("inert"), &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, inert, text) == LXA_OK);
            if (path == 1) {
                TEST_CHECK(lxa_dom_nodes_append_html_attribute(nodes, name("multiple"), name("false"), &attribute) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, select, attribute) == LXA_OK);
            }
            if (path == 2) {
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &removed) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, targets[1], removed) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("option"), &retained) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, select, retained) == LXA_OK);
            }
            records = (lxa_dom_record_t *) nodes->records.bytes.data;
            records[(lxa_id_t) targets[0].handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
            records[(lxa_id_t) targets[2].handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
            if (path == 1) records[(lxa_id_t) primary.handle].flags = LXA_DOM_SELECTEDCONTENT_DISABLED;
            if (path == 2) {
                records[(lxa_id_t) removed.handle].flags = LXA_DOM_OPTION_SELECTED;
                records[(lxa_id_t) retained.handle].flags = LXA_DOM_OPTION_DIRTY;
            }
            count = lxa_dom_nodes_length(nodes);
            TEST_CHECK(count <= sizeof(before) / sizeof(before[0]));
            memcpy(before, records, count * sizeof(before[0]));
            if (allowance == 0) {
                budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
                TEST_CHECK(lxa_dom_nodes_clear_nonprimary_selectedcontent(nodes, select) == LXA_ERROR_LIMIT
                           && nodes->limit_reason == LXA_BUDGET_MEMORY);
                budget.limits.max_memory_bytes = lxa_limits_default().max_memory_bytes;
                TEST_CHECK(memcmp(before, nodes->records.bytes.data, count * sizeof(before[0])) == 0);
            }
            budget.limits.max_work = budget.used[LXA_BUDGET_WORK] + allowance;
            status = lxa_dom_nodes_clear_nonprimary_selectedcontent(nodes, select);
            success = status == LXA_OK;
            if (!success)
                TEST_CHECK(status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_WORK);
            budget.limits.max_work = lxa_limits_default().max_work;
            for (size_t i = 0; i < 4; ++i) {
                TEST_CHECK(lxa_dom_nodes_read(nodes, targets[i], &record) == LXA_OK);
                if (record.first_child == LXA_ID_NONE) {
                    changed = true;
                    TEST_CHECK(record.last_child == LXA_ID_NONE);
                    TEST_CHECK(lxa_dom_nodes_read(nodes, old[i], &record) == LXA_OK
                        && record.parent == LXA_ID_NONE && record.prev == LXA_ID_NONE && record.next == LXA_ID_NONE);
                }
                else TEST_CHECK(!success && record.first_child == before[(lxa_id_t) targets[i].handle].first_child
                                && record.last_child == before[(lxa_id_t) targets[i].handle].last_child);
            }
            if (!changed)
                TEST_CHECK(memcmp(before, nodes->records.bytes.data, count * sizeof(before[0])) == 0);
            for (size_t i = 0; i < count; ++i) {
                lxa_dom_ref_t ref = {.document = nodes, .epoch = nodes->epoch};
                if (i != (lxa_id_t) select.handle && i != (lxa_id_t) primary.handle
                    && i != (lxa_id_t) foreign.handle && i != (lxa_id_t) inert.handle
                    && i != (lxa_id_t) fragment.handle) continue;
                TEST_CHECK(lxa_handle_pack((lxa_id_t) i, nodes->generation, &ref.handle) == LXA_OK);
                TEST_CHECK(lxa_dom_nodes_read(nodes, ref, &record) == LXA_OK
                    && memcmp(&record, &before[i], sizeof(record)) == 0);
            }
            if (success && path == 2) {
                check_option_state(nodes, retained, LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY);
                check_option_state(nodes, removed, LXA_DOM_OPTION_SELECTED);
            }
            TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
            lxa_dom_nodes_destroy(nodes);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        }
        TEST_CHECK(success);
    }
}

static void
check_element_clone(void)
{
    static const struct {
        const char *name;
        lxa_dom_attr_namespace_t ns;
    } attributes[] = {
        {"viewBox", LXA_DOM_ATTR_NS_NONE},
        {"xml:lang", LXA_DOM_ATTR_NS_XML},
        {"xlink:href", LXA_DOM_ATTR_NS_XLINK},
        {"xmlns", LXA_DOM_ATTR_NS_XMLNS}
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_budget_t budget;
    lxa_dom_nodes_t *nodes, *other;
    lxa_dom_ref_t source, clone, attribute, child, cursor = {0};
    lxa_dom_record_t record;
    lxa_dom_namespace_t ns;
    lxa_span_t key, value;
    size_t count;
    TEST_CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                &nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create(&allocator, &other) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_namespaced_element(nodes, name("linearGradient"),
        LXA_DOM_NS_SVG, &source) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("span"), &child)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_constructed_child(nodes, source, child)
               == LXA_OK);
    for (size_t i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
        TEST_CHECK(lxa_dom_nodes_append_foreign_attribute(nodes,
            name(attributes[i].name), name("original"), attributes[i].ns,
            &attribute) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attach_attribute(nodes, source, attribute)
                   == LXA_OK);
    }
    TEST_CHECK(lxa_dom_nodes_clone_element(nodes, source, &clone) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_read(nodes, clone, &record) == LXA_OK
               && record.parent == LXA_ID_NONE
               && record.first_child == LXA_ID_NONE
               && record.last_child == LXA_ID_NONE);
    TEST_CHECK(lxa_dom_nodes_element_namespace(nodes, clone, &ns) == LXA_OK
               && ns == LXA_DOM_NS_SVG);
    TEST_CHECK(lxa_dom_nodes_element_name(nodes, clone, &key) == LXA_OK
               && key.length == 14 && memcmp(key.data, "linearGradient", 14) == 0);
    for (size_t i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
        lxa_dom_attr_namespace_t attr_ns;
        lxa_span_t local_name;
        TEST_CHECK(lxa_dom_nodes_attribute_next(nodes, clone, cursor, &cursor)
                   == LXA_OK && cursor.handle != LXA_HANDLE_NONE);
        TEST_CHECK(lxa_dom_nodes_attribute_namespace(nodes, cursor, &attr_ns,
            &local_name) == LXA_OK && attr_ns == attributes[i].ns);
        TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, cursor, &key, &value)
                   == LXA_OK);
        TEST_CHECK(key.length == strlen(attributes[i].name)
                   && memcmp(key.data, attributes[i].name, key.length) == 0
                   && value.length == 8 && memcmp(value.data, "original", 8) == 0);
        TEST_CHECK(lxa_dom_nodes_attribute_set_value(nodes, cursor, name("changed"))
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_find_attribute(nodes, source,
            name(attributes[i].name), &attribute) == LXA_OK);
        TEST_CHECK(attribute.handle != cursor.handle);
        TEST_CHECK(lxa_dom_nodes_attribute_read(nodes, attribute, &key, &value)
                   == LXA_OK && value.length == 8
                   && memcmp(value.data, "original", 8) == 0);
    }
    TEST_CHECK(lxa_dom_nodes_attribute_next(nodes, clone, cursor, &cursor)
               == LXA_OK && cursor.handle == LXA_HANDLE_NONE);
    count = lxa_dom_nodes_length(nodes);
    TEST_CHECK(lxa_dom_nodes_clone_element(nodes, source, NULL)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_clone_element(other, source, &clone)
               == LXA_ERROR_ARGUMENT && clone.handle == LXA_HANDLE_NONE);
    TEST_CHECK(lxa_dom_nodes_clone_element(nodes, attribute, &clone)
               == LXA_ERROR_ARGUMENT && clone.handle == LXA_HANDLE_NONE);
    TEST_CHECK(lxa_dom_nodes_length(nodes) == count);
    budget.limits.max_nodes = (uint32_t) count;
    TEST_CHECK(lxa_dom_nodes_clone_element(nodes, source, &clone)
               == LXA_ERROR_LIMIT && clone.handle == LXA_HANDLE_NONE
               && nodes->limit_reason == LXA_BUDGET_NODES);
    budget.limits.max_nodes = (uint32_t) count + 1;
    TEST_CHECK(lxa_dom_nodes_clone_element(nodes, source, &clone)
               == LXA_ERROR_LIMIT && clone.handle == LXA_HANDLE_NONE
               && nodes->limit_reason == LXA_BUDGET_NODES
               && lxa_dom_nodes_length(nodes) == count + 1);
    budget.limits = lxa_limits_default();
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    TEST_CHECK(lxa_dom_nodes_clone_element(nodes, source, &clone)
               == LXA_ERROR_LIMIT && clone.handle == LXA_HANDLE_NONE
               && nodes->limit_reason == LXA_BUDGET_WORK);
    budget.limits = lxa_limits_default();
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_dom_nodes_destroy(other);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
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
    check_element_clone();
    check_option_selected_attributes();
    check_option_select_reset();
    check_option_detach();
    check_option_move();
    check_container_option_detach();
    check_container_option_move();
    check_selectedcontent_connection();
    check_option_pop_clone();
    check_option_clone_select_resets();
    check_selectedcontent_update();
    check_selectedcontent_postconnection();
    check_nonprimary_selectedcontent_clear();
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
               == LXA_ERROR_UNSUPPORTED);
    TEST_CHECK(lxa_dom_nodes_append_html_element(nodes, name("a b"), &next)
               == LXA_ERROR_UNSUPPORTED);
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
