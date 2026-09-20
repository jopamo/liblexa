#include <assert.h>
#include <string.h>
#include "html/root_context.h"
#include "html/tree_categories.h"

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

static bool
body_start_mode_valid(const lxa_html_root_context_t *context, lxa_html_root_mode_t mode)
{
    return (mode == LXA_TREE_BEFORE_HTML || mode == LXA_TREE_BEFORE_HEAD
            || mode == LXA_TREE_IN_HEAD || mode == LXA_TREE_AFTER_HEAD || mode == LXA_TREE_IN_BODY)
        && (mode != LXA_TREE_BEFORE_HEAD || context->html)
        && ((mode != LXA_TREE_IN_HEAD && mode != LXA_TREE_AFTER_HEAD) || context->head);
}

static lxa_status_t
reserve_open_slots(lxa_html_root_context_t *context, size_t additional)
{
    size_t count;
    lxa_status_t status = lxa_budget_check(context->stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
        additional, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&context->stack->elements), additional, &count);
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&context->stack->elements, count);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = LXA_BUDGET_MEMORY;
    }
    return status;
}

static bool
frameset_owner_valid(const lxa_html_root_context_t *context, lxa_html_root_mode_t mode)
{
    size_t length = lxa_vector_length(&context->stack->elements);
    return context->head && !context->body && lxa_html_tree_frameset_mode(mode)
        && (mode != LXA_TREE_IN_FRAMESET || length != 0)
        && (mode != LXA_TREE_AFTER_FRAMESET || length == 0);
}

static lxa_status_t
reserve_open_slot(lxa_html_root_context_t *context)
{
    return reserve_open_slots(context, 1);
}

static lxa_status_t
check_body_child_depth(lxa_html_root_context_t *context, size_t retained)
{
    size_t depth;
    lxa_status_t status = lxa_size_add(retained, 2, &depth);
    return status == LXA_OK
        ? lxa_budget_check(context->stack->budget, LXA_BUDGET_TREE_DEPTH,
            depth, &context->limit_reason) : status;
}

#define LXA_FRAMESET_END_NAME context_frameset_end
#define LXA_FRAMESET_END_VIEW lxa_html_root_context_t
#define LXA_FRAMESET_END_LENGTH(view) lxa_vector_length(&(view)->stack->elements)
#define LXA_FRAMESET_END_POP(view) \
    lxa_html_control_stack_pop_to((view)->stack, LXA_FRAMESET_END_LENGTH(view) - 1)
#include "html/frameset_end.inc"
#undef LXA_FRAMESET_END_NAME
#undef LXA_FRAMESET_END_VIEW
#undef LXA_FRAMESET_END_LENGTH
#undef LXA_FRAMESET_END_POP

lxa_status_t
lxa_html_root_context_end_frameset(lxa_html_root_context_t *context,
    lxa_span_t name, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || !context->head || context->body)
        return LXA_ERROR_ARGUMENT;
    lxa_html_frameset_action_t action;
    status = lxa_html_tree_frameset_action(*mode, LXA_HTML_END_TAG, name, &action);
    if (status != LXA_OK) return status;
    if ((action != LXA_HTML_FRAMESET_END && action != LXA_HTML_FRAMESET_HTML_END)
        || !frameset_owner_valid(context, *mode))
        return LXA_ERROR_ARGUMENT;
    return finish(context, context_frameset_end(context, action, mode));
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
    lxa_status_t status = lxa_html_control_stack_push(context->stack,
        (lxa_span_t){(const uint8_t *) "body", 4}, LXA_DOM_NS_HTML, false, &identity);
    if (status == LXA_OK) context->body_identity = identity;
    return status;
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

#define LXA_ROOT_START_NAME context_start_root
#define LXA_ROOT_START_VIEW lxa_html_root_context_t
#define LXA_ROOT_START_HTML context_ensure_html
#define LXA_ROOT_START_HEAD context_ensure_head
#define LXA_ROOT_START_BODY context_ensure_body
#define LXA_ROOT_START_FRAMESET(view) ((view)->frameset_ok = false)
#define LXA_ROOT_START_ATTRIBUTES(view, element, token) \
    ((void) (view), (void) (element), (void) (token), LXA_OK)
#include "html/root_start.inc"
#undef LXA_ROOT_START_NAME
#undef LXA_ROOT_START_VIEW
#undef LXA_ROOT_START_HTML
#undef LXA_ROOT_START_HEAD
#undef LXA_ROOT_START_BODY
#undef LXA_ROOT_START_FRAMESET
#undef LXA_ROOT_START_ATTRIBUTES

#define LXA_ROOT_START_DISPATCH_NAME context_dispatch_root_start
#define LXA_ROOT_START_DISPATCH_VIEW lxa_html_root_context_t
#define LXA_ROOT_START_DISPATCH_EXECUTE context_start_root
#include "html/root_start_dispatch.inc"
#undef LXA_ROOT_START_DISPATCH_NAME
#undef LXA_ROOT_START_DISPATCH_VIEW
#undef LXA_ROOT_START_DISPATCH_EXECUTE

lxa_status_t
lxa_html_root_context_start_root(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG)
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_start_action_t action;
    status = lxa_html_tree_root_start_action(*mode, token->name, &action);
    if (status != LXA_OK) return status;
    bool noscript = *mode == LXA_TREE_HEAD_NOSCRIPT && context->head && !context->body
        && (action == LXA_HTML_ROOT_START_HTML || action == LXA_HTML_ROOT_START_IGNORE_HEAD);
    bool after_body = (*mode == LXA_TREE_AFTER_BODY || *mode == LXA_TREE_AFTER_HTML)
        && context->body && action == LXA_HTML_ROOT_START_HTML;
    if (!body_start_mode_valid(context, *mode) && !noscript && !after_body)
        return LXA_ERROR_ARGUMENT;
    if (!context->body && lxa_vector_length(&context->stack->elements) != 0)
        return LXA_ERROR_ARGUMENT;
    if (action == LXA_HTML_ROOT_START_OTHER) return LXA_ERROR_ARGUMENT;
    if (*mode == LXA_TREE_IN_BODY && lxa_html_tree_root_control_group(token->kind, token->name))
        status = lxa_html_root_context_control_group(context, *mode, token->kind, token->name);
    if (status == LXA_OK)
        status = context_dispatch_root_start(context, action, token, mode);
    return finish(context, status);
}

#define LXA_ROOT_END_NAME context_prebody_end
#define LXA_ROOT_END_VIEW lxa_html_root_context_t
#define LXA_ROOT_END_HTML context_ensure_html
#define LXA_ROOT_END_HEAD context_ensure_head
#define LXA_ROOT_END_BODY context_ensure_body
#include "html/root_prebody_end.inc"
#undef LXA_ROOT_END_NAME
#undef LXA_ROOT_END_VIEW
#undef LXA_ROOT_END_HTML
#undef LXA_ROOT_END_HEAD
#undef LXA_ROOT_END_BODY

#define LXA_ROOT_FINISH_NAME context_finish_root
#define LXA_ROOT_FINISH_VIEW lxa_html_root_context_t
#define LXA_ROOT_FINISH_BODY context_ensure_body
#define LXA_ROOT_FINISH_POP(view) lxa_html_control_stack_pop_to((view)->stack, 0)
#include "html/root_finish.inc"
#undef LXA_ROOT_FINISH_NAME
#undef LXA_ROOT_FINISH_VIEW
#undef LXA_ROOT_FINISH_BODY
#undef LXA_ROOT_FINISH_POP

lxa_status_t
lxa_html_root_context_finish(lxa_html_root_context_t *context, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || !lxa_html_tree_root_mode_valid(*mode)
        || *mode == LXA_TREE_INITIAL || *mode == LXA_TREE_DONE
        || *mode == LXA_TREE_TEXT || *mode == LXA_TREE_TEMPLATE_TEXT)
        return LXA_ERROR_ARGUMENT;
    return finish(context, context_finish_root(context, mode));
}

#define LXA_ROOT_HEAD_END_NAME context_head_end
#define LXA_ROOT_HEAD_END_VIEW lxa_html_root_context_t
#define LXA_ROOT_HEAD_END_PREPARE(view, action) ((void) (view), (void) (action), LXA_OK)
#define LXA_ROOT_HEAD_END_BODY context_ensure_body
#include "html/root_head_end.inc"
#undef LXA_ROOT_HEAD_END_NAME
#undef LXA_ROOT_HEAD_END_VIEW
#undef LXA_ROOT_HEAD_END_PREPARE
#undef LXA_ROOT_HEAD_END_BODY

lxa_status_t
lxa_html_root_context_head_end(lxa_html_root_context_t *context, lxa_span_t name,
    lxa_html_root_mode_t *mode, lxa_html_head_end_action_t *action)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || action == NULL || *mode != LXA_TREE_IN_HEAD || !context->head
        || context->body || lxa_vector_length(&context->stack->elements) != 0)
        return LXA_ERROR_ARGUMENT;
    lxa_html_head_end_action_t out;
    status = lxa_html_tree_head_end_action(name, &out);
    if (status != LXA_OK) return status;
    status = context_head_end(context, out, mode);
    if (status == LXA_OK) *action = out;
    return finish(context, status);
}

#define LXA_ROOT_TEXT_PREPARE_NAME context_prepare_text
#define LXA_ROOT_TEXT_PREPARE_VIEW lxa_html_root_context_t
#define LXA_ROOT_TEXT_PREPARE_BODY context_ensure_body
#define LXA_ROOT_TEXT_PREPARE_HEAD context_ensure_head
#define LXA_ROOT_TEXT_PREPARE_FRAMESET(view) ((view)->frameset_ok)
#include "html/root_text_prepare.inc"
#undef LXA_ROOT_TEXT_PREPARE_NAME
#undef LXA_ROOT_TEXT_PREPARE_VIEW
#undef LXA_ROOT_TEXT_PREPARE_BODY
#undef LXA_ROOT_TEXT_PREPARE_HEAD
#undef LXA_ROOT_TEXT_PREPARE_FRAMESET

lxa_status_t
lxa_html_root_context_start_text(lxa_html_root_context_t *context,
    lxa_html_text_context_t *text, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, bool *skip_newline)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || text == NULL || tokenizer == NULL || skip_newline == NULL
        || text->plan.mode != LXA_HTML_DATA || text->plan.end_tag.data != NULL
        || text->plan.end_tag.length != 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG) return LXA_ERROR_ARGUMENT;
    lxa_html_root_text_plan_t plan;
    status = lxa_html_tree_root_text_plan(*mode, token->name, &plan);
    if (status != LXA_OK) return status;
    bool frameset = lxa_html_tree_frameset_mode(*mode);
    size_t length = lxa_vector_length(&context->stack->elements);
    if ((!plan.body && length != 0 && !frameset)
        || (frameset && !frameset_owner_valid(context, *mode))
        || (*mode == LXA_TREE_BEFORE_HEAD && !context->html)
        || ((*mode == LXA_TREE_IN_HEAD || *mode == LXA_TREE_HEAD_NOSCRIPT
            || *mode == LXA_TREE_AFTER_HEAD) && !context->head))
        return LXA_ERROR_ARGUMENT;
    status = context_prepare_text(context, &plan, mode);
    if (status == LXA_OK) {
        status = lxa_html_text_context_enter(text, tokenizer, token->name,
            plan.return_mode, &context->limit_reason);
    }
    if (status == LXA_OK) {
        *mode = LXA_TREE_TEXT;
        *skip_newline = plan.skip_newline;
    }
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_start_plaintext(lxa_html_root_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || identity == NULL || tokenizer == NULL
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "plaintext", 9)
        || !body_start_mode_valid(context, *mode))
        return LXA_ERROR_ARGUMENT;
    status = context_ensure_body(context);
    size_t keep;
    if (status == LXA_OK)
        status = lxa_html_control_stack_scope_find(context->stack, 0,
            (lxa_span_t){(const uint8_t *) "p", 1}, LXA_HTML_SCOPE_BUTTON, &keep);
    if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
    bool group;
    if (status == LXA_OK)
        status = lxa_html_control_stack_column_group(context->stack, 0, &group);
    if (status == LXA_OK) {
        size_t length = lxa_vector_length(&context->stack->elements);
        size_t retained = length - group, count;
        status = check_body_child_depth(context, retained);
        if (status == LXA_OK)
            status = lxa_budget_check(context->stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
                group ? 0 : 1, &context->limit_reason);
        if (status == LXA_OK)
            status = lxa_budget_check_item(context->stack->budget, LXA_BUDGET_NAME_BYTES,
                token->name.length, &context->limit_reason);
        if (status == LXA_OK) status = lxa_size_add(retained, 1, &count);
        if (status == LXA_OK) status = lxa_vector_reserve(&context->stack->elements, count);
        if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
            context->limit_reason = LXA_BUDGET_MEMORY;
        if (status == LXA_OK && group)
            status = lxa_html_control_stack_pop_to(context->stack, retained);
    }
    if (status == LXA_OK)
        status = lxa_html_control_stack_push_token(context->stack, token, LXA_DOM_NS_HTML, identity);
    if (status == LXA_OK) {
        *mode = LXA_TREE_IN_BODY;
        status = lxa_html_tree_text_start_feedback(tokenizer, token->name, &context->limit_reason);
    }
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

static lxa_status_t
context_reconstruct(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, bool after_frameset)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget
        || (after_frameset ? (!context->head || context->body)
            : (!context->body || lxa_vector_length(&context->stack->elements) == 0)))
        return LXA_ERROR_ARGUMENT;
    size_t start;
    status = lxa_html_formatting_list_reconstruct_plan(formatting, context->stack, 0, &start);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = formatting->store->limit_reason;
    if (status != LXA_OK) return finish(context, status);
    size_t length = lxa_vector_length(&formatting->entries);
    if (start == length) return LXA_OK;
    if (start > length) {
        assert(false);
        return finish(context, LXA_ERROR_INTERNAL);
    }
    size_t depth;
    status = lxa_size_add(lxa_vector_length(&context->stack->elements), length - start, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, 1, &depth);
    if (status == LXA_OK)
        status = lxa_budget_check(context->stack->budget, LXA_BUDGET_TREE_DEPTH,
            depth, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_html_formatting_list_reconstruct_planned(formatting, context->stack, 0, start);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = formatting->store->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_reconstruct(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting)
{
    return context_reconstruct(context, formatting, false);
}

lxa_status_t
lxa_html_root_context_reconstruct_after_frameset(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting)
{
    return context_reconstruct(context, formatting, true);
}

lxa_status_t
lxa_html_root_context_prepare_frameset_data(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_html_root_mode_t mode,
    lxa_span_t data, lxa_buffer_t *spaces)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!frameset_owner_valid(context, mode) || spaces == NULL
        || spaces->budget != context->stack->budget
        || (formatting != NULL && (formatting->store == NULL
            || formatting->store->budget != context->stack->budget))
        || (mode == LXA_TREE_AFTER_AFTER_FRAMESET && formatting == NULL))
        return LXA_ERROR_ARGUMENT;
    status = lxa_html_tree_filter_spaces(data, spaces, &context->limit_reason);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status == LXA_OK && spaces->length != 0 && mode == LXA_TREE_AFTER_AFTER_FRAMESET)
        status = lxa_html_root_context_reconstruct_after_frameset(context, formatting);
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_start_xmp(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_html_text_context_t *text,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget || text == NULL || tokenizer == NULL
        || text->plan.mode != LXA_HTML_DATA || text->plan.end_tag.data != NULL
        || text->plan.end_tag.length != 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG || !lxa_html_tree_name_equals(token->name, "xmp", 3)
        || !body_start_mode_valid(context, *mode)) return LXA_ERROR_ARGUMENT;
    status = context_ensure_body(context);
    bool group;
    if (status == LXA_OK)
        status = lxa_html_control_stack_column_group(context->stack, 0, &group);
    if (status == LXA_OK && group)
        status = lxa_html_control_stack_pop_to(context->stack,
            lxa_vector_length(&context->stack->elements) - 1);
    size_t keep;
    if (status == LXA_OK)
        status = lxa_html_control_stack_scope_find(context->stack, 0,
            (lxa_span_t){(const uint8_t *) "p", 1}, LXA_HTML_SCOPE_BUTTON, &keep);
    if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
    if (status == LXA_OK) status = lxa_html_root_context_reconstruct(context, formatting);
    if (status == LXA_OK) {
        *mode = LXA_TREE_IN_BODY;
        context->frameset_ok = false;
        status = lxa_html_text_context_enter(text, tokenizer, token->name,
            LXA_TREE_IN_BODY, &context->limit_reason);
    }
    if (status == LXA_OK) *mode = LXA_TREE_TEXT;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_formatting_list_t *formatting;
} root_void_view_t;

static lxa_status_t
root_void_paragraph(root_void_view_t *view)
{
    size_t keep;
    lxa_status_t status = lxa_html_control_stack_scope_find(view->context->stack, 0,
        (lxa_span_t){(const uint8_t *) "p", 1}, LXA_HTML_SCOPE_BUTTON, &keep);
    return status == LXA_OK ? lxa_html_control_stack_pop_to(view->context->stack, keep) : status;
}

#define LXA_ROOT_VOID_NAME context_start_void
#define LXA_ROOT_VOID_VIEW root_void_view_t
#define LXA_ROOT_VOID_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_VOID_HEAD(view) context_ensure_head((view)->context)
#define LXA_ROOT_VOID_FRAMESET(view) ((view)->context->frameset_ok)
#define LXA_ROOT_VOID_BUDGET(view) ((view)->context->stack->budget)
#define LXA_ROOT_VOID_REASON(view) (&(view)->context->limit_reason)
#define LXA_ROOT_VOID_PARAGRAPH root_void_paragraph
#define LXA_ROOT_VOID_RECONSTRUCT(view) \
    lxa_html_root_context_reconstruct((view)->context, (view)->formatting)
#define LXA_ROOT_VOID_INSERT(view, token, head, hr, mode) \
    ((void) (view), (void) (token), (void) (head), (void) (hr), (void) (mode), LXA_OK)
#include "html/root_void_start.inc"
#undef LXA_ROOT_VOID_NAME
#undef LXA_ROOT_VOID_VIEW
#undef LXA_ROOT_VOID_BODY
#undef LXA_ROOT_VOID_HEAD
#undef LXA_ROOT_VOID_FRAMESET
#undef LXA_ROOT_VOID_BUDGET
#undef LXA_ROOT_VOID_REASON
#undef LXA_ROOT_VOID_PARAGRAPH
#undef LXA_ROOT_VOID_RECONSTRUCT
#undef LXA_ROOT_VOID_INSERT

lxa_status_t
lxa_html_root_context_start_void(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG) return LXA_ERROR_ARGUMENT;
    if (lxa_html_tree_frameset_mode(*mode)) {
        lxa_html_frameset_action_t action;
        status = lxa_html_tree_frameset_action(*mode, token->kind, token->name, &action);
        if (status != LXA_OK) return status;
        if (action != LXA_HTML_FRAMESET_FRAME_START || !frameset_owner_valid(context, *mode)
            || (formatting != NULL && (formatting->store == NULL
                || formatting->store->budget != context->stack->budget)))
            return LXA_ERROR_ARGUMENT;
        status = lxa_budget_check_item(context->stack->budget, LXA_BUDGET_NAME_BYTES,
            5, &context->limit_reason);
        if (status == LXA_OK)
            status = lxa_budget_charge(context->stack->budget, LXA_BUDGET_WORK, 1,
                &context->limit_reason);
        if (status == LXA_OK)
            status = check_body_child_depth(context, lxa_vector_length(&context->stack->elements));
        return finish(context, status);
    }
    bool head = lxa_html_tree_head_void(token->name);
    bool reconstruct = !head && lxa_html_tree_body_void_reconstructs(token->name);
    if ((!head && !lxa_html_tree_body_void(token->name))
        || (!body_start_mode_valid(context, *mode)
            && !(head && *mode == LXA_TREE_HEAD_NOSCRIPT && context->head))
        || (head && *mode != LXA_TREE_IN_BODY
            && (context->body || lxa_vector_length(&context->stack->elements) != 0))
        || (reconstruct && formatting == NULL)
        || (formatting != NULL && (formatting->store == NULL
            || formatting->store->budget != context->stack->budget)))
        return LXA_ERROR_ARGUMENT;
    root_void_view_t view = {context, formatting};
    return finish(context, context_start_void(&view, token, head, reconstruct, mode));
}

lxa_status_t
lxa_html_root_context_prebody_end(lxa_html_root_context_t *context,
    lxa_span_t name, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || context->body || lxa_vector_length(&context->stack->elements) != 0
        || (*mode == LXA_TREE_BEFORE_HEAD && !context->html)
        || (*mode == LXA_TREE_AFTER_HEAD && !context->head))
        return LXA_ERROR_ARGUMENT;
    lxa_html_prebody_end_action_t action;
    status = lxa_html_tree_prebody_end_action(*mode, name, &action);
    if (status != LXA_OK) return status;
    return finish(context, context_prebody_end(context, action, mode));
}

lxa_status_t
lxa_html_root_context_initial_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token,
    lxa_html_root_initial_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_INITIAL || result == NULL || context->html || context->head || context->body
        || lxa_vector_length(&context->stack->elements) != 0
        || !lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    lxa_html_root_initial_result_t out = {
        lxa_html_tree_initial_action(token->kind, token->data), LXA_HTML_NO_QUIRKS
    };
    if (out.action == LXA_HTML_INITIAL_DOCTYPE)
        status = lxa_html_tree_doctype_mode(token, context->stack->budget,
            &context->limit_reason, &out.document_mode);
    else if (out.action == LXA_HTML_INITIAL_QUIRKS_REPROCESS)
        out.document_mode = LXA_HTML_QUIRKS;
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_prebody_text(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_span_t data, lxa_html_root_prebody_text_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (result == NULL || context->body || lxa_vector_length(&context->stack->elements) != 0
        || (mode == LXA_TREE_BEFORE_HEAD && !context->html)
        || ((mode == LXA_TREE_IN_HEAD || mode == LXA_TREE_AFTER_HEAD) && !context->head))
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_prebody_text_result_t out;
    status = lxa_html_tree_prebody_text_plan(mode, data, context->stack->budget,
        &context->limit_reason, &out.plan);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status == LXA_OK && out.plan.ensure_head) status = context_ensure_head(context);
    if (status == LXA_OK) {
        out.next_mode = out.plan.space_prefix < data.length ? LXA_TREE_AFTER_HEAD : mode;
        *result = out;
    }
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_formatting_list_t *formatting;
} root_start_view_t;

static lxa_status_t
root_start_stack_status(const root_start_view_t *view, lxa_status_t status)
{
    if (status == LXA_ERROR_LIMIT)
        view->context->limit_reason = view->context->stack->limit_reason;
    return status;
}

static lxa_status_t
root_start_identity(const root_start_view_t *view, size_t index, uint64_t *identity)
{
    lxa_html_formatting_item_t item;
    lxa_status_t status = lxa_html_formatting_list_get(view->formatting, index, &item);
    if (status == LXA_OK) *identity = item.identity;
    return status;
}

#define LXA_FORMATTING_START_NAME root_prepare_start
#define LXA_FORMATTING_START_VIEW root_start_view_t
#define LXA_FORMATTING_START_REFERENCE uint64_t
#define LXA_FORMATTING_START_FIND(view, name, index) \
    lxa_html_formatting_list_find((view)->formatting, name, index)
#define LXA_FORMATTING_START_IDENTITY root_start_identity
#define LXA_FORMATTING_START_ADOPT(view, name) \
    lxa_html_formatting_list_adopt((view)->formatting, (view)->context->stack, 0, name)
#define LXA_FORMATTING_START_FIND_REFERENCE(view, identity, index) \
    lxa_html_formatting_list_find_reference((view)->formatting, identity, index)
#define LXA_FORMATTING_START_FORMATTING_LENGTH(view) lxa_vector_length(&(view)->formatting->entries)
#define LXA_FORMATTING_START_REMOVE_FORMATTING(view, index) \
    lxa_html_formatting_list_remove((view)->formatting, index)
#define LXA_FORMATTING_START_OPEN(view, identity, index) root_start_stack_status(view, \
    lxa_html_control_stack_open_reference_find((view)->context->stack, 0, identity, index))
#define LXA_FORMATTING_START_OPEN_LENGTH(view) lxa_vector_length(&(view)->context->stack->elements)
#define LXA_FORMATTING_START_REMOVE_OPEN(view, index) root_start_stack_status(view, \
    lxa_html_control_stack_remove((view)->context->stack, index))
#define LXA_FORMATTING_START_RECONSTRUCT(view) lxa_html_root_context_reconstruct((view)->context, (view)->formatting)
#define LXA_FORMATTING_START_SCOPED(view, name, index) root_start_stack_status(view, \
    lxa_html_control_stack_scope_find((view)->context->stack, 0, name, LXA_HTML_SCOPE_NORMAL, index))
#define LXA_FORMATTING_START_PLAN(view, token, drop) \
    lxa_html_formatting_list_plan_add((view)->formatting, (view)->context->stack, 0, token, drop)
#include "html/formatting_start.inc"
#undef LXA_FORMATTING_START_NAME
#undef LXA_FORMATTING_START_VIEW
#undef LXA_FORMATTING_START_REFERENCE
#undef LXA_FORMATTING_START_FIND
#undef LXA_FORMATTING_START_IDENTITY
#undef LXA_FORMATTING_START_ADOPT
#undef LXA_FORMATTING_START_FIND_REFERENCE
#undef LXA_FORMATTING_START_FORMATTING_LENGTH
#undef LXA_FORMATTING_START_REMOVE_FORMATTING
#undef LXA_FORMATTING_START_OPEN
#undef LXA_FORMATTING_START_OPEN_LENGTH
#undef LXA_FORMATTING_START_REMOVE_OPEN
#undef LXA_FORMATTING_START_RECONSTRUCT
#undef LXA_FORMATTING_START_SCOPED
#undef LXA_FORMATTING_START_PLAN

lxa_status_t
lxa_html_root_context_start_formatting(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || identity == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget || !body_start_mode_valid(context, *mode))
        return LXA_ERROR_ARGUMENT;
    status = lxa_html_formatting_list_start_intake(formatting, context->stack, 0, token);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status == LXA_OK) status = context_ensure_body(context);
    size_t drop;
    root_start_view_t view = {context, formatting};
    if (status == LXA_OK) {
        *mode = LXA_TREE_IN_BODY;
        status = root_prepare_start(&view, token, &drop);
    }
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = formatting->store->limit_reason;
    if (status == LXA_OK)
        status = check_body_child_depth(context, lxa_vector_length(&context->stack->elements));
    if (status == LXA_OK)
        status = lxa_html_formatting_list_admit_planned(formatting, context->stack,
            token, drop, identity);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = formatting->store->limit_reason;
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

static lxa_status_t
context_foreign_insert(root_start_view_t *view, const lxa_html_token_t *token,
    lxa_dom_namespace_t ns)
{
    lxa_html_root_context_t *context = view->context;
    uint64_t identity;
    lxa_status_t status = check_body_child_depth(context,
        lxa_vector_length(&context->stack->elements));
    return status == LXA_OK
        ? lxa_html_control_stack_push_token(context->stack, token, ns, &identity) : status;
}

#define LXA_ROOT_FOREIGN_START_NAME context_start_foreign
#define LXA_ROOT_FOREIGN_START_VIEW root_start_view_t
#define LXA_ROOT_FOREIGN_START_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_FOREIGN_START_RESERVE(view) reserve_open_slot((view)->context)
#define LXA_ROOT_FOREIGN_START_RECONSTRUCT(view) \
    lxa_html_root_context_reconstruct((view)->context, (view)->formatting)
#define LXA_ROOT_FOREIGN_START_INSERT context_foreign_insert
#define LXA_ROOT_FOREIGN_START_POP(view) \
    lxa_html_control_stack_pop_to((view)->context->stack, \
        lxa_vector_length(&(view)->context->stack->elements) - 1)
#include "html/root_foreign_start.inc"
#undef LXA_ROOT_FOREIGN_START_NAME
#undef LXA_ROOT_FOREIGN_START_VIEW
#undef LXA_ROOT_FOREIGN_START_BODY
#undef LXA_ROOT_FOREIGN_START_RESERVE
#undef LXA_ROOT_FOREIGN_START_RECONSTRUCT
#undef LXA_ROOT_FOREIGN_START_INSERT
#undef LXA_ROOT_FOREIGN_START_POP

lxa_status_t
lxa_html_root_context_start_foreign(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget || !body_start_mode_valid(context, *mode)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || (!lxa_html_tree_name_equals(token->name, "svg", 3)
            && !lxa_html_tree_name_equals(token->name, "math", 4))) return LXA_ERROR_ARGUMENT;
    root_start_view_t view = {context, formatting};
    return finish(context, context_start_foreign(&view, token, mode));
}


lxa_status_t
lxa_html_root_context_end_scoped(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_span_t name,
    lxa_html_root_mode_t mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_IN_BODY || !context->body
        || lxa_vector_length(&context->stack->elements) == 0
        || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget
        || name.data == NULL || name.length == 0) return LXA_ERROR_ARGUMENT;
    lxa_html_scope_kind_t scope = lxa_html_tree_root_end_scope(name);
    if (scope == 0) return LXA_ERROR_ARGUMENT;
    status = lxa_html_formatting_list_end_scoped(formatting, context->stack,
        name, scope);
    if (status == LXA_ERROR_LIMIT)
        context->limit_reason = formatting->store->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_end_body(lxa_html_root_context_t *context,
    bool html_end, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode == NULL || *mode != LXA_TREE_IN_BODY)
        return LXA_ERROR_ARGUMENT;
    return finish(context, lxa_html_control_stack_end_body(context->stack, html_end, mode));
}

lxa_status_t
lxa_html_root_context_end_ordinary(lxa_html_root_context_t *context,
    lxa_span_t name, lxa_html_root_mode_t mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode != LXA_TREE_IN_BODY || name.data == NULL || name.length == 0)
        return LXA_ERROR_ARGUMENT;
    lxa_html_end_target_plan_t plan;
    status = lxa_html_control_stack_ordinary_end_plan(context->stack, 0, name, &plan);
    if (status == LXA_OK)
        status = lxa_html_control_stack_pop_to(context->stack, plan.index);
    return finish(context, status);
}

#define LXA_ROOT_GROUP_NAME recover_root_group
#define LXA_ROOT_GROUP_VIEW lxa_html_root_context_t
#define LXA_ROOT_GROUP_QUERY(view, out) lxa_html_control_stack_column_group((view)->stack, 0, out)
#define LXA_ROOT_GROUP_BUDGET(view) ((view)->stack->budget)
#define LXA_ROOT_GROUP_REASON(view) (&(view)->limit_reason)
#define LXA_ROOT_GROUP_POP(view) \
    lxa_html_control_stack_pop_to((view)->stack, lxa_vector_length(&(view)->stack->elements) - 1)
#include "html/root_group_recover.inc"
#undef LXA_ROOT_GROUP_NAME
#undef LXA_ROOT_GROUP_VIEW
#undef LXA_ROOT_GROUP_QUERY
#undef LXA_ROOT_GROUP_BUDGET
#undef LXA_ROOT_GROUP_REASON
#undef LXA_ROOT_GROUP_POP

lxa_status_t
lxa_html_root_context_end_formatting(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_span_t name,
    lxa_html_root_mode_t mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode != LXA_TREE_IN_BODY) return LXA_ERROR_ARGUMENT;
    status = lxa_html_formatting_list_adopt_intake(formatting, context->stack, 0, name);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status == LXA_OK) status = recover_root_group(context);
    if (status == LXA_OK) {
        status = lxa_html_formatting_list_adopt(formatting, context->stack, 0, name);
        if (status == LXA_ERROR_LIMIT)
            context->limit_reason = formatting->store->limit_reason;
    }
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_end_form(lxa_html_root_context_t *context,
    uint64_t *owner, lxa_html_root_mode_t mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode != LXA_TREE_IN_BODY || owner == NULL)
        return LXA_ERROR_ARGUMENT;
    return finish(context, lxa_html_control_stack_end_form(context->stack, owner));
}

typedef struct {
    lxa_html_root_context_t *context;
    uint64_t *owner;
    lxa_html_root_form_action_t action;
} root_form_view_t;

static lxa_status_t
context_form_closed(root_form_view_t *view, const lxa_html_token_t *token, size_t keep)
{
    (void) token;
    lxa_html_root_context_t *context = view->context;
    uint64_t identity;
    lxa_status_t status = lxa_budget_check_item(context->stack->budget,
        LXA_BUDGET_NAME_BYTES, 4, &context->limit_reason);
    if (status == LXA_OK) status = check_body_child_depth(context, keep);
    if (status == LXA_OK)
        status = lxa_html_control_stack_claim_identity(context->stack, &identity);
    if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
    if (status == LXA_OK) {
        *view->owner = identity;
        view->action = LXA_HTML_ROOT_FORM_CLOSED;
    }
    return status;
}

static lxa_status_t
context_form_open(root_form_view_t *view, const lxa_html_token_t *token)
{
    (void) token;
    lxa_html_root_context_t *context = view->context;
    size_t keep;
    uint64_t identity;
    lxa_status_t status = lxa_html_control_stack_scope_find(context->stack, 0,
        (lxa_span_t){(const uint8_t *) "p", 1}, LXA_HTML_SCOPE_BUTTON, &keep);
    if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
    if (status == LXA_OK) status = reserve_open_slot(context);
    if (status == LXA_OK)
        status = check_body_child_depth(context, lxa_vector_length(&context->stack->elements));
    if (status == LXA_OK)
        status = lxa_html_control_stack_push(context->stack,
            (lxa_span_t){(const uint8_t *) "form", 4}, LXA_DOM_NS_HTML, false, &identity);
    if (status == LXA_OK) {
        *view->owner = identity;
        view->action = LXA_HTML_ROOT_FORM_OPEN;
    }
    return status;
}

#define LXA_ROOT_FORM_NAME context_start_form
#define LXA_ROOT_FORM_VIEW root_form_view_t
#define LXA_ROOT_FORM_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_FORM_PLAN(view, plan) \
    lxa_html_control_stack_table_context_plan((view)->context->stack, 0, false, plan)
#define LXA_ROOT_FORM_OWNED(view) (*(view)->owner != 0)
#define LXA_ROOT_FORM_POP(view, keep) lxa_html_control_stack_pop_to((view)->context->stack, keep)
#define LXA_ROOT_FORM_CLOSED context_form_closed
#define LXA_ROOT_FORM_OPEN context_form_open
#include "html/root_form_start.inc"
#undef LXA_ROOT_FORM_NAME
#undef LXA_ROOT_FORM_VIEW
#undef LXA_ROOT_FORM_BODY
#undef LXA_ROOT_FORM_PLAN
#undef LXA_ROOT_FORM_OWNED
#undef LXA_ROOT_FORM_POP
#undef LXA_ROOT_FORM_CLOSED
#undef LXA_ROOT_FORM_OPEN

lxa_status_t
lxa_html_root_context_start_form(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, uint64_t *owner, lxa_html_root_mode_t *mode,
    lxa_html_root_form_action_t *action)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (owner == NULL || mode == NULL || action == NULL
        || !body_start_mode_valid(context, *mode)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "form", 4)) return LXA_ERROR_ARGUMENT;
    root_form_view_t view = {context, owner, LXA_HTML_ROOT_FORM_IGNORE};
    status = context_start_form(&view, token, mode);
    if (status == LXA_OK) *action = view.action;
    return finish(context, status);
}

static lxa_status_t
close_scoped(lxa_html_root_context_t *context, lxa_span_t name, lxa_html_scope_kind_t scope)
{
    size_t keep;
    lxa_status_t status = lxa_html_control_stack_scope_find(context->stack, 0, name, scope, &keep);
    return status == LXA_OK ? lxa_html_control_stack_pop_to(context->stack, keep) : status;
}

static lxa_status_t
close_list(lxa_html_root_context_t *context, bool definition)
{
    if (lxa_vector_length(&context->stack->elements) <= 1) return LXA_OK;
    size_t keep;
    lxa_status_t status = lxa_html_control_stack_list_item_plan(context->stack, 1, definition, &keep);
    return status == LXA_OK ? lxa_html_control_stack_pop_to(context->stack, keep) : status;
}

static lxa_status_t
close_heading(lxa_html_root_context_t *context)
{
    size_t length = lxa_vector_length(&context->stack->elements);
    if (length == 0) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_get(context->stack, length - 1, &entry, NULL);
    if (status == LXA_OK && entry.namespace_id == LXA_DOM_NS_HTML && lxa_html_tree_heading(entry.name))
        status = lxa_html_control_stack_pop_to(context->stack, length - 1);
    return status;
}

#define LXA_ROOT_CONTAINER_NAME root_prepare_container
#define LXA_ROOT_CONTAINER_VIEW lxa_html_root_context_t
#define LXA_ROOT_CONTAINER_RESERVE reserve_open_slots
#define LXA_ROOT_CONTAINER_NODE(view) ((void) (view), LXA_OK)
#define LXA_ROOT_CONTAINER_RUBY(view, name, out) lxa_html_control_stack_ruby_start_plan((view)->stack, 0, \
    lxa_html_tree_name_equals(name, "rp", 2) || lxa_html_tree_name_equals(name, "rt", 2), out)
#define LXA_ROOT_CONTAINER_POP(view, keep) lxa_html_control_stack_pop_to((view)->stack, keep)
#define LXA_ROOT_CONTAINER_LIST close_list
#define LXA_ROOT_CONTAINER_PARAGRAPH(view) close_scoped(view, \
    ((lxa_span_t){(const uint8_t *) "p", 1}), LXA_HTML_SCOPE_BUTTON)
#define LXA_ROOT_CONTAINER_HEADING close_heading
#define LXA_ROOT_CONTAINER_BUTTON(view) close_scoped(view, \
    ((lxa_span_t){(const uint8_t *) "button", 6}), LXA_HTML_SCOPE_NORMAL)
#include "html/root_container_prepare.inc"
#undef LXA_ROOT_CONTAINER_NAME
#undef LXA_ROOT_CONTAINER_VIEW
#undef LXA_ROOT_CONTAINER_RESERVE
#undef LXA_ROOT_CONTAINER_NODE
#undef LXA_ROOT_CONTAINER_RUBY
#undef LXA_ROOT_CONTAINER_POP
#undef LXA_ROOT_CONTAINER_LIST
#undef LXA_ROOT_CONTAINER_PARAGRAPH
#undef LXA_ROOT_CONTAINER_HEADING
#undef LXA_ROOT_CONTAINER_BUTTON

static bool
ordinary_container_start(lxa_span_t name)
{
    return !lxa_html_tree_body_control_start(name) && !lxa_html_tree_head_void(name)
        && !lxa_html_tree_body_void(name) && !lxa_html_tree_table_part(name)
        && !lxa_html_tree_returning_text_element(name) && !lxa_html_tree_formatting_tag(name)
        && !lxa_html_tree_template_marker_container(name)
        && !lxa_html_tree_name_equals(name, "form", 4)
        && !lxa_html_tree_name_equals(name, "template", 8)
        && !lxa_html_tree_name_equals(name, "xmp", 3)
        && !lxa_html_tree_name_equals(name, "plaintext", 9)
        && !lxa_html_tree_name_equals(name, "svg", 3)
        && !lxa_html_tree_name_equals(name, "math", 4);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_formatting_list_t *formatting;
    uint64_t *identity;
    bool marker;
} root_container_start_view_t;

static lxa_status_t
context_container_insert(root_container_start_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_root_context_t *context = view->context;
    lxa_status_t status = check_body_child_depth(context,
        lxa_vector_length(&context->stack->elements));
    if (status != LXA_OK) return status;
    return view->marker
        ? lxa_html_formatting_list_admit_marker(view->formatting, context->stack,
            token->name, view->identity)
        : lxa_html_control_stack_push_token(context->stack, token, LXA_DOM_NS_HTML, view->identity);
}

#define LXA_ROOT_CONTAINER_START_NAME context_start_container
#define LXA_ROOT_CONTAINER_START_VIEW root_container_start_view_t
#define LXA_ROOT_CONTAINER_START_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_CONTAINER_START_FRAMESET(view) ((view)->context->frameset_ok)
#define LXA_ROOT_CONTAINER_START_PREPARE(view, name) root_prepare_container((view)->context, name, false)
#define LXA_ROOT_CONTAINER_START_RECONSTRUCT(view) \
    lxa_html_root_context_reconstruct((view)->context, (view)->formatting)
#define LXA_ROOT_CONTAINER_START_INSERT context_container_insert
#include "html/root_container_start.inc"
#undef LXA_ROOT_CONTAINER_START_NAME
#undef LXA_ROOT_CONTAINER_START_VIEW
#undef LXA_ROOT_CONTAINER_START_BODY
#undef LXA_ROOT_CONTAINER_START_FRAMESET
#undef LXA_ROOT_CONTAINER_START_PREPARE
#undef LXA_ROOT_CONTAINER_START_RECONSTRUCT
#undef LXA_ROOT_CONTAINER_START_INSERT

lxa_status_t
lxa_html_root_context_start_container(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity, bool *skip_newline)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || identity == NULL || skip_newline == NULL
        || !body_start_mode_valid(context, *mode)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !ordinary_container_start(token->name)
        || (lxa_html_tree_name_equals(token->name, "noscript", 8) && *mode < LXA_TREE_AFTER_HEAD)
        || (formatting != NULL && (formatting->store == NULL
            || formatting->store->budget != context->stack->budget))) return LXA_ERROR_ARGUMENT;
    bool reconstruct = lxa_html_tree_container_reconstructs(token->name);
    if (reconstruct && formatting == NULL) return LXA_ERROR_ARGUMENT;
    bool supported;
    status = lxa_html_tree_supported_container(token->name, context->stack->budget,
        &context->limit_reason, &supported);
    if (status != LXA_OK) return finish(context, status);
    if (!supported) return LXA_ERROR_ARGUMENT;
    root_container_start_view_t view = {context, formatting, identity, false};
    status = context_start_container(&view, token, mode, skip_newline);
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_start_marker(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || identity == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget || !body_start_mode_valid(context, *mode)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_template_marker_container(token->name)) return LXA_ERROR_ARGUMENT;
    root_container_start_view_t view = {context, formatting, identity, true};
    bool newline;
    status = context_start_container(&view, token, mode, &newline);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = formatting->store->limit_reason;
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

static lxa_status_t
context_table_top(root_container_start_view_t *view, lxa_span_t *top)
{
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_get(view->context->stack,
        lxa_vector_length(&view->context->stack->elements) - 1, &entry, NULL);
    if (status == LXA_OK) *top = entry.name;
    return status;
}

static lxa_status_t
context_table_close(root_container_start_view_t *view)
{
    lxa_status_t status = lxa_html_formatting_list_end_scoped(view->formatting,
        view->context->stack, (lxa_span_t){(const uint8_t *) "table", 5}, LXA_HTML_SCOPE_TABLE);
    if (status == LXA_ERROR_LIMIT) view->context->limit_reason = view->formatting->store->limit_reason;
    return status;
}

#define LXA_ROOT_TABLE_START_NAME context_start_table
#define LXA_ROOT_TABLE_START_VIEW root_container_start_view_t
#define LXA_ROOT_TABLE_START_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_TABLE_START_TOP context_table_top
#define LXA_ROOT_TABLE_START_FRAMESET(view) ((view)->context->frameset_ok)
#define LXA_ROOT_TABLE_START_PLAN(view, top, out) \
    lxa_html_control_stack_root_table_close_plan((view)->context->stack, 1, \
        lxa_html_tree_table_context(top), out)
#define LXA_ROOT_TABLE_START_RESERVE(view, count) reserve_open_slots((view)->context, count)
#define LXA_ROOT_TABLE_START_NODE(view) LXA_OK
#define LXA_ROOT_TABLE_START_CLOSE context_table_close
#define LXA_ROOT_TABLE_START_PARAGRAPH(view) \
    close_scoped((view)->context, (lxa_span_t){(const uint8_t *) "p", 1}, LXA_HTML_SCOPE_BUTTON)
#define LXA_ROOT_TABLE_START_INSERT context_container_insert
#include "html/root_table_start.inc"
#undef LXA_ROOT_TABLE_START_NAME
#undef LXA_ROOT_TABLE_START_VIEW
#undef LXA_ROOT_TABLE_START_BODY
#undef LXA_ROOT_TABLE_START_TOP
#undef LXA_ROOT_TABLE_START_FRAMESET
#undef LXA_ROOT_TABLE_START_PLAN
#undef LXA_ROOT_TABLE_START_RESERVE
#undef LXA_ROOT_TABLE_START_NODE
#undef LXA_ROOT_TABLE_START_CLOSE
#undef LXA_ROOT_TABLE_START_PARAGRAPH
#undef LXA_ROOT_TABLE_START_INSERT

lxa_status_t
lxa_html_root_context_start_table(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_document_mode_t document_mode, lxa_html_root_mode_t *mode, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || identity == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget
        || !body_start_mode_valid(context, *mode)
        || (unsigned) document_mode > LXA_HTML_QUIRKS
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "table", 5)) return LXA_ERROR_ARGUMENT;
    root_container_start_view_t view = {context, formatting, identity, false};
    status = context_start_table(&view, token, document_mode == LXA_HTML_QUIRKS, mode);
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

static lxa_status_t
context_table_part_insert(root_container_start_view_t *view, const lxa_html_token_t *token)
{
    view->marker = lxa_html_tree_formatting_marker_tag(token->name);
    lxa_status_t status = context_container_insert(view, token);
    if (status == LXA_ERROR_LIMIT && view->marker
        && view->context->limit_reason == LXA_BUDGET_COUNT)
        view->context->limit_reason = view->formatting->store->limit_reason;
    return status;
}

#define LXA_TABLE_PART_ADMIT_NAME context_admit_table_part
#define LXA_TABLE_PART_ADMIT_VIEW root_container_start_view_t
#define LXA_TABLE_PART_ADMIT_RESERVE(view, count) reserve_open_slots((view)->context, count)
#define LXA_TABLE_PART_ADMIT_NODE(view, count) LXA_OK
#define LXA_TABLE_PART_ADMIT_INSERT context_table_part_insert
#include "html/table_part_admit.inc"
#undef LXA_TABLE_PART_ADMIT_NAME
#undef LXA_TABLE_PART_ADMIT_VIEW
#undef LXA_TABLE_PART_ADMIT_RESERVE
#undef LXA_TABLE_PART_ADMIT_NODE
#undef LXA_TABLE_PART_ADMIT_INSERT

lxa_status_t
lxa_html_root_context_admit_table_part(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t mode, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length(&context->stack->elements);
    if (identity == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget || !context->body
        || mode != LXA_TREE_IN_BODY || length == 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG)
        return LXA_ERROR_ARGUMENT;
    lxa_html_virtual_table_start_plan_t plan;
    status = lxa_html_control_stack_prepared_table_part_plan(context->stack, token->name, &plan);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status != LXA_OK) return finish(context, status);
    root_container_start_view_t view = {context, formatting, identity, false};
    status = context_admit_table_part(&view, token, &plan);
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    bool implied;
} root_paragraph_end_view_t;

static lxa_status_t
paragraph_end_close(root_paragraph_end_view_t *view, bool *closed)
{
    size_t length = lxa_vector_length(&view->context->stack->elements), keep;
    lxa_status_t status = lxa_html_control_stack_scope_find(view->context->stack, 0,
        (lxa_span_t){(const uint8_t *) "p", 1}, LXA_HTML_SCOPE_BUTTON, &keep);
    if (status == LXA_OK) status = lxa_html_control_stack_pop_to(view->context->stack, keep);
    if (status == LXA_OK) *closed = keep != length;
    return status;
}

static lxa_status_t
paragraph_end_missing(root_paragraph_end_view_t *view)
{
    lxa_html_control_stack_t *stack = view->context->stack;
    bool group;
    lxa_status_t status = lxa_html_control_stack_column_group(stack, 0, &group);
    if (status == LXA_OK && group)
        status = lxa_html_control_stack_pop_to(stack, lxa_vector_length(&stack->elements) - 1);
    if (status == LXA_OK) view->implied = true;
    return status;
}

#define LXA_ROOT_PARAGRAPH_END_NAME root_end_paragraph
#define LXA_ROOT_PARAGRAPH_END_VIEW root_paragraph_end_view_t
#define LXA_ROOT_PARAGRAPH_END_CLOSE paragraph_end_close
#define LXA_ROOT_PARAGRAPH_END_LENGTH(view) lxa_vector_length(&(view)->context->stack->elements)
#define LXA_ROOT_PARAGRAPH_END_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_PARAGRAPH_END_MISSING paragraph_end_missing
#include "html/root_paragraph_end.inc"
#undef LXA_ROOT_PARAGRAPH_END_NAME
#undef LXA_ROOT_PARAGRAPH_END_VIEW
#undef LXA_ROOT_PARAGRAPH_END_CLOSE
#undef LXA_ROOT_PARAGRAPH_END_LENGTH
#undef LXA_ROOT_PARAGRAPH_END_BODY
#undef LXA_ROOT_PARAGRAPH_END_MISSING

lxa_status_t
lxa_html_root_context_end_paragraph(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, bool *implied)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode != LXA_TREE_IN_BODY || implied == NULL) return LXA_ERROR_ARGUMENT;
    root_paragraph_end_view_t view = {context, false};
    status = root_end_paragraph(&view);
    if (status == LXA_OK) *implied = view.implied;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_formatting_list_t *formatting;
    lxa_html_root_mode_t *mode;
} root_data_view_t;

static lxa_status_t
data_table(root_data_view_t *view, bool *table)
{
    lxa_html_control_stack_t *stack = view->context->stack;
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_get(stack,
        lxa_vector_length(&stack->elements) - 1, &entry, NULL);
    if (status == LXA_OK) *table = lxa_html_tree_table_context(entry.name);
    return status;
}

#define LXA_ROOT_DATA_NAME root_prepare_data
#define LXA_ROOT_DATA_VIEW root_data_view_t
#define LXA_ROOT_DATA_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_DATA_MODE(view) (*(view)->mode)
#define LXA_ROOT_DATA_FRAMESET(view) ((view)->context->frameset_ok)
#define LXA_ROOT_DATA_LENGTH(view) lxa_vector_length(&(view)->context->stack->elements)
#define LXA_ROOT_DATA_TABLE_QUERY data_table
#define LXA_ROOT_DATA_RECONSTRUCT(view) lxa_html_root_context_reconstruct((view)->context, (view)->formatting)
#include "html/root_data_prepare.inc"
#undef LXA_ROOT_DATA_NAME
#undef LXA_ROOT_DATA_VIEW
#undef LXA_ROOT_DATA_BODY
#undef LXA_ROOT_DATA_MODE
#undef LXA_ROOT_DATA_FRAMESET
#undef LXA_ROOT_DATA_LENGTH
#undef LXA_ROOT_DATA_TABLE_QUERY
#undef LXA_ROOT_DATA_RECONSTRUCT

lxa_status_t
lxa_html_root_context_prepare_data(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_span_t data,
    lxa_html_root_mode_t *mode, lxa_html_root_data_action_t *action)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || action == NULL || (*mode != LXA_TREE_AFTER_HEAD && *mode != LXA_TREE_IN_BODY)
        || !body_start_mode_valid(context, *mode) || formatting == NULL
        || formatting->store == NULL || formatting->store->budget != context->stack->budget
        || (data.length != 0 && data.data == NULL)) return LXA_ERROR_ARGUMENT;
    status = lxa_budget_charge(context->stack->budget, LXA_BUDGET_WORK,
        data.length, &context->limit_reason);
    root_data_view_t view = {context, formatting, mode};
    lxa_html_root_data_action_t out;
    if (status == LXA_OK) status = root_prepare_data(&view, data, &out);
    if (status == LXA_OK) *action = out;
    return finish(context, status);
}

typedef struct {
    lxa_html_control_stack_t *stack;
} root_select_view_t;

#define LXA_SELECT_EXECUTE_NAME root_execute_select
#define LXA_SELECT_EXECUTE_VIEW root_select_view_t
#define LXA_SELECT_EXECUTE_PLAN(view, kind, name, out) \
    lxa_html_control_stack_select_plan((view)->stack, 0, kind, name, out)
#define LXA_SELECT_EXECUTE_POP(view, keep) \
    lxa_html_control_stack_pop_to((view)->stack, keep)
#include "html/select_execute.inc"
#undef LXA_SELECT_EXECUTE_NAME
#undef LXA_SELECT_EXECUTE_VIEW
#undef LXA_SELECT_EXECUTE_PLAN
#undef LXA_SELECT_EXECUTE_POP

lxa_status_t
lxa_html_root_context_select_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_html_token_kind_t kind, lxa_span_t name,
    bool *ignore)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_IN_BODY || !context->body || ignore == NULL
        || !lxa_html_select_arguments_valid(kind, name)) return LXA_ERROR_ARGUMENT;
    const root_select_view_t view = {context->stack};
    return finish(context, root_execute_select(&view, kind, name, ignore));
}

lxa_status_t
lxa_html_root_context_control_group(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_html_token_kind_t kind, lxa_span_t name)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_IN_BODY || !context->body
        || !lxa_html_select_arguments_valid(kind, name)
        || !lxa_html_tree_root_control_group(kind, name)) return LXA_ERROR_ARGUMENT;
    return finish(context, recover_root_group(context));
}

#define LXA_ROOT_END_PRELUDE_NAME context_end_prelude
#define LXA_ROOT_END_PRELUDE_VIEW lxa_html_root_context_t
#define LXA_ROOT_END_PRELUDE_GROUP recover_root_group
#include "html/root_end_prelude.inc"
#undef LXA_ROOT_END_PRELUDE_NAME
#undef LXA_ROOT_END_PRELUDE_VIEW
#undef LXA_ROOT_END_PRELUDE_GROUP

lxa_status_t
lxa_html_root_context_end_prelude(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t mode,
    lxa_html_root_end_prelude_action_t *action)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (action == NULL || !body_start_mode_valid(context, mode)
        || (mode == LXA_TREE_IN_BODY && !context->body)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_END_TAG)
        return LXA_ERROR_ARGUMENT;
    return finish(context, context_end_prelude(context, mode, token->name, action));
}

#define LXA_AFTER_BODY_NAME root_execute_after_body
#define LXA_AFTER_BODY_VIEW root_data_view_t
#define LXA_AFTER_BODY_RECONSTRUCT(view) lxa_html_root_context_reconstruct((view)->context, (view)->formatting)
#define LXA_AFTER_BODY_MODE(view) (*(view)->mode)
#include "html/after_body_execute.inc"
#undef LXA_AFTER_BODY_NAME
#undef LXA_AFTER_BODY_VIEW
#undef LXA_AFTER_BODY_RECONSTRUCT
#undef LXA_AFTER_BODY_MODE

lxa_status_t
lxa_html_root_context_after_body(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, lxa_html_after_body_action_t *action)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode == NULL || action == NULL
        || (*mode != LXA_TREE_AFTER_BODY && *mode != LXA_TREE_AFTER_HTML)
        || !lxa_html_tree_token_shape_valid(token)
        || (token->kind != LXA_HTML_TEXT && token->kind != LXA_HTML_START_TAG
            && token->kind != LXA_HTML_END_TAG)
        || (formatting != NULL && (formatting->store == NULL
            || formatting->store->budget != context->stack->budget)))
        return LXA_ERROR_ARGUMENT;
    if (token->kind == LXA_HTML_TEXT)
        status = lxa_budget_charge(context->stack->budget, LXA_BUDGET_WORK,
            token->data.length, &context->limit_reason);
    lxa_html_after_body_action_t out;
    if (status == LXA_OK) status = lxa_html_tree_after_body_action(token, &out);
    if (status == LXA_OK && out == LXA_HTML_AFTER_BODY_SPACE_TEXT
        && (formatting == NULL || lxa_vector_length(&context->stack->elements) == 0))
        return LXA_ERROR_ARGUMENT;
    root_data_view_t view = {context, formatting, mode};
    if (status == LXA_OK) status = root_execute_after_body(&view, out);
    if (status == LXA_OK) *action = out;
    return finish(context, status);
}

static lxa_status_t
hidden_table_destination(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, size_t keep)
{
    (void) token;
    if (keep == 0 || keep > lxa_vector_length(&context->stack->elements)) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return LXA_OK;
}

#define LXA_HIDDEN_TABLE_NAME root_hidden_table_input
#define LXA_HIDDEN_TABLE_VIEW lxa_html_root_context_t
#define LXA_HIDDEN_TABLE_BUDGET(view) ((view)->stack->budget)
#define LXA_HIDDEN_TABLE_REASON(view) (&(view)->limit_reason)
#define LXA_HIDDEN_TABLE_PLAN(view, out) lxa_html_control_stack_table_context_plan((view)->stack, 0, false, out)
#define LXA_HIDDEN_TABLE_INSERT hidden_table_destination
#define LXA_HIDDEN_TABLE_POP(view, keep) lxa_html_control_stack_pop_to((view)->stack, keep)
#include "html/hidden_table_input.inc"
#undef LXA_HIDDEN_TABLE_NAME
#undef LXA_HIDDEN_TABLE_VIEW
#undef LXA_HIDDEN_TABLE_BUDGET
#undef LXA_HIDDEN_TABLE_REASON
#undef LXA_HIDDEN_TABLE_PLAN
#undef LXA_HIDDEN_TABLE_INSERT
#undef LXA_HIDDEN_TABLE_POP

lxa_status_t
lxa_html_root_context_hidden_table_input(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token, bool *handled)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode != LXA_TREE_IN_BODY || handled == NULL
        || !lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    bool out;
    status = root_hidden_table_input(context, token, &out);
    if (status == LXA_OK) *handled = out;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_root_mode_t *mode;
} root_head_noscript_view_t;

static lxa_status_t
head_noscript_admit(root_head_noscript_view_t *view, const lxa_html_token_t *token)
{
    (void) token;
    lxa_html_root_context_t *context = view->context;
    lxa_status_t status = lxa_budget_check_item(context->stack->budget,
        LXA_BUDGET_NAME_BYTES, 8, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_charge(context->stack->budget, LXA_BUDGET_WORK, 1, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(context->stack->budget, LXA_BUDGET_TREE_DEPTH, 3, &context->limit_reason);
    return status;
}

#define LXA_HEAD_NOSCRIPT_NAME root_start_head_noscript
#define LXA_HEAD_NOSCRIPT_VIEW root_head_noscript_view_t
#define LXA_HEAD_NOSCRIPT_HEAD(view) context_ensure_head((view)->context)
#define LXA_HEAD_NOSCRIPT_ADMIT head_noscript_admit
#define LXA_HEAD_NOSCRIPT_MODE(view) (*(view)->mode)
#include "html/head_noscript_start.inc"
#undef LXA_HEAD_NOSCRIPT_NAME
#undef LXA_HEAD_NOSCRIPT_VIEW
#undef LXA_HEAD_NOSCRIPT_HEAD
#undef LXA_HEAD_NOSCRIPT_ADMIT
#undef LXA_HEAD_NOSCRIPT_MODE

lxa_status_t
lxa_html_root_context_start_head_noscript(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || context->body || lxa_vector_length(&context->stack->elements) != 0
        || !body_start_mode_valid(context, *mode) || *mode > LXA_TREE_IN_HEAD
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "noscript", 8)) return LXA_ERROR_ARGUMENT;
    root_head_noscript_view_t view = {context, mode};
    return finish(context, root_start_head_noscript(&view, token));
}

lxa_status_t
lxa_html_root_context_head_noscript_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token,
    lxa_html_root_head_noscript_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_HEAD_NOSCRIPT || result == NULL || !context->head || context->body
        || lxa_vector_length(&context->stack->elements) != 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF)
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_head_noscript_result_t out;
    status = lxa_html_tree_head_noscript_plan(token, context->stack->budget,
        &context->limit_reason, &out.plan);
    if (status == LXA_OK) {
        out.next_mode = lxa_html_tree_head_noscript_next_mode(out.plan.action);
        *result = out;
    }
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_formatting_list_t *formatting;
    uint64_t *form_owner;
    bool implied_paragraph;
} root_body_end_view_t;

#define LXA_ROOT_BODY_END_NAME context_body_end_token
#define LXA_ROOT_BODY_END_VIEW root_body_end_view_t
#define LXA_ROOT_BODY_END_BODY(view, html, mode) \
    lxa_html_root_context_end_body((view)->context, html, mode)
#define LXA_ROOT_BODY_END_FORMATTING(view, name) \
    lxa_html_root_context_end_formatting((view)->context, (view)->formatting, name, LXA_TREE_IN_BODY)
#define LXA_ROOT_BODY_END_FORM(view) \
    lxa_html_root_context_end_form((view)->context, (view)->form_owner, LXA_TREE_IN_BODY)
#define LXA_ROOT_BODY_END_PARAGRAPH(view) \
    lxa_html_root_context_end_paragraph((view)->context, LXA_TREE_IN_BODY, &(view)->implied_paragraph)
#define LXA_ROOT_BODY_END_SCOPED(view, name, scope) \
    ((void) (scope), lxa_html_root_context_end_scoped((view)->context, \
        (view)->formatting, name, LXA_TREE_IN_BODY))
#define LXA_ROOT_BODY_END_ORDINARY(view, name) \
    lxa_html_root_context_end_ordinary((view)->context, name, LXA_TREE_IN_BODY)
#include "html/root_body_end.inc"
#undef LXA_ROOT_BODY_END_NAME
#undef LXA_ROOT_BODY_END_VIEW
#undef LXA_ROOT_BODY_END_BODY
#undef LXA_ROOT_BODY_END_FORMATTING
#undef LXA_ROOT_BODY_END_FORM
#undef LXA_ROOT_BODY_END_PARAGRAPH
#undef LXA_ROOT_BODY_END_SCOPED
#undef LXA_ROOT_BODY_END_ORDINARY

lxa_status_t
lxa_html_root_context_body_end_token(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, uint64_t *form_owner,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, bool *implied_paragraph)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || implied_paragraph == NULL || *mode != LXA_TREE_IN_BODY || !context->body
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_END_TAG
        || lxa_html_tree_name_equals(token->name, "head", 4)
        || lxa_html_tree_name_equals(token->name, "template", 8)
        || lxa_html_tree_name_equals(token->name, "br", 2))
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_body_end_plan_t plan = lxa_html_tree_root_body_end_plan(token->name);
    bool needs_formatting = plan.action == LXA_HTML_ROOT_BODY_END_FORMATTING
        || plan.action == LXA_HTML_ROOT_BODY_END_SCOPED;
    if ((needs_formatting && formatting == NULL)
        || (formatting != NULL && (formatting->store == NULL
            || formatting->store->budget != context->stack->budget))
        || (plan.action == LXA_HTML_ROOT_BODY_END_FORM && form_owner == NULL))
        return LXA_ERROR_ARGUMENT;
    bool ignore;
    root_body_end_view_t view = {context, formatting, form_owner, false};
    status = lxa_html_root_context_select_token(context, *mode, token->kind, token->name, &ignore);
    if (status == LXA_OK && !ignore)
        status = context_body_end_token(&view, token->name, plan, mode);
    if (status == LXA_OK) *implied_paragraph = view.implied_paragraph;
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_frameset_body(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, bool *eligible)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_IN_BODY || eligible == NULL) return LXA_ERROR_ARGUMENT;
    size_t length = lxa_vector_length(&context->stack->elements);
    uint64_t first = 0;
    if (context->frameset_ok && context->body && length != 0) {
        if (context->body_identity == 0) {
            assert(false);
            return finish(context, LXA_ERROR_INTERNAL);
        }
        lxa_html_scope_entry_t entry;
        status = lxa_html_control_stack_get(context->stack, 0, &entry, NULL);
        if (status == LXA_OK) first = entry.identity;
    }
    if (status == LXA_OK)
        *eligible = lxa_html_tree_frameset_body_eligible(context->frameset_ok,
            context->body ? context->body_identity : 0, length, first);
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_root_mode_t *mode;
    uint64_t *identity;
    bool replace_body;
} root_frameset_control_view_t;

static lxa_status_t
frameset_prepare(root_frameset_control_view_t *view)
{
    if (!view->replace_body) return reserve_open_slot(view->context);
    lxa_html_root_context_t *context = view->context;
    lxa_html_control_stack_t *stack = context->stack;
    lxa_budget_t after = *stack->budget;
    lxa_status_t status = lxa_budget_release(&after, LXA_BUDGET_OPEN_ELEMENTS,
        lxa_vector_length(&stack->elements));
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    status = lxa_budget_check(&after, LXA_BUDGET_OPEN_ELEMENTS, 1, &context->limit_reason);
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&stack->elements, 1);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = LXA_BUDGET_MEMORY;
    }
    return status;
}

static lxa_status_t
frameset_admit(root_frameset_control_view_t *view, const lxa_html_token_t *token)
{
    (void) token;
    lxa_html_root_context_t *context = view->context;
    lxa_status_t status = lxa_budget_check_item(context->stack->budget,
        LXA_BUDGET_NAME_BYTES, 8, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_charge(context->stack->budget, LXA_BUDGET_WORK, 1, &context->limit_reason);
    if (status == LXA_OK)
        status = check_body_child_depth(context,
            *view->mode == LXA_TREE_IN_FRAMESET
                ? lxa_vector_length(&context->stack->elements) : 0);
    if (status == LXA_OK && view->replace_body) {
        lxa_html_control_stack_t *stack = context->stack;
        if (stack->next_identity == 0) return LXA_ERROR_OVERFLOW;
        size_t work;
        status = lxa_size_add(lxa_vector_length(&stack->elements), 1, &work);
        if (status == LXA_OK)
            status = lxa_budget_check(stack->budget, LXA_BUDGET_WORK, work, &context->limit_reason);
        if (status == LXA_OK && stack->charged_name_bytes < 8)
            status = lxa_budget_check(stack->budget, LXA_BUDGET_STRINGS,
                8 - stack->charged_name_bytes, &context->limit_reason);
        if (status == LXA_OK) {
            status = lxa_buffer_reserve(&stack->names, 8);
            if (status == LXA_ERROR_LIMIT) context->limit_reason = LXA_BUDGET_MEMORY;
        }
    }
    return status;
}

static lxa_status_t
frameset_replace(root_frameset_control_view_t *view)
{
    if (!view->replace_body) return LXA_OK;
    lxa_status_t status = lxa_html_control_stack_pop_to(view->context->stack, 0);
    if (status == LXA_OK) {
        view->context->body = false;
        view->context->body_identity = 0;
    }
    return status;
}

#define LXA_FRAMESET_START_NAME root_start_frameset
#define LXA_FRAMESET_START_VIEW root_frameset_control_view_t
#define LXA_FRAMESET_START_PREPARE frameset_prepare
#define LXA_FRAMESET_START_HEAD(view) context_ensure_head((view)->context)
#define LXA_FRAMESET_START_ADMIT frameset_admit
#define LXA_FRAMESET_START_REPLACE frameset_replace
#define LXA_FRAMESET_START_PUSH(view) lxa_html_control_stack_push((view)->context->stack, \
    (lxa_span_t){(const uint8_t *) "frameset", 8}, LXA_DOM_NS_HTML, false, (view)->identity)
#define LXA_FRAMESET_START_MODE(view) (*(view)->mode)
#include "html/frameset_start.inc"
#undef LXA_FRAMESET_START_NAME
#undef LXA_FRAMESET_START_VIEW
#undef LXA_FRAMESET_START_PREPARE
#undef LXA_FRAMESET_START_HEAD
#undef LXA_FRAMESET_START_ADMIT
#undef LXA_FRAMESET_START_REPLACE
#undef LXA_FRAMESET_START_PUSH
#undef LXA_FRAMESET_START_MODE

lxa_status_t
lxa_html_root_context_start_frameset(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || identity == NULL
        || (*mode != LXA_TREE_IN_FRAMESET && !body_start_mode_valid(context, *mode))
        || (*mode == LXA_TREE_IN_FRAMESET && !frameset_owner_valid(context, *mode))
        || (*mode != LXA_TREE_IN_BODY && *mode != LXA_TREE_IN_FRAMESET && (context->body
            || lxa_vector_length(&context->stack->elements) != 0))
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "frameset", 8)) return LXA_ERROR_ARGUMENT;
    bool replace = *mode == LXA_TREE_IN_BODY;
    if (replace) {
        bool eligible;
        status = lxa_html_root_context_frameset_body(context, *mode, &eligible);
        if (status != LXA_OK || !eligible) return status;
    }
    root_frameset_control_view_t view = {context, mode, identity, replace};
    status = root_start_frameset(&view, token);
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    const lxa_html_root_frameset_borrowers_t *borrowers;
    lxa_html_root_mode_t *mode;
    lxa_html_root_frameset_result_t *result;
} root_frameset_execute_view_t;

#define LXA_FRAMESET_EXECUTE_NAME root_execute_frameset
#define LXA_FRAMESET_EXECUTE_VIEW root_frameset_execute_view_t
#define LXA_FRAMESET_EXECUTE_FINISH(view) lxa_html_root_context_finish((view)->context, (view)->mode)
#define LXA_FRAMESET_EXECUTE_COMMENT(view, token) LXA_OK
#define LXA_FRAMESET_EXECUTE_HTML(view, token) LXA_OK
#define LXA_FRAMESET_EXECUTE_SPACES(view, token) lxa_html_root_context_prepare_frameset_data( \
    (view)->context, (view)->borrowers->formatting, *(view)->mode, (token)->data, (view)->borrowers->spaces)
#define LXA_FRAMESET_EXECUTE_TEXT(view, token) lxa_html_root_context_start_text((view)->context, \
    (view)->borrowers->text, (view)->borrowers->tokenizer, token, (view)->mode, &(view)->result->skip_newline)
#define LXA_FRAMESET_EXECUTE_START(view, token) lxa_html_root_context_start_frameset( \
    (view)->context, token, (view)->mode, &(view)->result->identity)
#define LXA_FRAMESET_EXECUTE_FRAME(view, token) \
    lxa_html_root_context_start_void((view)->context, NULL, token, (view)->mode)
#define LXA_FRAMESET_EXECUTE_END(view, token, action) \
    lxa_html_root_context_end_frameset((view)->context, (token)->name, (view)->mode)
#include "html/frameset_execute.inc"
#undef LXA_FRAMESET_EXECUTE_NAME
#undef LXA_FRAMESET_EXECUTE_VIEW
#undef LXA_FRAMESET_EXECUTE_FINISH
#undef LXA_FRAMESET_EXECUTE_COMMENT
#undef LXA_FRAMESET_EXECUTE_HTML
#undef LXA_FRAMESET_EXECUTE_SPACES
#undef LXA_FRAMESET_EXECUTE_TEXT
#undef LXA_FRAMESET_EXECUTE_START
#undef LXA_FRAMESET_EXECUTE_FRAME
#undef LXA_FRAMESET_EXECUTE_END

lxa_status_t
lxa_html_root_context_frameset_token(lxa_html_root_context_t *context,
    const lxa_html_root_frameset_borrowers_t *borrowers, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, lxa_html_root_frameset_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || result == NULL || token == NULL || !frameset_owner_valid(context, *mode))
        return LXA_ERROR_ARGUMENT;
    lxa_html_frameset_action_t action;
    status = lxa_html_tree_frameset_action(*mode, token->kind, token->name, &action);
    if (status != LXA_OK) return status;
    /* Admission hooks validate their token once; quiet actions have no hook. */
    if (action != LXA_HTML_FRAMESET_START && action != LXA_HTML_FRAMESET_FRAME_START
        && action != LXA_HTML_FRAMESET_NOFRAMES_START && !lxa_html_tree_token_shape_valid(token))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_root_frameset_borrowers_t empty = {0};
    lxa_html_root_frameset_result_t out = {action, 0, false};
    root_frameset_execute_view_t view = {context, borrowers == NULL ? &empty : borrowers, mode, &out};
    status = root_execute_frameset(&view, token, action);
    if (status == LXA_OK) *result = out;
    if (status == LXA_ERROR_ARGUMENT && context->failure == LXA_OK) return status;
    return finish(context, status);
}
