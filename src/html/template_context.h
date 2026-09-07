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
/* Apply a prepared retained-length replacement within active content. Check
 * replacement depth/net OPEN growth and reserve slots before ordinary pops,
 * then copy accepted token metadata. This performs stack effects only, not
 * formatting/marker cleanup or dispatch. Later capture failure latches the
 * completed pops. Clear identity on failure. Token views/output must not alias
 * any owner storage.
 */
lxa_status_t lxa_html_template_context_replace_token(lxa_html_template_context_t *context,
    size_t keep, const lxa_html_token_t *token, lxa_dom_namespace_t ns, uint64_t *identity);
/* Coordinate an accepted ordinary HTML container after earlier foreign,
 * select/table/group/text rules. Button starts close a scoped button before
 * reconstruction. Formatting/marker starts use their
 * separate coordinators. Reconstruct, recover retained content and admit
 * metadata; report foster insertion on success. Normal success publishes
 * initial-template mode. Engines own DOM actions, newline and text feedback.
 * Clear identity on failure; preserve foster output. Outputs require distinct
 * stable storage outside token views/owners. Token views must not alias owners.
 * Failures latch completed reconstruction/pops.
 */
lxa_status_t lxa_html_template_context_start_container(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool quirks, uint64_t *identity, bool *foster);
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
/* Apply table/marker end dispatch after foreign handling. Share plan-before-
 * effects ordering, table cleanup or ordinary pops followed by one marker
 * clear. Publish handled only on success; failure latches completed effects.
 * Token views and output must not alias owner storage. Template closure and
 * ordinary/formatting ends remain separate engine rules.
 */
lxa_status_t lxa_html_template_context_end_boundary(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled);
/* Apply the final template in-body end rules after foreign/select/table/group
 * recovery and template-close dispatch. Share scoped/list/ordinary matching
 * and formatting adoption. Report a missing paragraph's closed insertion
 * action without retaining metadata or charging node quotas. Output changes
 * only on success; token views/output must not alias owner storage.
 */
lxa_status_t lxa_html_template_context_end_body(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *implied_paragraph);
/* Apply an accepted HTML table-part start (not table). Share retained-boundary
 * plans, preflight replacement depth/OPEN and reserve slots before INSERT pops,
 * then admit implied section/row/group and marker owners. Col retains only its
 * implied group. Report false for ignored starts and failures. Token views and
 * output must not alias owner storage. Engines retain mode publication.
 */
lxa_status_t lxa_html_template_context_start_table_part(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted);
/* Route an accepted HTML col start through virtual/current/implied groups.
 * Append-only columns retain no metadata or OPEN charge; group admission uses
 * table-part effects. Preserve canonical transient-column depth checks.
 * Token views and output must not alias owner storage. Engines publish modes.
 */
lxa_status_t lxa_html_template_context_start_column(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted);
/* Apply an already accepted SVG/MathML start, including self-closing pops.
 * Publish initial-template mode only after successful effects. Foreign ends
 * use adjusted-name matching and publish reprocess only after successful pops;
 * breakout retains the nearest HTML/integration entry in the active slice.
 * Engines own dispatch eligibility and post-effect tokenizer feedback.
 * Token views and outputs must not alias owner storage. Failures latch effects.
 */
lxa_status_t lxa_html_template_context_start_foreign(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns);
lxa_status_t lxa_html_template_context_end_foreign(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *reprocess);
lxa_status_t lxa_html_template_context_breakout(lxa_html_template_context_t *context);
/* Apply select-token recovery after earlier foreign/end and column-group
 * dispatch. Plan within active content, then apply ordinary pops. Publish
 * ignore only on success; failures latch completed effects. Engines retain
 * continued-token dispatch and insertion-mode publication. Token views/output
 * must not alias context or borrowed owners.
 */
lxa_status_t lxa_html_template_context_select_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *ignore);
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
/* Precharge EOF recovery WORK for all templates/content, then close every
 * unpatched frame through leave. Publish root mode only after complete unwind;
 * later failure retains closed frames/effects and latches. Mode storage must
 * not alias context or borrowed owners. Inactive contexts are rejected.
 */
lxa_status_t lxa_html_template_context_finish(lxa_html_template_context_t *context,
    lxa_html_root_mode_t *mode);

#endif
