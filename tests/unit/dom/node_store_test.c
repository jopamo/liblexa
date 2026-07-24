/* Copyright (C) 2026 Paul Moses */
#include "tests/support/test_check.h"
#include <stdbool.h>
#include <string.h>
#include "liblexa/dom.h"
#include "dom/node_store_internal.h"

typedef struct {
    size_t calls, fail_at, active;
} test_alloc_t;

typedef union {
    long double alignment;
    size_t size;
} block_t;

static void *
allocate(void *context, size_t size)
{
    test_alloc_t *state = context;
    block_t *block;
    if (state->calls++ == state->fail_at) {
        return NULL;
    }
    block = malloc(sizeof(*block) + size);
    if (block == NULL) {
        return NULL;
    }
    block->size = size;
    ++state->active;
    return block + 1;
}

/* The test deliberately relocates on every growth. */
static void *
resize(void *context, void *ptr, size_t size)
{
    test_alloc_t *state = context;
    block_t *next, *old = ptr == NULL ? NULL : (block_t *) ptr - 1;
    if (state->calls++ == state->fail_at) {
        return NULL;
    }
    next = malloc(sizeof(*next) + size);
    if (next == NULL) {
        return NULL;
    }
    if (old != NULL) {
        memcpy(next + 1, ptr, old->size < size ? old->size : size);
        free(old);
    }
    else {
        ++state->active;
    }
    next->size = size;
    return next + 1;
}

static void
release(void *context, void *ptr)
{
    test_alloc_t *state = context;
    if (ptr != NULL) {
        --state->active;
        free((block_t *) ptr - 1);
    }
}

static lxa_id_t
test_slot(lxa_dom_ref_t ref)
{
    lxa_id_t slot;
    uint32_t generation;
    return lxa_handle_unpack(ref.handle, &slot, &generation) == LXA_OK
           ? slot : LXA_ID_NONE;
}

int
main(void)
{
    test_alloc_t state = {0, SIZE_MAX, 0};
    lxa_allocator_t allocator = {&state, allocate, resize, release};
    lxa_dom_nodes_t *a = NULL, *b = NULL;
    lxa_dom_ref_t first, last, invalid, fragment, second;
    lxa_dom_record_t record;
    lxa_span_t input = {(const uint8_t *) "a\0b", 3}, view;
    unsigned int i, failure;
    bool failed = false;

    TEST_CHECK(lxa_dom_nodes_create(&allocator, &a) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_create(&allocator, &b) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_TEXT, &first) == LXA_OK);
    TEST_CHECK(test_slot(first) == 0 && first.epoch == 1);
    for (i = 0; i < 4096; ++i) {
        TEST_CHECK(lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_COMMENT, &last)
               == LXA_OK);
        TEST_CHECK(test_slot(last) == i + 1);
        TEST_CHECK(lxa_dom_nodes_read(a, first, &record) == LXA_OK);
        TEST_CHECK(record.kind == LXA_DOM_KIND_TEXT);
        TEST_CHECK(record.parent == LXA_ID_NONE && record.next == LXA_ID_NONE);
        TEST_CHECK(record.payload == LXA_ID_NONE && record.generation == 1);
    }
    TEST_CHECK(lxa_dom_nodes_length(a) == 4097);
    TEST_CHECK(lxa_dom_nodes_append_character(a, LXA_DOM_KIND_TEXT, input, &invalid)
           == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_character_read(a, invalid, &view) == LXA_OK);
    TEST_CHECK(view.length == 3 && memcmp(view.data, input.data, 3) == 0);
    TEST_CHECK(lxa_dom_nodes_character_read(a, first, &view) == LXA_ERROR_ARGUMENT);
    for (i = 0; i < 128; ++i) {
        TEST_CHECK(lxa_dom_nodes_append_character(a, LXA_DOM_KIND_COMMENT, input,
                                              &last) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_character_read(a, invalid, &view) == LXA_OK);
        TEST_CHECK(view.length == 3 && memcmp(view.data, input.data, 3) == 0);
    }
    TEST_CHECK(lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                          &fragment) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_text_child(a, fragment, invalid) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_text_child(a, fragment, last) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_read(a, fragment, &record) == LXA_OK);
    TEST_CHECK(record.first_child == test_slot(invalid) && record.last_child == test_slot(last));
    TEST_CHECK(lxa_dom_nodes_read(a, invalid, &record) == LXA_OK);
    TEST_CHECK(record.parent == test_slot(fragment) && record.prev == LXA_ID_NONE
               && record.next == test_slot(last));
    TEST_CHECK(lxa_dom_nodes_read(a, last, &record) == LXA_OK);
    TEST_CHECK(record.parent == test_slot(fragment) && record.prev == test_slot(invalid)
               && record.next == LXA_ID_NONE);
    {
        lxa_buffer_t result;
        lxa_span_t prefix = {(const uint8_t *) "!", 1};
        TEST_CHECK(lxa_buffer_init(&result, &allocator) == LXA_OK);
        state.fail_at = state.calls;
        TEST_CHECK(lxa_dom_nodes_text_content(a, fragment, &result, 16)
                   == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(result.length == 0);
        state.fail_at = SIZE_MAX;
        TEST_CHECK(lxa_buffer_append(&result, prefix) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_text_content(a, fragment, &result, 3)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(result.length == 1 && result.data[0] == '!');
        TEST_CHECK(lxa_dom_nodes_text_content(a, fragment, &result, 16) == LXA_OK);
        TEST_CHECK(result.length == 4 && memcmp(result.data, "!a\0b", 4) == 0);
        lxa_buffer_clear(&result);
        TEST_CHECK(lxa_dom_nodes_text_content(a, last, &result, 16) == LXA_OK);
        TEST_CHECK(result.length == 3 && memcmp(result.data, input.data, 3) == 0);
        lxa_buffer_destroy(&result);
    }
    TEST_CHECK(lxa_dom_nodes_append_text_child(a, fragment, invalid)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_append_text_child(a, fragment, first)
               == LXA_ERROR_ARGUMENT); /* A bare text draft has no payload. */
    TEST_CHECK(lxa_dom_nodes_append_text_child(a, invalid, last)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_append_draft(b, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                          &second) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_text_child(a, second, last)
               == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_read(a, fragment, &record) == LXA_OK);
    TEST_CHECK(record.first_child == test_slot(invalid) && record.last_child == test_slot(last));
    TEST_CHECK(lxa_dom_nodes_read(b, first, &record) == LXA_ERROR_ARGUMENT);
    invalid = first;
    invalid.handle += UINT64_C(1) << 32;
    TEST_CHECK(lxa_dom_nodes_read(a, invalid, &record) == LXA_ERROR_ARGUMENT);
    invalid = first;
    invalid.handle = (UINT64_C(1) << 32) | LXA_ID_NONE;
    TEST_CHECK(lxa_dom_nodes_read(a, invalid, &record) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_handle_pack((lxa_id_t) lxa_dom_nodes_length(a), 1,
                               &invalid.handle) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_read(a, invalid, &record) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_INVALID, &invalid)
           == LXA_ERROR_ARGUMENT && test_slot(invalid) == LXA_ID_NONE);
    TEST_CHECK(lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_COUNT, &invalid)
           == LXA_ERROR_ARGUMENT && test_slot(invalid) == LXA_ID_NONE);

    state.fail_at = state.calls;
    while (lxa_dom_nodes_length(a) < 65536) {
        lxa_status_t status = lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_TEXT,
                                                          &last);
        if (status == LXA_ERROR_NO_MEMORY) {
            failed = true;
            TEST_CHECK(test_slot(last) == LXA_ID_NONE);
            TEST_CHECK(lxa_dom_nodes_length(a) >= 4097);
            TEST_CHECK(lxa_dom_nodes_read(a, first, &record) == LXA_OK);
            break;
        }
        TEST_CHECK(status == LXA_OK);
    }
    TEST_CHECK(failed);
    state.fail_at = SIZE_MAX;
    TEST_CHECK(lxa_dom_nodes_reset(a) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_length(a) == 0);
    TEST_CHECK(lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_DOCUMENT, &last) == LXA_OK);
    TEST_CHECK(test_slot(first) == test_slot(last) && first.epoch != last.epoch);
    TEST_CHECK(first.handle != last.handle);
    invalid = first;
    invalid.epoch = last.epoch;
    TEST_CHECK(lxa_dom_nodes_read(a, invalid, &record) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_read(a, first, &record) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_read(a, last, &record) == LXA_OK);
    TEST_CHECK(record.kind == LXA_DOM_KIND_DOCUMENT);
    TEST_CHECK(lxa_dom_nodes_character_read(a, invalid, &view) == LXA_ERROR_ARGUMENT);
    /* Force the terminal generation without billions of resets. Retire
     * every prior slot rather than permitting an old packed value to revive.
     */
    a->generation = UINT32_MAX;
    ((lxa_dom_record_t *) a->records.bytes.data)[test_slot(last)].generation
        = UINT32_MAX;
    TEST_CHECK(lxa_handle_pack(test_slot(last), UINT32_MAX, &last.handle) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_read(a, last, &record) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_reset(a) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_length(a) == 0);
    TEST_CHECK(lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_DOCUMENT, &second)
               == LXA_OK);
    TEST_CHECK(test_slot(second) > test_slot(last));
    TEST_CHECK(second.handle != first.handle && second.handle != last.handle);
    invalid = last;
    invalid.epoch = second.epoch;
    TEST_CHECK(lxa_dom_nodes_read(a, invalid, &record) == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_dom_nodes_read(a, second, &record) == LXA_OK);
    TEST_CHECK(record.generation == 1);
    TEST_CHECK(lxa_dom_nodes_reset(a) == LXA_OK);
    TEST_CHECK(lxa_dom_nodes_append_draft(a, LXA_DOM_KIND_DOCUMENT, &fragment)
               == LXA_OK);
    TEST_CHECK(test_slot(fragment) == test_slot(second)
               && fragment.handle != second.handle);
    TEST_CHECK(lxa_dom_nodes_read(a, second, &record) == LXA_ERROR_ARGUMENT);
    lxa_dom_nodes_destroy(b);
    lxa_dom_nodes_destroy(a);
    TEST_CHECK(state.active == 0);
    {
        lxa_dom_nodes_t *test = NULL;
        lxa_dom_ref_t pi, copy;
        lxa_span_t target, data, second_target, second_data;
        lxa_span_t name = {(const uint8_t *) "custom", 6};
        lxa_span_t binary = {(const uint8_t *) "a\0b", 3};
        TEST_CHECK(lxa_dom_nodes_create(&allocator, &test) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_pi(test, (lxa_span_t){
            (const uint8_t *) "Xml-Stylesheet", 14}, binary, &pi)
            == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_append_pi(test, (lxa_span_t){
            (const uint8_t *) "XML", 3}, binary, &pi)
            == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_append_pi(test, name, binary, &pi) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_pi_read(test, pi, &target, &data) == LXA_OK);
        TEST_CHECK(target.length == 6
                   && memcmp(target.data, name.data, 6) == 0
                   && data.length == 3
                   && memcmp(data.data, binary.data, 3) == 0);
        /* Both construction inputs may be views of a relocating store. */
        TEST_CHECK(lxa_dom_nodes_append_pi(test, target, data, &copy) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_pi_read(test, copy, &second_target,
                                         &second_data) == LXA_OK);
        TEST_CHECK(second_target.length == 6
                   && memcmp(second_target.data, name.data, 6) == 0
                   && second_data.length == 3
                   && memcmp(second_data.data, binary.data, 3) == 0);
        TEST_CHECK(lxa_dom_nodes_append_pi(test, name,
            (lxa_span_t){NULL, 0}, &copy) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_pi_read(test, copy, &second_target,
                                         &second_data) == LXA_OK
                   && second_target.length == 6 && second_data.length == 0);
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        {
            lxa_dom_record_t *records =
                (lxa_dom_record_t *) test->records.bytes.data;
            lxa_id_t slot = test_slot(copy), payload = records[slot].payload;
            records[slot].payload = UINT32_MAX;
            TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_ERROR_ARGUMENT);
            records[slot].payload = payload;
        }
        TEST_CHECK(lxa_dom_nodes_reset(test) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_pi_read(test, pi, &target, &data)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(state.active == 0);
    }
    for (failure = 0; failure < 3; ++failure) {
        lxa_dom_nodes_t *test = NULL;
        test_alloc_t injected = {0, SIZE_MAX, 0};
        lxa_allocator_t fail_alloc = {&injected, allocate, resize, release};
        lxa_dom_ref_t pi;
        TEST_CHECK(lxa_dom_nodes_create(&fail_alloc, &test) == LXA_OK);
        injected.fail_at = injected.calls + failure;
        TEST_CHECK(lxa_dom_nodes_append_pi(test,
            (lxa_span_t){(const uint8_t *) "custom", 6}, input, &pi)
            == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(pi.handle == LXA_HANDLE_NONE
                   && lxa_dom_nodes_length(test) == 0
                   && lxa_vector_length(&test->pis) == 0
                   && test->strings.length == 0);
        injected.fail_at = SIZE_MAX;
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(injected.active == 0);
    }
    {
        lxa_dom_nodes_t *test = NULL;
        lxa_dom_ref_t pi;
        lxa_budget_t budget;
        lxa_limits_t limits = lxa_limits_default();
        lxa_span_t name = {(const uint8_t *) "custom", 6};
        TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                                                     NULL, &test) == LXA_OK);
        budget.limits.max_nodes = 0;
        TEST_CHECK(lxa_dom_nodes_append_pi(test, name, input, &pi)
                   == LXA_ERROR_LIMIT
                   && lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_NODES);
        budget.limits.max_nodes = limits.max_nodes;
        budget.limits.max_name_bytes = 3;
        TEST_CHECK(lxa_dom_nodes_append_pi(test, name, input, &pi)
                   == LXA_ERROR_LIMIT
                   && lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_NAME_BYTES);
        budget.limits.max_name_bytes = limits.max_name_bytes;
        budget.limits.max_token_bytes = 2;
        TEST_CHECK(lxa_dom_nodes_append_pi(test, name, input, &pi)
                   == LXA_ERROR_LIMIT
                   && lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_TOKEN_BYTES);
        budget.limits.max_token_bytes = limits.max_token_bytes;
        budget.limits.max_string_bytes = 8;
        TEST_CHECK(lxa_dom_nodes_append_pi(test, name, input, &pi)
                   == LXA_ERROR_LIMIT
                   && lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_STRINGS);
        TEST_CHECK(lxa_dom_nodes_length(test) == 0
                   && lxa_dom_nodes_validate(test) == LXA_OK);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0 && state.active == 0);
    }
    for (failure = 0; failure < 3; ++failure) {
        lxa_dom_nodes_t *test = NULL;
        test_alloc_t injected = {0, SIZE_MAX, 0};
        lxa_allocator_t fail_alloc = {&injected, allocate, resize, release};
        size_t initial_memory;
        TEST_CHECK(lxa_dom_nodes_create(&fail_alloc, &test) == LXA_OK);
        initial_memory = test->budget->used[LXA_BUDGET_MEMORY];
        injected.fail_at = injected.calls + failure;
        TEST_CHECK(lxa_dom_nodes_append_character(test, LXA_DOM_KIND_TEXT, input,
                                               &last) == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(test_slot(last) == LXA_ID_NONE && lxa_dom_nodes_length(test) == 0);
        TEST_CHECK(test->budget->used[LXA_BUDGET_MEMORY]
                   == initial_memory + test->records.bytes.capacity
                      + test->characters.bytes.capacity + test->strings.capacity);
        TEST_CHECK(test->budget->used[LXA_BUDGET_NODES] == 0
                   && test->budget->used[LXA_BUDGET_STRINGS] == 0);
        injected.fail_at = SIZE_MAX;
        TEST_CHECK(lxa_dom_nodes_append_character(test, LXA_DOM_KIND_TEXT, input,
                                               &last) == LXA_OK);
        TEST_CHECK(test_slot(last) == 0);
        TEST_CHECK(lxa_dom_nodes_character_read(test, last, &view) == LXA_OK);
        TEST_CHECK(view.length == 3 && memcmp(view.data, input.data, 3) == 0);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(injected.active == 0);
    }
    for (failure = 0; failure < 7; ++failure) {
        lxa_dom_nodes_t *test = NULL;
        test_alloc_t injected = {0, SIZE_MAX, 0};
        lxa_allocator_t fail_alloc = {&injected, allocate, resize, release};
        lxa_span_t name = {(const uint8_t *) "section", 7};
        TEST_CHECK(lxa_dom_nodes_create(&fail_alloc, &test) == LXA_OK);
        injected.fail_at = injected.calls + failure;
        TEST_CHECK(lxa_dom_nodes_append_html_element(test, name, &last)
                   == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(test_slot(last) == LXA_ID_NONE
                   && lxa_dom_nodes_length(test) == 0
                   && lxa_vector_length(&test->elements) == 0
                   && lxa_vector_length(&test->atoms) == 0
                   && lxa_table_length(test->atom_lookup) == 0
                   && test->strings.length == 0);
        TEST_CHECK(test->budget->used[LXA_BUDGET_NODES] == 0
                   && test->budget->used[LXA_BUDGET_STRINGS] == 0);
        injected.fail_at = SIZE_MAX;
        TEST_CHECK(lxa_dom_nodes_append_html_element(test, name, &last) == LXA_OK);
        TEST_CHECK(test_slot(last) == 0);
        TEST_CHECK(lxa_dom_nodes_element_name(test, last, &view) == LXA_OK);
        TEST_CHECK(view.length == name.length
                   && memcmp(view.data, name.data, name.length) == 0);
        /* A borrowed name is interned once, even across record growth. */
        for (i = 0; i < 3; ++i) {
            lxa_dom_ref_t copied;
            size_t old_capacity = test->strings.capacity;
            TEST_CHECK(lxa_dom_nodes_append_html_element(test, view, &copied)
                       == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_element_name(test, copied, &view) == LXA_OK);
            TEST_CHECK(view.length == name.length
                       && memcmp(view.data, name.data, name.length) == 0);
            TEST_CHECK(test->strings.capacity == old_capacity);
        }
        TEST_CHECK(lxa_table_length(test->atom_lookup) == 1);
        {
            lxa_span_t new_text = {
                (const uint8_t *) "anothernamethatislong", 21};
            lxa_dom_ref_t text, copied;
            size_t old_capacity;
            TEST_CHECK(lxa_dom_nodes_append_character(test, LXA_DOM_KIND_TEXT,
                                                       new_text, &text) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_character_read(test, text, &view) == LXA_OK);
            old_capacity = test->strings.capacity;
            TEST_CHECK(lxa_dom_nodes_append_html_element(test, view, &copied)
                       == LXA_OK);
            TEST_CHECK(test->strings.capacity > old_capacity);
            TEST_CHECK(lxa_dom_nodes_element_name(test, copied, &view) == LXA_OK);
            TEST_CHECK(view.length == new_text.length
                       && memcmp(view.data, new_text.data, view.length) == 0);
        }
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(injected.active == 0);
    }
    for (failure = 0; failure < 3; ++failure) {
        lxa_dom_nodes_t *test = NULL;
        test_alloc_t injected = {0, SIZE_MAX, 0};
        lxa_allocator_t fail_alloc = {&injected, allocate, resize, release};
        lxa_span_t name = {(const uint8_t *) "class", 5};
        TEST_CHECK(lxa_dom_nodes_create(&fail_alloc, &test) == LXA_OK);
        injected.fail_at = injected.calls + failure;
        TEST_CHECK(lxa_dom_nodes_append_html_attribute(test, name, input,
                                                        &last) == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(test_slot(last) == LXA_ID_NONE
                   && lxa_dom_nodes_length(test) == 0
                   && lxa_vector_length(&test->attributes) == 0
                   && test->strings.length == 0
                   && test->budget->used[LXA_BUDGET_ATTRIBUTES] == 0
                   && test->budget->used[LXA_BUDGET_STRINGS] == 0);
        injected.fail_at = SIZE_MAX;
        TEST_CHECK(lxa_dom_nodes_append_html_attribute(test, name, input,
                                                        &last) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attribute_read(test, last, &view, &input)
                   == LXA_OK);
        TEST_CHECK(view.length == 5 && memcmp(view.data, "class", 5) == 0
                   && input.length == 3 && memcmp(input.data, "a\0b", 3) == 0);
        input = (lxa_span_t){(const uint8_t *) "a\0b", 3};
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(injected.active == 0);
    }
    for (failure = 0; failure < 3; ++failure) {
        lxa_dom_nodes_t *test = NULL;
        test_alloc_t injected = {0, SIZE_MAX, 0};
        lxa_allocator_t fail_alloc = {&injected, allocate, resize, release};
        lxa_dom_doctype_data_t data = {0}, read_back;
        data.name = (lxa_span_t){(const uint8_t *) "html", 4};
        data.has_public_id = true; /* present but empty */
        TEST_CHECK(lxa_dom_nodes_create(&fail_alloc, &test) == LXA_OK);
        injected.fail_at = injected.calls + failure;
        TEST_CHECK(lxa_dom_nodes_append_doctype(test, data, &last)
                   == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(lxa_dom_nodes_length(test) == 0
                   && lxa_vector_length(&test->doctypes) == 0
                   && test->strings.length == 0);
        injected.fail_at = SIZE_MAX;
        TEST_CHECK(lxa_dom_nodes_append_doctype(test, data, &last) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_doctype_read(test, last, &read_back) == LXA_OK
                   && read_back.has_public_id && !read_back.has_system_id
                   && read_back.public_id.length == 0
                   && read_back.name.length == 4
                   && memcmp(read_back.name.data, "html", 4) == 0);
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(injected.active == 0);
    }
    {
        lxa_dom_nodes_t *test = NULL;
        test_alloc_t injected = {0, SIZE_MAX, 0};
        lxa_allocator_t fail_alloc = {&injected, allocate, resize, release};
        lxa_span_t attr_name = {(const uint8_t *) "class", 5};
        static const char replacement[] =
            "a longer replacement than the current string capacity";
        lxa_span_t large = {(const uint8_t *) replacement,
                            sizeof(replacement) - 1};
        size_t old_length;
        TEST_CHECK(lxa_dom_nodes_create(&fail_alloc, &test) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_attribute(test, attr_name, input,
                                                        &last) == LXA_OK);
        old_length = test->strings.length;
        injected.fail_at = injected.calls;
        TEST_CHECK(lxa_dom_nodes_attribute_set_value(test, last, large)
                   == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(test->strings.length == old_length);
        TEST_CHECK(lxa_dom_nodes_attribute_read(test, last, &view, &input)
                   == LXA_OK && input.length == 3
                   && memcmp(input.data, "a\0b", 3) == 0);
        injected.fail_at = SIZE_MAX;
        TEST_CHECK(lxa_dom_nodes_attribute_set_value(test, last, large) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_attribute_read(test, last, &view, &input)
                   == LXA_OK && input.length == large.length
                   && memcmp(input.data, large.data, large.length) == 0);
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        input = (lxa_span_t){(const uint8_t *) "a\0b", 3};
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(injected.active == 0);
    }
    {
        lxa_budget_t budget;
        lxa_budget_kind_t reason = LXA_BUDGET_COUNT;
        lxa_limits_t limits = lxa_limits_default();
        lxa_dom_nodes_t *test = NULL;
        size_t base;
        limits.max_memory_bytes = 0;
        TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                                                    &reason, &test)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(reason == LXA_BUDGET_MEMORY && test == NULL);
        {
            test_alloc_t injected = {0, 0, 0};
            lxa_allocator_t fail_alloc = {&injected, allocate, resize, release};
            limits.max_memory_bytes = 1024 * 1024;
            TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
            TEST_CHECK(lxa_dom_nodes_create_with_budget(&fail_alloc, &budget,
                                                        &reason, &test)
                       == LXA_ERROR_NO_MEMORY);
            TEST_CHECK(test == NULL && injected.active == 0
                       && budget.used[LXA_BUDGET_MEMORY] == 0);
        }
        limits.max_memory_bytes = 1024 * 1024;
        limits.max_nodes = 1;
        TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                                                    &reason, &test) == LXA_OK);
        TEST_CHECK(reason == LXA_BUDGET_COUNT);
        TEST_CHECK(lxa_dom_nodes_append_draft(test, LXA_DOM_KIND_DOCUMENT,
                                              &first) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(test, LXA_DOM_KIND_DOCUMENT,
                                              &last) == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_NODES);
        TEST_CHECK(lxa_dom_nodes_length(test) == 1);
        TEST_CHECK(budget.used[LXA_BUDGET_NODES] == 1);
        TEST_CHECK(lxa_dom_nodes_reset(test) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(test, LXA_DOM_KIND_DOCUMENT,
                                              &last) == LXA_OK);
        TEST_CHECK(test_slot(first) == test_slot(last));
        TEST_CHECK(first.handle != last.handle);
        TEST_CHECK(budget.used[LXA_BUDGET_NODES] == 1);
        test->generation = UINT32_MAX;
        TEST_CHECK(lxa_dom_nodes_reset(test) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(test, LXA_DOM_KIND_DOCUMENT,
                                              &last) == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_NODES);
        TEST_CHECK(budget.used[LXA_BUDGET_NODES] == 1);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        limits.max_nodes = 10;
        limits.max_memory_bytes = 1024 * 1024;
        TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                                                    &reason, &test) == LXA_OK);
        base = budget.used[LXA_BUDGET_MEMORY];
        budget.limits.max_memory_bytes = base;
        TEST_CHECK(lxa_dom_nodes_append_draft(test, LXA_DOM_KIND_DOCUMENT,
                                              &last) == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_MEMORY);
        TEST_CHECK(lxa_dom_nodes_length(test) == 0);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == base);
        TEST_CHECK(budget.used[LXA_BUDGET_NODES] == 0);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                                                    &reason, &test) == LXA_OK);
        base = budget.used[LXA_BUDGET_MEMORY];
        budget.limits.max_memory_bytes = base + sizeof(lxa_dom_record_t);
        TEST_CHECK(lxa_dom_nodes_append_character(test, LXA_DOM_KIND_TEXT,
                                                   input, &last) == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_MEMORY);
        TEST_CHECK(lxa_dom_nodes_length(test) == 0
                   && budget.used[LXA_BUDGET_NODES] == 0
                   && budget.used[LXA_BUDGET_STRINGS] == 0);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY]
                   == base + test->records.bytes.capacity);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        limits.max_memory_bytes = 1024 * 1024;
        limits.max_string_bytes = 2;
        TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                                                    &reason, &test) == LXA_OK);
        base = budget.used[LXA_BUDGET_MEMORY];
        TEST_CHECK(lxa_dom_nodes_append_character(test, LXA_DOM_KIND_TEXT,
                                                   input, &last) == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_STRINGS);
        TEST_CHECK(lxa_dom_nodes_length(test) == 0
                   && budget.used[LXA_BUDGET_STRINGS] == 0);
        {
            lxa_span_t short_text = {input.data, 1};
            TEST_CHECK(lxa_dom_nodes_append_character(test, LXA_DOM_KIND_TEXT,
                                                       short_text, &last) == LXA_OK);
            TEST_CHECK(budget.used[LXA_BUDGET_STRINGS] == 1);
            TEST_CHECK(budget.used[LXA_BUDGET_MEMORY]
                       == base + test->records.bytes.capacity
                          + test->characters.bytes.capacity + test->strings.capacity);
        }
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
    {
        lxa_dom_nodes_t *test = NULL;
        lxa_budget_t budget;
        lxa_limits_t limits = lxa_limits_default();
        lxa_dom_ref_t owner, fragment, other, content, found;
        lxa_dom_ref_t container, source;
        lxa_span_t template_name = {(const uint8_t *) "template", 8};
        lxa_span_t div_name = {(const uint8_t *) "div", 3};
        size_t retained;
        TEST_CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_create_with_budget(&allocator, &budget,
                                                     NULL, &test) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(test, template_name,
                                                      &owner) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(test, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                              &fragment) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_character(test, LXA_DOM_KIND_TEXT, input,
                                                   &content) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_template_contents(test, owner, &found) == LXA_OK
                   && found.handle == LXA_HANDLE_NONE);
        retained = budget.used[LXA_BUDGET_MEMORY];
        budget.limits.max_memory_bytes = retained;
        TEST_CHECK(lxa_dom_nodes_bind_template(test, owner, fragment)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_MEMORY
                   && lxa_vector_length(&test->templates) == 0);
        budget.limits.max_memory_bytes = limits.max_memory_bytes;
        state.fail_at = state.calls;
        TEST_CHECK(lxa_dom_nodes_bind_template(test, owner, fragment)
                   == LXA_ERROR_NO_MEMORY);
        TEST_CHECK(lxa_vector_length(&test->templates) == 0);
        state.fail_at = SIZE_MAX;
        TEST_CHECK(lxa_dom_nodes_bind_template(test, owner, fragment) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_template_contents(test, owner, &found) == LXA_OK
                   && found.handle == fragment.handle);
        TEST_CHECK(lxa_dom_nodes_bind_template(test, owner, fragment)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(test, fragment, owner)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(test, owner, content)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(test, fragment, content)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(test, owner, content)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_read(test, content, &record) == LXA_OK
                   && record.parent == test_slot(fragment));
        TEST_CHECK(lxa_dom_nodes_append_draft(test, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                              &source) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_html_element(test, div_name,
                                                      &container) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(test, source, container)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(test, container, owner)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_reparent_constructed_child(test, fragment,
                                                              container)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_read(test, container, &record) == LXA_OK
                   && record.parent == test_slot(source));
        TEST_CHECK(lxa_dom_nodes_append_html_element(test, template_name,
                                                      &other) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_bind_template(test, other, fragment)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        {
            lxa_dom_template_t *pair =
                (lxa_dom_template_t *) test->templates.bytes.data;
            pair->fragment = pair->element;
            TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_ERROR_ARGUMENT);
            pair->fragment = test_slot(fragment);
        }
        budget.limits.max_work = budget.used[LXA_BUDGET_WORK];
        TEST_CHECK(lxa_dom_nodes_template_contents(test, owner, &found)
                   == LXA_ERROR_LIMIT);
        TEST_CHECK(found.handle == LXA_HANDLE_NONE
                   && lxa_dom_nodes_limit_reason(test) == LXA_BUDGET_WORK);
        budget.limits.max_work = limits.max_work;
        TEST_CHECK(lxa_dom_nodes_reset(test) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_template_contents(test, owner, &found)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_vector_length(&test->templates) == 0);
        TEST_CHECK(lxa_dom_nodes_append_html_element(test, template_name,
                                                      &owner) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_draft(test, LXA_DOM_KIND_DOCUMENT_FRAGMENT,
                                              &fragment) == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_append_constructed_child(test, fragment, owner)
                   == LXA_OK);
        TEST_CHECK(lxa_dom_nodes_bind_template(test, owner, fragment)
                   == LXA_ERROR_ARGUMENT);
        TEST_CHECK(lxa_dom_nodes_validate(test) == LXA_OK);
        lxa_dom_nodes_destroy(test);
        TEST_CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
    return 0;
}
