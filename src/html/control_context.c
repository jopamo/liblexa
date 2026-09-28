#include <assert.h>
#include <string.h>
#include "html/control_context.h"

lxa_status_t
lxa_html_control_context_init(lxa_html_control_context_t *context,
    const lxa_allocator_t *allocator, lxa_budget_t *budget)
{
    if (context == NULL) return LXA_ERROR_ARGUMENT;
    memset(context, 0, sizeof(*context));
    context->limit_reason = LXA_BUDGET_COUNT;
    lxa_status_t status = lxa_html_formatting_store_init(&context->snapshots, allocator, budget);
    if (status == LXA_OK) status = lxa_html_formatting_list_init(&context->formatting, &context->snapshots);
    if (status == LXA_OK) status = lxa_html_control_stack_init(&context->stack, allocator, budget);
    if (status == LXA_OK) status = lxa_html_root_context_init(&context->root, &context->stack);
    if (status == LXA_OK)
        status = lxa_html_template_context_init(&context->templates, &context->stack, &context->formatting);
    if (status == LXA_OK) status = lxa_buffer_init(&context->pending_table_text, allocator);
    if (status == LXA_OK) status = lxa_buffer_bind_budget(&context->pending_table_text, budget);
    if (status == LXA_OK) status = lxa_buffer_init(&context->spaces, allocator);
    if (status == LXA_OK) status = lxa_buffer_bind_budget(&context->spaces, budget);
    if (status != LXA_OK) lxa_html_control_context_destroy(context);
    return status;
}

void
lxa_html_control_context_destroy(lxa_html_control_context_t *context)
{
    if (context == NULL) return;
    lxa_buffer_destroy(&context->spaces);
    lxa_buffer_destroy(&context->pending_table_text);
    lxa_html_template_context_destroy(&context->templates);
    lxa_html_root_context_destroy(&context->root);
    lxa_html_formatting_list_destroy(&context->formatting);
    lxa_html_control_stack_destroy(&context->stack);
    lxa_html_formatting_store_destroy(&context->snapshots);
    memset(context, 0, sizeof(*context));
    context->limit_reason = LXA_BUDGET_COUNT;
}

static bool
state_valid(const lxa_html_control_context_t *context)
{
    return context->root.stack == &context->stack && context->templates.stack == &context->stack
        && context->templates.formatting == &context->formatting
        && context->formatting.store == &context->snapshots
        && context->snapshots.budget == context->stack.budget
        && lxa_html_tree_root_mode_valid(context->mode)
        && (unsigned) context->document_mode <= LXA_HTML_QUIRKS
        && (lxa_html_tree_mode_has_template(context->mode, context->text.return_mode)
            == (context->templates.frames.bytes.length != 0))
        && ((context->mode == LXA_TREE_TEXT) == (context->text.plan.mode != LXA_HTML_DATA))
        && (context->mode != LXA_TREE_TEXT || lxa_html_text_context_valid(&context->text));
}

lxa_status_t
lxa_html_control_context_start_root(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, lxa_html_root_start_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || has_patch || context->mode == LXA_TREE_INITIAL
        || context->mode == LXA_TREE_DONE || context->mode == LXA_TREE_TEXT
        || lxa_html_tree_frameset_mode(context->mode)
        || context->templates.frames.bytes.length != 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    const lxa_html_root_token_borrowers_t borrowers = {
        &context->formatting, &context->text, tokenizer, &context->form_owner
    };
    lxa_html_root_start_result_t out;
    lxa_status_t status = lxa_html_root_context_start_token(&context->root, &borrowers, token,
        context->document_mode, &context->mode, &context->pending_newline, &out);
    lxa_limit_reason_t reason = context->root.limit_reason;
    if (status == LXA_OK && out.action == LXA_HTML_ROOT_START_RESULT_TEMPLATE) {
        status = lxa_html_template_context_enter(&context->templates, context->mode, false);
        reason = context->templates.limit_reason;
        if (status == LXA_OK) {
            context->mode = LXA_TREE_TEMPLATE_TEXT;
            context->root.frameset_ok = false;
        }
    }
    if (status == LXA_OK) *result = out;
    else {
        context->failure = status;
        context->limit_reason = reason;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_after_token(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, lxa_html_token_kind_t kind,
    lxa_status_t status, lxa_limit_reason_t reason)
{
    if (context == NULL || context->stack.budget == NULL
        || (unsigned) kind > LXA_HTML_PROCESSING_INSTRUCTION) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (status == LXA_OK && context->root.failure != LXA_OK) {
        status = context->root.failure;
        reason = context->root.limit_reason;
    }
    if (status == LXA_OK && context->templates.failure != LXA_OK) {
        status = context->templates.failure;
        reason = context->templates.limit_reason;
    }
    if (status == LXA_OK && !state_valid(context)) {
        assert(false);
        status = LXA_ERROR_INTERNAL;
        reason = LXA_BUDGET_COUNT;
    }
    if (status == LXA_OK && tokenizer != NULL && kind != LXA_HTML_EOF) {
        status = lxa_html_template_context_namespace_feedback(&context->templates, tokenizer);
        reason = context->templates.limit_reason;
    }
    if (status != LXA_OK) {
        context->failure = status;
        context->limit_reason = reason;
    }
    return status;
}
