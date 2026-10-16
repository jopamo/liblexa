#ifndef LIBLEXA_HTML_CONTROL_STACK_INTERNAL_H
#define LIBLEXA_HTML_CONTROL_STACK_INTERNAL_H

#include <assert.h>
#include "base/vector_internal.h"
#include "html/control_stack.h"

static inline lxa_status_t
lxa_html_control_stack_begin_impl(lxa_html_control_stack_t *stack)
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
    {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return LXA_OK;
}

typedef struct {
    lxa_html_control_stack_t *stack;
    size_t length;
} control_stack_view_t;

static inline lxa_status_t
control_stack_namespace(const control_stack_view_t *view, size_t index,
                         lxa_dom_namespace_t *ns)
{
    const lxa_html_control_element_t *entries =
        (const lxa_html_control_element_t *) view->stack->elements.bytes.data;
    if ((unsigned) entries[index].namespace_id < LXA_DOM_NS_HTML
        || (unsigned) entries[index].namespace_id > LXA_DOM_NS_SVG)
    {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
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

static inline lxa_status_t
lxa_html_control_stack_current_namespace_impl(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_dom_namespace_t *out)
{
    lxa_status_t status = lxa_html_control_stack_begin_impl(stack);
    if (status != LXA_OK) return status;
    const control_stack_view_t view = {stack, lxa_vector_length_sized(
        &stack->elements, sizeof(lxa_html_control_element_t))};
    return control_stack_current_namespace(&view, boundary, out);
}


#endif
