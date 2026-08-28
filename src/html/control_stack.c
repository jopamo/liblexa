#include <assert.h>
#include <string.h>
#include "html/control_stack.h"
#include "html/foreign_names.h"

static lxa_status_t
internal(lxa_status_t status)
{
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return LXA_OK;
}

lxa_status_t
lxa_html_control_stack_init(lxa_html_control_stack_t *stack,
    const lxa_allocator_t *allocator, lxa_budget_t *budget)
{
    if (stack == NULL) return LXA_ERROR_ARGUMENT;
    memset(stack, 0, sizeof(*stack));
    stack->limit_reason = LXA_BUDGET_COUNT;
    if (budget == NULL) return LXA_ERROR_ARGUMENT;
    lxa_status_t status = lxa_vector_init(&stack->elements, allocator,
                                         sizeof(lxa_html_control_element_t));
    if (status == LXA_OK) status = lxa_buffer_init(&stack->names, allocator);
    if (status == LXA_OK) status = lxa_vector_bind_budget(&stack->elements, budget);
    if (status == LXA_OK) status = lxa_buffer_bind_budget(&stack->names, budget);
    if (status != LXA_OK) {
        lxa_html_control_stack_destroy(stack);
        return status;
    }
    stack->budget = budget;
    stack->next_identity = 1;
    return LXA_OK;
}

void
lxa_html_control_stack_destroy(lxa_html_control_stack_t *stack)
{
    if (stack == NULL) return;
    if (stack->budget != NULL)
        (void) internal(lxa_budget_release(stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
                                           lxa_vector_length(&stack->elements)));
    lxa_vector_destroy(&stack->elements);
    lxa_buffer_destroy(&stack->names);
    memset(stack, 0, sizeof(*stack));
    stack->limit_reason = LXA_BUDGET_COUNT;
}

static lxa_status_t
begin(lxa_html_control_stack_t *stack)
{
    if (stack == NULL) return LXA_ERROR_ARGUMENT;
    stack->limit_reason = LXA_BUDGET_COUNT;
    if (stack->budget == NULL) return LXA_ERROR_ARGUMENT;
    if (stack->elements.element_size != sizeof(lxa_html_control_element_t)
        || stack->elements.bytes.length > stack->elements.bytes.capacity
        || stack->elements.bytes.length % sizeof(lxa_html_control_element_t) != 0
        || stack->names.length > stack->names.capacity
        || stack->names.length > stack->charged_name_bytes
        || stack->elements.bytes.budget != stack->budget
        || stack->names.budget != stack->budget
        || (stack->elements.bytes.length != 0 && stack->elements.bytes.data == NULL)
        || (stack->names.length != 0 && stack->names.data == NULL))
        return internal(LXA_ERROR_INTERNAL);
    return LXA_OK;
}

static lxa_status_t
check_push(lxa_html_control_stack_t *stack, size_t total)
{
    lxa_status_t status = lxa_budget_check(stack->budget,
        LXA_BUDGET_OPEN_ELEMENTS, 1, &stack->limit_reason);
    if (status == LXA_OK && total > stack->charged_name_bytes)
        status = lxa_budget_check(stack->budget, LXA_BUDGET_STRINGS,
            total - stack->charged_name_bytes, &stack->limit_reason);
    return status;
}

lxa_status_t
lxa_html_control_stack_push(lxa_html_control_stack_t *stack, lxa_span_t name,
    lxa_dom_namespace_t ns, bool annotation_html, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    if (identity == NULL || name.data == NULL || name.length == 0
        || (unsigned) ns < LXA_DOM_NS_HTML || (unsigned) ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    if (stack->next_identity == 0) return LXA_ERROR_OVERFLOW;
    size_t total, count, source = 0;
    bool self = false;
    uintptr_t address = (uintptr_t) name.data;
    uintptr_t names = (uintptr_t) stack->names.data;
    uintptr_t elements = (uintptr_t) stack->elements.bytes.data;
    if (stack->elements.bytes.data != NULL && address >= elements
        && address - elements <= stack->elements.bytes.capacity)
        return LXA_ERROR_ARGUMENT;
    if (stack->names.data != NULL && address >= names
        && address - names <= stack->names.capacity) {
        source = (size_t) (address - names);
        if (source > stack->names.length
            || name.length > stack->names.length - source)
            return LXA_ERROR_ARGUMENT;
        self = true;
    }
    status = lxa_budget_check_item(stack->budget, LXA_BUDGET_NAME_BYTES,
                                   name.length, &stack->limit_reason);
    if (status == LXA_OK) status = lxa_size_add(stack->names.length, name.length, &total);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&stack->elements), 1, &count);
    if (status == LXA_OK) status = check_push(stack, total);
    if (status == LXA_OK)
        status = lxa_budget_charge(stack->budget, LXA_BUDGET_WORK,
            name.length / 8 + (name.length % 8 != 0), &stack->limit_reason);
    if (status == LXA_OK) status = lxa_vector_reserve(&stack->elements, count);
    if (status == LXA_OK) status = lxa_buffer_reserve(&stack->names, total);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT && stack->limit_reason == LXA_BUDGET_COUNT)
            stack->limit_reason = LXA_BUDGET_MEMORY;
        return status;
    }
    status = check_push(stack, total);
    if (status != LXA_OK) return status;
    lxa_html_control_element_t element = {
        stack->names.length, name.length, stack->next_identity, ns, annotation_html
    };
    status = self ? lxa_buffer_append_self(&stack->names, source, name.length)
                  : lxa_buffer_append(&stack->names, name);
    if (status == LXA_OK) status = lxa_vector_push(&stack->elements, &element);
    if (status != LXA_OK) {
        (void) internal(lxa_buffer_truncate(&stack->names, element.name_offset));
        return internal(status);
    }
    status = lxa_budget_charge(stack->budget, LXA_BUDGET_OPEN_ELEMENTS, 1, NULL);
    bool open_charged = status == LXA_OK;
    if (open_charged && total > stack->charged_name_bytes)
        status = lxa_budget_charge(stack->budget, LXA_BUDGET_STRINGS,
                                   total - stack->charged_name_bytes, NULL);
    if (status != LXA_OK) {
        if (open_charged)
            (void) internal(lxa_budget_release(stack->budget, LXA_BUDGET_OPEN_ELEMENTS, 1));
        (void) internal(lxa_vector_erase(&stack->elements, count - 1, 1));
        (void) internal(lxa_buffer_truncate(&stack->names, element.name_offset));
        return internal(status);
    }
    if (total > stack->charged_name_bytes) stack->charged_name_bytes = total;
    *identity = element.identity;
    ++stack->next_identity;
    return LXA_OK;
}

lxa_status_t
lxa_html_control_stack_push_token(lxa_html_control_stack_t *stack,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    if (identity == NULL || token == NULL || token->kind != LXA_HTML_START_TAG
        || token->name.data == NULL || token->name.length == 0
        || (unsigned) ns < LXA_DOM_NS_HTML || (unsigned) ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    if (stack->next_identity == 0) return LXA_ERROR_OVERFLOW;
    lxa_span_t name = token->name;
    bool annotation_html = false;
    if (ns == LXA_DOM_NS_SVG) {
        status = lxa_html_svg_tag_name(name, &name);
    }
    else if (ns == LXA_DOM_NS_MATHML
             && lxa_html_tree_name_equals(name, "annotation-xml", 14)) {
        status = lxa_html_foreign_annotation_attributes(token->attributes,
            token->attribute_count, stack->budget, &stack->limit_reason,
            &annotation_html);
    }
    return status == LXA_OK
        ? lxa_html_control_stack_push(stack, name, ns, annotation_html, identity)
        : status;
}

static lxa_status_t
read_element(const lxa_html_control_stack_t *stack, size_t index,
              lxa_html_scope_entry_t *entry, bool *annotation_html)
{
    const lxa_html_control_element_t *elements =
        (const lxa_html_control_element_t *) stack->elements.bytes.data;
    const lxa_html_control_element_t *element = &elements[index];
    if (element->identity == 0 || element->name_length == 0
        || element->name_offset > stack->names.length
        || element->name_length > stack->names.length - element->name_offset
        || (unsigned) element->namespace_id < LXA_DOM_NS_HTML
        || (unsigned) element->namespace_id > LXA_DOM_NS_SVG)
        return internal(LXA_ERROR_INTERNAL);
    *entry = (lxa_html_scope_entry_t){
        {stack->names.data + element->name_offset, element->name_length},
        element->namespace_id, element->identity
    };
    if (annotation_html != NULL) *annotation_html = element->annotation_html;
    return LXA_OK;
}

static lxa_status_t
retained_name_length(const lxa_html_control_stack_t *stack, size_t keep,
                      size_t *length)
{
    if (keep == 0) {
        *length = 0;
        return LXA_OK;
    }
    lxa_html_scope_entry_t entry;
    lxa_status_t status = read_element(stack, keep - 1, &entry, NULL);
    if (status != LXA_OK) return status;
    /* Push and move keep names in stack order; removal leaves only gaps. */
    const lxa_html_control_element_t *elements =
        (const lxa_html_control_element_t *) stack->elements.bytes.data;
    *length = elements[keep - 1].name_offset + elements[keep - 1].name_length;
    return LXA_OK;
}

lxa_status_t
lxa_html_control_stack_replace_identity(lxa_html_control_stack_t *stack,
    size_t index, uint64_t expected, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    if (identity == NULL || expected == 0 || index >= lxa_vector_length(&stack->elements))
        return LXA_ERROR_ARGUMENT;
    if (stack->next_identity == 0) return LXA_ERROR_OVERFLOW;
    status = lxa_budget_charge(stack->budget, LXA_BUDGET_WORK, 1, &stack->limit_reason);
    if (status != LXA_OK) return status;
    lxa_html_scope_entry_t entry;
    status = read_element(stack, index, &entry, NULL);
    if (status != LXA_OK) return status;
    if (entry.identity != expected) return LXA_ERROR_ARGUMENT;
    lxa_html_control_element_t *elements =
        (lxa_html_control_element_t *) stack->elements.bytes.data;
    elements[index].identity = stack->next_identity;
    *identity = stack->next_identity++;
    return LXA_OK;
}

static void
reverse_names(uint8_t *names, size_t start, size_t end)
{
    while (start < end) {
        --end;
        if (start == end) break;
        uint8_t byte = names[start];
        names[start++] = names[end];
        names[end] = byte;
    }
}

lxa_status_t
lxa_html_control_stack_move(lxa_html_control_stack_t *stack, size_t from, size_t to)
{
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length(&stack->elements);
    if (from >= length || to >= length) return LXA_ERROR_ARGUMENT;
    if (from == to) return LXA_OK;
    size_t low = from < to ? from : to, high = from < to ? to : from;
    size_t first = low == 0 ? 0 : low - 1;
    size_t end = high + 1 < length ? high + 2 : length;
    status = lxa_budget_charge(stack->budget, LXA_BUDGET_WORK, end - first,
                               &stack->limit_reason);
    if (status != LXA_OK) return status;
    lxa_html_control_element_t *elements =
        (lxa_html_control_element_t *) stack->elements.bytes.data;
    size_t previous_end = 0;
    for (size_t i = first; i < end; ++i) {
        lxa_html_scope_entry_t entry;
        status = read_element(stack, i, &entry, NULL);
        if (status != LXA_OK) return status;
        if (i != first && elements[i].name_offset < previous_end)
            return internal(LXA_ERROR_INTERNAL);
        previous_end = elements[i].name_offset + elements[i].name_length;
    }
    size_t start = elements[low].name_offset;
    size_t finish = elements[high].name_offset + elements[high].name_length;
    size_t bytes = finish - start;
    status = lxa_budget_charge(stack->budget, LXA_BUDGET_WORK,
                               bytes / 8 + (bytes % 8 != 0), &stack->limit_reason);
    if (status != LXA_OK) return status;
    lxa_html_control_element_t moved = elements[from];
    size_t pivot = from < to ? moved.name_offset + moved.name_length : moved.name_offset;
    reverse_names(stack->names.data, start, pivot);
    reverse_names(stack->names.data, pivot, finish);
    reverse_names(stack->names.data, start, finish);
    /* Vector geometry bounds the shifted byte count before multiplication. */
    if (from < to) {
        memmove(elements + from, elements + from + 1, (to - from) * sizeof(*elements));
        for (size_t i = from; i < to; ++i) elements[i].name_offset -= moved.name_length;
        moved.name_offset = finish - moved.name_length;
    }
    else {
        memmove(elements + to + 1, elements + to, (from - to) * sizeof(*elements));
        for (size_t i = to + 1; i <= from; ++i) elements[i].name_offset += moved.name_length;
        moved.name_offset = start;
    }
    elements[to] = moved;
    return LXA_OK;
}

lxa_status_t
lxa_html_control_stack_remove(lxa_html_control_stack_t *stack, size_t index)
{
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length(&stack->elements);
    if (index >= length) return LXA_ERROR_ARGUMENT;
    lxa_html_scope_entry_t entry;
    status = read_element(stack, index, &entry, NULL);
    if (status != LXA_OK) return status;
    size_t name_length = stack->names.length;
    if (index == length - 1) {
        status = retained_name_length(stack, index, &name_length);
        if (status != LXA_OK) return status;
    }
    if (stack->budget->used[LXA_BUDGET_OPEN_ELEMENTS] == 0)
        return internal(LXA_ERROR_INTERNAL);
    status = lxa_budget_charge(stack->budget, LXA_BUDGET_WORK,
                               length - index, &stack->limit_reason);
    if (status != LXA_OK) return status;
    if (internal(lxa_vector_erase(&stack->elements, index, 1)) != LXA_OK
        || internal(lxa_buffer_truncate(&stack->names, name_length)) != LXA_OK
        || internal(lxa_budget_release(stack->budget, LXA_BUDGET_OPEN_ELEMENTS, 1)) != LXA_OK)
        return LXA_ERROR_INTERNAL;
    return LXA_OK;
}

lxa_status_t
lxa_html_control_stack_pop_to(lxa_html_control_stack_t *stack, size_t keep)
{
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length(&stack->elements);
    if (keep > length) return LXA_ERROR_ARGUMENT;
    if (keep == length) return LXA_OK;
    lxa_html_scope_entry_t entry;
    status = read_element(stack, keep, &entry, NULL);
    if (status != LXA_OK) return status;
    size_t name_length;
    status = retained_name_length(stack, keep, &name_length);
    if (status != LXA_OK) return status;
    if (length - keep > stack->budget->used[LXA_BUDGET_OPEN_ELEMENTS])
        return internal(LXA_ERROR_INTERNAL);
    status = lxa_budget_charge(stack->budget, LXA_BUDGET_WORK,
                               length - keep, &stack->limit_reason);
    if (status != LXA_OK) return status;
    if (internal(lxa_vector_erase(&stack->elements, keep, length - keep)) != LXA_OK
        || internal(lxa_buffer_truncate(&stack->names, name_length)) != LXA_OK
        || internal(lxa_budget_release(stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
                                      length - keep)) != LXA_OK)
        return LXA_ERROR_INTERNAL;
    return LXA_OK;
}

lxa_status_t
lxa_html_control_stack_get(lxa_html_control_stack_t *stack, size_t index,
    lxa_html_scope_entry_t *entry, lxa_html_foreign_context_t *context)
{
    if (entry != NULL) memset(entry, 0, sizeof(*entry));
    if (context != NULL) *context = LXA_HTML_FOREIGN_NONE;
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    if (entry == NULL || index >= lxa_vector_length(&stack->elements))
        return LXA_ERROR_ARGUMENT;
    status = lxa_budget_charge(stack->budget, LXA_BUDGET_WORK,
                               1, &stack->limit_reason);
    if (status != LXA_OK) return status;
    bool annotation_html;
    status = read_element(stack, index, entry, &annotation_html);
    if (status == LXA_OK && context != NULL)
        *context = lxa_html_foreign_context(entry->namespace_id, entry->name,
                                            annotation_html);
    return status;
}

typedef struct {
    lxa_html_control_stack_t *stack;
    size_t length;
} control_stack_view_t;

static inline lxa_status_t
control_stack_read(const control_stack_view_t *view, size_t index,
                    lxa_span_t *name, lxa_dom_namespace_t *ns)
{
    lxa_html_scope_entry_t entry;
    lxa_status_t status = read_element(view->stack, index, &entry, NULL);
    if (status == LXA_OK) {
        *name = entry.name;
        *ns = entry.namespace_id;
    }
    return status;
}

static inline lxa_status_t
control_stack_identity(const control_stack_view_t *view, size_t index,
                        uint64_t *identity)
{
    const lxa_html_control_element_t *entries =
        (const lxa_html_control_element_t *) view->stack->elements.bytes.data;
    *identity = entries[index].identity;
    return *identity == 0 ? internal(LXA_ERROR_INTERNAL) : LXA_OK;
}

#define LXA_SPECIAL_NAME control_stack_special_find
#define LXA_SPECIAL_VIEW control_stack_view_t
#define LXA_SPECIAL_LENGTH(view) ((view)->length)
#define LXA_SPECIAL_READ control_stack_read
#define LXA_SPECIAL_BUDGET(view) ((view)->stack->budget)
#define LXA_SPECIAL_REASON(view) (&(view)->stack->limit_reason)
#include "html/special_find.inc"
#undef LXA_SPECIAL_NAME
#undef LXA_SPECIAL_VIEW
#undef LXA_SPECIAL_LENGTH
#undef LXA_SPECIAL_READ
#undef LXA_SPECIAL_BUDGET
#undef LXA_SPECIAL_REASON

static inline lxa_status_t
control_stack_namespace(const control_stack_view_t *view, size_t index,
                         lxa_dom_namespace_t *ns)
{
    const lxa_html_control_element_t *entries =
        (const lxa_html_control_element_t *) view->stack->elements.bytes.data;
    if ((unsigned) entries[index].namespace_id < LXA_DOM_NS_HTML
        || (unsigned) entries[index].namespace_id > LXA_DOM_NS_SVG)
        return internal(LXA_ERROR_INTERNAL);
    *ns = entries[index].namespace_id;
    return LXA_OK;
}

#define LXA_CURRENT_NAMESPACE_NAME control_stack_current_namespace
#define LXA_CURRENT_NAMESPACE_VIEW control_stack_view_t
#define LXA_CURRENT_NAMESPACE_LENGTH(view) ((view)->length)
#define LXA_CURRENT_NAMESPACE_READ control_stack_namespace
#include "html/current_namespace.inc"
#undef LXA_CURRENT_NAMESPACE_NAME
#undef LXA_CURRENT_NAMESPACE_VIEW
#undef LXA_CURRENT_NAMESPACE_LENGTH
#undef LXA_CURRENT_NAMESPACE_READ

lxa_status_t
lxa_html_control_stack_current_namespace(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_dom_namespace_t *out)
{
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    const control_stack_view_t view = {stack, lxa_vector_length(&stack->elements)};
    return control_stack_current_namespace(&view, boundary, out);
}

#define LXA_SCOPE_NAME control_stack_scope_search
#define LXA_ORDINARY_NAME control_stack_ordinary_search
#define LXA_COLUMN_GROUP_NAME control_stack_column_group
#define LXA_SCOPE_PLAN_NAME control_stack_scope_plan
#define LXA_ORDINARY_PLAN_NAME control_stack_ordinary_plan
#define LXA_OPEN_REFERENCE_NAME control_stack_open_reference
#define LXA_SCOPED_REFERENCE_NAME control_stack_scoped_reference
#define LXA_IMPLIED_PLAN_NAME control_stack_implied_plan
#define LXA_RUBY_PLAN_NAME control_stack_ruby_plan
#define LXA_SELECT_PLAN_NAME control_stack_select_plan
#define LXA_TABLE_CONTEXT_PLAN_NAME control_stack_table_context_plan
#define LXA_SCOPE_NAMESPACE control_stack_namespace
#define LXA_SCOPE_IDENTITY control_stack_identity
#define LXA_SCOPE_VIEW control_stack_view_t
#define LXA_SCOPE_LENGTH(view) ((view)->length)
#define LXA_SCOPE_BUDGET(view) ((view)->stack->budget)
#define LXA_SCOPE_REASON(view) (&(view)->stack->limit_reason)
#define LXA_SCOPE_READ control_stack_read
#include "html/end_target_find.inc"
#undef LXA_SCOPE_READ
#undef LXA_SCOPE_REASON
#undef LXA_SCOPE_BUDGET
#undef LXA_SCOPE_LENGTH
#undef LXA_SCOPE_VIEW
#undef LXA_SCOPE_NAME
#undef LXA_ORDINARY_NAME
#undef LXA_COLUMN_GROUP_NAME
#undef LXA_SCOPE_PLAN_NAME
#undef LXA_ORDINARY_PLAN_NAME
#undef LXA_OPEN_REFERENCE_NAME
#undef LXA_SCOPED_REFERENCE_NAME
#undef LXA_IMPLIED_PLAN_NAME
#undef LXA_RUBY_PLAN_NAME
#undef LXA_SELECT_PLAN_NAME
#undef LXA_TABLE_CONTEXT_PLAN_NAME
#undef LXA_SCOPE_NAMESPACE
#undef LXA_SCOPE_IDENTITY

static inline lxa_status_t
control_stack_name(const control_stack_view_t *view, size_t index, lxa_span_t *name)
{
    lxa_html_scope_entry_t entry;
    lxa_status_t status = read_element(view->stack, index, &entry, NULL);
    if (status == LXA_OK) *name = entry.name;
    return status;
}

static inline lxa_status_t
control_stack_breakout_boundary(const control_stack_view_t *view, size_t index,
                                bool *boundary)
{
    lxa_dom_namespace_t ns;
    lxa_status_t status = control_stack_namespace(view, index, &ns);
    if (status != LXA_OK) return status;
    if (ns == LXA_DOM_NS_HTML) {
        *boundary = true;
        return LXA_OK;
    }
    lxa_html_scope_entry_t entry;
    bool annotation_html;
    status = read_element(view->stack, index, &entry, &annotation_html);
    if (status == LXA_OK)
        *boundary = lxa_html_foreign_integration(lxa_html_foreign_context(
            ns, entry.name, annotation_html));
    return status;
}

#define LXA_FOREIGN_VIEW control_stack_view_t
#define LXA_FOREIGN_LENGTH(view) ((view)->length)
#define LXA_FOREIGN_BUDGET(view) ((view)->stack->budget)
#define LXA_FOREIGN_REASON(view) (&(view)->stack->limit_reason)
#define LXA_FOREIGN_NAMESPACE control_stack_namespace
#define LXA_FOREIGN_NAME control_stack_name
#define LXA_FOREIGN_BREAKOUT_BOUNDARY control_stack_breakout_boundary
#define LXA_FOREIGN_END_PLAN_NAME control_stack_foreign_end_plan
#define LXA_FOREIGN_BREAKOUT_PLAN_NAME control_stack_foreign_breakout_plan
#include "html/foreign_stack_plan.inc"
#undef LXA_FOREIGN_VIEW
#undef LXA_FOREIGN_LENGTH
#undef LXA_FOREIGN_BUDGET
#undef LXA_FOREIGN_REASON
#undef LXA_FOREIGN_NAMESPACE
#undef LXA_FOREIGN_NAME
#undef LXA_FOREIGN_BREAKOUT_BOUNDARY
#undef LXA_FOREIGN_END_PLAN_NAME
#undef LXA_FOREIGN_BREAKOUT_PLAN_NAME

static lxa_status_t
prepare_query(lxa_html_control_stack_t *stack, size_t boundary,
               control_stack_view_t *view)
{
    lxa_status_t status = begin(stack);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length(&stack->elements);
    if (boundary > length) return LXA_ERROR_ARGUMENT;
    *view = (control_stack_view_t){stack, length};
    return LXA_OK;
}

lxa_status_t
lxa_html_control_stack_scope_find(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, lxa_html_scope_kind_t scope, size_t *index)
{
    if (index != NULL) *index = 0;
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (index == NULL || target.data == NULL || target.length == 0
        || !lxa_html_scope_kind_valid(scope))
        return LXA_ERROR_ARGUMENT;
    return control_stack_scope_search(&view, boundary, target, scope, index);
}

lxa_status_t
lxa_html_control_stack_ordinary_end_find(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, size_t *index)
{
    if (index != NULL) *index = 0;
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (index == NULL || target.data == NULL || target.length == 0)
        return LXA_ERROR_ARGUMENT;
    return control_stack_ordinary_search(&view, boundary, target, index);
}

lxa_status_t
lxa_html_control_stack_open_reference_find(lxa_html_control_stack_t *stack,
    size_t boundary, uint64_t identity, size_t *index)
{
    if (index != NULL) *index = 0;
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (index == NULL || identity == 0) return LXA_ERROR_ARGUMENT;
    return control_stack_open_reference(&view, boundary, identity, index);
}

lxa_status_t
lxa_html_control_stack_special_find(lxa_html_control_stack_t *stack,
    size_t start, size_t *index)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, start, &view);
    if (status != LXA_OK) return status;
    if (index == NULL) return LXA_ERROR_ARGUMENT;
    return control_stack_special_find(&view, start, index);
}

lxa_status_t
lxa_html_control_stack_scoped_reference_find(lxa_html_control_stack_t *stack,
    size_t boundary, uint64_t identity, size_t *index)
{
    if (index != NULL) *index = 0;
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (index == NULL || identity == 0) return LXA_ERROR_ARGUMENT;
    return control_stack_scoped_reference(&view, boundary, identity, index);
}

lxa_status_t
lxa_html_control_stack_scope_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, lxa_html_scope_kind_t scope,
    lxa_html_end_target_plan_t *out)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL || target.data == NULL || target.length == 0
        || !lxa_html_scope_kind_valid(scope))
        return LXA_ERROR_ARGUMENT;
    return control_stack_scope_plan(&view, boundary, target, scope, out);
}

lxa_status_t
lxa_html_control_stack_ordinary_end_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, lxa_html_end_target_plan_t *out)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL || target.data == NULL || target.length == 0)
        return LXA_ERROR_ARGUMENT;
    return control_stack_ordinary_plan(&view, boundary, target, out);
}

lxa_status_t
lxa_html_control_stack_implied_end_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t except, size_t *out)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL || (except.length != 0 && except.data == NULL))
        return LXA_ERROR_ARGUMENT;
    return control_stack_implied_plan(&view, boundary, except, out);
}

lxa_status_t
lxa_html_control_stack_ruby_start_plan(lxa_html_control_stack_t *stack,
    size_t boundary, bool except_rtc, size_t *out)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    return control_stack_ruby_plan(&view, boundary, except_rtc, out);
}

lxa_status_t
lxa_html_control_stack_foreign_end_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t name, lxa_html_foreign_end_plan_t *out)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL || name.data == NULL || name.length == 0)
        return LXA_ERROR_ARGUMENT;
    return control_stack_foreign_end_plan(&view, boundary, name, out);
}

lxa_status_t
lxa_html_control_stack_foreign_breakout_plan(lxa_html_control_stack_t *stack,
    size_t boundary, size_t *out)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    return control_stack_foreign_breakout_plan(&view, boundary, out);
}

lxa_status_t
lxa_html_control_stack_select_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_html_select_plan_t *out)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL || !lxa_html_select_arguments_valid(kind, name))
        return LXA_ERROR_ARGUMENT;
    return control_stack_select_plan(&view, boundary, kind, name, out);
}

lxa_status_t
lxa_html_control_stack_table_context_plan(lxa_html_control_stack_t *stack,
    size_t boundary, bool virtual_table, lxa_html_table_context_plan_t *out)
{
    control_stack_view_t view;
    lxa_status_t status = prepare_query(stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    return control_stack_table_context_plan(&view, boundary, virtual_table, out);
}
