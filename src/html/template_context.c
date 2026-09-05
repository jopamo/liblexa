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

static lxa_status_t
check_depth(lxa_html_template_context_t *context, size_t root,
    lxa_html_root_mode_t returning, size_t templates, size_t extra)
{
    size_t count = lxa_vector_length(&context->frames);
    size_t length = lxa_vector_length(&context->stack->elements);
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
