#include <assert.h>
#include <string.h>
#include "base/vector_internal.h"
#include "base/budget_internal.h"
#include "html/root_context.h"
#include "html/control_stack_internal.h"
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
    return lxa_html_control_stack_current_namespace_impl(context->stack, 0, &ns);
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
    bool may_grow = false;
    lxa_status_t status = lxa_budget_check_impl(context->stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
        additional, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)), additional, &count);
    if (status == LXA_OK) {
        may_grow = count > context->stack->elements.bytes.capacity
                           / sizeof(lxa_html_control_element_t);
        status = lxa_vector_reserve(&context->stack->elements, count);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = LXA_BUDGET_MEMORY;
    }
    if (status == LXA_OK && may_grow)
        status = lxa_budget_check_impl(context->stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
            additional, &context->limit_reason);
    return status;
}

static bool
frameset_owner_valid(const lxa_html_root_context_t *context, lxa_html_root_mode_t mode)
{
    size_t length = lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t));
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
check_body_child_depth(lxa_html_root_context_t *context, size_t retained,
    size_t *required)
{
    if (required != NULL) *required = 0;
    size_t depth;
    lxa_status_t status = lxa_size_add(retained, 2, &depth);
    if (status == LXA_OK)
        status = lxa_budget_check_impl(context->stack->budget, LXA_BUDGET_TREE_DEPTH,
            depth, &context->limit_reason);
    if (status == LXA_OK && required != NULL) *required = depth;
    return status;
}

#define LXA_FRAMESET_END_NAME context_frameset_end
#define LXA_FRAMESET_END_VIEW lxa_html_root_context_t
#define LXA_FRAMESET_END_LENGTH(view) lxa_vector_length_sized(&(view)->stack->elements, \
    sizeof(lxa_html_control_element_t))
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
    if (lxa_vector_length_sized(&stack->elements,
        sizeof(lxa_html_control_element_t)) != 0) return LXA_ERROR_ARGUMENT;
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
    lxa_status_t status = lxa_budget_check_item_impl(budget, LXA_BUDGET_NAME_BYTES, 4,
        &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_charge_impl(budget, LXA_BUDGET_WORK, 1, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check_impl(budget, LXA_BUDGET_TREE_DEPTH,
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
#define LXA_ROOT_ENSURE_OPEN_LENGTH(view) lxa_vector_length_sized(&(view)->stack->elements, \
    sizeof(lxa_html_control_element_t))
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
    if (!context->body && lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t)) != 0)
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
        || context->body || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) != 0)
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
    size_t length = lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t));
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

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_text_context_t *text;
    lxa_html_tokenizer_t *tokenizer;
    lxa_html_root_mode_t *mode;
} root_foster_text_view_t;

static lxa_status_t
context_foster_text_enter(root_foster_text_view_t *view, const lxa_html_token_t *token)
{
    lxa_status_t status = lxa_html_text_context_enter(view->text, view->tokenizer,
        token->name, *view->mode, &view->context->limit_reason);
    if (status == LXA_OK) *view->mode = LXA_TREE_TEXT;
    return status;
}

#define LXA_ROOT_FOSTER_TEXT_NAME context_start_foster_text
#define LXA_ROOT_FOSTER_TEXT_VIEW root_foster_text_view_t
#define LXA_ROOT_FOSTER_TEXT_INSERT(view, token) LXA_OK
#define LXA_ROOT_FOSTER_TEXT_ENTER context_foster_text_enter
#define LXA_ROOT_FOSTER_TEXT_POP(view) \
    lxa_html_control_stack_pop_to((view)->context->stack, \
        lxa_vector_length_sized(&(view)->context->stack->elements, \
            sizeof(lxa_html_control_element_t)) - 1)
#include "html/root_foster_text.inc"
#undef LXA_ROOT_FOSTER_TEXT_NAME
#undef LXA_ROOT_FOSTER_TEXT_VIEW
#undef LXA_ROOT_FOSTER_TEXT_INSERT
#undef LXA_ROOT_FOSTER_TEXT_ENTER
#undef LXA_ROOT_FOSTER_TEXT_POP

static bool
text_start_owners_valid(const lxa_html_text_context_t *text, const lxa_html_tokenizer_t *tokenizer)
{
    return text != NULL && tokenizer != NULL && text->plan.mode == LXA_HTML_DATA
        && text->plan.end_tag.data == NULL && text->plan.end_tag.length == 0;
}

static bool
root_token_borrowers_valid(const lxa_html_root_token_borrowers_t *borrowers, lxa_budget_t *budget)
{
    return borrowers != NULL && borrowers->formatting != NULL
        && borrowers->formatting->store != NULL && borrowers->formatting->store->budget == budget
        && borrowers->form_owner != NULL && text_start_owners_valid(borrowers->text, borrowers->tokenizer);
}

static lxa_status_t
root_table_text_intake(lxa_html_root_context_t *context, lxa_html_text_context_t *text,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token, lxa_html_root_mode_t *mode,
    lxa_html_root_table_start_action_t expected, bool *column_group)
{
    if (mode == NULL || !text_start_owners_valid(text, tokenizer)
        || !context->body || *mode != LXA_TREE_IN_BODY
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) == 0
        || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_returning_text_element(token->name)) return LXA_ERROR_ARGUMENT;
    lxa_html_root_table_start_action_t action;
    lxa_status_t status = lxa_html_tree_root_table_start_action(token->name, context->stack->budget,
        &context->limit_reason, &action);
    if (status != LXA_OK) return status;
    if (action != expected) return LXA_ERROR_ARGUMENT;
    return lxa_html_control_stack_column_group(context->stack, 0, column_group);
}

lxa_status_t
lxa_html_root_context_foster_text(lxa_html_root_context_t *context,
    lxa_html_text_context_t *text, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, bool *pending_newline)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (pending_newline == NULL) return LXA_ERROR_ARGUMENT;
    bool column_group;
    status = root_table_text_intake(context, text, tokenizer, token, mode,
        LXA_HTML_ROOT_TABLE_TEXT_FOSTER, &column_group);
    if (status == LXA_ERROR_ARGUMENT) return status;
    root_foster_text_view_t view = {context, text, tokenizer, mode};
    if (status == LXA_OK) status = context_start_foster_text(&view, token, column_group, pending_newline);
    return finish(context, status);
}

#define LXA_ROOT_TABLE_TEXT_NAME context_start_table_text
#define LXA_ROOT_TABLE_TEXT_VIEW root_foster_text_view_t
#define LXA_ROOT_TABLE_TEXT_PREPARE(view) LXA_OK
#define LXA_ROOT_TABLE_TEXT_POP(view) \
    lxa_html_control_stack_pop_to((view)->context->stack, \
        lxa_vector_length_sized(&(view)->context->stack->elements, \
            sizeof(lxa_html_control_element_t)) - 1)
#define LXA_ROOT_TABLE_TEXT_ENTER context_foster_text_enter
#include "html/root_table_text_start.inc"
#undef LXA_ROOT_TABLE_TEXT_NAME
#undef LXA_ROOT_TABLE_TEXT_VIEW
#undef LXA_ROOT_TABLE_TEXT_PREPARE
#undef LXA_ROOT_TABLE_TEXT_POP
#undef LXA_ROOT_TABLE_TEXT_ENTER

lxa_status_t
lxa_html_root_context_start_table_text(lxa_html_root_context_t *context,
    lxa_html_text_context_t *text, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    bool column_group;
    status = root_table_text_intake(context, text, tokenizer, token, mode,
        LXA_HTML_ROOT_TABLE_TEXT_CURRENT, &column_group);
    if (status == LXA_ERROR_ARGUMENT) return status;
    root_foster_text_view_t view = {context, text, tokenizer, mode};
    if (status == LXA_OK) status = context_start_table_text(&view, token, column_group);
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
        size_t length = lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t));
        size_t retained = length - group, count;
        bool may_grow = false;
        status = check_body_child_depth(context, retained, NULL);
        if (status == LXA_OK)
            status = lxa_budget_check_impl(context->stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
                group ? 0 : 1, &context->limit_reason);
        if (status == LXA_OK)
            status = lxa_budget_check_item_impl(context->stack->budget, LXA_BUDGET_NAME_BYTES,
                token->name.length, &context->limit_reason);
        if (status == LXA_OK) status = lxa_size_add(retained, 1, &count);
        if (status == LXA_OK) {
            may_grow = count > context->stack->elements.bytes.capacity
                               / sizeof(lxa_html_control_element_t);
            status = lxa_vector_reserve(&context->stack->elements, count);
        }
        if (status == LXA_OK && may_grow)
            status = check_body_child_depth(context, retained, NULL);
        if (status == LXA_OK && may_grow)
            status = lxa_budget_check_impl(context->stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
                group ? 0 : 1, &context->limit_reason);
        if (status == LXA_OK && may_grow)
            status = lxa_budget_check_item_impl(context->stack->budget, LXA_BUDGET_NAME_BYTES,
                token->name.length, &context->limit_reason);
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
            : (!context->body || lxa_vector_length_sized(&context->stack->elements,
                sizeof(lxa_html_control_element_t)) == 0)))
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
    status = lxa_size_add(lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t)), length - start, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, 1, &depth);
    if (status == LXA_OK)
        status = lxa_budget_check_impl(context->stack->budget, LXA_BUDGET_TREE_DEPTH,
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
            lxa_vector_length_sized(&context->stack->elements,
                sizeof(lxa_html_control_element_t)) - 1);
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
    lxa_html_tree_observe_meta((view)->context->meta, token, LXA_DOM_NS_HTML)
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
        status = lxa_budget_check_item_impl(context->stack->budget, LXA_BUDGET_NAME_BYTES,
            5, &context->limit_reason);
        if (status == LXA_OK)
            status = lxa_budget_charge_impl(context->stack->budget, LXA_BUDGET_WORK, 1,
                &context->limit_reason);
        if (status == LXA_OK)
            status = check_body_child_depth(context, lxa_vector_length_sized(&context->stack->elements,
                sizeof(lxa_html_control_element_t)), NULL);
        return finish(context, status);
    }
    bool head = lxa_html_tree_head_void(token->name);
    bool reconstruct = !head && lxa_html_tree_body_void_reconstructs(token->name);
    if ((!head && !lxa_html_tree_body_void(token->name))
        || (!body_start_mode_valid(context, *mode)
            && !(head && *mode == LXA_TREE_HEAD_NOSCRIPT && context->head))
        || (head && *mode != LXA_TREE_IN_BODY
            && (context->body || lxa_vector_length_sized(&context->stack->elements,
                sizeof(lxa_html_control_element_t)) != 0))
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
    if (mode == NULL || context->body || lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t)) != 0
        || (*mode == LXA_TREE_BEFORE_HEAD && !context->html)
        || (*mode == LXA_TREE_AFTER_HEAD && !context->head))
        return LXA_ERROR_ARGUMENT;
    lxa_html_prebody_end_action_t action;
    status = lxa_html_tree_prebody_end_action(*mode, name, &action);
    if (status != LXA_OK) return status;
    return finish(context, context_prebody_end(context, action, mode));
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_root_mode_t *mode;
    lxa_html_document_mode_t *document_mode;
} root_initial_view_t;

#define LXA_ROOT_INITIAL_NAME context_execute_initial
#define LXA_ROOT_INITIAL_VIEW root_initial_view_t
#define LXA_ROOT_INITIAL_RESOLVE(view, token, result) \
    lxa_html_tree_doctype_mode(token, (view)->context->stack->budget, \
        &(view)->context->limit_reason, result)
#define LXA_ROOT_INITIAL_INSERT(view, token) LXA_OK
#define LXA_ROOT_INITIAL_DOCUMENT_MODE(view) (*(view)->document_mode)
#define LXA_ROOT_INITIAL_MODE(view) (*(view)->mode)
#include "html/root_initial_execute.inc"
#undef LXA_ROOT_INITIAL_NAME
#undef LXA_ROOT_INITIAL_VIEW
#undef LXA_ROOT_INITIAL_RESOLVE
#undef LXA_ROOT_INITIAL_INSERT
#undef LXA_ROOT_INITIAL_DOCUMENT_MODE
#undef LXA_ROOT_INITIAL_MODE

lxa_status_t
lxa_html_root_context_initial_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t *mode, const lxa_html_token_t *token,
    lxa_html_document_mode_t *document_mode, lxa_html_initial_action_t *action)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || *mode != LXA_TREE_INITIAL || document_mode == NULL
        || (unsigned) *document_mode > LXA_HTML_QUIRKS || action == NULL
        || context->html || context->head || context->body
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) != 0
        || !lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    root_initial_view_t view = {context, mode, document_mode};
    lxa_html_initial_action_t out = lxa_html_tree_initial_action(token->kind, token->data);
    status = context_execute_initial(&view, token, out);
    if (status == LXA_OK) *action = out;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_root_prebody_text_result_t *result;
    lxa_html_root_prebody_emit_t emit;
    void *user;
} root_prebody_text_view_t;

static lxa_status_t
context_prebody_text_plan(root_prebody_text_view_t *view, lxa_span_t bytes,
    lxa_html_prebody_text_plan_t *plan)
{
    lxa_status_t status = lxa_html_tree_prebody_text_plan(view->result->next_mode, bytes,
        view->context->stack->budget, &view->context->limit_reason, plan);
    if (status == LXA_OK) view->result->plan = *plan;
    return status;
}

static lxa_status_t
context_prebody_text_emit(root_prebody_text_view_t *view, lxa_span_t bytes)
{
    if (view->emit == NULL) return LXA_OK;
    return view->emit(view->user, bytes, view->result->next_mode == LXA_TREE_IN_HEAD
        ? LXA_HTML_ROOT_MISC_HEAD : LXA_HTML_ROOT_MISC_HTML);
}

#define LXA_PREBODY_TEXT_NAME context_execute_prebody_text
#define LXA_PREBODY_TEXT_VIEW root_prebody_text_view_t
#define LXA_PREBODY_TEXT_PLAN context_prebody_text_plan
#define LXA_PREBODY_TEXT_EMIT context_prebody_text_emit
#define LXA_PREBODY_TEXT_HEAD(view) context_ensure_head((view)->context)
#define LXA_PREBODY_TEXT_MODE(view) ((view)->result->next_mode)
#include "html/prebody_text_execute.inc"
#undef LXA_PREBODY_TEXT_NAME
#undef LXA_PREBODY_TEXT_VIEW
#undef LXA_PREBODY_TEXT_PLAN
#undef LXA_PREBODY_TEXT_EMIT
#undef LXA_PREBODY_TEXT_HEAD
#undef LXA_PREBODY_TEXT_MODE

lxa_status_t
lxa_html_root_context_prebody_text(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_span_t data, lxa_html_root_prebody_text_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (result == NULL || context->body || lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t)) != 0
        || (mode == LXA_TREE_BEFORE_HEAD && !context->html)
        || ((mode == LXA_TREE_IN_HEAD || mode == LXA_TREE_AFTER_HEAD) && !context->head))
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_prebody_text_result_t out = {.next_mode = mode};
    root_prebody_text_view_t view = {.context = context, .result = &out};
    status = context_execute_prebody_text(&view, &data);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status == LXA_OK) *result = out;
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
#define LXA_FORMATTING_START_OPEN_LENGTH(view) lxa_vector_length_sized(&(view)->context->stack->elements, \
    sizeof(lxa_html_control_element_t))
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

typedef struct {
    root_start_view_t recovery;
    uint64_t *identity;
} root_formatting_start_view_t;

static lxa_status_t
context_root_formatting_admit(root_formatting_start_view_t *view,
    const lxa_html_token_t *token, size_t drop)
{
    lxa_html_root_context_t *context = view->recovery.context;
    lxa_html_formatting_list_t *formatting = view->recovery.formatting;
    size_t depth;
    lxa_status_t status = check_body_child_depth(context, lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t)), &depth);
    if (status == LXA_OK)
        status = lxa_html_formatting_list_admit_planned(formatting, context->stack,
            token, drop, depth, view->identity);
    return status;
}

#define LXA_ROOT_FORMATTING_START_NAME context_start_root_formatting
#define LXA_ROOT_FORMATTING_START_VIEW root_formatting_start_view_t
#define LXA_ROOT_FORMATTING_START_BODY(view) context_ensure_body((view)->recovery.context)
#define LXA_ROOT_FORMATTING_START_FRAMESET(view) ((view)->recovery.context->frameset_ok)
#define LXA_ROOT_FORMATTING_START_PREPARE(view, token, drop) root_prepare_start(&(view)->recovery, token, drop)
#define LXA_ROOT_FORMATTING_START_ADMIT context_root_formatting_admit
#include "html/root_formatting_start.inc"
#undef LXA_ROOT_FORMATTING_START_NAME
#undef LXA_ROOT_FORMATTING_START_VIEW
#undef LXA_ROOT_FORMATTING_START_BODY
#undef LXA_ROOT_FORMATTING_START_FRAMESET
#undef LXA_ROOT_FORMATTING_START_PREPARE
#undef LXA_ROOT_FORMATTING_START_ADMIT

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
    root_formatting_start_view_t view = {{context, formatting}, identity};
    if (status == LXA_OK) status = context_start_root_formatting(&view, token, mode);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = formatting->store->limit_reason;
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

#define LXA_ROOT_FOSTER_GROUP_NAME context_recover_foster_group
#define LXA_ROOT_FOSTER_GROUP_VIEW lxa_html_root_context_t
#define LXA_ROOT_FOSTER_GROUP_RESERVE(view) reserve_open_slots(view, 0)
#define LXA_ROOT_FOSTER_GROUP_NODE(view) LXA_OK
#define LXA_ROOT_FOSTER_GROUP_WORK(view) LXA_OK
#define LXA_ROOT_FOSTER_GROUP_POP(view) \
    lxa_html_control_stack_pop_to((view)->stack, lxa_vector_length_sized(&(view)->stack->elements, \
        sizeof(lxa_html_control_element_t)) - 1)
#include "html/root_foster_group_recover.inc"
#undef LXA_ROOT_FOSTER_GROUP_NAME
#undef LXA_ROOT_FOSTER_GROUP_VIEW
#undef LXA_ROOT_FOSTER_GROUP_RESERVE
#undef LXA_ROOT_FOSTER_GROUP_NODE
#undef LXA_ROOT_FOSTER_GROUP_WORK
#undef LXA_ROOT_FOSTER_GROUP_POP

lxa_status_t
lxa_html_root_context_recover_foster_group(lxa_html_root_context_t *context,
    lxa_html_root_table_start_action_t action, lxa_html_root_mode_t mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode != LXA_TREE_IN_BODY
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) == 0
        || (action != LXA_HTML_ROOT_TABLE_VOID && action != LXA_HTML_ROOT_TABLE_SVG
            && action != LXA_HTML_ROOT_TABLE_MATH && action != LXA_HTML_ROOT_TABLE_CONTAINER))
        return LXA_ERROR_ARGUMENT;
    bool group;
    status = lxa_html_control_stack_column_group(context->stack, 0, &group);
    if (status == LXA_OK && group)
        status = context_recover_foster_group(context, action == LXA_HTML_ROOT_TABLE_CONTAINER);
    return finish(context, status);
}

static lxa_status_t
context_foster_html_admit(root_formatting_start_view_t *view,
    const lxa_html_token_t *token, bool keep_open, bool formatting, bool marker, size_t drop)
{
    lxa_html_root_context_t *context = view->recovery.context;
    if (!keep_open) return lxa_html_tree_observe_meta(context->meta, token, LXA_DOM_NS_HTML);
    lxa_html_formatting_list_t *list = view->recovery.formatting;
    size_t depth;
    lxa_status_t status = check_body_child_depth(context, lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t)), &depth);
    if (status != LXA_OK) return status;
    if (formatting) status = lxa_html_formatting_list_admit_planned(list,
        context->stack, token, drop, depth, view->identity);
    else if (marker) status = lxa_html_formatting_list_admit_marker(list,
        context->stack, token->name, depth, view->identity);
    else return root_start_stack_status(&view->recovery,
        lxa_html_control_stack_push_token(context->stack, token, LXA_DOM_NS_HTML, view->identity));
    if (status == LXA_ERROR_LIMIT) context->limit_reason = list->store->limit_reason;
    return status;
}

#define LXA_FOSTER_HTML_NAME context_foster_html
#define LXA_FOSTER_HTML_VIEW root_formatting_start_view_t
#define LXA_FOSTER_HTML_PREPARE(view, token, drop) root_prepare_start(&(view)->recovery, token, drop)
#define LXA_FOSTER_HTML_RECONSTRUCT(view) \
    lxa_html_root_context_reconstruct((view)->recovery.context, (view)->recovery.formatting)
#define LXA_FOSTER_HTML_MARKER(view) lxa_html_formatting_list_reserve_marker((view)->recovery.formatting)
#define LXA_FOSTER_HTML_ADMIT context_foster_html_admit
#include "html/foster_html_start.inc"
#undef LXA_FOSTER_HTML_NAME
#undef LXA_FOSTER_HTML_VIEW
#undef LXA_FOSTER_HTML_PREPARE
#undef LXA_FOSTER_HTML_RECONSTRUCT
#undef LXA_FOSTER_HTML_MARKER
#undef LXA_FOSTER_HTML_ADMIT

lxa_status_t
lxa_html_root_context_foster_html(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    bool keep_open, lxa_html_root_mode_t mode, uint64_t *identity, bool *skip_newline)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (identity == NULL || skip_newline == NULL || !context->body || mode != LXA_TREE_IN_BODY
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) == 0
        || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG)
        return LXA_ERROR_ARGUMENT;
    if (lxa_html_tree_formatting_tag(token->name)) {
        status = lxa_html_formatting_list_start_intake(formatting, context->stack, 0, token);
        if (status == LXA_ERROR_ARGUMENT) return status;
        if (status != LXA_OK) {
            if (status == LXA_ERROR_LIMIT) context->limit_reason = formatting->store->limit_reason;
            return finish(context, status);
        }
    }
    lxa_html_root_table_start_action_t action;
    status = lxa_html_tree_root_table_start_action(token->name, context->stack->budget,
        &context->limit_reason, &action);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status != LXA_OK) return finish(context, status);
    if (action != (keep_open ? LXA_HTML_ROOT_TABLE_CONTAINER : LXA_HTML_ROOT_TABLE_VOID))
        return LXA_ERROR_ARGUMENT;
    root_formatting_start_view_t view = {{context, formatting}, identity};
    status = context_foster_html(&view, token, keep_open, skip_newline);
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
        lxa_vector_length_sized(&context->stack->elements, sizeof(lxa_html_control_element_t)), NULL);
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
        lxa_vector_length_sized(&(view)->context->stack->elements, \
            sizeof(lxa_html_control_element_t)) - 1)
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

#define LXA_ROOT_FOSTER_FOREIGN_NAME context_foster_foreign
#define LXA_ROOT_FOSTER_FOREIGN_VIEW root_start_view_t
#define LXA_ROOT_FOSTER_FOREIGN_RECONSTRUCT(view) \
    lxa_html_root_context_reconstruct((view)->context, (view)->formatting)
#define LXA_ROOT_FOSTER_FOREIGN_RESERVE(view) reserve_open_slot((view)->context)
#define LXA_ROOT_FOSTER_FOREIGN_INSERT context_foreign_insert
#define LXA_ROOT_FOSTER_FOREIGN_POP(view) \
    lxa_html_control_stack_pop_to((view)->context->stack, \
        lxa_vector_length_sized(&(view)->context->stack->elements, \
            sizeof(lxa_html_control_element_t)) - 1)
#include "html/root_foster_foreign.inc"
#undef LXA_ROOT_FOSTER_FOREIGN_NAME
#undef LXA_ROOT_FOSTER_FOREIGN_VIEW
#undef LXA_ROOT_FOSTER_FOREIGN_RECONSTRUCT
#undef LXA_ROOT_FOSTER_FOREIGN_RESERVE
#undef LXA_ROOT_FOSTER_FOREIGN_INSERT
#undef LXA_ROOT_FOSTER_FOREIGN_POP

lxa_status_t
lxa_html_root_context_foster_foreign(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode != LXA_TREE_IN_BODY
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) == 0
        || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || (!lxa_html_tree_name_equals(token->name, "svg", 3)
            && !lxa_html_tree_name_equals(token->name, "math", 4))) return LXA_ERROR_ARGUMENT;
    root_start_view_t view = {context, formatting};
    return finish(context, context_foster_foreign(&view, token));
}

typedef struct {
    root_formatting_start_view_t html;
    root_foster_text_view_t text;
    bool *pending_newline;
} root_table_exec_view_t;

static lxa_status_t
context_table_exec_group(root_table_exec_view_t *view, bool container)
{
    lxa_html_root_context_t *context = view->html.recovery.context;
    lxa_status_t status = context_recover_foster_group(context, container);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->stack->limit_reason;
    return status;
}

static lxa_status_t
context_table_exec_html(root_table_exec_view_t *view, const lxa_html_token_t *token, bool keep_open)
{
    bool newline;
    lxa_status_t status = context_foster_html(&view->html, token, keep_open, &newline);
    if (status == LXA_OK && newline) *view->pending_newline = true;
    return status;
}

#define LXA_ROOT_TABLE_EXEC_NAME context_execute_table_start
#define LXA_ROOT_TABLE_EXEC_VIEW root_table_exec_view_t
#define LXA_ROOT_TABLE_EXEC_TEMPLATE(view, token) LXA_OK
#define LXA_ROOT_TABLE_EXEC_TEXT(view, token, group) context_start_table_text(&(view)->text, token, group)
#define LXA_ROOT_TABLE_EXEC_PLAINTEXT(view, token) \
    lxa_html_root_context_start_plaintext((view)->html.recovery.context, (view)->text.tokenizer, \
        token, (view)->text.mode, (view)->html.identity)
#define LXA_ROOT_TABLE_EXEC_XMP(view, token) \
    lxa_html_root_context_start_xmp((view)->html.recovery.context, (view)->html.recovery.formatting, \
        (view)->text.text, (view)->text.tokenizer, token, (view)->text.mode)
#define LXA_ROOT_TABLE_EXEC_FOSTER_TEXT(view, token, group) \
    context_start_foster_text(&(view)->text, token, group, (view)->pending_newline)
#define LXA_ROOT_TABLE_EXEC_GROUP context_table_exec_group
#define LXA_ROOT_TABLE_EXEC_FOREIGN(view, token) context_foster_foreign(&(view)->html.recovery, token)
#define LXA_ROOT_TABLE_EXEC_HTML context_table_exec_html
#include "html/root_table_start_execute.inc"
#undef LXA_ROOT_TABLE_EXEC_NAME
#undef LXA_ROOT_TABLE_EXEC_VIEW
#undef LXA_ROOT_TABLE_EXEC_TEMPLATE
#undef LXA_ROOT_TABLE_EXEC_TEXT
#undef LXA_ROOT_TABLE_EXEC_PLAINTEXT
#undef LXA_ROOT_TABLE_EXEC_XMP
#undef LXA_ROOT_TABLE_EXEC_FOSTER_TEXT
#undef LXA_ROOT_TABLE_EXEC_GROUP
#undef LXA_ROOT_TABLE_EXEC_FOREIGN
#undef LXA_ROOT_TABLE_EXEC_HTML

static lxa_status_t
context_table_start_prepared(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_html_text_context_t *text,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token, lxa_html_root_mode_t *mode,
    uint64_t *identity, bool *pending_newline, bool *template_start, bool group)
{
    lxa_status_t status;
    lxa_html_root_table_start_action_t action;
    status = lxa_html_tree_root_table_start_action(token->name, context->stack->budget,
        &context->limit_reason, &action);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status != LXA_OK) return finish(context, status);
    if (action == LXA_HTML_ROOT_TABLE_UNSUPPORTED) return LXA_ERROR_ARGUMENT;
    bool needs_text = action == LXA_HTML_ROOT_TABLE_TEXT_CURRENT
        || action == LXA_HTML_ROOT_TABLE_TEXT_FOSTER || action == LXA_HTML_ROOT_TABLE_XMP;
    if ((needs_text && !text_start_owners_valid(text, tokenizer))
        || (action == LXA_HTML_ROOT_TABLE_PLAINTEXT && tokenizer == NULL)) return LXA_ERROR_ARGUMENT;
    bool html = action == LXA_HTML_ROOT_TABLE_CONTAINER || action == LXA_HTML_ROOT_TABLE_VOID;
    bool is_formatting = html && lxa_html_tree_formatting_tag(token->name);
    bool needs_formatting = action == LXA_HTML_ROOT_TABLE_SVG || action == LXA_HTML_ROOT_TABLE_MATH
        || action == LXA_HTML_ROOT_TABLE_XMP || is_formatting
        || (html && (lxa_html_tree_formatting_marker_tag(token->name)
            || lxa_html_tree_foster_reconstructs(token->name, LXA_DOM_NS_HTML,
                action == LXA_HTML_ROOT_TABLE_CONTAINER)));
    if (needs_formatting && formatting == NULL) return LXA_ERROR_ARGUMENT;
    if (is_formatting) {
        status = lxa_html_formatting_list_start_intake(formatting, context->stack, 0, token);
        if (status == LXA_ERROR_ARGUMENT) return status;
        if (status == LXA_ERROR_LIMIT) context->limit_reason = formatting->store->limit_reason;
        if (status != LXA_OK) return finish(context, status);
    }
    root_table_exec_view_t view = {{{context, formatting}, identity},
        {context, text, tokenizer, mode}, pending_newline};
    status = context_execute_table_start(&view, token, action, group);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT && formatting != NULL)
        context->limit_reason = formatting->store->limit_reason;
    if (status != LXA_OK) *identity = 0;
    else *template_start = action == LXA_HTML_ROOT_TABLE_TEMPLATE;
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_table_start_token(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_html_text_context_t *text,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token, lxa_html_root_mode_t *mode,
    uint64_t *identity, bool *pending_newline, bool *template_start)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t));
    if (mode == NULL || identity == NULL || pending_newline == NULL || template_start == NULL
        || !context->body || *mode != LXA_TREE_IN_BODY || length <= 1
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || (formatting != NULL && (formatting->store == NULL
            || formatting->store->budget != context->stack->budget))) return LXA_ERROR_ARGUMENT;
    lxa_html_scope_entry_t entry;
    status = lxa_html_control_stack_get(context->stack, length - 1, &entry, NULL);
    if (status != LXA_OK) return finish(context, status);
    if (entry.namespace_id != LXA_DOM_NS_HTML || !lxa_html_tree_table_context(entry.name))
        return LXA_ERROR_ARGUMENT;
    bool group = lxa_html_tree_name_equals(entry.name, "colgroup", 8);
    return context_table_start_prepared(context, formatting, text, tokenizer, token,
        mode, identity, pending_newline, template_start, group);
}


lxa_status_t
lxa_html_root_context_end_scoped(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_span_t name,
    lxa_html_root_mode_t mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_IN_BODY || !context->body
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) == 0
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
    lxa_html_control_stack_pop_to((view)->stack, lxa_vector_length_sized(&(view)->stack->elements, \
        sizeof(lxa_html_control_element_t)) - 1)
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
    lxa_status_t status = lxa_budget_check_item_impl(context->stack->budget,
        LXA_BUDGET_NAME_BYTES, 4, &context->limit_reason);
    if (status == LXA_OK) status = check_body_child_depth(context, keep, NULL);
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
        status = check_body_child_depth(context, lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)), NULL);
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
    if (lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t)) <= 1) return LXA_OK;
    size_t keep;
    lxa_status_t status = lxa_html_control_stack_list_item_plan(context->stack, 1, definition, &keep);
    return status == LXA_OK ? lxa_html_control_stack_pop_to(context->stack, keep) : status;
}

static lxa_status_t
close_heading(lxa_html_root_context_t *context)
{
    size_t length = lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t));
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
    size_t depth;
    lxa_status_t status = check_body_child_depth(context,
        lxa_vector_length_sized(&context->stack->elements, sizeof(lxa_html_control_element_t)), &depth);
    if (status != LXA_OK) return status;
    return view->marker
        ? lxa_html_formatting_list_admit_marker(view->formatting, context->stack,
            token->name, depth, view->identity)
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
        lxa_vector_length_sized(&view->context->stack->elements,
            sizeof(lxa_html_control_element_t)) - 1, &entry, NULL);
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

static lxa_status_t
context_table_part_void(root_container_start_view_t *view, const lxa_html_token_t *token)
{
    (void) token;
    *view->identity = 0;
    return LXA_OK;
}

#define LXA_TABLE_PART_ADMIT_NAME context_admit_table_part
#define LXA_TABLE_PART_ADMIT_VIEW root_container_start_view_t
#define LXA_TABLE_PART_ADMIT_RESERVE(view, count) reserve_open_slots((view)->context, count)
#define LXA_TABLE_PART_ADMIT_NODE(view, count) LXA_OK
#define LXA_TABLE_PART_ADMIT_INSERT context_table_part_insert
#define LXA_TABLE_PART_ADMIT_VOID context_table_part_void
#include "html/table_part_admit.inc"
#undef LXA_TABLE_PART_ADMIT_NAME
#undef LXA_TABLE_PART_ADMIT_VIEW
#undef LXA_TABLE_PART_ADMIT_RESERVE
#undef LXA_TABLE_PART_ADMIT_NODE
#undef LXA_TABLE_PART_ADMIT_INSERT
#undef LXA_TABLE_PART_ADMIT_VOID

lxa_status_t
lxa_html_root_context_admit_table_part(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t mode, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t));
    if (identity == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget || !context->body
        || mode != LXA_TREE_IN_BODY || length == 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG)
        return LXA_ERROR_ARGUMENT;
    lxa_html_prepared_table_part_plan_t plan;
    status = lxa_html_control_stack_prepared_table_part_plan(context->stack, token->name, &plan);
    if (status == LXA_ERROR_ARGUMENT) return status;
    if (status != LXA_OK) return finish(context, status);
    root_container_start_view_t view = {context, formatting, identity, false};
    status = context_admit_table_part(&view, token, &plan);
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

static lxa_status_t
context_table_part_scope(root_start_view_t *view, bool *found)
{
    size_t index;
    lxa_status_t status = lxa_html_control_stack_scope_find(view->context->stack, 0,
        (lxa_span_t){(const uint8_t *) "table", 5}, LXA_HTML_SCOPE_TABLE, &index);
    if (status == LXA_OK) *found = index != lxa_vector_length_sized(&view->context->stack->elements,
        sizeof(lxa_html_control_element_t));
    return status;
}

static lxa_status_t
context_table_part_pop(root_start_view_t *view, size_t keep)
{
    lxa_status_t status = lxa_html_formatting_list_pop_table(view->formatting,
        view->context->stack, keep, false);
    if (status == LXA_ERROR_LIMIT) view->context->limit_reason = view->formatting->store->limit_reason;
    return status;
}

static lxa_status_t
context_caption_find(root_start_view_t *view, size_t *index)
{
    return lxa_html_control_stack_scope_find(view->context->stack, 0,
        (lxa_span_t){(const uint8_t *) "caption", 7}, LXA_HTML_SCOPE_TABLE, index);
}

#define LXA_ROOT_CAPTION_NAME context_recover_caption
#define LXA_ROOT_CAPTION_VIEW root_start_view_t
#define LXA_ROOT_CAPTION_FIND context_caption_find
#define LXA_ROOT_CAPTION_LENGTH(view) lxa_vector_length_sized(&(view)->context->stack->elements, \
    sizeof(lxa_html_control_element_t))
#define LXA_ROOT_CAPTION_RESERVE(view, count) reserve_open_slots((view)->context, count)
#define LXA_ROOT_CAPTION_NODE(view, count) LXA_OK
#define LXA_ROOT_CAPTION_POP context_table_part_pop
#include "html/root_caption_recover.inc"
#undef LXA_ROOT_CAPTION_NAME
#undef LXA_ROOT_CAPTION_VIEW
#undef LXA_ROOT_CAPTION_FIND
#undef LXA_ROOT_CAPTION_LENGTH
#undef LXA_ROOT_CAPTION_RESERVE
#undef LXA_ROOT_CAPTION_NODE
#undef LXA_ROOT_CAPTION_POP

lxa_status_t
lxa_html_root_context_recover_caption(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || mode != LXA_TREE_IN_BODY || formatting == NULL
        || formatting->store == NULL || formatting->store->budget != context->stack->budget
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_table_part(token->name)
        || lxa_html_tree_name_equals(token->name, "table", 5)) return LXA_ERROR_ARGUMENT;
    root_start_view_t view = {context, formatting};
    return finish(context, context_recover_caption(&view, token->name));
}

#define LXA_ROOT_TABLE_PART_RECOVER_NAME context_recover_table_part_context
#define LXA_ROOT_TABLE_PART_RECOVER_VIEW root_start_view_t
#define LXA_ROOT_TABLE_PART_RECOVER_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_TABLE_PART_RECOVER_SCOPE context_table_part_scope
#define LXA_ROOT_TABLE_PART_RECOVER_PLAN(view, target, plan) \
    lxa_html_control_stack_root_table_part_context_plan((view)->context->stack, 1, target, plan)
#define LXA_ROOT_TABLE_PART_RECOVER_RESERVE(view, count) reserve_open_slots((view)->context, count)
#define LXA_ROOT_TABLE_PART_RECOVER_NODE(view, count) LXA_OK
#define LXA_ROOT_TABLE_PART_RECOVER_POP context_table_part_pop
#define LXA_ROOT_TABLE_PART_RECOVER_POSTPOP(view) LXA_OK
#include "html/root_table_part_context_recover.inc"
#undef LXA_ROOT_TABLE_PART_RECOVER_NAME
#undef LXA_ROOT_TABLE_PART_RECOVER_VIEW
#undef LXA_ROOT_TABLE_PART_RECOVER_BODY
#undef LXA_ROOT_TABLE_PART_RECOVER_SCOPE
#undef LXA_ROOT_TABLE_PART_RECOVER_PLAN
#undef LXA_ROOT_TABLE_PART_RECOVER_RESERVE
#undef LXA_ROOT_TABLE_PART_RECOVER_NODE
#undef LXA_ROOT_TABLE_PART_RECOVER_POP
#undef LXA_ROOT_TABLE_PART_RECOVER_POSTPOP

lxa_status_t
lxa_html_root_context_recover_table_part_context(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, bool *in_scope)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || in_scope == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget || !body_start_mode_valid(context, *mode)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_table_part(token->name)
        || lxa_html_tree_name_equals(token->name, "table", 5)) return LXA_ERROR_ARGUMENT;
    root_start_view_t view = {context, formatting};
    return finish(context, context_recover_table_part_context(&view, token, mode, in_scope));
}

static lxa_status_t
context_part_close_pop(root_container_start_view_t *view, size_t keep)
{
    root_start_view_t recovery = {view->context, view->formatting};
    return context_table_part_pop(&recovery, keep);
}

#define LXA_ROOT_PART_CLOSE_EXEC_NAME context_close_table_part
#define LXA_ROOT_PART_CLOSE_EXEC_VIEW root_container_start_view_t
#define LXA_ROOT_PART_CLOSE_EXEC_PLAN(view, group, index) \
    lxa_html_control_stack_root_table_part_close_plan((view)->context->stack, 1, group, index)
#define LXA_ROOT_PART_CLOSE_EXEC_LENGTH(view) lxa_vector_length_sized(&(view)->context->stack->elements, \
    sizeof(lxa_html_control_element_t))
#define LXA_ROOT_PART_CLOSE_EXEC_NODE(view) LXA_OK
#define LXA_ROOT_PART_CLOSE_EXEC_POP context_part_close_pop
#include "html/root_table_part_close.inc"
#undef LXA_ROOT_PART_CLOSE_EXEC_NAME
#undef LXA_ROOT_PART_CLOSE_EXEC_VIEW
#undef LXA_ROOT_PART_CLOSE_EXEC_PLAN
#undef LXA_ROOT_PART_CLOSE_EXEC_LENGTH
#undef LXA_ROOT_PART_CLOSE_EXEC_NODE
#undef LXA_ROOT_PART_CLOSE_EXEC_POP

static lxa_status_t
context_part_recover(root_container_start_view_t *view, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, bool *found)
{
    root_start_view_t recovery = {view->context, view->formatting};
    return context_recover_table_part_context(&recovery, token, mode, found);
}

static lxa_status_t
context_part_caption(root_container_start_view_t *view, lxa_span_t incoming)
{
    root_start_view_t recovery = {view->context, view->formatting};
    return context_recover_caption(&recovery, incoming);
}

#define LXA_ROOT_PART_START_NAME context_start_table_part
#define LXA_ROOT_PART_START_VIEW root_container_start_view_t
#define LXA_ROOT_PART_START_CONTEXT context_part_recover
#define LXA_ROOT_PART_START_CAPTION context_part_caption
#define LXA_ROOT_PART_START_TOP context_table_top
#define LXA_ROOT_PART_START_CLOSE context_close_table_part
#define LXA_ROOT_PART_START_RESERVE(view, count) reserve_open_slots((view)->context, count)
#define LXA_ROOT_PART_START_NODE(view, count) LXA_OK
#define LXA_ROOT_PART_START_POP(view) \
    lxa_html_control_stack_pop_to((view)->context->stack, \
        lxa_vector_length_sized(&(view)->context->stack->elements, \
            sizeof(lxa_html_control_element_t)) - 1)
#define LXA_ROOT_PART_START_ADMIT context_admit_table_part
#include "html/root_table_part_start.inc"
#undef LXA_ROOT_PART_START_NAME
#undef LXA_ROOT_PART_START_VIEW
#undef LXA_ROOT_PART_START_CONTEXT
#undef LXA_ROOT_PART_START_CAPTION
#undef LXA_ROOT_PART_START_TOP
#undef LXA_ROOT_PART_START_CLOSE
#undef LXA_ROOT_PART_START_RESERVE
#undef LXA_ROOT_PART_START_NODE
#undef LXA_ROOT_PART_START_POP
#undef LXA_ROOT_PART_START_ADMIT

lxa_status_t
lxa_html_root_context_start_table_part(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || identity == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget
        || !body_start_mode_valid(context, *mode)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_table_part(token->name)
        || lxa_html_tree_name_equals(token->name, "table", 5)) return LXA_ERROR_ARGUMENT;
    root_container_start_view_t view = {context, formatting, identity, false};
    status = context_start_table_part(&view, token, mode);
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

lxa_status_t
lxa_html_root_context_ordinary_start_token(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_document_mode_t document_mode, lxa_html_root_mode_t *mode,
    uint64_t *identity, bool *pending_newline)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || identity == NULL || pending_newline == NULL
        || (unsigned) document_mode > LXA_HTML_QUIRKS
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || (!body_start_mode_valid(context, *mode)
            && !(*mode == LXA_TREE_HEAD_NOSCRIPT && context->head
                && lxa_html_tree_head_void(token->name)))
        || lxa_html_tree_body_control_start(token->name)
        || lxa_html_tree_returning_text_element(token->name)
        || lxa_html_tree_name_equals(token->name, "form", 4)
        || lxa_html_tree_name_equals(token->name, "template", 8)
        || lxa_html_tree_name_equals(token->name, "xmp", 3)
        || lxa_html_tree_name_equals(token->name, "plaintext", 9)
        || (lxa_html_tree_name_equals(token->name, "noscript", 8) && *mode < LXA_TREE_AFTER_HEAD)
        || (formatting != NULL && (formatting->store == NULL
            || formatting->store->budget != context->stack->budget)))
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_ordinary_start_action_t action;
    status = lxa_html_tree_root_ordinary_start_action(token->name,
        context->stack->budget, &context->limit_reason, &action);
    if (status != LXA_OK) return finish(context, status);
    if (action == LXA_HTML_ROOT_ORDINARY_UNSUPPORTED) return LXA_ERROR_ARGUMENT;
    bool marker = action == LXA_HTML_ROOT_ORDINARY_CONTAINER
        && lxa_html_tree_template_marker_container(token->name);
    bool needs_formatting = action == LXA_HTML_ROOT_ORDINARY_FORMATTING
        || action == LXA_HTML_ROOT_ORDINARY_FOREIGN
        || action == LXA_HTML_ROOT_ORDINARY_TABLE_PART || marker
        || (action == LXA_HTML_ROOT_ORDINARY_CONTAINER
            && lxa_html_tree_container_reconstructs(token->name))
        || (action == LXA_HTML_ROOT_ORDINARY_BODY_VOID
            && lxa_html_tree_body_void_reconstructs(token->name));
    if (needs_formatting && formatting == NULL) return LXA_ERROR_ARGUMENT;
    bool newline = false;
    switch (action) {
    case LXA_HTML_ROOT_ORDINARY_HEAD_VOID:
    case LXA_HTML_ROOT_ORDINARY_BODY_VOID:
        status = lxa_html_root_context_start_void(context, formatting, token, mode);
        break;
    case LXA_HTML_ROOT_ORDINARY_TABLE_PART:
        status = lxa_html_tree_name_equals(token->name, "table", 5)
            ? lxa_html_root_context_start_table(context, formatting, token, document_mode, mode, identity)
            : lxa_html_root_context_start_table_part(context, formatting, token, mode, identity);
        break;
    case LXA_HTML_ROOT_ORDINARY_FOREIGN:
        status = lxa_html_root_context_start_foreign(context, formatting, token, mode);
        break;
    case LXA_HTML_ROOT_ORDINARY_FORMATTING:
        status = lxa_html_root_context_start_formatting(context, formatting, token, mode, identity);
        break;
    case LXA_HTML_ROOT_ORDINARY_CONTAINER: {
        root_container_start_view_t view = {context, formatting, identity, marker};
        status = context_start_container(&view, token, mode, &newline);
        if (status == LXA_ERROR_LIMIT && marker && context->limit_reason == LXA_BUDGET_COUNT)
            context->limit_reason = formatting->store->limit_reason;
        break;
    }
    default:
        assert(false);
        status = LXA_ERROR_INTERNAL;
        break;
    }
    if (status == LXA_OK && newline) *pending_newline = true;
    if (status == LXA_ERROR_ARGUMENT) return status;
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
    size_t length = lxa_vector_length_sized(&view->context->stack->elements,
        sizeof(lxa_html_control_element_t)), keep;
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
        status = lxa_html_control_stack_pop_to(stack, lxa_vector_length_sized(&stack->elements,
            sizeof(lxa_html_control_element_t)) - 1);
    if (status == LXA_OK) view->implied = true;
    return status;
}

#define LXA_ROOT_PARAGRAPH_END_NAME root_end_paragraph
#define LXA_ROOT_PARAGRAPH_END_VIEW root_paragraph_end_view_t
#define LXA_ROOT_PARAGRAPH_END_CLOSE paragraph_end_close
#define LXA_ROOT_PARAGRAPH_END_LENGTH(view) lxa_vector_length_sized(&(view)->context->stack->elements, \
    sizeof(lxa_html_control_element_t))
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
        lxa_vector_length_sized(&stack->elements,
            sizeof(lxa_html_control_element_t)) - 1, &entry, NULL);
    if (status == LXA_OK) *table = lxa_html_tree_table_context(entry.name);
    return status;
}

#define LXA_ROOT_DATA_NAME root_prepare_data
#define LXA_ROOT_DATA_VIEW root_data_view_t
#define LXA_ROOT_DATA_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_DATA_MODE(view) (*(view)->mode)
#define LXA_ROOT_DATA_FRAMESET(view) ((view)->context->frameset_ok)
#define LXA_ROOT_DATA_LENGTH(view) lxa_vector_length_sized(&(view)->context->stack->elements, \
    sizeof(lxa_html_control_element_t))
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
    status = lxa_budget_charge_impl(context->stack->budget, LXA_BUDGET_WORK,
        data.length, &context->limit_reason);
    root_data_view_t view = {context, formatting, mode};
    lxa_html_root_data_action_t out;
    if (status == LXA_OK) status = root_prepare_data(&view, data, &out);
    if (status == LXA_OK) *action = out;
    return finish(context, status);
}

typedef struct {
    root_data_view_t preparation;
    const lxa_html_root_data_sink_t *sink;
} root_data_execute_view_t;

static lxa_status_t context_emit_data(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_span_t data,
    lxa_html_data_emit_t emit, void *user, bool resolve_foreign);

static lxa_status_t
context_root_data_prebody(root_data_execute_view_t *view, lxa_span_t *bytes)
{
    lxa_html_root_prebody_text_result_t result = {.next_mode = *view->preparation.mode};
    root_prebody_text_view_t prebody = {
        view->preparation.context, &result, view->sink->prebody_emit, view->sink->user
    };
    lxa_status_t status = context_execute_prebody_text(&prebody, bytes);
    if (status == LXA_OK) *view->preparation.mode = result.next_mode;
    return status;
}

static lxa_status_t
context_root_data_prepare(root_data_execute_view_t *view, lxa_span_t bytes,
    lxa_html_root_data_action_t *action)
{
    if (bytes.length == 0) {
        *action = LXA_HTML_ROOT_DATA_IGNORE;
        return LXA_OK;
    }
    return lxa_html_root_context_prepare_data(view->preparation.context,
        view->preparation.formatting, bytes, view->preparation.mode, action);
}

static lxa_status_t
context_root_data_emit(root_data_execute_view_t *view, lxa_span_t bytes,
    lxa_html_root_data_action_t action)
{
    if (action == LXA_HTML_ROOT_DATA_TABLE)
        return lxa_html_root_context_table_data(view->preparation.context,
            *view->preparation.mode, bytes, view->sink->pending, view->sink->emit, view->sink->user);
    return context_emit_data(view->preparation.context,
        *view->preparation.mode, bytes, view->sink->emit, view->sink->user, false);
}

#define LXA_ROOT_DATA_EXEC_NAME context_execute_root_data
#define LXA_ROOT_DATA_EXEC_VIEW root_data_execute_view_t
#define LXA_ROOT_DATA_EXEC_MODE(view) (*(view)->preparation.mode)
#define LXA_ROOT_DATA_EXEC_PREBODY context_root_data_prebody
#define LXA_ROOT_DATA_EXEC_PREPARE context_root_data_prepare
#define LXA_ROOT_DATA_EXEC_EMIT context_root_data_emit
#include "html/root_data_execute.inc"
#undef LXA_ROOT_DATA_EXEC_NAME
#undef LXA_ROOT_DATA_EXEC_VIEW
#undef LXA_ROOT_DATA_EXEC_MODE
#undef LXA_ROOT_DATA_EXEC_PREBODY
#undef LXA_ROOT_DATA_EXEC_PREPARE
#undef LXA_ROOT_DATA_EXEC_EMIT

lxa_status_t
lxa_html_root_context_data_token(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_span_t data,
    lxa_html_root_mode_t *mode, const lxa_html_root_data_sink_t *sink)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || sink == NULL || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget
        || !lxa_html_tree_table_text_input_valid(sink->pending, data)
        || sink->pending->budget != context->stack->budget
        || !body_start_mode_valid(context, *mode)
        || (*mode == LXA_TREE_IN_BODY ? !context->body
            : context->body || lxa_vector_length_sized(&context->stack->elements,
                sizeof(lxa_html_control_element_t)) != 0))
        return LXA_ERROR_ARGUMENT;
    root_data_execute_view_t view = {{context, formatting, mode}, sink};
    return finish(context, context_execute_root_data(&view, data));
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

typedef struct {
    root_data_view_t preparation;
    lxa_html_data_emit_t emit;
    void *user;
} root_after_body_view_t;

static lxa_status_t
context_after_body_emit(root_after_body_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_root_context_t *context = view->preparation.context;
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_current(context->stack, 0, &entry, NULL);
    if (status != LXA_OK) return status;
    const lxa_html_data_emitter_t emitter = {
        context->stack->budget, &context->limit_reason, entry.identity, view->emit, view->user
    };
    return lxa_html_tree_emit_data(&emitter, token->data, false);
}

#define LXA_AFTER_BODY_NAME root_execute_after_body
#define LXA_AFTER_BODY_VIEW root_after_body_view_t
#define LXA_AFTER_BODY_RECONSTRUCT(view) lxa_html_root_context_reconstruct( \
    (view)->preparation.context, (view)->preparation.formatting)
#define LXA_AFTER_BODY_EMIT context_after_body_emit
#define LXA_AFTER_BODY_MODE(view) (*(view)->preparation.mode)
#include "html/after_body_execute.inc"
#undef LXA_AFTER_BODY_NAME
#undef LXA_AFTER_BODY_VIEW
#undef LXA_AFTER_BODY_RECONSTRUCT
#undef LXA_AFTER_BODY_EMIT
#undef LXA_AFTER_BODY_MODE

lxa_status_t
lxa_html_root_context_after_body(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, lxa_html_data_emit_t emit, void *user,
    lxa_html_after_body_action_t *action)
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
        status = lxa_budget_charge_impl(context->stack->budget, LXA_BUDGET_WORK,
            token->data.length, &context->limit_reason);
    lxa_html_after_body_action_t out;
    if (status == LXA_OK) status = lxa_html_tree_after_body_action(token, &out);
    if (status == LXA_OK && out == LXA_HTML_AFTER_BODY_SPACE_TEXT
        && (formatting == NULL || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) == 0))
        return LXA_ERROR_ARGUMENT;
    root_after_body_view_t view = {{context, formatting, mode}, emit, user};
    if (status == LXA_OK) status = root_execute_after_body(&view, token, out);
    if (status == LXA_OK) *action = out;
    return finish(context, status);
}

static lxa_status_t
hidden_table_destination(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, size_t keep)
{
    (void) token;
    if (keep == 0 || keep > lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t))) {
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
    lxa_status_t status = lxa_budget_check_item_impl(context->stack->budget,
        LXA_BUDGET_NAME_BYTES, 8, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_charge_impl(context->stack->budget, LXA_BUDGET_WORK, 1, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check_impl(context->stack->budget, LXA_BUDGET_TREE_DEPTH, 3, &context->limit_reason);
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
    if (mode == NULL || context->body || lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t)) != 0
        || !body_start_mode_valid(context, *mode) || *mode > LXA_TREE_IN_HEAD
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "noscript", 8)) return LXA_ERROR_ARGUMENT;
    root_head_noscript_view_t view = {context, mode};
    return finish(context, root_start_head_noscript(&view, token));
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_root_mode_t *mode;
    lxa_html_root_prebody_emit_t emit;
    void *user;
} root_head_noscript_execute_view_t;

#define LXA_HEAD_NOSCRIPT_EXEC_NAME context_execute_head_noscript
#define LXA_HEAD_NOSCRIPT_EXEC_VIEW root_head_noscript_execute_view_t
#define LXA_HEAD_NOSCRIPT_EXEC_PLAN(view, token, out) \
    lxa_html_tree_head_noscript_plan(token, (view)->context->stack->budget, &(view)->context->limit_reason, out)
#define LXA_HEAD_NOSCRIPT_EXEC_EMIT(view, bytes) ((view)->emit == NULL ? LXA_OK \
    : (view)->emit((view)->user, bytes, LXA_HTML_ROOT_MISC_HEAD_NOSCRIPT))
#define LXA_HEAD_NOSCRIPT_EXEC_MODE(view) (*(view)->mode)
#define LXA_HEAD_NOSCRIPT_EXEC_CLEAR(view) ((void) (view))
#include "html/head_noscript_execute.inc"
#undef LXA_HEAD_NOSCRIPT_EXEC_NAME
#undef LXA_HEAD_NOSCRIPT_EXEC_VIEW
#undef LXA_HEAD_NOSCRIPT_EXEC_PLAN
#undef LXA_HEAD_NOSCRIPT_EXEC_EMIT
#undef LXA_HEAD_NOSCRIPT_EXEC_MODE
#undef LXA_HEAD_NOSCRIPT_EXEC_CLEAR

lxa_status_t
lxa_html_root_context_head_noscript_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t *mode, const lxa_html_token_t *token,
    lxa_html_root_prebody_emit_t emit, void *user, lxa_html_head_noscript_plan_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || *mode != LXA_TREE_HEAD_NOSCRIPT || result == NULL || !context->head || context->body
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) != 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF)
        return LXA_ERROR_ARGUMENT;
    root_head_noscript_execute_view_t view = {context, mode, emit, user};
    status = context_execute_head_noscript(&view, token, result);
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
    size_t length = lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t));
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
        lxa_vector_length_sized(&stack->elements, sizeof(lxa_html_control_element_t)));
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    status = lxa_budget_check_impl(&after, LXA_BUDGET_OPEN_ELEMENTS, 1, &context->limit_reason);
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
    lxa_status_t status = lxa_budget_check_item_impl(context->stack->budget,
        LXA_BUDGET_NAME_BYTES, 8, &context->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_charge_impl(context->stack->budget, LXA_BUDGET_WORK, 1, &context->limit_reason);
    if (status == LXA_OK)
        status = check_body_child_depth(context,
            *view->mode == LXA_TREE_IN_FRAMESET
                ? lxa_vector_length_sized(&context->stack->elements,
                    sizeof(lxa_html_control_element_t)) : 0, NULL);
    if (status == LXA_OK && view->replace_body) {
        lxa_html_control_stack_t *stack = context->stack;
        if (stack->next_identity == 0) return LXA_ERROR_OVERFLOW;
        size_t work;
        status = lxa_size_add(lxa_vector_length_sized(&stack->elements,
            sizeof(lxa_html_control_element_t)), 1, &work);
        if (status == LXA_OK)
            status = lxa_budget_check_impl(stack->budget, LXA_BUDGET_WORK, work, &context->limit_reason);
        if (status == LXA_OK && stack->charged_name_bytes < 8)
            status = lxa_budget_check_impl(stack->budget, LXA_BUDGET_STRINGS,
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
            || lxa_vector_length_sized(&context->stack->elements,
                sizeof(lxa_html_control_element_t)) != 0))
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

typedef struct {
    lxa_html_root_context_t *context;
    const lxa_html_root_token_borrowers_t *borrowers;
    lxa_html_root_mode_t *mode;
    bool *pending_newline;
    lxa_html_root_special_start_result_t *result;
} root_special_execute_view_t;

static lxa_status_t
context_special_text(root_special_execute_view_t *view, const lxa_html_token_t *token)
{
    bool newline = false;
    lxa_status_t status = lxa_html_root_context_start_text(view->context, view->borrowers->text,
        view->borrowers->tokenizer, token, view->mode, &newline);
    if (status == LXA_OK && newline) *view->pending_newline = true;
    return status;
}

#define LXA_ROOT_SPECIAL_EXEC_NAME context_execute_special_start
#define LXA_ROOT_SPECIAL_EXEC_VIEW root_special_execute_view_t
#define LXA_ROOT_SPECIAL_EXEC_BODY(view) context_ensure_body((view)->context)
#define LXA_ROOT_SPECIAL_EXEC_HEAD(view) context_ensure_head((view)->context)
#define LXA_ROOT_SPECIAL_EXEC_MODE(view) (*(view)->mode)
#define LXA_ROOT_SPECIAL_EXEC_TEMPLATE(view, token) ((void) (view), (void) (token), LXA_OK)
#define LXA_ROOT_SPECIAL_EXEC_NOSCRIPT(view, token) \
    lxa_html_root_context_start_head_noscript((view)->context, token, (view)->mode)
#define LXA_ROOT_SPECIAL_EXEC_FORM(view, token) lxa_html_root_context_start_form((view)->context, \
    token, (view)->borrowers->form_owner, (view)->mode, &(view)->result->form_action)
#define LXA_ROOT_SPECIAL_EXEC_TEXT context_special_text
#define LXA_ROOT_SPECIAL_EXEC_XMP(view, token) lxa_html_root_context_start_xmp((view)->context, \
    (view)->borrowers->formatting, (view)->borrowers->text, (view)->borrowers->tokenizer, token, (view)->mode)
#define LXA_ROOT_SPECIAL_EXEC_PLAINTEXT(view, token) lxa_html_root_context_start_plaintext((view)->context, \
    (view)->borrowers->tokenizer, token, (view)->mode, &(view)->result->identity)
#include "html/root_special_start_execute.inc"
#undef LXA_ROOT_SPECIAL_EXEC_NAME
#undef LXA_ROOT_SPECIAL_EXEC_VIEW
#undef LXA_ROOT_SPECIAL_EXEC_BODY
#undef LXA_ROOT_SPECIAL_EXEC_HEAD
#undef LXA_ROOT_SPECIAL_EXEC_MODE
#undef LXA_ROOT_SPECIAL_EXEC_TEMPLATE
#undef LXA_ROOT_SPECIAL_EXEC_NOSCRIPT
#undef LXA_ROOT_SPECIAL_EXEC_FORM
#undef LXA_ROOT_SPECIAL_EXEC_TEXT
#undef LXA_ROOT_SPECIAL_EXEC_XMP
#undef LXA_ROOT_SPECIAL_EXEC_PLAINTEXT

lxa_status_t
lxa_html_root_context_special_start_token(lxa_html_root_context_t *context,
    const lxa_html_root_token_borrowers_t *borrowers, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, bool *pending_newline,
    lxa_html_root_special_start_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || pending_newline == NULL || result == NULL
        || (!body_start_mode_valid(context, *mode)
            && !(*mode == LXA_TREE_HEAD_NOSCRIPT && context->head))
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG)
        return LXA_ERROR_ARGUMENT;
    const lxa_html_root_token_borrowers_t empty = {0};
    if (borrowers == NULL) borrowers = &empty;
    lxa_html_formatting_list_t *formatting = borrowers->formatting;
    if (formatting != NULL && (formatting->store == NULL
        || formatting->store->budget != context->stack->budget)) return LXA_ERROR_ARGUMENT;
    lxa_html_root_special_start_action_t action;
    status = lxa_html_tree_root_special_start_action(*mode, token->name, &action);
    if (status != LXA_OK) return status;
    if (action == LXA_HTML_ROOT_SPECIAL_ORDINARY) return LXA_ERROR_ARGUMENT;
    if (*mode == LXA_TREE_HEAD_NOSCRIPT) {
        lxa_html_head_noscript_plan_t plan;
        status = lxa_html_tree_head_noscript_plan(token, context->stack->budget,
            &context->limit_reason, &plan);
        if (status != LXA_OK) return status;
        if (plan.action != LXA_HTML_HEAD_NOSCRIPT_CONTINUE) return LXA_ERROR_ARGUMENT;
    }
    if ((action == LXA_HTML_ROOT_SPECIAL_TEXT || action == LXA_HTML_ROOT_SPECIAL_XMP)
        && !text_start_owners_valid(borrowers->text, borrowers->tokenizer))
        return LXA_ERROR_ARGUMENT;
    if ((action == LXA_HTML_ROOT_SPECIAL_XMP && formatting == NULL)
        || (action == LXA_HTML_ROOT_SPECIAL_PLAINTEXT && borrowers->tokenizer == NULL)
        || (action == LXA_HTML_ROOT_SPECIAL_FORM && borrowers->form_owner == NULL))
        return LXA_ERROR_ARGUMENT;
    if ((action == LXA_HTML_ROOT_SPECIAL_TEMPLATE_HEAD || action == LXA_HTML_ROOT_SPECIAL_TEMPLATE)
        && (*mode == LXA_TREE_IN_BODY ? !context->body
            : context->body || lxa_vector_length_sized(&context->stack->elements,
                sizeof(lxa_html_control_element_t)) != 0))
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_special_start_result_t out = {action, LXA_HTML_ROOT_FORM_IGNORE, 0};
    root_special_execute_view_t view = {context, borrowers, mode, pending_newline, &out};
    status = context_execute_special_start(&view, token, action);
    if (status == LXA_OK) *result = out;
    if (status == LXA_ERROR_ARGUMENT && context->failure == LXA_OK) return status;
    return finish(context, status);
}

#define LXA_ROOT_START_PRELUDE_NAME context_start_prelude
#define LXA_ROOT_START_PRELUDE_VIEW root_frameset_control_view_t
#define LXA_ROOT_START_PRELUDE_MODE(view) (*(view)->mode)
#define LXA_ROOT_START_PRELUDE_GROUP(view) recover_root_group((view)->context)
#define LXA_ROOT_START_PRELUDE_ELIGIBLE(view, out) \
    lxa_html_root_context_frameset_body((view)->context, *(view)->mode, out)
#define LXA_ROOT_START_PRELUDE_FRAMESET root_start_frameset
#define LXA_ROOT_START_PRELUDE_ROOT(view, action, token) \
    context_dispatch_root_start((view)->context, action, token, (view)->mode)
#include "html/root_start_prelude.inc"
#undef LXA_ROOT_START_PRELUDE_NAME
#undef LXA_ROOT_START_PRELUDE_VIEW
#undef LXA_ROOT_START_PRELUDE_MODE
#undef LXA_ROOT_START_PRELUDE_GROUP
#undef LXA_ROOT_START_PRELUDE_ELIGIBLE
#undef LXA_ROOT_START_PRELUDE_FRAMESET
#undef LXA_ROOT_START_PRELUDE_ROOT

lxa_status_t
lxa_html_root_context_start_prelude(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode,
    lxa_html_root_start_prelude_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || result == NULL || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG) return LXA_ERROR_ARGUMENT;
    bool noscript = *mode == LXA_TREE_HEAD_NOSCRIPT && context->head && !context->body;
    bool after_body = (*mode == LXA_TREE_AFTER_BODY || *mode == LXA_TREE_AFTER_HTML)
        && context->body && lxa_html_tree_name_equals(token->name, "html", 4);
    if ((!body_start_mode_valid(context, *mode) && !noscript && !after_body)
        || (*mode == LXA_TREE_IN_BODY && !context->body)
        || (!context->body && lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) != 0))
        return LXA_ERROR_ARGUMENT;
    if (noscript) {
        lxa_html_head_noscript_plan_t plan;
        status = lxa_html_tree_head_noscript_plan(token, context->stack->budget,
            &context->limit_reason, &plan);
        if (status != LXA_OK) return status;
        if (plan.action != LXA_HTML_HEAD_NOSCRIPT_CONTINUE) return LXA_ERROR_ARGUMENT;
    }
    lxa_html_root_start_prelude_result_t out = {LXA_HTML_ROOT_PRELUDE_CONTINUE, 0};
    root_frameset_control_view_t view = {context, mode, &out.identity, true};
    status = context_start_prelude(&view, token, &out.action);
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

typedef struct {
    root_start_view_t recovery;
    lxa_html_root_table_text_result_t *result;
    lxa_html_table_text_emit_t emit;
    void *user;
} root_table_text_view_t;

static lxa_status_t
context_table_text_emit(root_table_text_view_t *view, lxa_span_t bytes, bool foster)
{
    if (bytes.length == 0 || view->emit == NULL) return LXA_OK;
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_current(view->recovery.context->stack, 0, &entry, NULL);
    return status == LXA_OK ? view->emit(view->user, bytes, entry.identity, foster) : status;
}

static lxa_status_t
context_table_text_group(root_table_text_view_t *view, bool *group)
{
    lxa_html_control_stack_t *stack = view->recovery.context->stack;
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_get(stack,
        lxa_vector_length_sized(&stack->elements,
            sizeof(lxa_html_control_element_t)) - 1, &entry, NULL);
    if (status == LXA_OK) {
        *group = entry.namespace_id == LXA_DOM_NS_HTML
            && lxa_html_tree_name_equals(entry.name, "colgroup", 8);
        if (*group) view->result->column_group = entry.identity;
    }
    return status;
}

static lxa_status_t
context_table_text_append(root_table_text_view_t *view, lxa_span_t bytes, bool prefix)
{
    if (prefix) view->result->space_prefix = bytes.length;
    return context_table_text_emit(view, bytes, false);
}

static lxa_status_t
context_table_text_foster(root_table_text_view_t *view, lxa_span_t bytes)
{
    view->result->foster = true;
    return context_table_text_emit(view, bytes, true);
}

#define LXA_TABLE_TEXT_FLUSH_NAME context_flush_table_text
#define LXA_TABLE_TEXT_FLUSH_VIEW root_table_text_view_t
#define LXA_TABLE_TEXT_FLUSH_BUDGET(view) ((view)->recovery.context->stack->budget)
#define LXA_TABLE_TEXT_FLUSH_REASON(view) (&(view)->recovery.context->limit_reason)
#define LXA_TABLE_TEXT_FLUSH_APPEND context_table_text_append
#define LXA_TABLE_TEXT_FLUSH_GROUP context_table_text_group
#define LXA_TABLE_TEXT_FLUSH_PREFLIGHT(view) LXA_OK
#define LXA_TABLE_TEXT_FLUSH_POP(view) lxa_html_control_stack_pop_to((view)->recovery.context->stack, \
    lxa_vector_length_sized(&(view)->recovery.context->stack->elements, \
        sizeof(lxa_html_control_element_t)) - 1)
#define LXA_TABLE_TEXT_FLUSH_RECONSTRUCT(view) \
    lxa_html_root_context_reconstruct((view)->recovery.context, (view)->recovery.formatting)
#define LXA_TABLE_TEXT_FLUSH_FOSTER context_table_text_foster
#include "html/table_text_flush.inc"
#undef LXA_TABLE_TEXT_FLUSH_NAME
#undef LXA_TABLE_TEXT_FLUSH_VIEW
#undef LXA_TABLE_TEXT_FLUSH_BUDGET
#undef LXA_TABLE_TEXT_FLUSH_REASON
#undef LXA_TABLE_TEXT_FLUSH_APPEND
#undef LXA_TABLE_TEXT_FLUSH_GROUP
#undef LXA_TABLE_TEXT_FLUSH_PREFLIGHT
#undef LXA_TABLE_TEXT_FLUSH_POP
#undef LXA_TABLE_TEXT_FLUSH_RECONSTRUCT
#undef LXA_TABLE_TEXT_FLUSH_FOSTER

lxa_status_t
lxa_html_root_context_table_text(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_html_root_mode_t mode,
    lxa_span_t data, lxa_html_table_text_emit_t emit, void *user,
    lxa_html_root_table_text_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_IN_BODY || !context->body || result == NULL
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) < 2
        || formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget
        || (data.length != 0 && data.data == NULL)) return LXA_ERROR_ARGUMENT;
    lxa_dom_namespace_t ns;
    status = lxa_html_control_stack_current_namespace(context->stack, 0, &ns);
    if (status != LXA_OK) return finish(context, status);
    if (ns != LXA_DOM_NS_HTML) return LXA_ERROR_ARGUMENT;
    lxa_html_root_table_text_result_t out = {0};
    root_table_text_view_t view = {{context, formatting}, &out, emit, user};
    status = context_flush_table_text(&view, data);
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

typedef struct {
    root_special_execute_view_t special;
    lxa_html_document_mode_t document_mode;
    lxa_html_root_start_result_t *result;
} root_start_execute_view_t;

static lxa_status_t
context_root_start_prelude(root_start_execute_view_t *view, const lxa_html_token_t *token,
    lxa_html_root_start_prelude_action_t *action)
{
    lxa_html_root_start_prelude_result_t out;
    lxa_status_t status = lxa_html_root_context_start_prelude(view->special.context,
        token, view->special.mode, &out);
    if (status == LXA_OK) {
        *action = out.action;
        view->result->identity = out.identity;
    }
    return status;
}

static lxa_status_t
context_root_start_ignore(root_start_execute_view_t *view)
{
    view->result->action = LXA_HTML_ROOT_START_RESULT_IGNORE;
    return LXA_OK;
}

static lxa_status_t
context_root_start_table_query(root_start_execute_view_t *view, bool *table, bool *group)
{
    *table = *group = false;
    lxa_html_control_stack_t *stack = view->special.context->stack;
    size_t length = lxa_vector_length_sized(&stack->elements, sizeof(lxa_html_control_element_t));
    if (length <= 1) return LXA_OK;
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_get(stack, length - 1, &entry, NULL);
    if (status == LXA_OK) {
        *table = entry.namespace_id == LXA_DOM_NS_HTML && lxa_html_tree_table_context(entry.name);
        *group = entry.namespace_id == LXA_DOM_NS_HTML
            && lxa_html_tree_name_equals(entry.name, "colgroup", 8);
    }
    return status;
}

static lxa_status_t
context_root_start_table(root_start_execute_view_t *view, const lxa_html_token_t *token, bool group)
{
    const lxa_html_root_token_borrowers_t *borrowers = view->special.borrowers;
    bool template_start;
    lxa_status_t status = context_table_start_prepared(view->special.context, borrowers->formatting,
        borrowers->text, borrowers->tokenizer, token, view->special.mode, &view->result->identity,
        view->special.pending_newline, &template_start, group);
    if (status == LXA_OK && template_start) view->result->action = LXA_HTML_ROOT_START_RESULT_TEMPLATE;
    return status;
}

static lxa_status_t
context_root_start_special(root_start_execute_view_t *view, const lxa_html_token_t *token,
    lxa_html_root_special_start_action_t action)
{
    lxa_status_t status = context_execute_special_start(&view->special, token, action);
    if (status != LXA_OK) return status;
    view->result->identity = view->special.result->identity;
    view->result->form_action = view->special.result->form_action;
    if (action == LXA_HTML_ROOT_SPECIAL_TEMPLATE_HEAD || action == LXA_HTML_ROOT_SPECIAL_TEMPLATE)
        view->result->action = LXA_HTML_ROOT_START_RESULT_TEMPLATE;
    else if (action == LXA_HTML_ROOT_SPECIAL_FORM) {
        if (view->result->form_action == LXA_HTML_ROOT_FORM_IGNORE)
            view->result->action = LXA_HTML_ROOT_START_RESULT_IGNORE;
        else view->result->identity = *view->special.borrowers->form_owner;
    }
    return LXA_OK;
}

#define LXA_ROOT_START_EXEC_NAME context_execute_root_start
#define LXA_ROOT_START_EXEC_VIEW root_start_execute_view_t
#define LXA_ROOT_START_EXEC_PRELUDE context_root_start_prelude
#define LXA_ROOT_START_EXEC_IGNORE context_root_start_ignore
#define LXA_ROOT_START_EXEC_MODE(view) (*(view)->special.mode)
#define LXA_ROOT_START_EXEC_HIDDEN(view, token, out) lxa_html_root_context_hidden_table_input( \
    (view)->special.context, *(view)->special.mode, token, out)
#define LXA_ROOT_START_EXEC_SELECT(view, token, out) lxa_html_root_context_select_token( \
    (view)->special.context, *(view)->special.mode, (token)->kind, (token)->name, out)
#define LXA_ROOT_START_EXEC_TABLE_QUERY context_root_start_table_query
#define LXA_ROOT_START_EXEC_TABLE context_root_start_table
#define LXA_ROOT_START_EXEC_SPECIAL context_root_start_special
#define LXA_ROOT_START_EXEC_ORDINARY(view, token) lxa_html_root_context_ordinary_start_token( \
    (view)->special.context, (view)->special.borrowers->formatting, token, (view)->document_mode, \
    (view)->special.mode, &(view)->result->identity, (view)->special.pending_newline)
#include "html/root_start_execute.inc"
#undef LXA_ROOT_START_EXEC_NAME
#undef LXA_ROOT_START_EXEC_VIEW
#undef LXA_ROOT_START_EXEC_PRELUDE
#undef LXA_ROOT_START_EXEC_IGNORE
#undef LXA_ROOT_START_EXEC_MODE
#undef LXA_ROOT_START_EXEC_HIDDEN
#undef LXA_ROOT_START_EXEC_SELECT
#undef LXA_ROOT_START_EXEC_TABLE_QUERY
#undef LXA_ROOT_START_EXEC_TABLE
#undef LXA_ROOT_START_EXEC_SPECIAL
#undef LXA_ROOT_START_EXEC_ORDINARY

lxa_status_t
lxa_html_root_context_start_token(lxa_html_root_context_t *context,
    const lxa_html_root_token_borrowers_t *borrowers, const lxa_html_token_t *token,
    lxa_html_document_mode_t document_mode, lxa_html_root_mode_t *mode,
    bool *pending_newline, lxa_html_root_start_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || pending_newline == NULL || result == NULL
        || (unsigned) document_mode > LXA_HTML_QUIRKS
        || !root_token_borrowers_valid(borrowers, context->stack->budget)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG)
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_start_result_t out = {LXA_HTML_ROOT_START_RESULT_ACCEPT, LXA_HTML_ROOT_FORM_IGNORE, 0};
    lxa_html_root_special_start_result_t special = {LXA_HTML_ROOT_SPECIAL_ORDINARY, LXA_HTML_ROOT_FORM_IGNORE, 0};
    root_start_execute_view_t view = {{context, borrowers, mode, pending_newline, &special}, document_mode, &out};
    status = context_execute_root_start(&view, token);
    if (status == LXA_OK) *result = out;
    if (status == LXA_ERROR_ARGUMENT && context->failure == LXA_OK) return status;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    const lxa_html_root_token_borrowers_t *borrowers;
    lxa_html_document_mode_t document_mode;
    lxa_html_root_mode_t *mode;
    bool *pending_newline;
    lxa_html_root_end_result_t *result;
} root_end_execute_view_t;

static lxa_status_t
context_root_end_body(root_end_execute_view_t *view, const lxa_html_token_t *token)
{
    root_body_end_view_t body = {
        view->context, view->borrowers->formatting, view->borrowers->form_owner, false
    };
    lxa_status_t status = context_body_end_token(&body, token->name,
        lxa_html_tree_root_body_end_plan(token->name), view->mode);
    if (status == LXA_OK) view->result->implied_paragraph = body.implied_paragraph;
    return status;
}

static lxa_status_t
context_root_end_start(root_end_execute_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_root_start_result_t result;
    return lxa_html_root_context_start_token(view->context, view->borrowers, token,
        view->document_mode, view->mode, view->pending_newline, &result);
}

#define LXA_ROOT_END_EXEC_NAME context_execute_root_end
#define LXA_ROOT_END_EXEC_VIEW root_end_execute_view_t
#define LXA_ROOT_END_EXEC_MODE(view) (*(view)->mode)
#define LXA_ROOT_END_EXEC_PREBODY(view, action) context_prebody_end((view)->context, action, (view)->mode)
#define LXA_ROOT_END_EXEC_PRELUDE(view, token, out) \
    context_end_prelude((view)->context, *(view)->mode, (token)->name, out)
#define LXA_ROOT_END_EXEC_SELECT(view, token, out) lxa_html_root_context_select_token( \
    (view)->context, *(view)->mode, (token)->kind, (token)->name, out)
#define LXA_ROOT_END_EXEC_HEAD(view, action) context_head_end((view)->context, action, (view)->mode)
#define LXA_ROOT_END_EXEC_BODY context_root_end_body
#define LXA_ROOT_END_EXEC_START context_root_end_start
#define LXA_ROOT_END_EXEC_IGNORE(view) ((view)->result->action = LXA_HTML_ROOT_END_RESULT_IGNORE)
#define LXA_ROOT_END_EXEC_BR(view) ((view)->result->action = LXA_HTML_ROOT_END_RESULT_BR)
#include "html/root_end_execute.inc"
#undef LXA_ROOT_END_EXEC_NAME
#undef LXA_ROOT_END_EXEC_VIEW
#undef LXA_ROOT_END_EXEC_MODE
#undef LXA_ROOT_END_EXEC_PREBODY
#undef LXA_ROOT_END_EXEC_PRELUDE
#undef LXA_ROOT_END_EXEC_SELECT
#undef LXA_ROOT_END_EXEC_HEAD
#undef LXA_ROOT_END_EXEC_BODY
#undef LXA_ROOT_END_EXEC_START
#undef LXA_ROOT_END_EXEC_IGNORE
#undef LXA_ROOT_END_EXEC_BR

lxa_status_t
lxa_html_root_context_end_token(lxa_html_root_context_t *context,
    const lxa_html_root_token_borrowers_t *borrowers, const lxa_html_token_t *token,
    lxa_html_document_mode_t document_mode, lxa_html_root_mode_t *mode,
    bool *pending_newline, lxa_html_root_end_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode == NULL || pending_newline == NULL || result == NULL
        || (unsigned) document_mode > LXA_HTML_QUIRKS
        || !root_token_borrowers_valid(borrowers, context->stack->budget)
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_END_TAG
        || !body_start_mode_valid(context, *mode)
        || (*mode == LXA_TREE_IN_BODY ? !context->body
            : context->body || lxa_vector_length_sized(&context->stack->elements,
                sizeof(lxa_html_control_element_t)) != 0))
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_end_result_t out = {LXA_HTML_ROOT_END_RESULT_ACCEPT, false};
    root_end_execute_view_t view = {context, borrowers, document_mode, mode, pending_newline, &out};
    status = context_execute_root_end(&view, token);
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_root_misc_destination_t *destination;
} root_misc_view_t;

static lxa_status_t
context_root_misc_insert(root_misc_view_t *view, const lxa_html_token_t *token,
    lxa_html_root_misc_destination_t destination)
{
    (void) token;
    *view->destination = destination;
    return LXA_OK;
}

#define LXA_ROOT_MISC_NAME context_execute_root_misc
#define LXA_ROOT_MISC_VIEW root_misc_view_t
#define LXA_ROOT_MISC_HTML(view) context_ensure_html((view)->context)
#define LXA_ROOT_MISC_INSERT context_root_misc_insert
#include "html/root_misc_execute.inc"
#undef LXA_ROOT_MISC_NAME
#undef LXA_ROOT_MISC_VIEW
#undef LXA_ROOT_MISC_HTML
#undef LXA_ROOT_MISC_INSERT

lxa_status_t
lxa_html_root_context_misc_token(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t mode,
    lxa_html_root_misc_destination_t *destination)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    bool noscript = mode == LXA_TREE_HEAD_NOSCRIPT && context->head && !context->body;
    bool after_body = (mode == LXA_TREE_AFTER_BODY || mode == LXA_TREE_AFTER_HTML) && context->body;
    if (destination == NULL || !lxa_html_tree_token_shape_valid(token)
        || (!body_start_mode_valid(context, mode) && !noscript && !after_body)
        || (mode == LXA_TREE_IN_BODY && !context->body)
        || (!context->body && lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) != 0))
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_misc_destination_t out;
    root_misc_view_t view = {context, &out};
    status = context_execute_root_misc(&view, token, mode);
    if (status == LXA_OK) *destination = out;
    if (status == LXA_ERROR_ARGUMENT) return status;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    uint64_t column_group;
    lxa_buffer_t *pending;
    lxa_html_data_emit_t emit;
    void *user;
} root_data_null_view_t;

static lxa_status_t
context_data_null_group(root_data_null_view_t *view, bool *group)
{
    lxa_status_t status = lxa_html_control_stack_column_group(view->context->stack, 0, group);
    if (status == LXA_OK && *group) {
        const lxa_html_control_element_t *entries =
            (const lxa_html_control_element_t *) view->context->stack->elements.bytes.data;
        view->column_group = entries[lxa_vector_length_sized(&view->context->stack->elements,
            sizeof(lxa_html_control_element_t)) - 1].identity;
    }
    return status;
}

static lxa_status_t
context_data_null_pop(root_data_null_view_t *view)
{
    return lxa_html_control_stack_pop_to(view->context->stack,
        lxa_vector_length_sized(&view->context->stack->elements,
            sizeof(lxa_html_control_element_t)) - 1);
}

static lxa_status_t
context_data_null_flush(root_data_null_view_t *view, lxa_span_t pending)
{
    lxa_status_t status = lxa_budget_charge_impl(view->context->stack->budget,
        LXA_BUDGET_WORK, pending.length, &view->context->limit_reason);
    if (status == LXA_OK && pending.length != 0 && view->emit != NULL)
        status = view->emit(view->user, pending, view->column_group);
    if (status == LXA_OK && view->pending != NULL) lxa_buffer_clear(view->pending);
    return status;
}

#define LXA_TABLE_DATA_NULL_NAME context_table_data_null
#define LXA_TABLE_DATA_NULL_VIEW root_data_null_view_t
#define LXA_TABLE_DATA_NULL_GROUP context_data_null_group
#define LXA_TABLE_DATA_NULL_FLUSH context_data_null_flush
#define LXA_TABLE_DATA_NULL_POP context_data_null_pop
#include "html/table_data_null.inc"
#undef LXA_TABLE_DATA_NULL_NAME
#undef LXA_TABLE_DATA_NULL_VIEW
#undef LXA_TABLE_DATA_NULL_GROUP
#undef LXA_TABLE_DATA_NULL_FLUSH
#undef LXA_TABLE_DATA_NULL_POP

lxa_status_t
lxa_html_root_context_table_data_null(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_span_t pending, uint64_t *column_group)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_IN_BODY || !context->body || column_group == NULL
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) < 2
        || (pending.length != 0 && pending.data == NULL)) return LXA_ERROR_ARGUMENT;
    lxa_dom_namespace_t ns;
    status = lxa_html_control_stack_current_namespace(context->stack, 0, &ns);
    if (status != LXA_OK) return finish(context, status);
    if (ns != LXA_DOM_NS_HTML) return LXA_ERROR_ARGUMENT;
    root_data_null_view_t view = {.context = context};
    size_t length = lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t));
    status = context_table_data_null(&view, pending);
    if (status == LXA_OK)
        *column_group = lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) < length ? view.column_group : 0;
    return finish(context, status);
}

static lxa_status_t
context_table_data_append(root_data_null_view_t *view, lxa_span_t bytes, bool table)
{
    if (!table) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return lxa_html_tree_queue_table_text(view->pending, bytes, &view->context->limit_reason);
}

#define LXA_DATA_EXECUTE_NAME context_execute_table_data
#define LXA_DATA_EXECUTE_VIEW root_data_null_view_t
#define LXA_DATA_EXECUTE_BUDGET(view) ((view)->context->stack->budget)
#define LXA_DATA_EXECUTE_REASON(view) (&(view)->context->limit_reason)
#define LXA_DATA_EXECUTE_APPEND context_table_data_append
#define LXA_DATA_EXECUTE_NULL(view) context_table_data_null(view, \
    ((lxa_span_t){(view)->pending->data, (view)->pending->length}))
#include "html/data_execute.inc"
#undef LXA_DATA_EXECUTE_NAME
#undef LXA_DATA_EXECUTE_VIEW
#undef LXA_DATA_EXECUTE_BUDGET
#undef LXA_DATA_EXECUTE_REASON
#undef LXA_DATA_EXECUTE_APPEND
#undef LXA_DATA_EXECUTE_NULL

lxa_status_t
lxa_html_root_context_table_data(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_span_t data, lxa_buffer_t *pending,
    lxa_html_data_emit_t emit, void *user)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (mode != LXA_TREE_IN_BODY || !context->body
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) < 2
        || !lxa_html_tree_table_text_input_valid(pending, data)
        || pending->budget != context->stack->budget) return LXA_ERROR_ARGUMENT;
    lxa_dom_namespace_t ns;
    status = lxa_html_control_stack_current_namespace(context->stack, 0, &ns);
    if (status != LXA_OK) return finish(context, status);
    if (ns != LXA_DOM_NS_HTML) return LXA_ERROR_ARGUMENT;
    root_data_null_view_t view = {.context = context, .pending = pending, .emit = emit, .user = user};
    return finish(context, context_execute_table_data(&view, data, false, true));
}

static lxa_status_t
context_emit_data(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_span_t data,
    lxa_html_data_emit_t emit, void *user, bool resolve_foreign)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || (mode != LXA_TREE_IN_BODY && mode != LXA_TREE_AFTER_BODY
            && mode != LXA_TREE_AFTER_HTML)
        || (data.length != 0 && data.data == NULL)) return LXA_ERROR_ARGUMENT;
    lxa_html_scope_entry_t entry;
    lxa_html_foreign_context_t integration;
    status = lxa_html_control_stack_current(context->stack, 0, &entry, &integration);
    if (status != LXA_OK) return finish(context, status);
    if (entry.namespace_id == LXA_DOM_NS_HTML && lxa_html_tree_table_context(entry.name))
        return LXA_ERROR_ARGUMENT;
    bool foreign = resolve_foreign && entry.namespace_id != LXA_DOM_NS_HTML
        && !lxa_html_foreign_integration(integration);
    uint64_t parent = entry.identity == 0 ? context->body_identity : entry.identity;
    if (parent == 0) {
        assert(false);
        return finish(context, LXA_ERROR_INTERNAL);
    }
    if (foreign)
        context->frameset_ok = lxa_html_tree_foreign_data_frameset(context->frameset_ok, data);
    const lxa_html_data_emitter_t emitter = {
        context->stack->budget, &context->limit_reason, parent, emit, user
    };
    return finish(context, lxa_html_tree_emit_data(&emitter, data, foreign));
}

lxa_status_t
lxa_html_root_context_emit_data(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_span_t data,
    lxa_html_data_emit_t emit, void *user)
{
    return context_emit_data(context, mode, data, emit, user, true);
}

lxa_status_t
lxa_html_root_context_foreign_route(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token,
    lxa_html_root_foreign_route_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    bool body = mode == LXA_TREE_IN_BODY || mode == LXA_TREE_AFTER_BODY || mode == LXA_TREE_AFTER_HTML;
    size_t length = lxa_vector_length_sized(&context->stack->elements,
        sizeof(lxa_html_control_element_t));
    bool valid = body ? context->body : mode == LXA_TREE_INITIAL ? !context->html
        : mode == LXA_TREE_HEAD_NOSCRIPT ? context->head && !context->body
        : body_start_mode_valid(context, mode) && !context->body;
    if (!valid || (!body && length != 0) || result == NULL
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF)
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_foreign_route_t out = {LXA_HTML_FOREIGN_ROOT_HTML, LXA_DOM_NS_HTML};
    if (body && length > 1) {
        lxa_html_scope_entry_t entry;
        lxa_html_foreign_context_t integration;
        status = lxa_html_control_stack_current(context->stack, 0, &entry, &integration);
        if (status == LXA_OK) {
            out.namespace_id = entry.namespace_id;
            status = lxa_html_foreign_root_plan(entry.namespace_id, integration, token,
                context->stack->budget, &context->limit_reason, &out.action);
        }
    }
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_dom_namespace_t namespace_id;
    lxa_html_data_emit_t emit;
    void *user;
    lxa_html_root_foreign_result_t *result;
} root_foreign_execute_view_t;

#define LXA_ROOT_BREAKOUT_NAME context_execute_foreign_breakout
#define LXA_ROOT_BREAKOUT_VIEW lxa_html_root_context_t
#define LXA_ROOT_BREAKOUT_PLAN(view, out) \
    lxa_html_control_stack_foreign_breakout_plan((view)->stack, 0, out)
#define LXA_ROOT_BREAKOUT_PARAGRAPH(view, keep, name, out) \
    lxa_html_control_stack_scope_prefix_find((view)->stack, keep, name, LXA_HTML_SCOPE_BUTTON, out)
#define LXA_ROOT_BREAKOUT_NODE(view) LXA_OK
#define LXA_ROOT_BREAKOUT_POP(view, keep) lxa_html_control_stack_pop_to((view)->stack, keep)
#include "html/root_foreign_breakout_execute.inc"
#undef LXA_ROOT_BREAKOUT_NAME
#undef LXA_ROOT_BREAKOUT_VIEW
#undef LXA_ROOT_BREAKOUT_PLAN
#undef LXA_ROOT_BREAKOUT_PARAGRAPH
#undef LXA_ROOT_BREAKOUT_NODE
#undef LXA_ROOT_BREAKOUT_POP

lxa_status_t
lxa_html_root_context_foreign_breakout(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || (mode != LXA_TREE_IN_BODY && mode != LXA_TREE_AFTER_BODY && mode != LXA_TREE_AFTER_HTML)
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) <= 1
        || !lxa_html_tree_token_shape_valid(token)
        || (token->kind == LXA_HTML_START_TAG
            ? !lxa_html_foreign_html_start_breakout(token->name)
                && !lxa_html_tree_name_equals(token->name, "font", 4)
            : token->kind != LXA_HTML_END_TAG
                || (!lxa_html_tree_name_equals(token->name, "br", 2)
                    && !lxa_html_tree_name_equals(token->name, "p", 1))))
        return LXA_ERROR_ARGUMENT;
    lxa_html_scope_entry_t entry;
    status = lxa_html_control_stack_current(context->stack, 0, &entry, NULL);
    if (status != LXA_OK) return finish(context, status);
    if (entry.namespace_id == LXA_DOM_NS_HTML) return LXA_ERROR_ARGUMENT;
    return finish(context, context_execute_foreign_breakout(context, token));
}

static lxa_status_t
context_foreign_data(root_foreign_execute_view_t *view, lxa_span_t bytes)
{
    lxa_html_root_context_t *context = view->context;
    context->frameset_ok = lxa_html_tree_foreign_data_frameset(context->frameset_ok, bytes);
    const lxa_html_data_emitter_t emitter = {
        context->stack->budget, &context->limit_reason, view->result->parent, view->emit, view->user
    };
    return lxa_html_tree_emit_data(&emitter, bytes, true);
}

static lxa_status_t
context_foreign_end(root_foreign_execute_view_t *view, lxa_span_t name, size_t *keep, bool *reprocess)
{
    lxa_html_foreign_end_plan_t plan;
    lxa_status_t status = lxa_html_control_stack_foreign_end_plan(view->context->stack, 0, name, &plan);
    if (status == LXA_OK) {
        *keep = plan.keep;
        *reprocess = plan.reprocess;
    }
    return status;
}

static lxa_status_t
context_foreign_start(root_foreign_execute_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_root_context_t *context = view->context;
    lxa_status_t status = reserve_open_slot(context);
    if (status == LXA_OK)
        status = check_body_child_depth(context, lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)), NULL);
    if (status == LXA_OK)
        status = lxa_html_control_stack_push_token(context->stack, token, view->namespace_id,
            &view->result->identity);
    return status;
}

#define LXA_FOREIGN_EXEC_NAME context_execute_foreign
#define LXA_FOREIGN_EXEC_VIEW root_foreign_execute_view_t
#define LXA_FOREIGN_EXEC_MISC(view, token) ((void) (view), (void) (token), LXA_OK)
#define LXA_FOREIGN_EXEC_DATA context_foreign_data
#define LXA_FOREIGN_EXEC_END context_foreign_end
#define LXA_FOREIGN_EXEC_POP(view, keep) lxa_html_control_stack_pop_to((view)->context->stack, keep)
#define LXA_FOREIGN_EXEC_START context_foreign_start
#define LXA_FOREIGN_EXEC_LENGTH(view) lxa_vector_length_sized(&(view)->context->stack->elements, \
    sizeof(lxa_html_control_element_t))
#include "html/foreign_execute.inc"
#undef LXA_FOREIGN_EXEC_NAME
#undef LXA_FOREIGN_EXEC_VIEW
#undef LXA_FOREIGN_EXEC_MISC
#undef LXA_FOREIGN_EXEC_DATA
#undef LXA_FOREIGN_EXEC_END
#undef LXA_FOREIGN_EXEC_POP
#undef LXA_FOREIGN_EXEC_START
#undef LXA_FOREIGN_EXEC_LENGTH

lxa_status_t
lxa_html_root_context_foreign_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token,
    lxa_html_data_emit_t emit, void *user, lxa_html_root_foreign_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (!context->body || (mode != LXA_TREE_IN_BODY && mode != LXA_TREE_AFTER_BODY && mode != LXA_TREE_AFTER_HTML)
        || lxa_vector_length_sized(&context->stack->elements,
            sizeof(lxa_html_control_element_t)) <= 1 || result == NULL
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF)
        return LXA_ERROR_ARGUMENT;
    lxa_html_scope_entry_t entry;
    lxa_html_foreign_context_t integration;
    status = lxa_html_control_stack_current(context->stack, 0, &entry, &integration);
    if (status != LXA_OK) return finish(context, status);
    if (entry.namespace_id == LXA_DOM_NS_HTML || lxa_html_foreign_uses_html(integration, token))
        return LXA_ERROR_ARGUMENT;
    lxa_html_root_foreign_result_t out = {entry.identity, 0, false};
    root_foreign_execute_view_t view = {context, entry.namespace_id, emit, user, &out};
    status = context_execute_foreign(&view, token, &out.reprocess);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->stack->limit_reason;
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

typedef struct {
    lxa_html_root_context_t *context;
    lxa_html_root_mode_t mode;
    lxa_html_data_emit_t emit;
    void *user;
    lxa_html_root_foreign_dispatch_result_t *result;
} root_foreign_dispatch_view_t;

static lxa_status_t
context_foreign_route(root_foreign_dispatch_view_t *view, const lxa_html_token_t *token,
    lxa_html_foreign_root_route_t *route)
{
    lxa_html_root_foreign_route_t plan;
    lxa_status_t status = lxa_html_root_context_foreign_route(view->context, view->mode, token, &plan);
    if (status == LXA_OK) *route = plan.action;
    return status;
}

static lxa_status_t
context_foreign_consume(root_foreign_dispatch_view_t *view, const lxa_html_token_t *token,
    bool *reprocess)
{
    lxa_html_root_foreign_result_t result;
    lxa_status_t status = lxa_html_root_context_foreign_token(view->context, view->mode, token,
        view->emit, view->user, &result);
    if (status == LXA_OK) {
        view->result->parent = result.parent;
        view->result->identity = result.identity;
        *reprocess = result.reprocess;
    }
    return status;
}

#define LXA_ROOT_FOREIGN_DISPATCH_NAME context_dispatch_foreign
#define LXA_ROOT_FOREIGN_DISPATCH_VIEW root_foreign_dispatch_view_t
#define LXA_ROOT_FOREIGN_DISPATCH_ROUTE context_foreign_route
#define LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT(view, token) \
    lxa_html_root_context_foreign_breakout((view)->context, (view)->mode, token)
#define LXA_ROOT_FOREIGN_DISPATCH_CONSUME context_foreign_consume
#include "html/root_foreign_dispatch.inc"
#undef LXA_ROOT_FOREIGN_DISPATCH_NAME
#undef LXA_ROOT_FOREIGN_DISPATCH_VIEW
#undef LXA_ROOT_FOREIGN_DISPATCH_ROUTE
#undef LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT
#undef LXA_ROOT_FOREIGN_DISPATCH_CONSUME

lxa_status_t
lxa_html_root_context_foreign_dispatch(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token,
    lxa_html_data_emit_t emit, void *user, lxa_html_root_foreign_dispatch_result_t *result)
{
    if (result == NULL) {
        lxa_status_t status = begin(context);
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    }
    /* Routing validates the context before dispatch has any effects. */
    lxa_html_root_foreign_dispatch_result_t out = {false, 0, 0};
    root_foreign_dispatch_view_t view = {context, mode, emit, user, &out};
    lxa_status_t status = context_dispatch_foreign(&view, token, &out.handled);
    if (status == LXA_OK) *result = out;
    return status;
}
