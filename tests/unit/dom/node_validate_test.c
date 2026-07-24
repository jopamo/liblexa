/* Copyright (C) 2026 Paul Moses */
#include "tests/support/test_check.h"
#include "dom/node_store_internal.h"

static lxa_id_t
slot(lxa_dom_ref_t ref)
{
    lxa_id_t id = LXA_ID_NONE;
    uint32_t generation;
    TEST_CHECK(lxa_handle_unpack(ref.handle, &id, &generation) == LXA_OK);
    return id;
}

int
main(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_dom_nodes_t *nodes = NULL;
    lxa_dom_ref_t root, first, second, draft;
    lxa_dom_record_t *records;
    lxa_budget_t budget;
    lxa_limits_t limits = lxa_limits_default();
    lxa_span_t bytes = {(const uint8_t *) "hello", 5};
    size_t retained, i;
    TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget, NULL,
                                                 &nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_ELEMENT, &draft)
               == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                           &root) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_TEXT, bytes,
                                               &first) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_character(nodes, LXA_DOM_KIND_COMMENT, bytes,
                                               &second) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK); /* detached is valid */
    TEST_CHECK(lxa_dom_nodes_append_text_child(nodes, root, first) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_text_child(nodes, root, second) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    retained = budget.used[LXA_BUDGET_MEMORY];

    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    records[slot(first)].next = slot(first); /* cycle / reciprocal mismatch */
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_ERROR_ARGUMENT);
    records[slot(first)].next = slot(second);
    records[slot(second)].parent = LXA_ID_NONE;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_ERROR_ARGUMENT);
    records[slot(second)].parent = slot(root);
    records[slot(second)].payload = records[slot(first)].payload;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_ERROR_ARGUMENT);
    records[slot(second)].payload = records[slot(first)].payload + 1;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);

    /* Validation's temporary bookkeeping is bounded and released on both
     * rejection and success. An insufficient budget does not edit the tree. */
    budget.limits.max_memory_bytes = retained;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_ERROR_LIMIT);
    budget.limits.max_memory_bytes = limits.max_memory_bytes;
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_ERROR_LIMIT);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
    budget.limits.max_work = limits.max_work;
    TEST_CHECK(lxa_dom_nodes_reset(nodes) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    for (i = 0; i != 4096; ++i) {
        TEST_CHECK(lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_ELEMENT,
                                              &draft) == LXA_OK);
    }
    TEST_CHECK(lxa_dom_nodes_validate(nodes) == LXA_OK);
    lxa_dom_nodes_destroy(nodes);
    TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    return 0;
}
