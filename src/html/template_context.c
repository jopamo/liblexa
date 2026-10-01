#include <assert.h>
#include <string.h>
#include "html/template_context.h"

static lxa_status_t
internal(void)
{
    assert(false);
    return LXA_ERROR_INTERNAL;
}

static lxa_status_t
finish(lxa_html_template_context_t *context, lxa_status_t status)
{
    if (status != LXA_OK) context->failure = status;
    return status;
}

static lxa_status_t
begin(lxa_html_template_context_t *context)
{
    if (context == NULL || context->stack == NULL || context->formatting == NULL)
        return LXA_ERROR_ARGUMENT;
    if (context->failure != LXA_OK) return context->failure;
    context->limit_reason = LXA_BUDGET_COUNT;
    if (context->frames.element_size != sizeof(lxa_html_template_control_frame_t)
        || context->frames.bytes.length > context->frames.bytes.capacity
        || context->frames.bytes.length % sizeof(lxa_html_template_control_frame_t) != 0
        || (context->frames.bytes.length != 0 && context->frames.bytes.data == NULL)
        || context->formatting->store == NULL
        || context->stack->budget == NULL
        || context->formatting->store->budget != context->stack->budget
        || context->frames.bytes.budget != context->stack->budget)
        return finish(context, internal());
    lxa_dom_namespace_t ignored;
    lxa_status_t status = lxa_html_control_stack_current_namespace(context->stack,
        lxa_vector_length(&context->stack->elements), &ignored);
    if (status == LXA_OK) {
        size_t count = lxa_vector_length(&context->frames);
        size_t length = lxa_vector_length(&context->stack->elements);
        if (count != 0) {
            const lxa_html_template_control_frame_t *frames =
                (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
            size_t previous = count == 1 ? context->root_length : frames[count - 2].boundary;
            const lxa_html_template_control_frame_t *frame = &frames[count - 1];
            if (context->root_length > length || count > length - context->root_length
                || frame->boundary <= previous
                || !lxa_html_tree_template_boundary_valid(count, length, frame->boundary, previous)
                || frame->boundary < context->root_length + count
                || frame->identity == 0 || (unsigned) frame->insertion_mode > LXA_TEMPLATE_BODY
                || (context->return_mode == LXA_TREE_IN_BODY && context->root_length == 0)
                || !lxa_html_tree_template_return_mode_valid(context->return_mode))
                status = internal();
        }
    }
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_init(lxa_html_template_context_t *context,
    lxa_html_control_stack_t *stack, lxa_html_formatting_list_t *formatting)
{
    if (context == NULL) return LXA_ERROR_ARGUMENT;
    memset(context, 0, sizeof(*context));
    context->limit_reason = LXA_BUDGET_COUNT;
    if (stack == NULL || formatting == NULL || formatting->store == NULL
        || stack->budget == NULL || stack->budget != formatting->store->budget)
        return LXA_ERROR_ARGUMENT;
    lxa_status_t status = lxa_vector_init(&context->frames,
        &stack->elements.bytes.allocator, sizeof(lxa_html_template_control_frame_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&context->frames, stack->budget);
    if (status != LXA_OK) {
        lxa_html_template_context_destroy(context);
        return status;
    }
    context->stack = stack;
    context->formatting = formatting;
    return LXA_OK;
}

void
lxa_html_template_context_destroy(lxa_html_template_context_t *context)
{
    if (context == NULL) return;
    lxa_vector_destroy(&context->frames);
    memset(context, 0, sizeof(*context));
    context->limit_reason = LXA_BUDGET_COUNT;
}

lxa_status_t
lxa_html_template_context_namespace_feedback(lxa_html_template_context_t *context,
    lxa_html_tokenizer_t *tokenizer)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    size_t count = lxa_vector_length(&context->frames), boundary = 0;
    if (count != 0) {
        const lxa_html_template_control_frame_t *frames =
            (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
        boundary = frames[count - 1].boundary;
    }
    return finish(context, lxa_html_control_stack_namespace_feedback(context->stack, boundary, tokenizer));
}

static lxa_status_t
check_depth_at(lxa_html_template_context_t *context, size_t length, size_t root,
    lxa_html_root_mode_t returning, size_t templates, size_t extra)
{
    size_t count = lxa_vector_length(&context->frames);
    if (root > length || count > length - root
        || (returning == LXA_TREE_IN_BODY && root == 0))
        return internal();
    size_t elements;
    lxa_status_t status = lxa_size_add(length - root - count, extra, &elements);
    if (status == LXA_OK)
        status = lxa_html_tree_template_depth_check(context->stack->budget,
            returning == LXA_TREE_IN_BODY ? root - 1 : 0,
            templates, elements, &context->limit_reason);
    return status;
}

static lxa_status_t
check_depth(lxa_html_template_context_t *context, size_t root,
    lxa_html_root_mode_t returning, size_t templates, size_t extra)
{
    return check_depth_at(context, lxa_vector_length(&context->stack->elements),
        root, returning, templates, extra);
}

static lxa_status_t
prepare_replacement(lxa_html_template_context_t *context, size_t keep, size_t extra)
{
    size_t count = lxa_vector_length(&context->frames);
    size_t length = lxa_vector_length(&context->stack->elements), after;
    lxa_status_t status = lxa_size_add(keep, extra, &after);
    if (status == LXA_OK)
        status = check_depth_at(context, keep, context->root_length, context->return_mode,
            count, extra);
    if (status == LXA_OK && extra > length - keep)
        status = lxa_budget_check(context->stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
            extra - (length - keep), &context->limit_reason);
    if (status == LXA_OK) status = lxa_vector_reserve(&context->stack->elements, after);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

lxa_status_t
lxa_html_template_context_depth_check(lxa_html_template_context_t *context, size_t extra)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0) return LXA_ERROR_ARGUMENT;
    return finish(context, check_depth(context, context->root_length,
        context->return_mode, count, extra));
}

lxa_status_t
lxa_html_template_context_push_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (identity == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || (unsigned) ns < LXA_DOM_NS_HTML || (unsigned) ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    status = check_depth(context, context->root_length, context->return_mode, count, 1);
    if (status == LXA_OK)
        status = lxa_html_control_stack_push_token(context->stack, token, ns, identity);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->stack->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_replace_token(lxa_html_template_context_t *context,
    size_t keep, const lxa_html_token_t *token, lxa_dom_namespace_t ns, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (identity == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || (unsigned) ns < LXA_DOM_NS_HTML || (unsigned) ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    if (keep < frames[count - 1].boundary
        || keep > lxa_vector_length(&context->stack->elements)) return LXA_ERROR_ARGUMENT;
    status = prepare_replacement(context, keep, 1);
    if (status == LXA_OK)
        status = lxa_budget_check_item(context->stack->budget, LXA_BUDGET_NAME_BYTES,
            token->name.length, &context->limit_reason);
    if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
    if (status == LXA_OK)
        status = lxa_html_control_stack_push_token(context->stack, token, ns, identity);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->stack->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_start_container(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool quirks, uint64_t *identity, bool *foster)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (identity == NULL || foster == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG || lxa_html_tree_formatting_tag(token->name)
        || lxa_html_tree_formatting_marker_tag(token->name)) return LXA_ERROR_ARGUMENT;
    bool accepted = lxa_html_tree_template_container_start(token->name);
    if (!accepted)
        status = lxa_html_tree_supported_container(token->name, context->stack->budget,
            &context->limit_reason, &accepted);
    if (status != LXA_OK) return finish(context, status);
    if (!accepted) return LXA_ERROR_ARGUMENT;
    if (lxa_html_tree_name_equals(token->name, "button", 6)) {
        const lxa_html_template_control_frame_t *frames =
            (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
        size_t keep;
        status = lxa_html_control_stack_scope_find(context->stack,
            frames[count - 1].boundary, token->name, LXA_HTML_SCOPE_NORMAL, &keep);
        if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
        if (status != LXA_OK) return finish(context, status);
        status = lxa_html_template_context_reconstruct(context);
        if (status != LXA_OK) return status;
    }
    if (lxa_html_tree_template_container_reconstructs(token->name)) {
        status = lxa_html_template_context_reconstruct(context);
        if (status != LXA_OK) return status;
    }
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_html_template_container_plan_t plan;
    status = lxa_html_control_stack_template_container_plan(context->stack,
        frames[count - 1].boundary, token->name, quirks, &plan);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    if (status != LXA_OK) return finish(context, status);
    if (plan.foster) {
        status = lxa_html_control_stack_pop_to(context->stack, plan.keep);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
        if (status == LXA_OK && lxa_html_tree_container_reconstructs(token->name))
            status = lxa_html_template_context_reconstruct(context);
        if (status == LXA_OK)
            status = lxa_html_template_context_push_token(context, token, LXA_DOM_NS_HTML, identity);
    }
    else {
        status = lxa_html_template_context_replace_token(context, plan.keep, token,
            LXA_DOM_NS_HTML, identity);
        if (status == LXA_OK) {
            lxa_html_template_control_frame_t *active =
                (lxa_html_template_control_frame_t *) context->frames.bytes.data;
            status = lxa_html_tree_template_start_mode(active[count - 1].insertion_mode,
                token->name, LXA_DOM_NS_HTML, &active[count - 1].insertion_mode);
        }
    }
    if (status == LXA_OK) *foster = plan.foster;
    else *identity = 0;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_admit_marker(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (identity == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_formatting_marker_tag(token->name))
        return LXA_ERROR_ARGUMENT;
    status = check_depth(context, context->root_length, context->return_mode, count, 1);
    if (status == LXA_OK)
        status = lxa_html_formatting_list_admit_marker(context->formatting, context->stack,
            token->name, identity);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->formatting->store->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_start_marker(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (identity == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_template_marker_container(token->name)) return LXA_ERROR_ARGUMENT;
    if (lxa_html_tree_template_container_reconstructs(token->name))
        status = lxa_html_template_context_reconstruct(context);
    if (status != LXA_OK) return status;
    status = lxa_html_formatting_list_reserve_marker(context->formatting);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->formatting->store->limit_reason;
    if (status == LXA_OK) status = lxa_html_template_context_admit_marker(context, token, identity);
    if (status == LXA_OK) {
        lxa_html_template_control_frame_t *frames =
            (lxa_html_template_control_frame_t *) context->frames.bytes.data;
        status = lxa_html_tree_template_start_mode(frames[count - 1].insertion_mode,
            token->name, LXA_DOM_NS_HTML, &frames[count - 1].insertion_mode);
    }
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_admit_formatting(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (identity == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_formatting_tag(token->name))
        return LXA_ERROR_ARGUMENT;
    status = check_depth(context, context->root_length, context->return_mode, count, 1);
    if (status == LXA_OK)
        status = lxa_html_formatting_list_admit(context->formatting, context->stack, token, identity);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->formatting->store->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_pop_table(lxa_html_template_context_t *context, size_t keep)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    if (keep < frames[count - 1].boundary
        || keep > lxa_vector_length(&context->stack->elements))
        return LXA_ERROR_ARGUMENT;
    status = lxa_html_formatting_list_pop_table(context->formatting, context->stack, keep, true);
    if (status == LXA_ERROR_LIMIT)
        context->limit_reason = context->formatting->store->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_start_foreign(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || (ns != LXA_DOM_NS_SVG && ns != LXA_DOM_NS_MATHML)) return LXA_ERROR_ARGUMENT;
    size_t keep = lxa_vector_length(&context->stack->elements);
    uint64_t identity;
    status = lxa_html_template_context_push_token(context, token, ns, &identity);
    if (status == LXA_OK && token->self_closing) {
        status = lxa_html_control_stack_pop_to(context->stack, keep);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    }
    if (status == LXA_OK) {
        lxa_html_template_control_frame_t *frames =
            (lxa_html_template_control_frame_t *) context->frames.bytes.data;
        status = lxa_html_tree_template_start_mode(frames[count - 1].insertion_mode,
            token->name, ns, &frames[count - 1].insertion_mode);
    }
    return finish(context, status);
}

static lxa_status_t
context_consume_foreign_end(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *reprocess)
{
    size_t count = lxa_vector_length(&context->frames);
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_html_foreign_end_plan_t plan;
    lxa_status_t status = lxa_html_control_stack_foreign_end_plan(context->stack,
        frames[count - 1].boundary, token->name, &plan);
    if (status == LXA_OK && !plan.reprocess)
        status = lxa_html_control_stack_pop_to(context->stack, plan.keep);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    if (status == LXA_OK) *reprocess = plan.reprocess;
    return status;
}

lxa_status_t
lxa_html_template_context_breakout(lxa_html_template_context_t *context)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t keep;
    status = lxa_html_control_stack_foreign_breakout_plan(context->stack,
        frames[count - 1].boundary, &keep);
    if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    return finish(context, status);
}

static lxa_status_t
context_foreign_end_route(lxa_html_template_context_t *context, const lxa_html_token_t *token,
    lxa_html_foreign_root_route_t *route)
{
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t boundary = frames[lxa_vector_length(&context->frames) - 1].boundary;
    lxa_dom_namespace_t ns;
    lxa_status_t status = lxa_html_control_stack_current_namespace(context->stack, boundary, &ns);
    if (status != LXA_OK) return status;
    if (ns == LXA_DOM_NS_HTML) {
        *route = LXA_HTML_FOREIGN_ROOT_HTML;
        return LXA_OK;
    }
    return lxa_html_foreign_root_plan(ns, LXA_HTML_FOREIGN_NONE, token, context->stack->budget,
        &context->limit_reason, route);
}

#define LXA_ROOT_FOREIGN_DISPATCH_NAME context_dispatch_template_foreign_end
#define LXA_ROOT_FOREIGN_DISPATCH_VIEW lxa_html_template_context_t
#define LXA_ROOT_FOREIGN_DISPATCH_ROUTE context_foreign_end_route
#define LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT(view, token) lxa_html_template_context_breakout(view)
#define LXA_ROOT_FOREIGN_DISPATCH_CONSUME context_consume_foreign_end
#include "html/root_foreign_dispatch.inc"
#undef LXA_ROOT_FOREIGN_DISPATCH_NAME
#undef LXA_ROOT_FOREIGN_DISPATCH_VIEW
#undef LXA_ROOT_FOREIGN_DISPATCH_ROUTE
#undef LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT
#undef LXA_ROOT_FOREIGN_DISPATCH_CONSUME

lxa_status_t
lxa_html_template_context_dispatch_foreign_end(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (handled == NULL || lxa_vector_length(&context->frames) == 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_END_TAG)
        return LXA_ERROR_ARGUMENT;
    return finish(context, context_dispatch_template_foreign_end(context, token, handled));
}

static lxa_status_t
end_plan(lxa_html_template_context_t *context, lxa_html_token_kind_t kind,
    lxa_span_t name, lxa_html_template_end_plan_t *plan)
{
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_status_t status = lxa_html_control_stack_template_end_plan(context->stack,
        frames[lxa_vector_length(&context->frames) - 1].boundary, kind, name, plan);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    return status;
}

lxa_status_t
lxa_html_template_context_select_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *ignore)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (ignore == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_html_select_plan_t plan;
    status = lxa_html_control_stack_select_plan(context->stack,
        frames[count - 1].boundary, token->kind, token->name, &plan);
    if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, plan.keep);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    if (status == LXA_OK) *ignore = plan.ignore;
    return finish(context, status);
}

static lxa_status_t end_pop(lxa_html_template_context_t *context, size_t keep);

typedef struct {
    lxa_html_template_context_t *context;
    lxa_html_template_control_frame_t *frame;
} template_prepare_start_view_t;

static lxa_status_t
prepare_current_name(template_prepare_start_view_t *view, lxa_span_t *name)
{
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_get(view->context->stack,
        lxa_vector_length(&view->context->stack->elements) - 1, &entry, NULL);
    if (status == LXA_ERROR_LIMIT) view->context->limit_reason = view->context->stack->limit_reason;
    if (status == LXA_OK) *name = entry.name;
    return status;
}

static lxa_status_t
prepare_table_scope(template_prepare_start_view_t *view, bool *found)
{
    size_t index;
    lxa_status_t status = lxa_html_control_stack_scope_find(view->context->stack, view->frame->boundary,
        (lxa_span_t){(const uint8_t *) "table", 5}, LXA_HTML_SCOPE_TABLE, &index);
    if (status == LXA_ERROR_LIMIT) view->context->limit_reason = view->context->stack->limit_reason;
    if (status == LXA_OK) *found = index != lxa_vector_length(&view->context->stack->elements);
    return status;
}

#define LXA_TEMPLATE_PREPARE_START_NAME context_prepare_html_start
#define LXA_TEMPLATE_PREPARE_START_VIEW template_prepare_start_view_t
#define LXA_TEMPLATE_PREPARE_START_CURRENT(view) \
    (lxa_vector_length(&(view)->context->stack->elements) > (view)->frame->boundary)
#define LXA_TEMPLATE_PREPARE_START_NAME_READ prepare_current_name
#define LXA_TEMPLATE_PREPARE_START_POP(view) \
    end_pop((view)->context, lxa_vector_length(&(view)->context->stack->elements) - 1)
#define LXA_TEMPLATE_PREPARE_START_MODE(view, token) lxa_html_tree_template_start_mode( \
    (view)->frame->insertion_mode, (token)->name, LXA_DOM_NS_HTML, &(view)->frame->insertion_mode)
#define LXA_TEMPLATE_PREPARE_START_MODE_GET(view) ((view)->frame->insertion_mode)
#define LXA_TEMPLATE_PREPARE_START_TABLE prepare_table_scope
#include "html/template_prepare_start.inc"
#undef LXA_TEMPLATE_PREPARE_START_NAME
#undef LXA_TEMPLATE_PREPARE_START_VIEW
#undef LXA_TEMPLATE_PREPARE_START_CURRENT
#undef LXA_TEMPLATE_PREPARE_START_NAME_READ
#undef LXA_TEMPLATE_PREPARE_START_POP
#undef LXA_TEMPLATE_PREPARE_START_MODE
#undef LXA_TEMPLATE_PREPARE_START_MODE_GET
#undef LXA_TEMPLATE_PREPARE_START_TABLE

lxa_status_t
lxa_html_template_context_prepare_start(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (handled == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG) return LXA_ERROR_ARGUMENT;
    lxa_html_template_control_frame_t *frames =
        (lxa_html_template_control_frame_t *) context->frames.bytes.data;
    template_prepare_start_view_t view = {context, &frames[count - 1]};
    return finish(context, context_prepare_html_start(&view, token, handled));
}

static lxa_status_t
end_pop(lxa_html_template_context_t *context, size_t keep)
{
    lxa_status_t status = lxa_html_control_stack_pop_to(context->stack, keep);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    return status;
}

static lxa_status_t
context_column_group_plan(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_column_group_action_t *action)
{
    size_t count = lxa_vector_length(&context->frames);
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_html_template_mode_t mode = frames[count - 1].insertion_mode;
    size_t length = lxa_vector_length(&context->stack->elements);
    bool group_open = false;
    if (mode == LXA_TEMPLATE_COLUMN_GROUP && length > frames[count - 1].boundary) {
        lxa_status_t status = lxa_budget_charge(context->stack->budget, LXA_BUDGET_WORK, 1,
            &context->limit_reason);
        if (status == LXA_OK)
            status = lxa_html_control_stack_column_group(context->stack,
                frames[count - 1].boundary, &group_open);
        if (status != LXA_OK) return status;
    }
    return lxa_html_tree_template_column_group(mode, group_open,
        token->kind, token->name, action);
}

typedef struct {
    lxa_html_template_context_t *context;
    lxa_buffer_t *spaces;
    lxa_html_data_emit_t emit;
    void *user;
} template_column_group_view_t;

static lxa_status_t
context_column_group_emit(template_column_group_view_t *view, lxa_span_t bytes)
{
    lxa_html_template_context_t *context = view->context;
    if (view->spaces == NULL || view->spaces->budget != context->stack->budget)
        return LXA_ERROR_ARGUMENT;
    lxa_status_t status = lxa_html_tree_filter_spaces(bytes, view->spaces, &context->limit_reason);
    if (status != LXA_OK || view->spaces->length == 0 || view->emit == NULL) return status;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_html_scope_entry_t entry;
    lxa_html_foreign_context_t integration;
    status = lxa_html_control_stack_current(context->stack,
        frames[lxa_vector_length(&context->frames) - 1].boundary, &entry, &integration);
    if (status == LXA_OK)
        status = view->emit(view->user, (lxa_span_t){view->spaces->data, view->spaces->length},
            entry.identity);
    return finish(context, status);
}

#define LXA_TEMPLATE_COLUMN_GROUP_NAME context_execute_column_group
#define LXA_TEMPLATE_COLUMN_GROUP_VIEW template_column_group_view_t
#define LXA_TEMPLATE_COLUMN_GROUP_PLAN(view, token, out) \
    context_column_group_plan((view)->context, token, out)
#define LXA_TEMPLATE_COLUMN_GROUP_EMIT context_column_group_emit
#include "html/template_column_group_execute.inc"
#undef LXA_TEMPLATE_COLUMN_GROUP_NAME
#undef LXA_TEMPLATE_COLUMN_GROUP_VIEW
#undef LXA_TEMPLATE_COLUMN_GROUP_PLAN
#undef LXA_TEMPLATE_COLUMN_GROUP_EMIT

lxa_status_t
lxa_html_template_context_column_group_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_buffer_t *spaces,
    lxa_html_data_emit_t emit, void *user, lxa_html_column_group_action_t *action)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (action == NULL || lxa_vector_length(&context->frames) == 0
        || !lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    template_column_group_view_t view = {context, spaces, emit, user};
    status = context_execute_column_group(&view, token, action);
    if (status == LXA_ERROR_ARGUMENT && context->failure == LXA_OK) return status;
    return finish(context, status);
}

static lxa_status_t
end_clear(lxa_html_template_context_t *context)
{
    lxa_status_t status = lxa_html_formatting_list_clear(context->formatting);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->formatting->store->limit_reason;
    return status;
}

lxa_status_t
lxa_html_template_context_start_text(lxa_html_template_context_t *context,
    lxa_html_text_context_t *text, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, bool *skip_newline)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (text == NULL || tokenizer == NULL || skip_newline == NULL || count == 0
        || text->plan.mode != LXA_HTML_DATA || text->plan.end_tag.data != NULL
        || text->plan.end_tag.length != 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || (!lxa_html_tree_returning_text_element(token->name)
            && !lxa_html_tree_name_equals(token->name, "xmp", 3))) return LXA_ERROR_ARGUMENT;
    if (lxa_html_tree_name_equals(token->name, "xmp", 3)) {
        const lxa_html_template_control_frame_t *frames =
            (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
        size_t boundary = frames[count - 1].boundary, keep;
        status = lxa_html_control_stack_scope_find(context->stack, boundary,
            (lxa_span_t){(const uint8_t *) "p", 1}, LXA_HTML_SCOPE_BUTTON, &keep);
        if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
        bool group;
        if (status == LXA_OK)
            status = lxa_html_control_stack_column_group(context->stack, boundary, &group);
        if (status == LXA_OK && group)
            status = lxa_html_control_stack_pop_to(context->stack,
                lxa_vector_length(&context->stack->elements) - 1);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
        if (status != LXA_OK) return finish(context, status);
        status = lxa_html_template_context_reconstruct(context);
        if (status != LXA_OK) return status;
    }
    status = lxa_html_text_context_enter(text, tokenizer, token->name,
        LXA_TREE_TEMPLATE_TEXT, &context->limit_reason);
    if (status == LXA_OK) {
        lxa_html_template_control_frame_t *frames =
            (lxa_html_template_control_frame_t *) context->frames.bytes.data;
        status = lxa_html_tree_template_start_mode(frames[count - 1].insertion_mode,
            token->name, LXA_DOM_NS_HTML, &frames[count - 1].insertion_mode);
    }
    if (status == LXA_OK) *skip_newline = lxa_html_tree_name_equals(token->name, "textarea", 8);
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_start_plaintext(lxa_html_template_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (tokenizer == NULL || identity == NULL || lxa_vector_length(&context->frames) == 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "plaintext", 9)) return LXA_ERROR_ARGUMENT;
    bool foster;
    status = lxa_html_template_context_start_container(context, token, false, identity, &foster);
    if (status == LXA_OK && foster) status = internal();
    if (status == LXA_OK)
        status = lxa_html_tree_text_start_feedback(tokenizer, token->name, &context->limit_reason);
    if (status != LXA_OK) *identity = 0;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_start_void(lxa_html_template_context_t *context,
    const lxa_html_token_t *token)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0 || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || (!lxa_html_tree_head_void(token->name) && !lxa_html_tree_body_void(token->name)
            && !lxa_html_tree_name_equals(token->name, "image", 5))) return LXA_ERROR_ARGUMENT;
    if (lxa_html_tree_head_void(token->name)) return LXA_OK;
    if (lxa_html_tree_body_void_reconstructs(token->name)) {
        status = lxa_html_template_context_reconstruct(context);
        if (status != LXA_OK) return status;
    }
    lxa_html_template_control_frame_t *frames =
        (lxa_html_template_control_frame_t *) context->frames.bytes.data;
    if (lxa_html_tree_name_equals(token->name, "hr", 2)) {
        size_t keep;
        status = lxa_html_control_stack_scope_find(context->stack, frames[count - 1].boundary,
            (lxa_span_t){(const uint8_t *) "p", 1}, LXA_HTML_SCOPE_BUTTON, &keep);
        if (status == LXA_OK) status = lxa_html_control_stack_pop_to(context->stack, keep);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
        if (status != LXA_OK) return finish(context, status);
    }
    status = lxa_html_tree_template_start_mode(frames[count - 1].insertion_mode,
        token->name, LXA_DOM_NS_HTML, &frames[count - 1].insertion_mode);
    return finish(context, status);
}

static lxa_status_t
context_form_plan(lxa_html_template_context_t *context, lxa_html_table_context_plan_t *plan)
{
    size_t count = lxa_vector_length(&context->frames);
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_status_t status = lxa_html_control_stack_table_context_plan(context->stack,
        frames[count - 1].boundary,
        lxa_html_tree_template_table_context(frames[count - 1].insertion_mode), plan);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    return status;
}

#define LXA_TEMPLATE_FORM_NAME context_form_token
#define LXA_TEMPLATE_FORM_VIEW lxa_html_template_context_t
#define LXA_TEMPLATE_FORM_PLAN context_form_plan
#define LXA_TEMPLATE_FORM_INSERT(view, token, keep) ((void) (token), LXA_OK)
#define LXA_TEMPLATE_FORM_POP end_pop
#include "html/template_form_token.inc"
#undef LXA_TEMPLATE_FORM_NAME
#undef LXA_TEMPLATE_FORM_VIEW
#undef LXA_TEMPLATE_FORM_PLAN
#undef LXA_TEMPLATE_FORM_INSERT
#undef LXA_TEMPLATE_FORM_POP

lxa_status_t
lxa_html_template_context_form_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (handled == NULL || lxa_vector_length(&context->frames) == 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "form", 4)) return LXA_ERROR_ARGUMENT;
    return finish(context, context_form_token(context, token, handled));
}

lxa_status_t
lxa_html_template_context_table_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_table_action_t *action)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (action == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "table", 5)) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t boundary = frames[count - 1].boundary;
    lxa_html_table_context_plan_t plan;
    status = lxa_html_control_stack_table_context_plan(context->stack, boundary,
        lxa_html_tree_template_table_context(frames[count - 1].insertion_mode), &plan);
    size_t index = lxa_vector_length(&context->stack->elements);
    if (status == LXA_OK && plan.in_table)
        status = lxa_html_control_stack_scope_find(context->stack, boundary,
            token->name, LXA_HTML_SCOPE_TABLE, &index);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    if (status != LXA_OK) return finish(context, status);
    lxa_html_template_table_action_t result = LXA_HTML_TEMPLATE_TABLE_CONTINUE;
    if (plan.in_table) {
        if (index == lxa_vector_length(&context->stack->elements)) {
            status = lxa_html_control_stack_pop_to(context->stack, plan.keep);
            if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
            result = LXA_HTML_TEMPLATE_TABLE_IGNORE;
        }
        else {
            status = lxa_html_template_context_pop_table(context, index);
            result = LXA_HTML_TEMPLATE_TABLE_REPROCESS;
        }
    }
    if (status == LXA_OK) *action = result;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_current_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_current_route_t *route)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (route == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t boundary = frames[count - 1].boundary;
    size_t length = lxa_vector_length(&context->stack->elements);
    lxa_html_template_current_route_t result = {LXA_HTML_TEMPLATE_CURRENT_CONTINUE, false};
    if (length > boundary) {
        lxa_html_scope_entry_t entry;
        status = lxa_html_control_stack_get(context->stack, length - 1, &entry, NULL);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
        if (status == LXA_OK) {
            result.action = lxa_html_tree_template_current_action(entry.name,
                entry.namespace_id, length - 1 == boundary, token->kind, token->name);
            result.column_group = entry.namespace_id == LXA_DOM_NS_HTML
                && lxa_html_tree_name_equals(entry.name, "colgroup", 8);
        }
    }
    if (status == LXA_OK) *route = result;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_foster_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns, bool keep_open,
    uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (identity == NULL || lxa_vector_length(&context->frames) == 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || (unsigned) ns < LXA_DOM_NS_HTML || (unsigned) ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    bool formatting = ns == LXA_DOM_NS_HTML && lxa_html_tree_formatting_tag(token->name);
    bool marker = ns == LXA_DOM_NS_HTML && lxa_html_tree_formatting_marker_tag(token->name);
    if (marker && !lxa_html_tree_template_marker_container(token->name))
        return LXA_ERROR_ARGUMENT;
    if ((formatting || marker) && !keep_open) return LXA_ERROR_ARGUMENT;
    if (formatting)
        return lxa_html_template_context_start_formatting(context, token, identity);
    if (lxa_html_tree_foster_reconstructs(token->name, ns, keep_open))
        status = lxa_html_template_context_reconstruct(context);
    if (status != LXA_OK) return status;
    if (marker) {
        status = lxa_html_formatting_list_reserve_marker(context->formatting);
        if (status == LXA_ERROR_LIMIT) context->limit_reason = context->formatting->store->limit_reason;
        if (status == LXA_OK) status = lxa_html_template_context_admit_marker(context, token, identity);
    }
    else if (keep_open)
        status = lxa_html_template_context_push_token(context, token, ns, identity);
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_foster_text(lxa_html_template_context_t *context,
    lxa_html_text_context_t *text, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, uint64_t *identity, bool *skip_newline)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (tokenizer == NULL || identity == NULL || skip_newline == NULL
        || lxa_vector_length(&context->frames) == 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_template_foster_text_element(token->name))
        return LXA_ERROR_ARGUMENT;
    bool plaintext = lxa_html_tree_name_equals(token->name, "plaintext", 9);
    if (!plaintext && (text == NULL || text->plan.mode != LXA_HTML_DATA
        || text->plan.end_tag.data != NULL || text->plan.end_tag.length != 0))
        return LXA_ERROR_ARGUMENT;
    status = lxa_html_template_context_foster_token(context, token, LXA_DOM_NS_HTML,
        plaintext, identity);
    if (status == LXA_OK)
        status = plaintext
            ? lxa_html_tree_text_start_feedback(tokenizer, token->name, &context->limit_reason)
            : lxa_html_text_context_enter(text, tokenizer, token->name,
                LXA_TREE_TEMPLATE_TEXT, &context->limit_reason);
    if (status == LXA_OK)
        *skip_newline = lxa_html_tree_name_equals(token->name, "textarea", 8);
    else *identity = 0;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_end_table(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_table_end_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (result == NULL || lxa_vector_length(&context->frames) == 0
        || !lxa_html_tree_token_shape_valid(token) || token->kind != LXA_HTML_END_TAG)
        return LXA_ERROR_ARGUMENT;
    lxa_html_template_current_route_t route;
    status = lxa_html_template_context_current_token(context, token, &route);
    if (status != LXA_OK) return status;
    lxa_html_template_table_end_result_t out = LXA_HTML_TEMPLATE_TABLE_RESULT_CONTINUE;
    size_t length = lxa_vector_length(&context->stack->elements);
    if (route.action == LXA_HTML_TEMPLATE_CURRENT_REPROCESS_END) {
        status = end_pop(context, length - 1);
        out = LXA_HTML_TEMPLATE_TABLE_RESULT_REPROCESS;
    }
    else if (route.action == LXA_HTML_TEMPLATE_CURRENT_TABLE) {
        switch (lxa_html_tree_template_table_end_action(token->name, route.column_group)) {
            case LXA_HTML_TEMPLATE_TABLE_END_CONTINUE: break;
            case LXA_HTML_TEMPLATE_TABLE_END_IGNORE:
                out = LXA_HTML_TEMPLATE_TABLE_RESULT_IGNORE;
                break;
            case LXA_HTML_TEMPLATE_TABLE_END_POP_IGNORE:
                status = end_pop(context, length - 1);
                out = LXA_HTML_TEMPLATE_TABLE_RESULT_IGNORE;
                break;
            case LXA_HTML_TEMPLATE_TABLE_END_PARAGRAPH: {
                if (route.column_group) status = end_pop(context, length - 1);
                lxa_html_token_t adjusted = *token;
                adjusted.kind = LXA_HTML_START_TAG;
                uint64_t identity;
                if (status == LXA_OK)
                    status = lxa_html_template_context_foster_token(context, &adjusted,
                        LXA_DOM_NS_HTML, false, &identity);
                out = LXA_HTML_TEMPLATE_TABLE_RESULT_PARAGRAPH;
                break;
            }
            case LXA_HTML_TEMPLATE_TABLE_END_ORDINARY: {
                bool paragraph;
                status = lxa_html_template_context_end_body(context, token, &paragraph);
                out = LXA_HTML_TEMPLATE_TABLE_RESULT_IGNORE;
                break;
            }
            case LXA_HTML_TEMPLATE_TABLE_END_FORMATTING:
                status = lxa_budget_charge(context->stack->budget, LXA_BUDGET_WORK, 1,
                    &context->limit_reason);
                if (status == LXA_OK) status = end_pop(context, length - 1);
                out = LXA_HTML_TEMPLATE_TABLE_RESULT_REPROCESS;
                break;
        }
    }
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

typedef struct {
    lxa_html_template_context_t *context;
    lxa_html_template_table_text_result_t *result;
    lxa_buffer_t *pending;
    lxa_html_data_emit_t emit;
    void *user;
    lxa_html_table_text_emit_t table_emit;
} template_table_text_view_t;

static lxa_status_t
context_table_text_emit(template_table_text_view_t *view, lxa_span_t bytes, bool foster)
{
    if (bytes.length == 0 || view->table_emit == NULL) return LXA_OK;
    lxa_html_template_context_t *context = view->context;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t count = lxa_vector_length(&context->frames);
    lxa_html_scope_entry_t entry;
    lxa_status_t status = lxa_html_control_stack_current(context->stack,
        frames[count - 1].boundary, &entry, NULL);
    return status == LXA_OK ? view->table_emit(view->user, bytes, entry.identity, foster) : status;
}

static lxa_status_t
context_table_text_group(template_table_text_view_t *view, bool *group)
{
    lxa_html_template_context_t *context = view->context;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t count = lxa_vector_length(&context->frames);
    lxa_status_t status = lxa_html_control_stack_column_group(context->stack,
        frames[count - 1].boundary, group);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    if (status == LXA_OK && *group) {
        const lxa_html_control_element_t *entries =
            (const lxa_html_control_element_t *) context->stack->elements.bytes.data;
        view->result->column_group = entries[lxa_vector_length(&context->stack->elements) - 1].identity;
    }
    return status;
}

static lxa_status_t
context_table_text_append(template_table_text_view_t *view, lxa_span_t bytes, bool prefix)
{
    if (prefix) view->result->space_prefix = bytes.length;
    return context_table_text_emit(view, bytes, false);
}

static lxa_status_t
context_table_text_foster(template_table_text_view_t *view, lxa_span_t bytes)
{
    view->result->foster = true;
    return context_table_text_emit(view, bytes, true);
}

#define LXA_TABLE_TEXT_FLUSH_NAME context_flush_table_text
#define LXA_TABLE_TEXT_FLUSH_VIEW template_table_text_view_t
#define LXA_TABLE_TEXT_FLUSH_BUDGET(view) ((view)->context->stack->budget)
#define LXA_TABLE_TEXT_FLUSH_REASON(view) (&(view)->context->limit_reason)
#define LXA_TABLE_TEXT_FLUSH_APPEND context_table_text_append
#define LXA_TABLE_TEXT_FLUSH_GROUP context_table_text_group
#define LXA_TABLE_TEXT_FLUSH_PREFLIGHT(view) LXA_OK
#define LXA_TABLE_TEXT_FLUSH_POP(view) \
    end_pop((view)->context, lxa_vector_length(&(view)->context->stack->elements) - 1)
#define LXA_TABLE_TEXT_FLUSH_RECONSTRUCT(view) lxa_html_template_context_reconstruct((view)->context)
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
lxa_html_template_context_table_text(lxa_html_template_context_t *context,
    lxa_span_t data, lxa_html_table_text_emit_t emit, void *user,
    lxa_html_template_table_text_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (lxa_vector_length(&context->frames) == 0 || result == NULL
        || (data.length != 0 && data.data == NULL)) return LXA_ERROR_ARGUMENT;
    lxa_html_template_table_text_result_t out = {0};
    template_table_text_view_t view = {.context = context, .result = &out,
        .user = user, .table_emit = emit};
    status = context_flush_table_text(&view, data);
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

static lxa_status_t
context_table_data_null_flush(template_table_text_view_t *view, lxa_span_t pending)
{
    lxa_status_t status = lxa_budget_charge(view->context->stack->budget,
        LXA_BUDGET_WORK, pending.length, &view->context->limit_reason);
    if (status == LXA_OK && pending.length != 0 && view->emit != NULL)
        status = view->emit(view->user, pending, view->result->column_group);
    if (status == LXA_OK && view->pending != NULL) lxa_buffer_clear(view->pending);
    return status;
}

#define LXA_TABLE_DATA_NULL_NAME context_table_data_null
#define LXA_TABLE_DATA_NULL_VIEW template_table_text_view_t
#define LXA_TABLE_DATA_NULL_GROUP context_table_text_group
#define LXA_TABLE_DATA_NULL_FLUSH context_table_data_null_flush
#define LXA_TABLE_DATA_NULL_POP(view) \
    end_pop((view)->context, lxa_vector_length(&(view)->context->stack->elements) - 1)
#include "html/table_data_null.inc"
#undef LXA_TABLE_DATA_NULL_NAME
#undef LXA_TABLE_DATA_NULL_VIEW
#undef LXA_TABLE_DATA_NULL_GROUP
#undef LXA_TABLE_DATA_NULL_FLUSH
#undef LXA_TABLE_DATA_NULL_POP

lxa_status_t
lxa_html_template_context_table_data_null(lxa_html_template_context_t *context,
    lxa_span_t pending, uint64_t *column_group)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0 || column_group == NULL || (pending.length != 0 && pending.data == NULL))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_dom_namespace_t ns;
    status = lxa_html_control_stack_current_namespace(context->stack, frames[count - 1].boundary, &ns);
    if (status != LXA_OK) return finish(context, status);
    if (ns != LXA_DOM_NS_HTML) return LXA_ERROR_ARGUMENT;
    size_t length = lxa_vector_length(&context->stack->elements);
    lxa_html_template_table_text_result_t out = {0};
    template_table_text_view_t view = {.context = context, .result = &out};
    status = context_table_data_null(&view, pending);
    if (status == LXA_OK)
        *column_group = lxa_vector_length(&context->stack->elements) < length ? out.column_group : 0;
    return finish(context, status);
}

static lxa_status_t
context_table_data_append(template_table_text_view_t *view, lxa_span_t bytes, bool table)
{
    if (!table) return internal();
    return lxa_html_tree_queue_table_text(view->pending, bytes, &view->context->limit_reason);
}

#define LXA_DATA_EXECUTE_NAME context_execute_table_data
#define LXA_DATA_EXECUTE_VIEW template_table_text_view_t
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
lxa_html_template_context_table_data(lxa_html_template_context_t *context,
    lxa_span_t data, lxa_buffer_t *pending, lxa_html_data_emit_t emit, void *user)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0 || !lxa_html_tree_table_text_input_valid(pending, data)
        || pending->budget != context->stack->budget) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_dom_namespace_t ns;
    status = lxa_html_control_stack_current_namespace(context->stack, frames[count - 1].boundary, &ns);
    if (status != LXA_OK) return finish(context, status);
    if (ns != LXA_DOM_NS_HTML) return LXA_ERROR_ARGUMENT;
    lxa_html_template_table_text_result_t out = {0};
    template_table_text_view_t view = {.context = context, .result = &out,
        .pending = pending, .emit = emit, .user = user};
    return finish(context, context_execute_table_data(&view, data, false, true));
}

static lxa_status_t
current_entry(lxa_html_template_context_t *context, lxa_html_scope_entry_t *entry,
    lxa_html_foreign_context_t *integration)
{
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    return lxa_html_control_stack_current(context->stack,
        frames[lxa_vector_length(&context->frames) - 1].boundary, entry, integration);
}

lxa_status_t
lxa_html_template_context_current(lxa_html_template_context_t *context,
    lxa_html_scope_entry_t *entry, lxa_html_foreign_context_t *integration)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (entry == NULL || integration == NULL || lxa_vector_length(&context->frames) == 0)
        return LXA_ERROR_ARGUMENT;
    return finish(context, current_entry(context, entry, integration));
}

lxa_status_t
lxa_html_template_context_emit_data(lxa_html_template_context_t *context,
    lxa_span_t data, lxa_html_data_emit_t emit, void *user)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0 || (data.length != 0 && data.data == NULL)) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_html_scope_entry_t entry;
    lxa_html_foreign_context_t integration;
    status = current_entry(context, &entry, &integration);
    if (status != LXA_OK) return finish(context, status);
    if (entry.namespace_id == LXA_DOM_NS_HTML
        && (lxa_html_tree_table_context(entry.name) || (entry.identity == 0
            && lxa_html_tree_template_table_context(frames[count - 1].insertion_mode))))
        return LXA_ERROR_ARGUMENT;
    bool foreign = entry.namespace_id != LXA_DOM_NS_HTML && !lxa_html_foreign_integration(integration);
    const lxa_html_data_emitter_t emitter = {
        context->stack->budget, &context->limit_reason, entry.identity, emit, user
    };
    return finish(context, lxa_html_tree_emit_data(&emitter, data, foreign));
}

typedef struct {
    lxa_html_template_context_t *context;
    lxa_buffer_t *pending;
    lxa_html_data_emit_t emit;
    void *user;
    bool table;
} template_data_execute_view_t;

static lxa_status_t
context_template_data_emit(template_data_execute_view_t *view, lxa_span_t bytes, bool table)
{
    return table
        ? lxa_html_template_context_table_data(view->context, bytes, view->pending, view->emit, view->user)
        : lxa_html_template_context_emit_data(view->context, bytes, view->emit, view->user);
}

#define LXA_TEMPLATE_DATA_NAME context_execute_template_data
#define LXA_TEMPLATE_DATA_VIEW template_data_execute_view_t
#define LXA_TEMPLATE_DATA_VALID(view) ((void) (view), LXA_OK)
#define LXA_TEMPLATE_DATA_TABLE(view) ((view)->table)
#define LXA_TEMPLATE_DATA_RECONSTRUCT(view) lxa_html_template_context_reconstruct((view)->context)
#define LXA_TEMPLATE_DATA_EMIT context_template_data_emit
#include "html/template_data_execute.inc"
#undef LXA_TEMPLATE_DATA_NAME
#undef LXA_TEMPLATE_DATA_VIEW
#undef LXA_TEMPLATE_DATA_VALID
#undef LXA_TEMPLATE_DATA_TABLE
#undef LXA_TEMPLATE_DATA_RECONSTRUCT
#undef LXA_TEMPLATE_DATA_EMIT

lxa_status_t
lxa_html_template_context_data_token(lxa_html_template_context_t *context,
    lxa_span_t data, lxa_buffer_t *pending, lxa_html_data_emit_t emit, void *user)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0 || !lxa_html_tree_table_text_input_valid(pending, data)
        || pending->budget != context->stack->budget) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_html_scope_entry_t entry;
    lxa_html_foreign_context_t integration;
    status = lxa_html_control_stack_current(context->stack, frames[count - 1].boundary, &entry, &integration);
    if (status != LXA_OK) return finish(context, status);
    if ((entry.namespace_id != LXA_DOM_NS_HTML && !lxa_html_foreign_integration(integration))
        || (entry.namespace_id == LXA_DOM_NS_HTML && lxa_html_tree_table_context(entry.name)))
        return LXA_ERROR_ARGUMENT;
    bool table = entry.identity == 0
        && lxa_html_tree_template_table_context(frames[count - 1].insertion_mode);
    template_data_execute_view_t view = {context, pending, emit, user, table};
    return finish(context, context_execute_template_data(&view, data));
}

#define LXA_TEMPLATE_END_NAME context_end_effects
#define LXA_TEMPLATE_END_VIEW lxa_html_template_context_t
#define LXA_TEMPLATE_END_PLAN end_plan
#define LXA_TEMPLATE_END_POP_TABLE lxa_html_template_context_pop_table
#define LXA_TEMPLATE_END_POP end_pop
#define LXA_TEMPLATE_END_CLEAR end_clear
#include "html/template_end.inc"
#undef LXA_TEMPLATE_END_NAME
#undef LXA_TEMPLATE_END_VIEW
#undef LXA_TEMPLATE_END_PLAN
#undef LXA_TEMPLATE_END_POP_TABLE
#undef LXA_TEMPLATE_END_POP
#undef LXA_TEMPLATE_END_CLEAR

lxa_status_t
lxa_html_template_context_end_boundary(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled)
{
    if (handled != NULL) *handled = false;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    if (handled == NULL || lxa_vector_length(&context->frames) == 0
        || !lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    return finish(context, context_end_effects(context, token->kind, token->name, handled));
}

typedef struct {
    lxa_html_template_context_t *context;
    size_t boundary;
    lxa_html_template_mode_t mode;
    bool implied_paragraph;
} template_body_end_view_t;

static lxa_status_t
body_end_paragraph(template_body_end_view_t *view)
{
    view->implied_paragraph = true;
    return LXA_OK;
}

#define LXA_TEMPLATE_BODY_END_NAME context_body_end
#define LXA_TEMPLATE_BODY_END_VIEW template_body_end_view_t
#define LXA_TEMPLATE_BODY_END_LENGTH(view) lxa_vector_length(&(view)->context->stack->elements)
#define LXA_TEMPLATE_BODY_END_BOUNDARY(view) ((view)->boundary)
#define LXA_TEMPLATE_BODY_END_MODE(view) ((view)->mode)
#define LXA_TEMPLATE_BODY_END_SCOPED(view, name, scope, index) \
    lxa_html_control_stack_scope_find((view)->context->stack, (view)->boundary, name, scope, index)
#define LXA_TEMPLATE_BODY_END_ORDINARY(view, name, index) \
    lxa_html_control_stack_ordinary_end_find((view)->context->stack, (view)->boundary, name, index)
#define LXA_TEMPLATE_BODY_END_POP(view, index) end_pop((view)->context, index)
#define LXA_TEMPLATE_BODY_END_ADOPT(view, name) \
    lxa_html_formatting_list_adopt((view)->context->formatting, (view)->context->stack, (view)->boundary, name)
#define LXA_TEMPLATE_BODY_END_PARAGRAPH body_end_paragraph
#include "html/template_body_end.inc"
#undef LXA_TEMPLATE_BODY_END_NAME
#undef LXA_TEMPLATE_BODY_END_VIEW
#undef LXA_TEMPLATE_BODY_END_LENGTH
#undef LXA_TEMPLATE_BODY_END_BOUNDARY
#undef LXA_TEMPLATE_BODY_END_MODE
#undef LXA_TEMPLATE_BODY_END_SCOPED
#undef LXA_TEMPLATE_BODY_END_ORDINARY
#undef LXA_TEMPLATE_BODY_END_POP
#undef LXA_TEMPLATE_BODY_END_ADOPT
#undef LXA_TEMPLATE_BODY_END_PARAGRAPH

lxa_status_t
lxa_html_template_context_end_body(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *implied_paragraph)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (implied_paragraph == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_END_TAG) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    template_body_end_view_t view = {context, frames[count - 1].boundary,
        frames[count - 1].insertion_mode, false};
    status = context_body_end(&view, token->name);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT) {
        context->limit_reason = lxa_html_tree_formatting_tag(token->name)
            ? context->formatting->store->limit_reason : context->stack->limit_reason;
    }
    if (status == LXA_OK) *implied_paragraph = view.implied_paragraph;
    return finish(context, status);
}

static lxa_status_t
push_table_part(lxa_html_template_context_t *context, const lxa_html_token_t *token)
{
    uint64_t identity;
    return lxa_html_tree_formatting_marker_tag(token->name)
        ? lxa_html_template_context_admit_marker(context, token, &identity)
        : lxa_html_template_context_push_token(context, token, LXA_DOM_NS_HTML, &identity);
}

static lxa_status_t start_column(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted, bool prepared);

static lxa_status_t
start_table_part(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted, bool prepared)
{
    if (inserted != NULL) *inserted = false;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (inserted == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG || !lxa_html_tree_table_part(token->name)
        || lxa_html_tree_name_equals(token->name, "table", 5)) return LXA_ERROR_ARGUMENT;
    if (!prepared) {
        bool ignore;
        status = lxa_html_template_context_prepare_start(context, token, &ignore);
        if (status != LXA_OK || ignore) return status;
    }
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t boundary = frames[count - 1].boundary;
    lxa_html_template_mode_t mode = frames[count - 1].insertion_mode;
    if (mode == LXA_TEMPLATE_COLUMN_GROUP && lxa_html_tree_name_equals(token->name, "col", 3))
        return start_column(context, token, inserted, true);
    lxa_html_control_stack_t *stack = context->stack;
    lxa_html_template_table_start_plan_t plan;
    for (;;) {
        size_t length = lxa_vector_length(&stack->elements);
        status = lxa_html_control_stack_template_table_start_plan(stack, boundary,
            mode, token->name, &plan);
        if (status != LXA_OK) break;
        if (plan.action == LXA_HTML_TABLE_START_IGNORE) return LXA_OK;
        size_t keep = plan.insertion.keep;
        if (keep < boundary || keep > length) { status = internal(); break; }
        if (plan.action == LXA_HTML_TABLE_START_INSERT) {
            status = prepare_replacement(context, keep, plan.insertion.extra);
            if (status != LXA_OK) break;
            status = lxa_html_template_context_pop_table(context, keep);
            if (status != LXA_OK) break;
            lxa_html_token_t implied = {.kind = LXA_HTML_START_TAG};
            if (plan.insertion.implied_section) {
                implied.name = (lxa_span_t){(const uint8_t *) "tbody", 5};
                status = push_table_part(context, &implied);
            }
            if (status == LXA_OK && plan.insertion.implied_row) {
                implied.name = (lxa_span_t){(const uint8_t *) "tr", 2};
                status = push_table_part(context, &implied);
            }
            if (status == LXA_OK) {
                if (lxa_html_tree_name_equals(token->name, "col", 3)) {
                    implied.name = (lxa_span_t){(const uint8_t *) "colgroup", 8};
                    status = push_table_part(context, &implied);
                }
                else status = push_table_part(context, token);
            }
            if (status == LXA_OK) *inserted = true;
            break;
        }
        if ((plan.action != LXA_HTML_TABLE_START_POP_IGNORE
                && plan.action != LXA_HTML_TABLE_START_POP_REPROCESS) || keep == length) {
            status = internal();
            break;
        }
        status = plan.table_pop ? lxa_html_template_context_pop_table(context, keep)
            : lxa_html_control_stack_pop_to(stack, keep);
        if (status != LXA_OK || plan.action == LXA_HTML_TABLE_START_POP_IGNORE) break;
        /* Each reprocess removes at least one content entry. */
    }
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = stack->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_start_table_part(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted)
{
    return start_table_part(context, token, inserted, false);
}

lxa_status_t
lxa_html_template_context_start_table_part_prepared(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted)
{
    return start_table_part(context, token, inserted, true);
}

lxa_status_t
lxa_html_template_context_mathml_table_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_mathml_table_result_t *result)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (result == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG || !lxa_html_tree_table_part(token->name)
        || lxa_html_tree_name_equals(token->name, "table", 5)) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t boundary = frames[count - 1].boundary;
    if (boundary >= lxa_vector_length(&context->stack->elements)) return LXA_ERROR_ARGUMENT;
    lxa_html_template_mathml_table_action_t action;
    status = lxa_html_control_stack_template_mathml_table_plan(context->stack,
        boundary, frames[count - 1].insertion_mode, token->name, &action);
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    lxa_html_template_mathml_table_result_t out = {false, false};
    if (status == LXA_OK) {
        switch (action) {
        case LXA_HTML_TEMPLATE_MATHML_TABLE_CONTINUE:
            break;
        case LXA_HTML_TEMPLATE_MATHML_TABLE_PART:
            status = lxa_html_tree_name_equals(token->name, "col", 3)
                ? lxa_html_template_context_start_column(context, token, &out.inserted)
                : lxa_html_template_context_start_table_part(context, token, &out.inserted);
            out.handled = true;
            break;
        case LXA_HTML_TEMPLATE_MATHML_TABLE_POP_IGNORE:
            status = lxa_html_control_stack_pop_to(context->stack, boundary);
            if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
            out.handled = true;
            break;
        default:
            status = internal();
        }
    }
    if (status == LXA_OK) *result = out;
    return finish(context, status);
}

static lxa_status_t
start_column(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted, bool prepared)
{
    if (inserted != NULL) *inserted = false;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (inserted == NULL || count == 0 || !lxa_html_tree_token_shape_valid(token)
        || token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "col", 3)) return LXA_ERROR_ARGUMENT;
    if (!prepared) {
        bool ignore;
        status = lxa_html_template_context_prepare_start(context, token, &ignore);
        if (status != LXA_OK || ignore) return status;
    }
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t boundary = frames[count - 1].boundary;
    lxa_html_template_mode_t mode = frames[count - 1].insertion_mode;
    for (;;) {
        lxa_html_column_start_plan_t plan;
        status = lxa_html_control_stack_column_start_plan(context->stack, boundary, mode, &plan);
        if (status != LXA_OK) break;
        switch (plan.action) {
            case LXA_HTML_COLUMN_START_VIRTUAL:
                *inserted = true;
                return LXA_OK;
            case LXA_HTML_COLUMN_START_IGNORE: return LXA_OK;
            case LXA_HTML_COLUMN_START_POP_IGNORE:
                return finish(context, end_pop(context, plan.keep));
            case LXA_HTML_COLUMN_START_POP_REPROCESS:
                if (plan.keep < boundary
                    || plan.keep >= lxa_vector_length(&context->stack->elements))
                    return finish(context, internal());
                status = lxa_html_template_context_pop_table(context, plan.keep);
                if (status != LXA_OK) return status;
                continue;
            case LXA_HTML_COLUMN_START_TABLE_PART:
                return start_table_part(context, token, inserted, true);
            case LXA_HTML_COLUMN_START_APPEND:
            case LXA_HTML_COLUMN_START_IMPLY_GROUP: break;
            default: return finish(context, internal());
        }
        bool group = plan.action == LXA_HTML_COLUMN_START_IMPLY_GROUP;
        status = check_depth(context, context->root_length, context->return_mode, count,
            group ? 2 : 1);
        if (status == LXA_OK && group)
            status = lxa_budget_check(context->stack->budget, LXA_BUDGET_OPEN_ELEMENTS,
                1, &context->limit_reason);
        if (status == LXA_OK && group) {
            size_t required;
            status = lxa_size_add(lxa_vector_length(&context->stack->elements), 1, &required);
            if (status == LXA_OK) status = lxa_vector_reserve(&context->stack->elements, required);
            if (status == LXA_ERROR_LIMIT) context->limit_reason = LXA_BUDGET_MEMORY;
        }
        if (status == LXA_OK && group) {
            lxa_html_token_t implied = {.kind = LXA_HTML_START_TAG,
                .name = {(const uint8_t *) "colgroup", 8}};
            bool admitted;
            status = start_table_part(context, &implied, &admitted, true);
            if (status == LXA_OK && !admitted) status = internal();
        }
        if (status == LXA_OK) *inserted = true;
        break;
    }
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->stack->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_start_column(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted)
{
    return start_column(context, token, inserted, false);
}

lxa_status_t
lxa_html_template_context_start_column_prepared(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted)
{
    return start_column(context, token, inserted, true);
}

lxa_status_t
lxa_html_template_context_reconstruct(lxa_html_template_context_t *context)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    size_t boundary = frames[count - 1].boundary, start;
    status = lxa_html_formatting_list_reconstruct_plan(context->formatting, context->stack,
        boundary, &start);
    if (status == LXA_ERROR_LIMIT)
        context->limit_reason = context->formatting->store->limit_reason;
    if (status != LXA_OK) return finish(context, status);
    size_t length = lxa_vector_length(&context->formatting->entries);
    if (start == length) return LXA_OK;
    if (start > length) return finish(context, internal());
    status = check_depth(context, context->root_length, context->return_mode, count, length - start);
    if (status == LXA_OK)
        status = lxa_html_formatting_list_reconstruct_planned(context->formatting, context->stack,
            boundary, start);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->formatting->store->limit_reason;
    return finish(context, status);
}

typedef struct {
    lxa_html_template_context_t *context;
    size_t boundary;
} template_start_view_t;

static lxa_status_t
start_stack_status(const template_start_view_t *view, lxa_status_t status)
{
    if (status == LXA_ERROR_LIMIT)
        view->context->limit_reason = view->context->stack->limit_reason;
    return status;
}

static lxa_status_t
start_identity(const template_start_view_t *view, size_t index, uint64_t *identity)
{
    lxa_html_formatting_item_t item;
    lxa_status_t status = lxa_html_formatting_list_get(view->context->formatting, index, &item);
    if (status == LXA_OK) *identity = item.identity;
    return status;
}

#define LXA_FORMATTING_START_NAME context_prepare_start
#define LXA_FORMATTING_START_VIEW template_start_view_t
#define LXA_FORMATTING_START_REFERENCE uint64_t
#define LXA_FORMATTING_START_FIND(view, name, index) \
    lxa_html_formatting_list_find((view)->context->formatting, name, index)
#define LXA_FORMATTING_START_IDENTITY start_identity
#define LXA_FORMATTING_START_ADOPT(view, name) \
    lxa_html_formatting_list_adopt((view)->context->formatting, (view)->context->stack, (view)->boundary, name)
#define LXA_FORMATTING_START_FIND_REFERENCE(view, identity, index) \
    lxa_html_formatting_list_find_reference((view)->context->formatting, identity, index)
#define LXA_FORMATTING_START_FORMATTING_LENGTH(view) lxa_vector_length(&(view)->context->formatting->entries)
#define LXA_FORMATTING_START_REMOVE_FORMATTING(view, index) \
    lxa_html_formatting_list_remove((view)->context->formatting, index)
#define LXA_FORMATTING_START_OPEN(view, identity, index) start_stack_status(view, \
    lxa_html_control_stack_open_reference_find((view)->context->stack, (view)->boundary, identity, index))
#define LXA_FORMATTING_START_OPEN_LENGTH(view) lxa_vector_length(&(view)->context->stack->elements)
#define LXA_FORMATTING_START_REMOVE_OPEN(view, index) start_stack_status(view, \
    lxa_html_control_stack_remove((view)->context->stack, index))
#define LXA_FORMATTING_START_RECONSTRUCT(view) lxa_html_template_context_reconstruct((view)->context)
#define LXA_FORMATTING_START_SCOPED(view, name, index) start_stack_status(view, \
    lxa_html_control_stack_scope_find((view)->context->stack, (view)->boundary, name, LXA_HTML_SCOPE_NORMAL, index))
#define LXA_FORMATTING_START_PLAN(view, token, drop) \
    lxa_html_formatting_list_plan_add((view)->context->formatting, (view)->context->stack, (view)->boundary, token, drop)
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
lxa_html_template_context_start_formatting(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (identity == NULL || count == 0) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    template_start_view_t view = {context, frames[count - 1].boundary};
    status = lxa_html_formatting_list_start_intake(context->formatting, context->stack,
        view.boundary, token);
    if (status == LXA_ERROR_ARGUMENT) return status;
    size_t drop;
    if (status == LXA_OK) status = context_prepare_start(&view, token, &drop);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->formatting->store->limit_reason;
    if (status == LXA_OK)
        status = check_depth(context, context->root_length, context->return_mode, count, 1);
    if (status == LXA_OK)
        status = lxa_html_formatting_list_admit_planned(context->formatting, context->stack,
            token, drop, identity);
    if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
        context->limit_reason = context->formatting->store->limit_reason;
    return finish(context, status);
}

lxa_status_t
lxa_html_template_context_enter(lxa_html_template_context_t *context,
    lxa_html_root_mode_t mode, bool has_patch)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    size_t length = lxa_vector_length(&context->stack->elements);
    if (has_patch || (count == 0 ? !lxa_html_tree_template_return_mode_valid(mode)
                                : mode != LXA_TREE_TEMPLATE_TEXT))
        return LXA_ERROR_ARGUMENT;
    size_t root = count == 0 ? length : context->root_length;
    lxa_html_root_mode_t returning = count == 0 ? mode : context->return_mode;
    size_t required;
    status = lxa_size_add(count, 1, &required);
    if (status == LXA_OK)
        status = check_depth(context, root, returning, required, 0);
    if (status == LXA_OK) status = lxa_vector_reserve(&context->frames, required);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT && context->limit_reason == LXA_BUDGET_COUNT)
            context->limit_reason = LXA_BUDGET_MEMORY;
        return finish(context, status);
    }
    uint64_t identity;
    static const uint8_t name[] = "template";
    status = lxa_html_formatting_list_admit_marker(context->formatting, context->stack,
        (lxa_span_t){name, sizeof(name) - 1}, &identity);
    if (status == LXA_ERROR_LIMIT)
        context->limit_reason = context->formatting->store->limit_reason;
    if (status != LXA_OK) return finish(context, status);
    lxa_html_template_control_frame_t frame = {
        lxa_vector_length(&context->stack->elements), identity, LXA_TEMPLATE_INITIAL
    };
    status = lxa_vector_push(&context->frames, &frame);
    if (status != LXA_OK) return finish(context, internal());
    context->root_length = root;
    context->return_mode = returning;
    return LXA_OK;
}

typedef struct {
    lxa_html_template_context_t *context;
    size_t count;
    lxa_html_root_mode_t *mode;
} template_close_view_t;

static lxa_status_t
close_plan(const template_close_view_t *view, size_t boundary,
    bool explicit_end, bool has_patch, size_t *keep)
{
    lxa_status_t status = lxa_html_control_stack_template_close_plan(view->context->stack,
        boundary, explicit_end, has_patch, keep);
    if (status == LXA_ERROR_LIMIT)
        view->context->limit_reason = view->context->stack->limit_reason;
    return status;
}

static lxa_status_t
close_pop(const template_close_view_t *view, size_t keep)
{
    lxa_status_t status = lxa_html_control_stack_pop_to(view->context->stack, keep);
    if (status == LXA_ERROR_LIMIT)
        view->context->limit_reason = view->context->stack->limit_reason;
    return status;
}

static lxa_status_t
close_clear(const template_close_view_t *view)
{
    lxa_status_t status = lxa_html_formatting_list_clear(view->context->formatting);
    if (status == LXA_ERROR_LIMIT)
        view->context->limit_reason = view->context->formatting->store->limit_reason;
    return status;
}

static lxa_status_t
close_publish(const template_close_view_t *view)
{
    lxa_html_template_context_t *context = view->context;
    if (lxa_vector_erase(&context->frames, view->count - 1, 1) != LXA_OK) return internal();
    *view->mode = view->count == 1 ? context->return_mode : LXA_TREE_TEMPLATE_TEXT;
    if (view->count == 1) {
        context->root_length = 0;
        context->return_mode = LXA_TREE_INITIAL;
    }
    return LXA_OK;
}

#define LXA_TEMPLATE_CLOSE_NAME context_template_close
#define LXA_TEMPLATE_CLOSE_VIEW template_close_view_t
#define LXA_TEMPLATE_CLOSE_PLAN close_plan
#define LXA_TEMPLATE_CLOSE_POP close_pop
#define LXA_TEMPLATE_CLOSE_PATCH(view) internal()
#define LXA_TEMPLATE_CLOSE_CLEAR close_clear
#define LXA_TEMPLATE_CLOSE_RELEASE(view, boundary) close_pop(view, (boundary) - 1)
#define LXA_TEMPLATE_CLOSE_PUBLISH close_publish
#include "html/template_close.inc"
#undef LXA_TEMPLATE_CLOSE_NAME
#undef LXA_TEMPLATE_CLOSE_VIEW
#undef LXA_TEMPLATE_CLOSE_PLAN
#undef LXA_TEMPLATE_CLOSE_POP
#undef LXA_TEMPLATE_CLOSE_PATCH
#undef LXA_TEMPLATE_CLOSE_CLEAR
#undef LXA_TEMPLATE_CLOSE_RELEASE
#undef LXA_TEMPLATE_CLOSE_PUBLISH

lxa_status_t
lxa_html_template_context_leave(lxa_html_template_context_t *context,
    bool explicit_end, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0 || mode == NULL) return LXA_ERROR_ARGUMENT;
    const lxa_html_template_control_frame_t *frames =
        (const lxa_html_template_control_frame_t *) context->frames.bytes.data;
    lxa_html_template_control_frame_t frame = frames[count - 1];
    size_t length = lxa_vector_length(&context->stack->elements);
    if (!lxa_html_tree_template_credit_valid(length, frame.boundary,
            context->stack->budget->used[LXA_BUDGET_OPEN_ELEMENTS]))
        return finish(context, internal());
    lxa_html_scope_entry_t owner;
    status = lxa_html_control_stack_get(context->stack, frame.boundary - 1, &owner, NULL);
    if (status == LXA_OK && (owner.identity != frame.identity
        || owner.namespace_id != LXA_DOM_NS_HTML
        || !lxa_html_tree_name_equals(owner.name, "template", 8)))
        status = internal();
    if (status == LXA_ERROR_LIMIT) context->limit_reason = context->stack->limit_reason;
    if (status != LXA_OK) return finish(context, status);
    const template_close_view_t view = {context, count, mode};
    return finish(context, context_template_close(&view, frame.boundary, explicit_end, false));
}

lxa_status_t
lxa_html_template_context_finish(lxa_html_template_context_t *context,
    lxa_html_root_mode_t *mode)
{
    lxa_status_t status = begin(context);
    if (status != LXA_OK) return status;
    size_t count = lxa_vector_length(&context->frames);
    if (count == 0 || mode == NULL) return LXA_ERROR_ARGUMENT;
    size_t length = lxa_vector_length(&context->stack->elements);
    /* Each metadata template owner replaces one canonical virtual-frame slot. */
    status = lxa_budget_charge(context->stack->budget, LXA_BUDGET_WORK,
        length - context->root_length, &context->limit_reason);
    if (status != LXA_OK) return finish(context, status);
    lxa_html_root_mode_t restored = context->return_mode;
    while (lxa_vector_length(&context->frames) != 0) {
        status = lxa_html_template_context_leave(context, false, &restored);
        if (status != LXA_OK) return status;
    }
    *mode = restored;
    return LXA_OK;
}
