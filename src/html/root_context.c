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

static bool
body_start_mode_valid(const lxa_html_root_context_t *context, lxa_html_root_mode_t mode)
{
    return (mode == LXA_TREE_BEFORE_HTML || mode == LXA_TREE_BEFORE_HEAD
            || mode == LXA_TREE_IN_HEAD || mode == LXA_TREE_AFTER_HEAD || mode == LXA_TREE_IN_BODY)
        && (mode != LXA_TREE_BEFORE_HEAD || context->html)
        && ((mode != LXA_TREE_IN_HEAD && mode != LXA_TREE_AFTER_HEAD) || context->head);
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
        || *mode == LXA_TREE_INITIAL || *mode == LXA_TREE_DONE || *mode == LXA_TREE_TEMPLATE_TEXT)
        return LXA_ERROR_ARGUMENT;
    return finish(context, context_finish_root(context, mode));
}

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
    if (out == LXA_HTML_HEAD_END_BODY || out == LXA_HTML_HEAD_END_HTML || out == LXA_HTML_HEAD_END_BR)
        status = context_ensure_body(context);
    if (status == LXA_OK) {
        switch (out) {
            case LXA_HTML_HEAD_END_IGNORE: break;
            case LXA_HTML_HEAD_END_CLOSE: *mode = LXA_TREE_AFTER_HEAD; break;
            case LXA_HTML_HEAD_END_BODY: *mode = LXA_TREE_AFTER_BODY; break;
            case LXA_HTML_HEAD_END_HTML: *mode = LXA_TREE_AFTER_HTML; break;
            case LXA_HTML_HEAD_END_BR: *mode = LXA_TREE_IN_BODY; break;
        }
        *action = out;
    }
    return finish(context, status);
}

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
    if ((!plan.body && lxa_vector_length(&context->stack->elements) != 0)
        || (*mode == LXA_TREE_BEFORE_HEAD && !context->html)
        || ((*mode == LXA_TREE_IN_HEAD || *mode == LXA_TREE_HEAD_NOSCRIPT
            || *mode == LXA_TREE_AFTER_HEAD) && !context->head))
        return LXA_ERROR_ARGUMENT;
    status = plan.body ? context_ensure_body(context) : context_ensure_head(context);
    if (status == LXA_OK) {
        *mode = plan.return_mode;
        if (plan.disable_frameset) context->frameset_ok = false;
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
        size_t retained = length - group, depth, count;
        status = lxa_size_add(retained, 2, &depth);
        if (status == LXA_OK)
            status = lxa_budget_check(context->stack->budget, LXA_BUDGET_TREE_DEPTH,
                depth, &context->limit_reason);
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

lxa_status_t
lxa_html_root_context_reconstruct(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (formatting == NULL || formatting->store == NULL
        || formatting->store->budget != context->stack->budget || !context->body
        || lxa_vector_length(&context->stack->elements) == 0)
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
