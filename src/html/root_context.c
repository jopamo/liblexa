#include <assert.h>
#include <string.h>
#include "html/root_context.h"

static lxa_status_t
begin(lxa_html_root_context_t *context)
{
    if (context == NULL || context->stack == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    context->limit_reason = LXA_BUDGET_COUNT;
    if ((context->head && !context->html) || (context->body && !context->head)) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    lxa_dom_namespace_t ns;
    return lxa_html_control_stack_current_namespace(context->stack, 0, &ns);
}

static lxa_status_t
finish(lxa_html_root_context_t *context, lxa_status_t status)
{
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->stack->limit_reason;
    if (status != LXA_OK) context->failure = status;
    return status;
}

lxa_status_t
lxa_html_root_context_init(lxa_html_root_context_t *context,
    lxa_html_control_stack_t *stack)
{
    if (context == NULL) return LXA_ERROR_ARGUMENT;
    memset(context, 0, sizeof(*context));
    context->limit_reason = LXA_BUDGET_COUNT;
    lxa_dom_namespace_t ns;
    lxa_status_t status = lxa_html_control_stack_current_namespace(stack, 0, &ns);
    if (status != LXA_OK) return status;
    if (lxa_vector_length(&stack->elements) != 0) return LXA_ERROR_ARGUMENT;
    context->stack = stack;
    context->frameset_ok = true;
    return LXA_OK;
}

void
lxa_html_root_context_destroy(lxa_html_root_context_t *context)
{
    if (context == NULL) return;
    memset(context, 0, sizeof(*context));
    context->limit_reason = LXA_BUDGET_COUNT;
}

static lxa_status_t
create(lxa_html_root_context_t *context, lxa_html_root_element_t element)
{
    lxa_budget_t *budget = context->stack->budget;
    lxa_status_t status = lxa_budget_check_item(budget, LXA_BUDGET_NAME_BYTES, 4,
        &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(budget, LXA_BUDGET_TREE_DEPTH,
            element == LXA_HTML_ROOT_ELEMENT_HTML ? 1 : 2, &context->limit_reason);
    if (status == LXA_OK) {
        switch (element) {
            case LXA_HTML_ROOT_ELEMENT_HTML: context->html = true; break;
            case LXA_HTML_ROOT_ELEMENT_HEAD: context->head = true; break;
            case LXA_HTML_ROOT_ELEMENT_BODY: context->body = true; break;
            default: assert(false); return LXA_ERROR_INTERNAL;
        }
    }
    return status;
}

static lxa_status_t
open_body(lxa_html_root_context_t *context)
{
    uint64_t identity;
    return lxa_html_control_stack_push(context->stack,
        (lxa_span_t){(const uint8_t *) "body", 4}, LXA_DOM_NS_HTML, false, &identity);
}

#define LXA_ROOT_ENSURE_NAME(name) context_ensure_##name
#define LXA_ROOT_ENSURE_VIEW lxa_html_root_context_t
#define LXA_ROOT_ENSURE_HAS(view, element) LXA_ROOT_HAS_##element(view)
#define LXA_ROOT_HAS_HTML(view) ((view)->html)
#define LXA_ROOT_HAS_HEAD(view) ((view)->head)
#define LXA_ROOT_HAS_BODY(view) ((view)->body)
#define LXA_ROOT_ENSURE_CREATE create
#define LXA_ROOT_ENSURE_OPEN_LENGTH(view) lxa_vector_length(&(view)->stack->elements)
#define LXA_ROOT_ENSURE_OPEN_BODY open_body
#define LXA_ROOT_ENSURE_FRAMESET(view) ((view)->frameset_ok = true)
#include "html/root_ensure.inc"
#undef LXA_ROOT_ENSURE_NAME
#undef LXA_ROOT_ENSURE_VIEW
#undef LXA_ROOT_ENSURE_HAS
#undef LXA_ROOT_HAS_HTML
#undef LXA_ROOT_HAS_HEAD
#undef LXA_ROOT_HAS_BODY
#undef LXA_ROOT_ENSURE_CREATE
#undef LXA_ROOT_ENSURE_OPEN_LENGTH
#undef LXA_ROOT_ENSURE_OPEN_BODY
#undef LXA_ROOT_ENSURE_FRAMESET

#define LXA_ROOT_ENSURE_PUBLIC(name) \
lxa_status_t lxa_html_root_context_ensure_##name(lxa_html_root_context_t *context) \
{ \
    lxa_status_t status = begin(context); \
    if (status != LXA_OK) return status; \
    return finish(context, context_ensure_##name(context)); \
}
LXA_ROOT_ENSURE_PUBLIC(html)
LXA_ROOT_ENSURE_PUBLIC(head)
LXA_ROOT_ENSURE_PUBLIC(body)
#undef LXA_ROOT_ENSURE_PUBLIC
