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
/* Reserve a unique key for an accepted closed owner without open membership
 * or name storage. Charge one WORK before publication; clear output on failure.
 * Keys share the open/replacement sequence and are never reused while live.
 * Output must not alias stack headers/storage.
 */
lxa_status_t lxa_html_control_stack_claim_identity(lxa_html_control_stack_t *stack,
    uint64_t *identity);
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
/* Replace an expected identity with a fresh key while retaining its metadata.
 * Charge one WORK before reading the entry. Failure preserves the entry and
 * key sequence; clear output on failure. No allocation or live-quota change.
 */
lxa_status_t lxa_html_control_stack_replace_identity(lxa_html_control_stack_t *stack,
    size_t index, uint64_t expected, uint64_t *identity);
/* Move an existing entry to its final index, preserving identities and metadata.
 * Rotate owned name bytes with the entries so suffix trimming remains valid.
 * Precharge one WORK per inspected slot (the moved interval and its immediate
 * neighbors), then ceil(rotated bytes / 8). No-op movement costs no WORK.
 * Failure preserves entries, names and key assignment; no allocation.
 */
lxa_status_t lxa_html_control_stack_move(lxa_html_control_stack_t *stack,
    size_t from, size_t to);

/* Returned names expire on mutation/destroy. Queries retain no views.
 * This owner stores no attributes, parent/child links or DOM records.
 */
lxa_status_t lxa_html_control_stack_get(lxa_html_control_stack_t *stack,
    size_t index, lxa_html_scope_entry_t *entry,
    lxa_html_foreign_context_t *context);
/* Post-effect namespace feedback over the active slice. An exhausted slice
 * is virtual HTML, not the entry below its boundary. No WORK or allocation;
 * publish only on success. Integration eligibility does not change namespace.
 */
lxa_status_t lxa_html_control_stack_current_namespace(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_dom_namespace_t *out);
/* Apply post-effect CDATA feedback over the same active slice. An exhausted
 * slice publishes HTML even when the entry below it is foreign. Engines skip
 * this after EOF and own failure latching. No WORK, allocation or stack mutation.
 */
lxa_status_t lxa_html_control_stack_namespace_feedback(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_html_tokenizer_t *tokenizer);
/* Test the current active entry for an HTML colgroup without WORK or mutation.
 * Exhausted slices exclude entries below boundary. Publish only on success.
 */
lxa_status_t lxa_html_control_stack_column_group(lxa_html_control_stack_t *stack,
    size_t boundary, bool *out);
lxa_status_t lxa_html_control_stack_scope_find(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, lxa_html_scope_kind_t scope, size_t *index);
lxa_status_t lxa_html_control_stack_ordinary_end_find(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t target, size_t *index);
lxa_status_t lxa_html_control_stack_open_reference_find(lxa_html_control_stack_t *stack,
    size_t boundary, uint64_t identity, size_t *index);
/* First special element at/after start, or stack length. Charge one WORK per
 * visited name/namespace pair; publish only on success.
 */
lxa_status_t lxa_html_control_stack_special_find(lxa_html_control_stack_t *stack,
    size_t start, size_t *index);
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
/* Plan accepted template-container recovery after formatting preparation.
 * Share ruby/list/paragraph/heading ordering and retained-table foster choice.
 * No membership effects or admission; publish only on success.
 */
lxa_status_t lxa_html_control_stack_template_container_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_span_t name, bool quirks, lxa_html_template_container_plan_t *out);
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
/* Accepted root table start after current-name classification. Skip lookup
 * when current_context is true; otherwise charge one WORK per entry above
 * boundary, stopping at an HTML table or cell/caption. Publish only on success.
 * The engine owns scoped closure, reservation, insertion and failure latching.
 */
lxa_status_t lxa_html_control_stack_root_table_close_plan(lxa_html_control_stack_t *stack,
    size_t boundary, bool current_context, bool *out);
/* Same precharge/publication contract as borrowed missing-table recovery.
 * Leave HTML cells and entries before boundary open; the engine owns effects.
 */
lxa_status_t lxa_html_control_stack_template_missing_table_plan(
    lxa_html_control_stack_t *stack, size_t boundary, size_t *out);
/* Same dispatch prerequisite, action and publication contract as borrowed
 * template end plans; no mutation, allocation or formatting effects.
 */
lxa_status_t lxa_html_control_stack_template_end_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_html_template_end_plan_t *out);
lxa_status_t lxa_html_control_stack_template_table_scan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_html_template_table_scan_t *out);
lxa_status_t lxa_html_control_stack_fostered_suffix_check(lxa_html_control_stack_t *stack,
    size_t start, bool first_cell);
lxa_status_t lxa_html_control_stack_virtual_table_suffix_check(lxa_html_control_stack_t *stack,
    size_t boundary, bool section_root);
lxa_status_t lxa_html_control_stack_virtual_table_item_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_html_template_mode_t mode, lxa_span_t target,
    lxa_html_table_start_plan_t *out);
lxa_status_t lxa_html_control_stack_template_table_start_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_html_template_mode_t mode, lxa_span_t target,
    lxa_html_template_table_start_plan_t *out);
lxa_status_t lxa_html_control_stack_template_close_plan(lxa_html_control_stack_t *stack,
    size_t boundary, bool explicit_end, bool has_patch, size_t *out);
/* Count popped HTML cell/caption markers. Precharge length - keep WORK,
 * then scan the suffix. Publish only on success; engines own pops and clears.
 */
lxa_status_t lxa_html_control_stack_table_markers_plan(lxa_html_control_stack_t *stack,
    size_t keep, size_t *out);
lxa_status_t lxa_html_control_stack_column_start_plan(lxa_html_control_stack_t *stack,
    size_t boundary, lxa_html_template_mode_t mode, lxa_html_column_start_plan_t *out);
lxa_status_t lxa_html_control_stack_form_end_plan(lxa_html_control_stack_t *stack,
    uint64_t identity, lxa_html_form_end_plan_t *out);
/* Execute an accepted root form end. Borrow a stable, separate owner field
 * in this stack's identity domain; it must not alias stack headers/storage.
 * Clear a found owner before pops/removal, an absent owner after successful
 * optional pops. Failure retains completed effects; abort rather than replay.
 * Ordinary descendants retain identities. No formatting or DOM effects.
 */
lxa_status_t lxa_html_control_stack_end_form(lxa_html_control_stack_t *stack,
    uint64_t *owner);
/* Execute an accepted body/html end in IN_BODY. The stable mode field must
 * not alias stack headers/storage. Pop the excluded column group before
 * ignoring an absent body or publishing AFTER_BODY/AFTER_HTML. Retain ordinary
 * descendants. Failed planning/pops leave the mode unchanged.
 */
lxa_status_t lxa_html_control_stack_end_body(lxa_html_control_stack_t *stack,
    bool html_end, lxa_html_root_mode_t *mode);
/* Same match, stop, WORK and publication rules as borrowed list-item plans.
 * Return stack length when absent. No mutation or formatting/DOM effects.
 */
lxa_status_t lxa_html_control_stack_list_item_plan(lxa_html_control_stack_t *stack,
    size_t boundary, bool definition, size_t *out);

#endif
