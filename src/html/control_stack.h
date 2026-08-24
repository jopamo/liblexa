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
lxa_status_t lxa_html_control_stack_pop_to(lxa_html_control_stack_t *stack,
    size_t keep);

/* Returned names expire on mutation/destroy. Queries retain no views.
 * This owner stores no attributes, parent/child links or DOM records.
 */
lxa_status_t lxa_html_control_stack_get(lxa_html_control_stack_t *stack,
    size_t index, lxa_html_scope_entry_t *entry,
    lxa_html_foreign_context_t *context);
lxa_status_t lxa_html_control_stack_scope_find(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, lxa_html_scope_kind_t scope, size_t *index);

#endif
