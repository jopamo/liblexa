#ifndef LIBLEXA_HTML_CONTROL_STACK_H
#define LIBLEXA_HTML_CONTROL_STACK_H

#include "html/control_scope.h"
#include "html/foreign_context.h"

typedef struct {
    size_t name_offset, name_length;
    uint64_t identity;
    lxa_dom_namespace_t namespace_id;
    bool annotation_html;
} lxa_html_control_element_t;

typedef struct {
    lxa_vector_t elements;
    lxa_buffer_t names;
    lxa_budget_t *budget;
    size_t charged_name_bytes;
    uint64_t next_identity;
    lxa_limit_reason_t limit_reason;
} lxa_html_control_stack_t;

/* Initialize fresh caller-owned storage. The allocator context and budget
 * outlive destroy. Single-thread confined; separate stacks may share one
 * operation budget when callers serialize access.
 */
lxa_status_t lxa_html_control_stack_init(lxa_html_control_stack_t *stack,
    const lxa_allocator_t *allocator, lxa_budget_t *budget);
void lxa_html_control_stack_destroy(lxa_html_control_stack_t *stack);

/* Copy a normalized HTML or adjusted foreign name and snapshot annotation
 * integration eligibility. Names may alias this stack's live name storage.
 * Failure preserves live elements and identities, but may retain capacity.
 * All mutation attempts invalidate borrowed name views. Identities are
 * nonzero and never reused within an initialized stack, including after pop.
 */
lxa_status_t lxa_html_control_stack_push(lxa_html_control_stack_t *stack,
    lxa_span_t name, lxa_dom_namespace_t ns, bool annotation_html,
    uint64_t *identity);
/* Capture an effective start token after the caller decides to keep it open.
 * Adjust SVG names and resolve MathML annotation encoding without retaining
 * attribute views. This does not choose namespace, insertion or text modes.
 */
lxa_status_t lxa_html_control_stack_push_token(lxa_html_control_stack_t *stack,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns, uint64_t *identity);
lxa_status_t lxa_html_control_stack_pop_to(lxa_html_control_stack_t *stack,
    size_t keep);
/* Remove one entry without changing surviving identities or name offsets.
 * Charge one WORK unit per removed/shifted metadata slot before mutation.
 * Interior name bytes remain until suffix trimming or destruction; suffix
 * removal truncates to the last retained name, or zero for an empty stack.
 */
lxa_status_t lxa_html_control_stack_remove(lxa_html_control_stack_t *stack,
    size_t index);

/* Returned names expire on mutation/destroy. Queries retain no views.
 * This owner stores no attributes, parent/child links or DOM records.
 */
lxa_status_t lxa_html_control_stack_get(lxa_html_control_stack_t *stack,
    size_t index, lxa_html_scope_entry_t *entry,
    lxa_html_foreign_context_t *context);
lxa_status_t lxa_html_control_stack_scope_find(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, lxa_html_scope_kind_t scope, size_t *index);
lxa_status_t lxa_html_control_stack_ordinary_end_find(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, size_t *index);
lxa_status_t lxa_html_control_stack_open_reference_find(lxa_html_control_stack_t *stack,
    size_t boundary, uint64_t identity, size_t *index);
lxa_status_t lxa_html_control_stack_scoped_reference_find(lxa_html_control_stack_t *stack,
    size_t boundary, uint64_t identity, size_t *index);
/* Failed planning leaves the result unchanged, as with borrowed-stack plans. */
lxa_status_t lxa_html_control_stack_scope_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, lxa_html_scope_kind_t scope,
    lxa_html_end_target_plan_t *out);
lxa_status_t lxa_html_control_stack_ordinary_end_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, lxa_html_end_target_plan_t *out);
/* Retained-length plans share the borrowed implied/ruby rules and publish
 * only on success. Apply with pop_to after any other required preflight.
 */
lxa_status_t lxa_html_control_stack_implied_end_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t except, size_t *out);
lxa_status_t lxa_html_control_stack_ruby_start_plan(lxa_html_control_stack_t *stack,
    size_t boundary, bool except_rtc, size_t *out);
/* Foreign-end plans adjust the token's normalized name separately for each
 * visited namespace. Reprocess at HTML or the virtual boundary without popping.
 * Breakout retains the nearest HTML/integration entry. Both publish only on
 * success; callers still decide dispatch and perform stack mutation.
 */
lxa_status_t lxa_html_control_stack_foreign_end_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t name, lxa_html_foreign_end_plan_t *out);
lxa_status_t lxa_html_control_stack_foreign_breakout_plan(lxa_html_control_stack_t *stack,
    size_t boundary, size_t *out);
/* Same dispatch-order prerequisite and publication contract as borrowed
 * select plans. This does not choose insertion modes or inspect attributes.
 */
lxa_status_t lxa_html_control_stack_select_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_html_select_plan_t *out);
lxa_status_t lxa_html_control_stack_table_context_plan(lxa_html_control_stack_t *stack,
    size_t boundary, bool virtual_table, lxa_html_table_context_plan_t *out);

#endif
