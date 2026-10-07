#include <assert.h>
#include <string.h>
#include "html/control_context.h"

static lxa_status_t
control_input_status(lxa_html_input_result_t result)
{
    switch (result) {
    case LXA_HTML_INPUT_OK: return LXA_OK;
    case LXA_HTML_INPUT_LIMIT: return LXA_ERROR_LIMIT;
    case LXA_HTML_INPUT_NO_MEMORY: return LXA_ERROR_NO_MEMORY;
    case LXA_HTML_INPUT_OVERFLOW: return LXA_ERROR_OVERFLOW;
    case LXA_HTML_INPUT_UNKNOWN_ENCODING: return LXA_ERROR_UNSUPPORTED;
    default: return LXA_ERROR_ARGUMENT;
    }
}

static lxa_status_t
control_input_token(void *user, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token)
{
    return lxa_html_control_context_on_token(user, tokenizer, token,
        false, NULL, NULL);
}

static lxa_status_t
control_input_value(void *user, lxa_span_t tag, lxa_span_t name, bool *retain)
{
    (void) user;
    *retain = lxa_html_tree_attribute_value_required(tag, name, true);
    return LXA_OK;
}

static lxa_status_t
control_input_meta(lxa_span_t decoded, const lxa_allocator_t *allocator,
    lxa_budget_t *budget, const lxa_encoding_t **declared,
    lxa_limit_reason_t *reason)
{
    lxa_html_control_context_t control = {0};
    lxa_html_tokenizer_t *tokenizer = NULL;
    lxa_html_input_meta_observer_t meta = {
        .budget = budget, .reason = LXA_BUDGET_COUNT
    };
    lxa_status_t status = lxa_html_control_context_init(&control, allocator, budget);
    if (status == LXA_OK) {
        control.meta = (lxa_html_meta_sink_t){lxa_html_input_observe_meta, &meta};
        status = lxa_html_tokenizer_create_with_budget(allocator,
            control_input_token, NULL, &control, budget, &tokenizer);
        if (status == LXA_ERROR_LIMIT && reason != NULL)
            *reason = LXA_BUDGET_MEMORY;
    }
    if (status == LXA_OK) {
        lxa_html_result_t result = lxa_html_tokenizer_filter_attribute_values(
            tokenizer, control_input_value, NULL);
        if (result == LXA_HTML_OK)
            result = lxa_html_tokenizer_feed_prepaid(tokenizer, decoded);
        if (result == LXA_HTML_OK)
            result = lxa_html_tokenizer_finish(tokenizer);
        switch (result) {
        case LXA_HTML_OK: break;
        case LXA_HTML_LIMIT: status = LXA_ERROR_LIMIT; break;
        case LXA_HTML_NO_MEMORY: status = LXA_ERROR_NO_MEMORY; break;
        case LXA_HTML_OVERFLOW: status = LXA_ERROR_OVERFLOW; break;
        case LXA_HTML_ENTROPY: status = LXA_ERROR_ENTROPY; break;
        case LXA_HTML_UNSUPPORTED: status = LXA_ERROR_UNSUPPORTED; break;
        case LXA_HTML_CALLBACK_FAILURE:
            status = lxa_html_tokenizer_callback_status(tokenizer);
            if (status == LXA_OK || status == LXA_ERROR_ARGUMENT)
                status = LXA_ERROR_INTERNAL;
            break;
        default: status = LXA_ERROR_INTERNAL; break;
        }
        if (status == LXA_ERROR_LIMIT && reason != NULL)
            *reason = meta.reason != LXA_BUDGET_COUNT ? meta.reason
                : result == LXA_HTML_LIMIT ? lxa_html_tokenizer_limit_reason(tokenizer)
                : control.limit_reason;
        if (status == LXA_OK && control.mode != LXA_TREE_DONE)
            status = LXA_ERROR_INTERNAL;
    }
    if (status == LXA_OK) *declared = meta.declared;
    lxa_html_tokenizer_destroy(tokenizer);
    lxa_html_control_context_destroy(&control);
    return status;
}

lxa_status_t
lxa_html_control_input_prepare(lxa_span_t input, const lxa_encoding_t *fixed,
    size_t prefix, lxa_budget_t *budget, lxa_buffer_t *decoded,
    lxa_vector_t *mapping, lxa_html_control_input_t *selection,
    lxa_limit_reason_t *reason)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (budget == NULL || decoded == NULL || mapping == NULL || selection == NULL
        || (input.data == NULL && input.length != 0) || prefix > input.length
        || (fixed == NULL && prefix != 0) || budget->used[LXA_BUDGET_INPUT] != 0
        || decoded->budget != budget || decoded->data != NULL
        || decoded->length != 0 || decoded->capacity != 0
        || mapping->element_size != sizeof(lxa_html_input_mapping_t)
        || mapping->bytes.budget != budget || mapping->bytes.data != NULL
        || mapping->bytes.length != 0 || mapping->bytes.capacity != 0)
        return LXA_ERROR_ARGUMENT;
    const lxa_encoding_t *encoding = fixed;
    lxa_status_t status;
    if (fixed == NULL) {
        status = control_input_status(lxa_html_input_select(input,
            budget->limits.max_input_bytes, &encoding, &prefix));
        if (status != LXA_OK) {
            if (status == LXA_ERROR_LIMIT && reason != NULL)
                *reason = LXA_BUDGET_INPUT;
            return status;
        }
    }
    lxa_buffer_t tentative = {0};
    lxa_vector_t offsets = {0};
    size_t malformed = 0;
    status = lxa_buffer_init(&tentative, &decoded->allocator);
    if (status == LXA_OK) status = lxa_buffer_bind_budget(&tentative, budget);
    if (status == LXA_OK)
        status = lxa_vector_init(&offsets, &mapping->bytes.allocator,
            sizeof(lxa_html_input_mapping_t));
    if (status == LXA_OK) status = lxa_vector_bind_budget(&offsets, budget);
    if (status == LXA_OK)
        status = control_input_status(lxa_html_input_decode_mapped(input, encoding,
            prefix, budget, &tentative, &offsets, &malformed, reason));
    if (status == LXA_OK && fixed == NULL && prefix == 0) {
        const lxa_encoding_t *declared = NULL;
        status = control_input_meta((lxa_span_t){tentative.data, tentative.length},
            &decoded->allocator, budget, &declared, reason);
        bool utf16 = encoding == lxa_encoding_lookup(
            (lxa_span_t){(const uint8_t *)"utf-16le", 8})
            || encoding == lxa_encoding_lookup(
                (lxa_span_t){(const uint8_t *)"utf-16be", 8});
        if (status == LXA_OK && !utf16 && declared != NULL && declared != encoding) {
            encoding = declared;
            lxa_buffer_destroy(&tentative);
            lxa_vector_destroy(&offsets);
            status = lxa_buffer_init(&tentative, &decoded->allocator);
            if (status == LXA_OK) status = lxa_buffer_bind_budget(&tentative, budget);
            if (status == LXA_OK)
                status = lxa_vector_init(&offsets, &mapping->bytes.allocator,
                    sizeof(lxa_html_input_mapping_t));
            if (status == LXA_OK) status = lxa_vector_bind_budget(&offsets, budget);
            if (status == LXA_OK)
                status = control_input_status(lxa_html_input_decode_mapped_prepaid(
                    input, encoding, prefix, budget, &tentative, &offsets,
                    &malformed, reason));
        }
    }
    if (status == LXA_OK) {
        *decoded = tentative;
        *mapping = offsets;
        *selection = (lxa_html_control_input_t){encoding, prefix, malformed};
    }
    else {
        lxa_buffer_destroy(&tentative);
        lxa_vector_destroy(&offsets);
    }
    return status;
}

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
    if (status == LXA_OK) {
        context->root.meta = &context->meta;
        context->templates.meta = &context->meta;
    }
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
        && context->root.meta == &context->meta && context->templates.meta == &context->meta
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

typedef struct {
    lxa_html_control_context_t *context;
    const lxa_html_control_intake_sink_t *sink;
} control_intake_view_t;

static lxa_status_t
control_intake_quirks(control_intake_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_control_context_t *context = view->context;
    lxa_html_initial_action_t action;
    return lxa_html_root_context_initial_token(&context->root, &context->mode, token,
        &context->document_mode, &action);
}

static lxa_status_t
control_intake_text(control_intake_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_text_action_t action;
    return lxa_html_text_context_token(&view->context->text, token, &view->context->mode,
        view->sink->text_emit, view->sink->user, &action);
}

#define LXA_TOKEN_INTAKE_NAME control_execute_intake
#define LXA_TOKEN_INTAKE_VIEW control_intake_view_t
#define LXA_TOKEN_INTAKE_MODE(view) ((view)->context->mode)
#define LXA_TOKEN_INTAKE_COMMENT(view, token) LXA_OK
#define LXA_TOKEN_INTAKE_QUIRKS control_intake_quirks
#define LXA_TOKEN_INTAKE_TEXT control_intake_text
#define LXA_TOKEN_INTAKE_FLUSH(view) lxa_html_control_context_flush_table_text( \
    (view)->context, (view)->sink->table_emit, (view)->sink->user)
#define LXA_TOKEN_INTAKE_NEWLINE(view) ((view)->context->pending_newline)
#include "html/token_intake_execute.inc"
#undef LXA_TOKEN_INTAKE_NAME
#undef LXA_TOKEN_INTAKE_VIEW
#undef LXA_TOKEN_INTAKE_MODE
#undef LXA_TOKEN_INTAKE_COMMENT
#undef LXA_TOKEN_INTAKE_QUIRKS
#undef LXA_TOKEN_INTAKE_TEXT
#undef LXA_TOKEN_INTAKE_FLUSH
#undef LXA_TOKEN_INTAKE_NEWLINE

lxa_status_t
lxa_html_control_context_intake(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, const lxa_html_control_intake_sink_t *sink,
    lxa_html_control_intake_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || context->mode == LXA_TREE_DONE
        || !lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    const lxa_html_control_intake_sink_t discard = {0};
    control_intake_view_t view = {context, sink == NULL ? &discard : sink};
    lxa_html_token_t adjusted;
    bool handled;
    lxa_status_t status = control_execute_intake(&view, &token, &adjusted, &handled);
    if (status == LXA_OK) *result = (lxa_html_control_intake_result_t){handled, *token};
    else if (context->failure == LXA_OK) {
        context->failure = status;
        context->limit_reason = context->root.failure == status ? context->root.limit_reason : LXA_BUDGET_COUNT;
    }
    return status;
}

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_root_prebody_emit_t emit;
    void *user;
} control_head_noscript_view_t;

#define LXA_HEAD_NOSCRIPT_DISPATCH_NAME control_dispatch_head_noscript
#define LXA_HEAD_NOSCRIPT_DISPATCH_VIEW control_head_noscript_view_t
#define LXA_HEAD_NOSCRIPT_DISPATCH_MODE(view) ((view)->context->mode)
#define LXA_HEAD_NOSCRIPT_DISPATCH_EXEC(view, token, out) lxa_html_root_context_head_noscript_token( \
    &(view)->context->root, &(view)->context->mode, token, (view)->emit, (view)->user, out)
#include "html/head_noscript_dispatch.inc"
#undef LXA_HEAD_NOSCRIPT_DISPATCH_NAME
#undef LXA_HEAD_NOSCRIPT_DISPATCH_VIEW
#undef LXA_HEAD_NOSCRIPT_DISPATCH_MODE
#undef LXA_HEAD_NOSCRIPT_DISPATCH_EXEC

lxa_status_t
lxa_html_control_context_head_noscript_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_prebody_emit_t emit, void *user,
    lxa_html_control_intake_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || context->mode == LXA_TREE_INITIAL
        || context->mode == LXA_TREE_DONE || context->mode == LXA_TREE_TEXT
        || lxa_html_tree_frameset_mode(context->mode)
        || context->templates.frames.bytes.length != 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    control_head_noscript_view_t view = {context, emit, user};
    lxa_html_token_t adjusted;
    bool handled;
    lxa_status_t status = control_dispatch_head_noscript(&view, &token, &adjusted, &handled);
    if (status == LXA_OK) *result = (lxa_html_control_intake_result_t){handled, *token};
    else {
        context->failure = status;
        context->limit_reason = context->root.limit_reason;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_frameset_token(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_text_emit_t emit, void *user, lxa_html_root_frameset_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || !lxa_html_tree_frameset_mode(context->mode)
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF
        || (token->kind == LXA_HTML_START_TAG
            && lxa_html_tree_name_equals(token->name, "noframes", 8) && tokenizer == NULL))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    const lxa_html_root_frameset_borrowers_t borrowers = {
        &context->formatting, &context->text, tokenizer, &context->spaces
    };
    lxa_html_root_frameset_result_t out;
    lxa_status_t status = lxa_html_root_context_frameset_token(&context->root,
        &borrowers, token, &context->mode, &out);
    if (status == LXA_OK && out.action == LXA_HTML_FRAMESET_TEXT) {
        if (context->spaces.length != 0 && emit != NULL)
            status = emit(user, (lxa_span_t){context->spaces.data, context->spaces.length});
        if (status == LXA_OK) lxa_buffer_clear(&context->spaces);
    }
    if (status == LXA_OK) {
        if (out.action == LXA_HTML_FRAMESET_NOFRAMES_START)
            context->pending_newline = out.skip_newline;
        *result = out;
    }
    else {
        context->failure = status;
        context->limit_reason = context->root.failure == status
            ? context->root.limit_reason : LXA_BUDGET_COUNT;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_template_column_group_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_data_emit_t emit, void *user,
    lxa_html_column_group_action_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_html_column_group_action_t out;
    lxa_status_t status = lxa_html_template_context_column_group_token(&context->templates,
        token, &context->spaces, emit, user, &out);
    if (status == LXA_OK) {
        if (out == LXA_HTML_COLUMN_GROUP_SPACE_TEXT) lxa_buffer_clear(&context->spaces);
        *result = out;
    }
    else {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_template_data_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_data_emit_t emit, void *user)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (context->mode != LXA_TREE_TEMPLATE_TEXT || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_TEXT
        || !lxa_html_tree_table_text_input_valid(&context->pending_table_text, token->data))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_html_template_current_route_t route;
    lxa_status_t status = lxa_html_template_context_current_token(&context->templates, token, &route);
    if (status == LXA_OK) {
        switch (route.action) {
        case LXA_HTML_TEMPLATE_CURRENT_TABLE:
            status = lxa_html_template_context_table_data(&context->templates, token->data,
                &context->pending_table_text, emit, user);
            break;
        case LXA_HTML_TEMPLATE_CURRENT_CONTINUE:
            status = lxa_html_template_context_data_token(&context->templates, token->data,
                &context->pending_table_text, emit, user);
            break;
        default:
            assert(false);
            status = LXA_ERROR_INTERNAL;
        }
    }
    if (status != LXA_OK) {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_template_text_start(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool foster, uint64_t *identity)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (identity == NULL || tokenizer == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !(foster ? lxa_html_tree_template_foster_text_element(token->name)
                    : lxa_html_tree_template_html_text_element(token->name)))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    bool plaintext = lxa_html_tree_name_equals(token->name, "plaintext", 9);
    bool newline = false;
    uint64_t out = 0;
    lxa_status_t status;
    if (foster)
        status = lxa_html_template_context_foster_text(&context->templates, &context->text,
            tokenizer, token, &out, &newline);
    else if (plaintext)
        status = lxa_html_template_context_start_plaintext(&context->templates, tokenizer, token, &out);
    else
        status = lxa_html_template_context_start_text(&context->templates, &context->text,
            tokenizer, token, &newline);
    if (status == LXA_OK) {
        if (!plaintext) context->mode = LXA_TREE_TEXT;
        if (newline) context->pending_newline = true;
        *identity = out;
    }
    else {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

static lxa_status_t
control_template_pop(lxa_html_control_context_t *context, size_t keep)
{
    lxa_status_t status = lxa_html_control_stack_pop_to(&context->stack, keep);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack.limit_reason;
    return status;
}

static lxa_status_t
control_template_hidden_plan(lxa_html_control_context_t *context, lxa_html_table_context_plan_t *out)
{
    /* Column-group intake validates these frames before hidden-input planning. */
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->templates.frames.bytes.data;
    const lxa_html_template_control_frame_t *frame =
        &frames[lxa_vector_length(&context->templates.frames) - 1];
    lxa_status_t status = lxa_html_control_stack_table_context_plan(&context->stack, frame->boundary,
        lxa_html_tree_template_table_context(frame->insertion_mode), out);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack.limit_reason;
    return status;
}

#define LXA_HIDDEN_TABLE_NAME control_template_hidden_input
#define LXA_HIDDEN_TABLE_VIEW lxa_html_control_context_t
#define LXA_HIDDEN_TABLE_BUDGET(view) ((view)->stack.budget)
#define LXA_HIDDEN_TABLE_REASON(view) (&(view)->limit_reason)
#define LXA_HIDDEN_TABLE_PLAN control_template_hidden_plan
#define LXA_HIDDEN_TABLE_INSERT(view, token, keep) LXA_OK
#define LXA_HIDDEN_TABLE_POP control_template_pop
#include "html/hidden_table_input.inc"
#undef LXA_HIDDEN_TABLE_NAME
#undef LXA_HIDDEN_TABLE_VIEW
#undef LXA_HIDDEN_TABLE_BUDGET
#undef LXA_HIDDEN_TABLE_REASON
#undef LXA_HIDDEN_TABLE_PLAN
#undef LXA_HIDDEN_TABLE_INSERT
#undef LXA_HIDDEN_TABLE_POP

#define LXA_TEMPLATE_HTML_PRELUDE_NAME control_execute_template_html_prelude
#define LXA_TEMPLATE_HTML_PRELUDE_VIEW lxa_html_control_context_t
#define LXA_TEMPLATE_HTML_PRELUDE_PREPARE(view, token, handled) \
    lxa_html_template_context_prepare_start(&(view)->templates, token, handled)
#define LXA_TEMPLATE_HTML_PRELUDE_GROUP(view, token, action) \
    lxa_html_template_context_column_group_token(&(view)->templates, token, NULL, NULL, NULL, action)
#define LXA_TEMPLATE_HTML_PRELUDE_HIDDEN control_template_hidden_input
#define LXA_TEMPLATE_HTML_PRELUDE_SELECT(view, token, handled) \
    lxa_html_template_context_select_token(&(view)->templates, token, handled)
#include "html/template_html_prelude.inc"
#undef LXA_TEMPLATE_HTML_PRELUDE_NAME
#undef LXA_TEMPLATE_HTML_PRELUDE_VIEW
#undef LXA_TEMPLATE_HTML_PRELUDE_PREPARE
#undef LXA_TEMPLATE_HTML_PRELUDE_GROUP
#undef LXA_TEMPLATE_HTML_PRELUDE_HIDDEN
#undef LXA_TEMPLATE_HTML_PRELUDE_SELECT

typedef enum {
    CONTROL_TEMPLATE_START_PRELUDE,
    CONTROL_TEMPLATE_START_FORM,
    CONTROL_TEMPLATE_START_HTML_FOREIGN
} control_template_start_route_t;

static lxa_status_t
control_template_start_route(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled, control_template_start_route_t route)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (handled == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || context->pending_table_text.length != 0 || context->pending_newline
        || (route == CONTROL_TEMPLATE_START_FORM && !lxa_html_tree_name_equals(token->name, "form", 4))
        || (route == CONTROL_TEMPLATE_START_HTML_FOREIGN
            && !lxa_html_tree_name_equals(token->name, "svg", 3)
            && !lxa_html_tree_name_equals(token->name, "math", 4)))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    bool out;
    lxa_status_t status;
    switch (route) {
    case CONTROL_TEMPLATE_START_PRELUDE:
        status = control_execute_template_html_prelude(context, token, &out);
        break;
    case CONTROL_TEMPLATE_START_FORM:
        status = lxa_html_template_context_form_token(&context->templates, token, &out);
        break;
    case CONTROL_TEMPLATE_START_HTML_FOREIGN:
        status = lxa_html_template_context_html_foreign_start(&context->templates, token, &out);
        break;
    default:
        assert(false);
        status = LXA_ERROR_INTERNAL;
        break;
    }
    if (status == LXA_OK) *handled = out;
    else {
        context->failure = status;
        if (context->templates.failure == status) context->limit_reason = context->templates.limit_reason;
        else if (status != LXA_ERROR_LIMIT) context->limit_reason = LXA_BUDGET_COUNT;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_template_start_prelude(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled)
{
    return control_template_start_route(context, token, handled, CONTROL_TEMPLATE_START_PRELUDE);
}

lxa_status_t
lxa_html_control_context_template_form_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled)
{
    return control_template_start_route(context, token, handled, CONTROL_TEMPLATE_START_FORM);
}

lxa_status_t
lxa_html_control_context_template_html_foreign_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled)
{
    return control_template_start_route(context, token, handled, CONTROL_TEMPLATE_START_HTML_FOREIGN);
}

lxa_status_t
lxa_html_control_context_template_table_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_table_action_t *action)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (action == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "table", 5)
        || context->pending_table_text.length != 0 || context->pending_newline)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_html_template_table_action_t out;
    lxa_status_t status = lxa_html_template_context_table_token(&context->templates, token, &out);
    if (status == LXA_OK) *action = out;
    else {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

static bool
control_template_has_content(lxa_html_control_context_t *context)
{
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->templates.frames.bytes.data;
    return lxa_vector_length(&context->stack.elements)
        > frames[lxa_vector_length(&context->templates.frames) - 1].boundary;
}

static lxa_status_t
control_template_column(lxa_html_control_context_t *context, const lxa_html_token_t *token)
{
    bool inserted;
    return lxa_html_template_context_start_column_prepared(&context->templates, token, &inserted);
}

#define LXA_TEMPLATE_START_ROUTE_NAME control_execute_template_start_route
#define LXA_TEMPLATE_START_ROUTE_VIEW lxa_html_control_context_t
#define LXA_TEMPLATE_START_ROUTE_PRELUDE lxa_html_control_context_template_start_prelude
#define LXA_TEMPLATE_START_ROUTE_FORM lxa_html_control_context_template_form_token
#define LXA_TEMPLATE_START_ROUTE_FOREIGN lxa_html_control_context_template_html_foreign_start
#define LXA_TEMPLATE_START_ROUTE_CURRENT control_template_has_content
#define LXA_TEMPLATE_START_ROUTE_COLUMN control_template_column
#define LXA_TEMPLATE_START_ROUTE_TABLE lxa_html_control_context_template_table_token
#include "html/template_start_route.inc"
#undef LXA_TEMPLATE_START_ROUTE_NAME
#undef LXA_TEMPLATE_START_ROUTE_VIEW
#undef LXA_TEMPLATE_START_ROUTE_PRELUDE
#undef LXA_TEMPLATE_START_ROUTE_FORM
#undef LXA_TEMPLATE_START_ROUTE_FOREIGN
#undef LXA_TEMPLATE_START_ROUTE_CURRENT
#undef LXA_TEMPLATE_START_ROUTE_COLUMN
#undef LXA_TEMPLATE_START_ROUTE_TABLE

lxa_status_t
lxa_html_control_context_template_start_route(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_start_action_t *action)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (action == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || context->pending_table_text.length != 0 || context->pending_newline)
        return LXA_ERROR_ARGUMENT;
    lxa_html_template_start_action_t out;
    lxa_status_t status = control_execute_template_start_route(context, token, &out);
    if (status == LXA_OK) *action = out;
    else if (context->failure == LXA_OK) {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

static bool
control_template_end_br(lxa_html_control_context_t *context)
{
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->templates.frames.bytes.data;
    const lxa_html_template_control_frame_t *frame =
        &frames[lxa_vector_length(&context->templates.frames) - 1];
    return lxa_html_tree_template_end_br_start(frame->insertion_mode,
        lxa_vector_length(&context->stack.elements) > frame->boundary);
}

#define LXA_TEMPLATE_END_DISPATCH_NAME control_dispatch_template_end
#define LXA_TEMPLATE_END_DISPATCH_VIEW lxa_html_control_context_t
#define LXA_TEMPLATE_END_DISPATCH_FOREIGN(view, token, handled) \
    lxa_html_template_context_dispatch_foreign_end(&(view)->templates, token, handled)
#define LXA_TEMPLATE_END_DISPATCH_BR control_template_end_br
#define LXA_TEMPLATE_END_DISPATCH_CLOSE(view) \
    lxa_html_template_context_leave(&(view)->templates, true, &(view)->mode)
#define LXA_TEMPLATE_END_DISPATCH_BOUNDARY(view, token, handled) \
    lxa_html_template_context_end_boundary(&(view)->templates, token, handled)
#include "html/template_end_dispatch.inc"
#undef LXA_TEMPLATE_END_DISPATCH_NAME
#undef LXA_TEMPLATE_END_DISPATCH_VIEW
#undef LXA_TEMPLATE_END_DISPATCH_FOREIGN
#undef LXA_TEMPLATE_END_DISPATCH_BR
#undef LXA_TEMPLATE_END_DISPATCH_CLOSE
#undef LXA_TEMPLATE_END_DISPATCH_BOUNDARY

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_control_template_token_result_t *out;
} control_template_end_view_t;

#define LXA_TEMPLATE_END_EXECUTE_NAME control_execute_template_end
#define LXA_TEMPLATE_END_EXECUTE_VIEW control_template_end_view_t
#define LXA_TEMPLATE_END_EXECUTE_LENGTH(view) lxa_vector_length(&(view)->context->stack.elements)
#define LXA_TEMPLATE_END_EXECUTE_DISPATCH(view, token, adjusted, handled) \
    control_dispatch_template_end((view)->context, token, adjusted, handled)
#define LXA_TEMPLATE_END_EXECUTE_PRELUDE(view, token, handled) \
    control_execute_template_html_prelude((view)->context, token, handled)
#define LXA_TEMPLATE_END_EXECUTE_TABLE(view, token, result) \
    lxa_html_template_context_end_table(&(view)->context->templates, token, result)
#define LXA_TEMPLATE_END_EXECUTE_PARAGRAPH(view) \
    ((view)->out->implied_paragraph = true, (view)->out->foster = true)
#define LXA_TEMPLATE_END_EXECUTE_BODY(view, token) lxa_html_template_context_end_body( \
    &(view)->context->templates, token, &(view)->out->implied_paragraph)
#include "html/template_end_execute.inc"
#undef LXA_TEMPLATE_END_EXECUTE_NAME
#undef LXA_TEMPLATE_END_EXECUTE_VIEW
#undef LXA_TEMPLATE_END_EXECUTE_LENGTH
#undef LXA_TEMPLATE_END_EXECUTE_DISPATCH
#undef LXA_TEMPLATE_END_EXECUTE_PRELUDE
#undef LXA_TEMPLATE_END_EXECUTE_TABLE
#undef LXA_TEMPLATE_END_EXECUTE_PARAGRAPH
#undef LXA_TEMPLATE_END_EXECUTE_BODY

lxa_status_t
lxa_html_control_context_template_mathml_table_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_mathml_table_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_table_part(token->name) || lxa_html_tree_name_equals(token->name, "table", 5))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_status_t status = lxa_html_template_context_mathml_table_token(&context->templates, token, result);
    if (status != LXA_OK && context->templates.failure != LXA_OK) {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_scope_entry_t current;
    lxa_html_foreign_context_t integration;
    lxa_html_data_emit_t emit;
    void *user;
} control_template_foreign_view_t;

static lxa_status_t
control_template_foreign_data(control_template_foreign_view_t *view,
    const lxa_html_token_t *token, bool integrated)
{
    return integrated
        ? lxa_html_control_context_template_data_token(view->context, token, view->emit, view->user)
        : lxa_html_template_context_emit_data(&view->context->templates,
            token->data, view->emit, view->user);
}

static lxa_status_t
control_template_foreign_table(control_template_foreign_view_t *view,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_html_template_mathml_table_result_t out;
    lxa_status_t status = lxa_html_template_context_mathml_table_token(
        &view->context->templates, token, &out);
    if (status == LXA_OK) *handled = out.handled;
    return status;
}

#define LXA_TEMPLATE_FOREIGN_DISPATCH_NAME control_dispatch_template_foreign
#define LXA_TEMPLATE_FOREIGN_DISPATCH_VIEW control_template_foreign_view_t
#define LXA_TEMPLATE_FOREIGN_DISPATCH_NAMESPACE(view, out) (*(out) = (view)->current.namespace_id, LXA_OK)
#define LXA_TEMPLATE_FOREIGN_DISPATCH_CURRENT_NAME(view, out) (*(out) = (view)->current.name, LXA_OK)
#define LXA_TEMPLATE_FOREIGN_DISPATCH_CONTEXT(view, ns, name, out) (*(out) = (view)->integration, LXA_OK)
#define LXA_TEMPLATE_FOREIGN_DISPATCH_START(view, token, ns) \
    lxa_html_template_context_start_foreign(&(view)->context->templates, token, ns)
#define LXA_TEMPLATE_FOREIGN_DISPATCH_DATA control_template_foreign_data
#define LXA_TEMPLATE_FOREIGN_DISPATCH_TABLE control_template_foreign_table
#define LXA_TEMPLATE_FOREIGN_DISPATCH_FONT(view, token, font) lxa_html_tree_font_breakout( \
    token, (view)->context->stack.budget, &(view)->context->limit_reason, font)
#define LXA_TEMPLATE_FOREIGN_DISPATCH_BREAKOUT(view, token) \
    lxa_html_template_context_breakout(&(view)->context->templates)
#include "html/template_foreign_dispatch.inc"
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_NAME
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_VIEW
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_NAMESPACE
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_CURRENT_NAME
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_CONTEXT
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_START
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_DATA
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_TABLE
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_FONT
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_BREAKOUT

lxa_status_t
lxa_html_control_context_template_foreign_dispatch(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_data_emit_t emit, void *user,
    lxa_html_template_foreign_action_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token)
        || (token->kind != LXA_HTML_START_TAG && token->kind != LXA_HTML_TEXT)
        || (token->kind == LXA_HTML_TEXT
            && !lxa_html_tree_table_text_input_valid(&context->pending_table_text, token->data)))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    control_template_foreign_view_t view = {.context = context, .emit = emit, .user = user};
    lxa_status_t status = lxa_html_template_context_current(&context->templates,
        &view.current, &view.integration);
    context->limit_reason = status == LXA_ERROR_LIMIT ? context->stack.limit_reason : LXA_BUDGET_COUNT;
    if (status == LXA_OK) status = control_dispatch_template_foreign(&view, token, result);
    if (status != LXA_OK) {
        context->failure = status;
        if (context->templates.failure == status) context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_tokenizer_t *tokenizer;
    lxa_html_control_template_table_start_result_t *out;
} control_template_table_start_view_t;

static lxa_status_t
control_template_table_pop(control_template_table_start_view_t *view)
{
    return control_template_pop(view->context, lxa_vector_length(&view->context->stack.elements) - 1);
}

static lxa_status_t
control_template_table_part(control_template_table_start_view_t *view,
    const lxa_html_token_t *token)
{
    bool inserted;
    lxa_status_t status = lxa_html_template_context_start_table_part_prepared(
        &view->context->templates, token, &inserted);
    if (status == LXA_OK && inserted) {
        lxa_html_scope_entry_t entry;
        lxa_html_foreign_context_t integration;
        status = lxa_html_template_context_current(&view->context->templates, &entry, &integration);
        if (status == LXA_OK) view->out->identity = entry.identity;
    }
    return status;
}

static lxa_status_t
control_template_table_column(control_template_table_start_view_t *view,
    const lxa_html_token_t *token)
{
    bool inserted;
    return lxa_html_template_context_start_column_prepared(&view->context->templates, token, &inserted);
}

static lxa_status_t
control_template_table_foster(control_template_table_start_view_t *view,
    const lxa_html_token_t *token, bool keep, lxa_dom_namespace_t ns)
{
    lxa_status_t status = lxa_html_template_context_foster_token(&view->context->templates,
        token, ns, keep, &view->out->identity);
    if (status == LXA_OK) {
        view->out->token = *token;
        view->out->foster = true;
    }
    return status;
}

static lxa_status_t
control_template_table_text_start(control_template_table_start_view_t *view,
    const lxa_html_token_t *token, bool foster)
{
    lxa_status_t status = lxa_html_control_context_template_text_start(view->context,
        view->tokenizer, token, foster, &view->out->identity);
    if (status == LXA_OK) view->out->foster = foster;
    return status;
}

#define LXA_TEMPLATE_TABLE_START_NAME control_execute_template_table_start
#define LXA_TEMPLATE_TABLE_START_VIEW control_template_table_start_view_t
#define LXA_TEMPLATE_TABLE_START_POP control_template_table_pop
#define LXA_TEMPLATE_TABLE_START_COLUMN control_template_table_column
#define LXA_TEMPLATE_TABLE_START_PART control_template_table_part
#define LXA_TEMPLATE_TABLE_START_FOSTER control_template_table_foster
#define LXA_TEMPLATE_TABLE_START_TEXT_CHECK(view) \
    ((view)->tokenizer == NULL ? LXA_ERROR_ARGUMENT : LXA_OK)
#define LXA_TEMPLATE_TABLE_START_TEXT control_template_table_text_start
#define LXA_TEMPLATE_TABLE_START_VOID_PREFLIGHT(view) LXA_OK
#define LXA_TEMPLATE_TABLE_START_NEWLINE(view) ((view)->context->pending_newline)
#define LXA_TEMPLATE_TABLE_START_SUPPORTED(view, name, out) lxa_html_tree_supported_container(name, \
    (view)->context->stack.budget, &(view)->context->limit_reason, out)
#include "html/template_table_start_execute.inc"
#undef LXA_TEMPLATE_TABLE_START_NAME
#undef LXA_TEMPLATE_TABLE_START_VIEW
#undef LXA_TEMPLATE_TABLE_START_POP
#undef LXA_TEMPLATE_TABLE_START_COLUMN
#undef LXA_TEMPLATE_TABLE_START_PART
#undef LXA_TEMPLATE_TABLE_START_FOSTER
#undef LXA_TEMPLATE_TABLE_START_TEXT_CHECK
#undef LXA_TEMPLATE_TABLE_START_TEXT
#undef LXA_TEMPLATE_TABLE_START_VOID_PREFLIGHT
#undef LXA_TEMPLATE_TABLE_START_NEWLINE
#undef LXA_TEMPLATE_TABLE_START_SUPPORTED

#define LXA_TEMPLATE_CURRENT_START_NAME control_execute_template_current_start
#define LXA_TEMPLATE_CURRENT_START_VIEW control_template_table_start_view_t
#define LXA_TEMPLATE_CURRENT_START_READ(view, token, route) \
    lxa_html_template_context_current_token(&(view)->context->templates, token, route)
#define LXA_TEMPLATE_CURRENT_START_TEXT_CHECK(view) \
    ((view)->tokenizer == NULL ? LXA_ERROR_ARGUMENT : LXA_OK)
#define LXA_TEMPLATE_CURRENT_START_PREFLIGHT(view) LXA_OK
#define LXA_TEMPLATE_CURRENT_START_POP control_template_table_pop
#define LXA_TEMPLATE_CURRENT_START_TABLE control_execute_template_table_start
#include "html/template_current_start.inc"
#undef LXA_TEMPLATE_CURRENT_START_NAME
#undef LXA_TEMPLATE_CURRENT_START_VIEW
#undef LXA_TEMPLATE_CURRENT_START_READ
#undef LXA_TEMPLATE_CURRENT_START_TEXT_CHECK
#undef LXA_TEMPLATE_CURRENT_START_PREFLIGHT
#undef LXA_TEMPLATE_CURRENT_START_POP
#undef LXA_TEMPLATE_CURRENT_START_TABLE

lxa_status_t
lxa_html_control_context_template_table_start(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_control_template_table_start_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || context->pending_table_text.length != 0 || context->pending_newline)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_html_control_template_table_start_result_t out = {
        .action = LXA_HTML_TEMPLATE_START_CONTINUE, .token = *token
    };
    control_template_table_start_view_t view = {context, tokenizer, &out};
    lxa_status_t status = control_execute_template_current_start(&view, token, &out.action);
    if (status == LXA_OK) *result = out;
    else if (context->failure == LXA_OK) {
        context->failure = status;
        if (context->templates.failure == status) context->limit_reason = context->templates.limit_reason;
        else if (status != LXA_ERROR_LIMIT) context->limit_reason = LXA_BUDGET_COUNT;
    }
    return status;
}

static lxa_status_t
control_template_part_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token)
{
    bool inserted;
    return lxa_html_template_context_start_table_part_prepared(&context->templates, token, &inserted);
}

#define LXA_TEMPLATE_PART_START_NAME control_execute_template_part_start
#define LXA_TEMPLATE_PART_START_VIEW lxa_html_control_context_t
#define LXA_TEMPLATE_PART_START_COLUMN control_template_column
#define LXA_TEMPLATE_PART_START_PART control_template_part_start
#include "html/template_part_start.inc"
#undef LXA_TEMPLATE_PART_START_NAME
#undef LXA_TEMPLATE_PART_START_VIEW
#undef LXA_TEMPLATE_PART_START_COLUMN
#undef LXA_TEMPLATE_PART_START_PART

lxa_status_t
lxa_html_control_context_template_part_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (handled == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || context->pending_table_text.length != 0 || context->pending_newline)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    bool out;
    lxa_status_t status = control_execute_template_part_start(context, token, &out);
    if (status == LXA_OK) *handled = out;
    else {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_template_void_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_token_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || (!lxa_html_tree_head_void(token->name) && !lxa_html_tree_body_void(token->name)
            && !lxa_html_tree_name_equals(token->name, "image", 5))
        || context->pending_table_text.length != 0 || context->pending_newline)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_status_t status = lxa_html_template_context_start_void(&context->templates, token, result);
    if (status != LXA_OK) {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_template_ordinary_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_control_template_start_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || lxa_html_tree_template_html_text_element(token->name))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_html_control_template_start_result_t out = {0};
    bool formatting = lxa_html_tree_formatting_tag(token->name);
    lxa_status_t status;
    if (formatting)
        status = lxa_html_template_context_start_formatting(&context->templates, token, &out.identity);
    else if (lxa_html_tree_formatting_marker_tag(token->name))
        status = lxa_html_template_context_start_marker(&context->templates, token, &out.identity);
    else
        status = lxa_html_template_context_start_container(&context->templates, token,
            context->document_mode == LXA_HTML_QUIRKS, &out.identity, &out.foster);
    if (status == LXA_OK) {
        if (formatting) {
            lxa_html_template_control_frame_t *frames =
                (lxa_html_template_control_frame_t *) context->templates.frames.bytes.data;
            size_t count = lxa_vector_length(&context->templates.frames);
            if (lxa_html_tree_template_start_mode(frames[count - 1].insertion_mode,
                token->name, LXA_DOM_NS_HTML, &frames[count - 1].insertion_mode) != LXA_OK) {
                assert(false);
                context->failure = LXA_ERROR_INTERNAL;
                context->limit_reason = LXA_BUDGET_COUNT;
                return context->failure;
            }
        }
        if (!out.foster && (lxa_html_tree_name_equals(token->name, "pre", 3)
            || lxa_html_tree_name_equals(token->name, "listing", 7)))
            context->pending_newline = true;
        *result = out;
    }
    else if (context->templates.failure != LXA_OK) {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_tokenizer_t *tokenizer;
    lxa_html_control_template_html_start_result_t *out;
} control_template_html_start_view_t;

static lxa_status_t
control_template_enter(control_template_html_start_view_t *view, const lxa_html_token_t *token)
{
    (void) token;
    lxa_status_t status = lxa_html_template_context_enter(&view->context->templates,
        view->context->mode, false);
    if (status == LXA_OK) {
        const lxa_html_template_control_frame_t *frames =
            (const lxa_html_template_control_frame_t *) view->context->templates.frames.bytes.data;
        view->out->identity = frames[lxa_vector_length(&view->context->templates.frames) - 1].identity;
    }
    return status;
}

#define LXA_TEMPLATE_HTML_START_NAME control_route_template_html_start
#define LXA_TEMPLATE_HTML_START_VIEW control_template_html_start_view_t
#define LXA_TEMPLATE_HTML_START_TEMPLATE control_template_enter
#define LXA_TEMPLATE_HTML_START_CLOSED(view, token) lxa_html_control_context_template_void_start( \
    (view)->context, token, &(view)->out->token)
#define LXA_TEMPLATE_HTML_START_TEXT(view, token) lxa_html_control_context_template_text_start( \
    (view)->context, (view)->tokenizer, token, false, &(view)->out->identity)
#include "html/template_html_start_route.inc"
#undef LXA_TEMPLATE_HTML_START_NAME
#undef LXA_TEMPLATE_HTML_START_VIEW
#undef LXA_TEMPLATE_HTML_START_TEMPLATE
#undef LXA_TEMPLATE_HTML_START_CLOSED
#undef LXA_TEMPLATE_HTML_START_TEXT

lxa_status_t
lxa_html_control_context_template_html_start(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, lxa_html_control_template_html_start_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || has_patch || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || (lxa_html_tree_template_html_text_element(token->name) && tokenizer == NULL)
        || context->pending_table_text.length != 0 || context->pending_newline)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_html_control_template_html_start_result_t out = {.token = *token};
    control_template_html_start_view_t view = {context, tokenizer, &out};
    lxa_status_t status = control_route_template_html_start(&view, token, &out.action);
    if (status == LXA_OK && out.action == LXA_HTML_TEMPLATE_HTML_START_PLAINTEXT)
        status = lxa_html_control_context_template_text_start(context, tokenizer, token, false, &out.identity);
    else if (status == LXA_OK && out.action == LXA_HTML_TEMPLATE_HTML_START_ORDINARY) {
        lxa_html_control_template_start_result_t ordinary;
        status = lxa_html_control_context_template_ordinary_start(context, token, &ordinary);
        if (status == LXA_OK) {
            out.identity = ordinary.identity;
            out.foster = ordinary.foster;
        }
    }
    if (status == LXA_OK) *result = out;
    else if (context->failure == LXA_OK) {
        context->failure = status;
        context->limit_reason = context->templates.limit_reason;
    }
    return status;
}

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_tokenizer_t *tokenizer;
    lxa_html_token_t *token;
} control_template_start_view_t;

static lxa_status_t
control_template_start_current(control_template_start_view_t *view,
    const lxa_html_token_t *token, lxa_html_template_start_action_t *action)
{
    lxa_html_control_template_table_start_result_t current;
    lxa_status_t status = lxa_html_control_context_template_table_start(view->context,
        view->tokenizer, token, &current);
    if (status == LXA_OK) {
        *view->token = current.token;
        *action = current.action;
    }
    return status;
}

static lxa_status_t
control_template_start_late(control_template_start_view_t *view,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_html_control_template_html_start_result_t late;
    lxa_status_t status = lxa_html_control_context_template_html_start(view->context,
        view->tokenizer, token, false, &late);
    if (status == LXA_OK) {
        *view->token = late.token;
        *handled = true;
    }
    return status;
}

#define LXA_TEMPLATE_START_NAME control_execute_template_start
#define LXA_TEMPLATE_START_VIEW control_template_start_view_t
#define LXA_TEMPLATE_START_EARLY(view, token, action) \
    lxa_html_control_context_template_start_route((view)->context, token, action)
#define LXA_TEMPLATE_START_CURRENT control_template_start_current
#define LXA_TEMPLATE_START_PART(view, token, handled) \
    lxa_html_control_context_template_part_start((view)->context, token, handled)
#define LXA_TEMPLATE_START_LATE control_template_start_late
#include "html/template_start_execute.inc"
#undef LXA_TEMPLATE_START_NAME
#undef LXA_TEMPLATE_START_VIEW
#undef LXA_TEMPLATE_START_EARLY
#undef LXA_TEMPLATE_START_CURRENT
#undef LXA_TEMPLATE_START_PART
#undef LXA_TEMPLATE_START_LATE

lxa_status_t
lxa_html_control_context_template_start_dispatch(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, lxa_html_control_template_start_dispatch_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || has_patch || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || context->pending_table_text.length != 0 || context->pending_newline)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_html_control_template_start_dispatch_result_t out = {.token = *token};
    control_template_start_view_t view = {context, tokenizer, &out.token};
    lxa_status_t status = control_execute_template_start(&view, token, &out.action);
    if (status == LXA_OK) *result = out;
    else if (context->failure == LXA_OK) {
        context->failure = status;
        context->limit_reason = status == LXA_ERROR_LIMIT ? context->templates.limit_reason : LXA_BUDGET_COUNT;
    }
    return status;
}

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_tokenizer_t *tokenizer;
    lxa_html_data_emit_t emit;
    void *user;
    lxa_html_control_template_token_result_t *out;
} control_template_token_view_t;

static lxa_status_t
control_template_token_end(control_template_token_view_t *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted, bool *handled)
{
    control_template_end_view_t end = {view->context, view->out};
    return control_execute_template_end(&end, token, adjusted, handled);
}

static lxa_status_t
control_template_token_start(control_template_token_view_t *view,
    const lxa_html_token_t **token, lxa_html_template_start_action_t *action)
{
    lxa_html_control_template_start_dispatch_result_t out;
    lxa_status_t status = lxa_html_control_context_template_start_dispatch(view->context,
        view->tokenizer, *token, false, &out);
    if (status == LXA_OK) {
        view->out->token = out.token;
        *token = &view->out->token;
        *action = out.action;
    }
    return status;
}

#define LXA_TEMPLATE_HTML_PRELUDE_NAME control_template_token_data_prelude
#define LXA_TEMPLATE_HTML_PRELUDE_VIEW control_template_token_view_t
#define LXA_TEMPLATE_HTML_PRELUDE_PREPARE(view, token, handled) (assert(false), LXA_ERROR_INTERNAL)
#define LXA_TEMPLATE_HTML_PRELUDE_GROUP(view, token, action) \
    lxa_html_control_context_template_column_group_token((view)->context, token, (view)->emit, (view)->user, action)
#define LXA_TEMPLATE_HTML_PRELUDE_HIDDEN(view, token, handled) \
    control_template_hidden_input((view)->context, token, handled)
#define LXA_TEMPLATE_HTML_PRELUDE_SELECT(view, token, handled) \
    lxa_html_template_context_select_token(&(view)->context->templates, token, handled)
#include "html/template_html_prelude.inc"
#undef LXA_TEMPLATE_HTML_PRELUDE_NAME
#undef LXA_TEMPLATE_HTML_PRELUDE_VIEW
#undef LXA_TEMPLATE_HTML_PRELUDE_PREPARE
#undef LXA_TEMPLATE_HTML_PRELUDE_GROUP
#undef LXA_TEMPLATE_HTML_PRELUDE_HIDDEN
#undef LXA_TEMPLATE_HTML_PRELUDE_SELECT

#define LXA_TEMPLATE_TOKEN_NAME control_execute_template_token
#define LXA_TEMPLATE_TOKEN_VIEW control_template_token_view_t
#define LXA_TEMPLATE_TOKEN_LENGTH(view) lxa_vector_length(&(view)->context->stack.elements)
#define LXA_TEMPLATE_TOKEN_MISC(view, token) LXA_OK
#define LXA_TEMPLATE_TOKEN_FOREIGN(view, token, action) lxa_html_control_context_template_foreign_dispatch( \
    (view)->context, token, (view)->emit, (view)->user, action)
#define LXA_TEMPLATE_TOKEN_END control_template_token_end
#define LXA_TEMPLATE_TOKEN_BOUNDARY(view, token, adjusted, handled) \
    control_dispatch_template_end((view)->context, token, adjusted, handled)
#define LXA_TEMPLATE_TOKEN_START control_template_token_start
#define LXA_TEMPLATE_TOKEN_ORDINARY(view, token) (assert(false), LXA_ERROR_INTERNAL)
#define LXA_TEMPLATE_TOKEN_DATA_PRELUDE control_template_token_data_prelude
#define LXA_TEMPLATE_TOKEN_DATA(view, token) lxa_html_control_context_template_data_token( \
    (view)->context, token, (view)->emit, (view)->user)
#include "html/template_token_execute.inc"
#undef LXA_TEMPLATE_TOKEN_NAME
#undef LXA_TEMPLATE_TOKEN_VIEW
#undef LXA_TEMPLATE_TOKEN_LENGTH
#undef LXA_TEMPLATE_TOKEN_MISC
#undef LXA_TEMPLATE_TOKEN_FOREIGN
#undef LXA_TEMPLATE_TOKEN_END
#undef LXA_TEMPLATE_TOKEN_BOUNDARY
#undef LXA_TEMPLATE_TOKEN_START
#undef LXA_TEMPLATE_TOKEN_ORDINARY
#undef LXA_TEMPLATE_TOKEN_DATA_PRELUDE
#undef LXA_TEMPLATE_TOKEN_DATA

lxa_status_t
lxa_html_control_context_template_token(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, lxa_html_data_emit_t emit, void *user,
    lxa_html_control_template_token_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || has_patch || context->mode != LXA_TREE_TEMPLATE_TEXT
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF
        || context->pending_newline
        || (token->kind != LXA_HTML_TEXT && context->pending_table_text.length != 0)
        || (token->kind == LXA_HTML_TEXT
            && !lxa_html_tree_table_text_input_valid(&context->pending_table_text, token->data)))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_html_control_template_token_result_t out = {.token = *token};
    control_template_token_view_t view = {context, tokenizer, emit, user, &out};
    lxa_html_token_t adjusted;
    lxa_status_t status = control_execute_template_token(&view, &token, &adjusted);
    if (status == LXA_OK) {
        out.token = *token;
        *result = out;
    }
    else if (context->failure == LXA_OK) {
        context->failure = status;
        context->limit_reason = status == LXA_ERROR_LIMIT
            ? context->templates.limit_reason : LXA_BUDGET_COUNT;
    }
    return status;
}

static lxa_status_t
control_start_root(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_root_start_result_t *result)
{
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

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_tokenizer_t *tokenizer;
    const lxa_html_control_root_sink_t *sink;
    lxa_html_control_root_result_t *result;
} control_root_view_t;

static lxa_status_t
control_end_root(control_root_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_control_context_t *context = view->context;
    const lxa_html_root_token_borrowers_t borrowers = {
        &context->formatting, &context->text, view->tokenizer, &context->form_owner
    };
    return lxa_html_root_context_end_token(&context->root, &borrowers, token,
        context->document_mode, &context->mode, &context->pending_newline, &view->result->value.end);
}

static lxa_status_t
control_root_data(control_root_view_t *view, lxa_span_t data)
{
    lxa_html_control_context_t *context = view->context;
    const lxa_html_root_data_sink_t sink = {
        &context->pending_table_text, view->sink->emit, view->sink->prebody_emit, view->sink->user
    };
    return lxa_html_root_context_data_token(&context->root, &context->formatting,
        data, &context->mode, &sink);
}

#define LXA_ROOT_TOKEN_NAME control_execute_root_token
#define LXA_ROOT_TOKEN_VIEW control_root_view_t
#define LXA_ROOT_TOKEN_MODE(view) ((view)->context->mode)
#define LXA_ROOT_TOKEN_MISC(view, token) lxa_html_root_context_misc_token( \
    &(view)->context->root, token, (view)->context->mode, &(view)->result->value.destination)
#define LXA_ROOT_TOKEN_AFTER_BODY(view, token, out) lxa_html_root_context_after_body( \
    &(view)->context->root, &(view)->context->formatting, token, &(view)->context->mode, \
    (view)->sink->emit, (view)->sink->user, out)
#define LXA_ROOT_TOKEN_END control_end_root
#define LXA_ROOT_TOKEN_START(view, token) control_start_root( \
    (view)->context, (view)->tokenizer, token, &(view)->result->value.start)
#define LXA_ROOT_TOKEN_DATA control_root_data
#include "html/root_token_execute.inc"
#undef LXA_ROOT_TOKEN_NAME
#undef LXA_ROOT_TOKEN_VIEW
#undef LXA_ROOT_TOKEN_MODE
#undef LXA_ROOT_TOKEN_MISC
#undef LXA_ROOT_TOKEN_AFTER_BODY
#undef LXA_ROOT_TOKEN_END
#undef LXA_ROOT_TOKEN_START
#undef LXA_ROOT_TOKEN_DATA

lxa_status_t
lxa_html_control_context_root_token(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, const lxa_html_control_root_sink_t *sink,
    lxa_html_control_root_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || has_patch || context->mode == LXA_TREE_INITIAL
        || context->mode == LXA_TREE_DONE || context->mode == LXA_TREE_TEXT
        || lxa_html_tree_frameset_mode(context->mode)
        || context->templates.frames.bytes.length != 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF
        || ((token->kind == LXA_HTML_START_TAG || token->kind == LXA_HTML_END_TAG)
            && tokenizer == NULL))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    const lxa_html_control_root_sink_t discard = {0};
    lxa_html_control_root_result_t out = {.kind = token->kind};
    control_root_view_t view = {context, tokenizer, sink == NULL ? &discard : sink, &out};
    lxa_status_t status = control_execute_root_token(&view, token);
    if (status == LXA_OK) *result = out;
    else if (context->failure == LXA_OK) {
        context->failure = status;
        context->limit_reason = context->root.limit_reason;
    }
    return status;
}

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_tokenizer_t *tokenizer;
    const lxa_html_control_root_sink_t *sink;
    lxa_html_control_root_dispatch_result_t *result;
} control_root_dispatch_view_t;

static lxa_status_t
control_root_noscript_dispatch(control_root_dispatch_view_t *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted, bool *handled)
{
    control_head_noscript_view_t noscript = {
        view->context, view->sink->prebody_emit, view->sink->user
    };
    view->result->action = LXA_HTML_CONTROL_ROOT_NOSCRIPT;
    return control_dispatch_head_noscript(&noscript, token, adjusted, handled);
}

static lxa_status_t
control_root_foreign_dispatch(control_root_dispatch_view_t *view,
    const lxa_html_token_t *token, bool *handled)
{
    view->result->action = LXA_HTML_CONTROL_ROOT_FOREIGN;
    lxa_status_t status = lxa_html_root_context_foreign_dispatch(&view->context->root,
        view->context->mode, token, view->sink->emit, view->sink->user, &view->result->value.foreign);
    if (status == LXA_OK) *handled = view->result->value.foreign.handled;
    return status;
}

static lxa_status_t
control_root_frameset_dispatch(control_root_dispatch_view_t *view, const lxa_html_token_t *token)
{
    view->result->action = LXA_HTML_CONTROL_ROOT_FRAMESET;
    return lxa_html_root_context_start_frameset(&view->context->root,
        token, &view->context->mode, &view->result->value.frameset_identity);
}

static lxa_status_t
control_root_initial_dispatch(control_root_dispatch_view_t *view, const lxa_html_token_t *token)
{
    view->result->action = LXA_HTML_CONTROL_ROOT_INITIAL;
    lxa_html_initial_action_t action;
    return lxa_html_root_context_initial_token(&view->context->root,
        &view->context->mode, token, &view->context->document_mode, &action);
}

static lxa_status_t
control_root_html_dispatch(control_root_dispatch_view_t *view, const lxa_html_token_t *token)
{
    view->result->action = LXA_HTML_CONTROL_ROOT_HTML;
    lxa_html_control_root_result_t out = {.kind = token->kind};
    control_root_view_t html = {view->context, view->tokenizer, view->sink, &out};
    lxa_status_t status = control_execute_root_token(&html, token);
    if (status == LXA_OK) view->result->value.html = out;
    return status;
}

#define LXA_ROOT_DISPATCH_NAME control_dispatch_root
#define LXA_ROOT_DISPATCH_VIEW control_root_dispatch_view_t
#define LXA_ROOT_DISPATCH_MODE(view) ((view)->context->mode)
#define LXA_ROOT_DISPATCH_HAS_BODY(view) ((view)->context->root.body)
#define LXA_ROOT_DISPATCH_NOSCRIPT control_root_noscript_dispatch
#define LXA_ROOT_DISPATCH_FRAMESET control_root_frameset_dispatch
#define LXA_ROOT_DISPATCH_FOREIGN control_root_foreign_dispatch
#define LXA_ROOT_DISPATCH_INITIAL control_root_initial_dispatch
#define LXA_ROOT_DISPATCH_HTML control_root_html_dispatch
#include "html/root_dispatch.inc"
#undef LXA_ROOT_DISPATCH_NAME
#undef LXA_ROOT_DISPATCH_VIEW
#undef LXA_ROOT_DISPATCH_MODE
#undef LXA_ROOT_DISPATCH_HAS_BODY
#undef LXA_ROOT_DISPATCH_NOSCRIPT
#undef LXA_ROOT_DISPATCH_FRAMESET
#undef LXA_ROOT_DISPATCH_FOREIGN
#undef LXA_ROOT_DISPATCH_INITIAL
#undef LXA_ROOT_DISPATCH_HTML

lxa_status_t
lxa_html_control_context_root_dispatch(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, const lxa_html_control_root_sink_t *sink,
    lxa_html_control_root_dispatch_result_t *result)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (result == NULL || has_patch || context->mode == LXA_TREE_DONE || context->mode == LXA_TREE_TEXT
        || lxa_html_tree_frameset_mode(context->mode)
        || context->templates.frames.bytes.length != 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind == LXA_HTML_EOF
        || (context->mode == LXA_TREE_INITIAL && token->kind != LXA_HTML_DOCTYPE)
        || ((token->kind == LXA_HTML_START_TAG || token->kind == LXA_HTML_END_TAG)
            && tokenizer == NULL))
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    const lxa_html_control_root_sink_t discard = {0};
    lxa_html_control_root_dispatch_result_t out = {0};
    control_root_dispatch_view_t view = {context, tokenizer, sink == NULL ? &discard : sink, &out};
    lxa_html_token_t adjusted;
    lxa_status_t status = control_dispatch_root(&view, &token, &adjusted);
    if (status == LXA_OK) {
        out.token = *token;
        *result = out;
    }
    else if (context->failure == LXA_OK) {
        context->failure = status;
        context->limit_reason = context->root.limit_reason;
    }
    return status;
}

lxa_status_t
lxa_html_control_context_flush_table_text(lxa_html_control_context_t *context,
    lxa_html_table_text_emit_t emit, void *user)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    if (context->pending_table_text.length == 0) return LXA_OK;
    const lxa_span_t pending = {context->pending_table_text.data, context->pending_table_text.length};
    lxa_status_t status;
    lxa_limit_reason_t reason;
    if (context->mode == LXA_TREE_TEMPLATE_TEXT) {
        lxa_html_template_table_text_result_t result;
        status = lxa_html_template_context_table_text(&context->templates, pending, emit, user, &result);
        reason = context->templates.limit_reason;
    }
    else {
        lxa_html_root_table_text_result_t result;
        status = lxa_html_root_context_table_text(&context->root, &context->formatting,
            context->mode, pending, emit, user, &result);
        reason = context->root.limit_reason;
    }
    if (status == LXA_OK) lxa_buffer_clear(&context->pending_table_text);
    else {
        context->failure = status;
        context->limit_reason = reason;
    }
    return status;
}

#define LXA_EOF_EXEC_NAME control_execute_eof
#define LXA_EOF_EXEC_VIEW lxa_html_control_context_t
#define LXA_EOF_EXEC_MODE(view) ((view)->mode)
#define LXA_EOF_EXEC_TEMPLATES(view) lxa_html_template_context_finish(&(view)->templates, &(view)->mode)
#define LXA_EOF_EXEC_ROOT(view) lxa_html_root_context_finish(&(view)->root, &(view)->mode)
#include "html/eof_execute.inc"
#undef LXA_EOF_EXEC_NAME
#undef LXA_EOF_EXEC_VIEW
#undef LXA_EOF_EXEC_MODE
#undef LXA_EOF_EXEC_TEMPLATES
#undef LXA_EOF_EXEC_ROOT

lxa_status_t
lxa_html_control_context_finish(lxa_html_control_context_t *context)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (context->mode == LXA_TREE_INITIAL || context->mode == LXA_TREE_TEXT
        || context->mode == LXA_TREE_DONE || context->pending_table_text.length != 0
        || context->pending_newline)
        return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    lxa_status_t status = LXA_OK;
    if (context->mode == LXA_TREE_TEMPLATE_TEXT) {
        const lxa_html_token_t token = {.kind = LXA_HTML_EOF};
        bool handled;
        status = control_execute_template_html_prelude(context, &token, &handled);
        if (status == LXA_OK && handled) {
            assert(false);
            context->failure = LXA_ERROR_INTERNAL;
            context->limit_reason = LXA_BUDGET_COUNT;
            return context->failure;
        }
        /* EOF pays the canonical current-entry read before unwind. */
        lxa_html_template_current_route_t route;
        if (status == LXA_OK)
            status = lxa_html_template_context_current_token(&context->templates, &token, &route);
    }
    if (status == LXA_OK) status = control_execute_eof(context);
    if (status != LXA_OK) {
        context->failure = status;
        context->limit_reason = context->templates.failure != LXA_OK
            ? context->templates.limit_reason : context->root.limit_reason;
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

typedef struct {
    lxa_html_control_context_t *context;
    lxa_html_tokenizer_t *tokenizer;
    const lxa_html_control_intake_sink_t *intake;
    const lxa_html_control_root_sink_t *root;
} control_token_view_t;

static lxa_status_t
control_token_intake(control_token_view_t *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted, bool *handled)
{
    lxa_html_control_intake_result_t out;
    lxa_status_t status = lxa_html_control_context_intake(view->context, *token, view->intake, &out);
    if (status == LXA_OK) {
        *adjusted = out.token;
        *token = adjusted;
        *handled = out.handled;
    }
    return status;
}

static lxa_status_t
control_token_template(control_token_view_t *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted)
{
    (void) adjusted;
    if ((*token)->kind == LXA_HTML_EOF)
        return lxa_html_control_context_finish(view->context);
    lxa_html_control_template_token_result_t out;
    return lxa_html_control_context_template_token(view->context, view->tokenizer,
        *token, false, view->root->emit, view->root->user, &out);
}

static lxa_status_t
control_token_frameset(control_token_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_root_frameset_result_t out;
    return lxa_html_control_context_frameset_token(view->context, view->tokenizer,
        token, view->intake->text_emit, view->intake->user, &out);
}

static lxa_status_t
control_token_root(control_token_view_t *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted)
{
    (void) adjusted;
    lxa_html_control_root_dispatch_result_t out;
    return lxa_html_control_context_root_dispatch(view->context, view->tokenizer,
        *token, false, view->root, &out);
}

#define LXA_TOKEN_EXEC_NAME control_execute_token
#define LXA_TOKEN_EXEC_VIEW control_token_view_t
#define LXA_TOKEN_EXEC_MODE(view) ((view)->context->mode)
#define LXA_TOKEN_EXEC_INTAKE control_token_intake
#define LXA_TOKEN_EXEC_TEMPLATE control_token_template
#define LXA_TOKEN_EXEC_EOF(view) lxa_html_control_context_finish((view)->context)
#define LXA_TOKEN_EXEC_FRAMESET control_token_frameset
#define LXA_TOKEN_EXEC_ROOT control_token_root
#include "html/token_execute.inc"
#undef LXA_TOKEN_EXEC_NAME
#undef LXA_TOKEN_EXEC_VIEW
#undef LXA_TOKEN_EXEC_MODE
#undef LXA_TOKEN_EXEC_INTAKE
#undef LXA_TOKEN_EXEC_TEMPLATE
#undef LXA_TOKEN_EXEC_EOF
#undef LXA_TOKEN_EXEC_FRAMESET
#undef LXA_TOKEN_EXEC_ROOT

lxa_status_t
lxa_html_control_context_on_token(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token, bool has_patch,
    const lxa_html_control_intake_sink_t *intake_sink,
    const lxa_html_control_root_sink_t *root_sink)
{
    if (context == NULL || context->stack.budget == NULL) return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    if (tokenizer == NULL || has_patch || context->mode == LXA_TREE_DONE
        || !lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    if (!state_valid(context)) {
        assert(false);
        context->failure = LXA_ERROR_INTERNAL;
        context->limit_reason = LXA_BUDGET_COUNT;
        return context->failure;
    }
    const lxa_html_control_intake_sink_t discard_intake = {0};
    const lxa_html_control_root_sink_t discard_root = {0};
    control_token_view_t view = {
        context, tokenizer, intake_sink == NULL ? &discard_intake : intake_sink,
        root_sink == NULL ? &discard_root : root_sink
    };
    lxa_status_t status = control_execute_token(&view, token);
    return lxa_html_control_context_after_token(context, tokenizer, token->kind,
        status, context->limit_reason);
}
