#ifndef LIBLEXA_HTML_TEMPLATE_CONTEXT_H
#define LIBLEXA_HTML_TEMPLATE_CONTEXT_H

#include "html/formatting_list.h"

typedef struct {
    size_t boundary; /* first content entry, after the template owner */
    uint64_t identity;
    lxa_html_template_mode_t insertion_mode;
} lxa_html_template_control_frame_t;

typedef struct {
    lxa_vector_t frames; /* engines update only the active insertion_mode */
    lxa_html_control_stack_t *stack;
    lxa_html_formatting_list_t *formatting;
    size_t root_length;
    lxa_html_root_mode_t return_mode;
    lxa_status_t failure;
    lxa_limit_reason_t limit_reason;
} lxa_html_template_context_t;

/* Single-thread-confined owner of unpatched virtual-template frames. Borrowed
 * stack, formatting list, allocator context and budget outlive destroy.
 * Stack template entries own OPEN_ELEMENTS credits; list markers own ACTIVE
 * charges. Destroy frees only frames, then callers destroy list/stack owners.
 * Engines must not remove/move retained entries across frame boundaries.
 */
lxa_status_t lxa_html_template_context_init(lxa_html_template_context_t *context,
    lxa_html_control_stack_t *stack, lxa_html_formatting_list_t *formatting);
void lxa_html_template_context_destroy(lxa_html_template_context_t *context);

/* Admit an accepted template after namespace/recovery dispatch. Patched
 * contexts are unsupported and rejected before effects. Reserve frames before
 * marker/stack admission, publish only on success. Failures after validation
 * latch the context; retained capacity/WORK and completed effects belong to
 * the borrowed owners until destruction. Engines publish TEMPLATE_TEXT.
 */
lxa_status_t lxa_html_template_context_enter(lxa_html_template_context_t *context,
    lxa_html_root_mode_t mode, bool has_patch);
/* Preflight retained content plus extra elements using this owner's geometry.
 * No allocation or live quota charge. Engines use this before coordinated
 * formatting, marker or implied-element admission; it reserves no capacity.
 */
lxa_status_t lxa_html_template_context_depth_check(lxa_html_template_context_t *context,
    size_t extra);
/* Copy an accepted open start into the active content after depth preflight.
 * Validate the effective token before effects; preserve SVG adjustments and
 * MathML annotation eligibility. This performs only the stack effect, not
 * formatting/marker admission, dispatch or tokenizer feedback. Clear identity
 * on failure; its stable storage must not alias context or borrowed owners.
 */
lxa_status_t lxa_html_template_context_push_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns, uint64_t *identity);
/* Admit an accepted HTML cell/caption/object-family marker owner after depth
 * preflight. Validate attributes but retain none. Nested templates use enter.
 * The name and identity output must not alias any owner storage. Clear
 * identity on failure.
 */
lxa_status_t lxa_html_template_context_admit_marker(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, uint64_t *identity);
/* Admit a prepared effective HTML formatting start after depth preflight.
 * Reuse snapshot capture, Noah admission and rollback without retaining token
 * views. Engines own anchor/nobr preprocessing and reconstruction scheduling.
 * Clear identity on failure; output must not alias any owner storage.
 */
lxa_status_t lxa_html_template_context_admit_formatting(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, uint64_t *identity);
/* Coordinate an accepted formatting start, including anchor/nobr recovery and
 * context-depth-checked reconstruction. Consume one Noah plan. Token views
 * and output must not alias owner storage. Failures latch completed effects.
 */
lxa_status_t lxa_html_template_context_start_formatting(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, uint64_t *identity);
/* Apply shared template table recovery only within the active content slice.
 * Retained boundaries cannot remove template owners or enclosing content.
 * Engines publish insertion-mode changes after successful effects.
 */
lxa_status_t lxa_html_template_context_pop_table(lxa_html_template_context_t *context,
    size_t keep);
/* Apply an accepted HTML table-part start (not table). Share retained-boundary
 * plans, preflight replacement depth/OPEN and reserve slots before INSERT pops,
 * then admit implied section/row/group and marker owners. Col retains only its
 * implied group. Report false for ignored starts and failures. Token views and
 * output must not alias owner storage. Engines retain mode publication.
 */
lxa_status_t lxa_html_template_context_start_table_part(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted);
/* Plan reconstruction once, preflight the entire closed suffix's depth, then
 * reopen it from immutable snapshots. No recapture or live formatting charge.
 * Failure latches completed effects; engines own reconstruction scheduling.
 */
lxa_status_t lxa_html_template_context_reconstruct(lxa_html_template_context_t *context);
/* Close the active unpatched frame: plan, pop content, clear the newest marker,
 * pop its template owner, then publish the enclosing mode. EOF skips planning
 * scans. Failure preserves output and latches any completed effects. Output
 * storage must not alias context or borrowed owners.
 */
lxa_status_t lxa_html_template_context_leave(lxa_html_template_context_t *context,
    bool explicit_end, lxa_html_root_mode_t *mode);

#endif
