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
/* Publish post-effect CDATA feedback over active content, or the root stack
 * when no frame is active. Exhausted slices resolve to virtual HTML without
 * reading enclosing entries. No WORK/allocation/membership effects. Latch
 * feedback failure; engines retain EOF exclusion and whole-token finalization.
 */
lxa_status_t lxa_html_template_context_namespace_feedback(lxa_html_template_context_t *context,
    lxa_html_tokenizer_t *tokenizer);
/* Borrow the current active entry/integration after owner validation. Empty
 * content publishes virtual HTML, excluding enclosing entries. No WORK or
 * membership effects. Publish only on success; names expire on mutation.
 * Outputs cannot alias owners or each other.
 */
lxa_status_t lxa_html_template_context_current(lxa_html_template_context_t *context,
    lxa_html_scope_entry_t *entry, lxa_html_foreign_context_t *integration);

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
/* Coordinate an accepted ordinary object/marquee/applet start after earlier
 * foreign/table/foster dispatch. Reconstruct, reserve marker membership, then
 * depth-check/admit and publish initial mode. Clear identity on failure and
 * latch completed reconstruction. Views/output must not alias owners.
 */
lxa_status_t lxa_html_template_context_start_marker(lxa_html_template_context_t *context,
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
 * output must not alias owner storage. Publish initial table-part mode before
 * planning; BODY starts without a table in scope are ignored. Later failure
 * retains that early mode change. Engines retain earlier dispatch precedence.
 */
lxa_status_t lxa_html_template_context_start_table_part(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted);
/* Route an accepted HTML col start through virtual/current/implied groups.
 * Append-only columns retain no metadata or OPEN charge; group admission uses
 * table-part effects. Preserve canonical transient-column depth checks.
 * Token views and output must not alias owner storage. Apply initial mode and
 * BODY table-in-scope gating once before effects, including delegated admission.
 */
lxa_status_t lxa_html_template_context_start_column(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted);
/* Admit after earlier initial-mode/BODY table-scope preparation and routing.
 * Do not repeat preparation or its scope WORK. Otherwise use the same column/
 * table-part admission, output and terminal-failure contracts as above.
 * Serialized callers must preserve the prepared phase, not replay it.
 */
lxa_status_t lxa_html_template_context_start_table_part_prepared(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted);
lxa_status_t lxa_html_template_context_start_column_prepared(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *inserted);
typedef struct {
    bool handled, inserted;
} lxa_html_template_mathml_table_result_t;
/* Execute accepted MathML integration-point table-part routing before ordinary
 * HTML dispatch. Plan over nonempty active content, then delegate column/part
 * admission or pop a direct virtual row without table-marker cleanup. CONTINUE
 * changes no membership/mode. Publish only on success; latch completed effects
 * on failure without replay. Earlier dispatch establishes integration; token/
 * result cannot alias owners. No DOM-node charge or enclosing-frame access.
 */
lxa_status_t lxa_html_template_context_mathml_table_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_mathml_table_result_t *result);
/* Apply an already accepted SVG/MathML start, including self-closing pops.
 * Publish initial-template mode only after successful effects.
 * Breakout retains the nearest HTML/integration entry in the active slice.
 * Engines own dispatch eligibility and post-effect tokenizer feedback.
 * Token views and outputs must not alias owner storage. Failures latch effects.
 */
lxa_status_t lxa_html_template_context_start_foreign(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns);
/* Route HTML-path SVG/MathML after earlier foreign/select/form dispatch.
 * Empty active slices are virtual HTML; otherwise charge one current read.
 * Eligible HTML/integration parents reconstruct formatting before admission.
 * Table/nonintegrating parents continue without effects. Publish handled only
 * on success; retain reconstruction/admission and latch later failures.
 * Token/output must not alias owners.
 */
lxa_status_t lxa_html_template_context_html_foreign_start(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled);
lxa_status_t lxa_html_template_context_breakout(lxa_html_template_context_t *context);
/* Route template ends before BR conversion and HTML end dispatch. Empty/HTML
 * content continues without effects. Foreign br/p breaks out; other names use
 * adjusted matching and pop only on a match. Unmatched ends retain membership
 * and request HTML processing. Publish handled only on success, preserving mode
 * and enclosing frames. Latch failures without replay; token/output storage must
 * not alias owners. Other intake and template closure remain engine-owned.
 */
lxa_status_t lxa_html_template_context_dispatch_foreign_end(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled);
/* Apply select-token recovery after earlier foreign/end and column-group
 * dispatch. Plan within active content, then apply ordinary pops. Publish
 * ignore only on success; failures latch completed effects. Engines retain
 * continued-token dispatch and insertion-mode publication. Token views/output
 * must not alias context or borrowed owners.
 */
lxa_status_t lxa_html_template_context_select_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *ignore);
/* Prepare accepted HTML starts after foreign/end dispatch. Ignore body-control
 * starts after non-html current-colgroup recovery and successful mode publication.
 * Publish initial table-part mode before BODY table-scope gating; ignore BODY
 * parts without a table in scope. Other starts continue without reads/effects.
 * Publish handled only on success; latch completed effects without replay.
 * Retain no token views or new metadata. Views/output cannot alias owners.
 */
lxa_status_t lxa_html_template_context_prepare_start(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled);
/* Gate virtual column-group tokens after earlier foreign/end/table/control
 * dispatch. Read current content only in column-group mode, charging one WORK.
 * For SPACE_TEXT, borrow an empty initialized same-budget spaces buffer, filter
 * the full input, then emit nonempty spaces under the active parent (zero for
 * virtual content). NULL emit discards; other actions permit NULL spaces.
 * Publish action only after successful delivery. Callback views expire on
 * return; do not retain, reenter or mutate owners. Latch scan/allocation/callback
 * failures without replay; retain filtered bytes for caller cleanup. Borrower
 * argument rejection may retain the current-entry WORK charge. Input/output/
 * spaces must not alias owners or each other. Engines continue HTML dispatch.
 */
lxa_status_t lxa_html_template_context_column_group_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_buffer_t *spaces,
    lxa_html_data_emit_t emit, void *user, lxa_html_column_group_action_t *action);
/* Enter an accepted returning HTML text element after earlier dispatch. Borrow
 * an inactive text context and tokenizer using the same budget. Retain no
 * open metadata. Commit feedback/text context before template mode and newline
 * output; feedback failure latches. Engines publish TEXT and handle text/end/
 * EOF. Xmp first closes a scoped paragraph/current HTML colgroup and reconstructs
 * formatting; failed feedback retains those effects. Plaintext is separate.
 * Views/outputs must not alias owners.
 */
lxa_status_t lxa_html_template_context_start_text(lxa_html_template_context_t *context,
    lxa_html_text_context_t *text, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, bool *skip_newline);
/* Admit an accepted plaintext start with ordinary paragraph recovery, then
 * apply nonreturning tokenizer feedback. Feedback failure retains admitted
 * metadata/mode, clears identity and latches. Tokenizer uses the same budget;
 * token views/output must not alias owners. Engines retain earlier dispatch.
 */
lxa_status_t lxa_html_template_context_start_plaintext(lxa_html_template_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token, uint64_t *identity);
/* Apply an accepted HTML void/image start after earlier dispatch. Share
 * reconstruction exclusions and hr paragraph recovery, retain no new open
 * metadata, and publish initial mode after effects. Failures latch completed
 * recovery. Engines own insertion, image normalization and meta observation.
 * Token views must not alias owners.
 */
lxa_status_t lxa_html_template_context_start_void(lxa_html_template_context_t *context,
    const lxa_html_token_t *token);
/* Route an accepted HTML form start after select recovery. Table-context forms
 * report a closed insertion after ordinary colgroup pops, with no new metadata
 * or mode publication. Other forms continue without admission. Publish handled
 * only on success. Engines own insertion and earlier dispatch.
 * Token views/outputs must not alias owners or each other.
 */
lxa_status_t lxa_html_template_context_form_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, bool *handled);
typedef enum {
    LXA_HTML_TEMPLATE_TABLE_CONTINUE,
    LXA_HTML_TEMPLATE_TABLE_IGNORE,
    LXA_HTML_TEMPLATE_TABLE_REPROCESS
} lxa_html_template_table_action_t;
/* Recover an accepted table start after select/form/foreign routing. Virtual
 * tables apply ordinary retained-length pops and ignore; actual tables apply
 * table-pop effects and request full token reprocessing. Otherwise continue
 * remaining dispatch without admission. Publish action only on success; failures
 * latch. Token views/output must not alias owners.
 */
lxa_status_t lxa_html_template_context_table_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_table_action_t *action);
typedef struct {
    lxa_html_template_current_action_t action;
    bool column_group;
} lxa_html_template_current_route_t;
/* Read current active content once after earlier dispatch, charging one WORK.
 * Empty content excludes outer entries and costs no WORK. Publish only on
 * success; latch failures without mutation. Engines consume the copied route
 * before changing membership, then own preflight, pops and reprocessing.
 * Token/output storage must not alias owners.
 */
lxa_status_t lxa_html_template_context_current_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_current_route_t *route);
/* Admit an accepted foster start after earlier routing/colgroup pops. Preserve
 * insertion mode; prepare formatting or reconstruct by the shared schedule.
 * Closed insertion retains no new metadata. Open insertion uses existing
 * formatting/marker/token admission. Clear identity on failure and latch
 * completed effects. Engines own insertion and text feedback. Views/output
 * must not alias owners.
 */
lxa_status_t lxa_html_template_context_foster_token(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_dom_namespace_t ns, bool keep_open,
    uint64_t *identity);
/* Coordinate accepted foster text admission and feedback without publishing
 * template mode. Returning elements borrow an inactive text context; plaintext
 * retains open metadata and uses nonreturning feedback. Later feedback failure
 * retains admission/reconstruction, clears identity and latches. Publish newline
 * output only on success. Tokenizer shares the budget; views/outputs must not
 * alias owners or each other. Engines own insertion and TEXT publication.
 */
lxa_status_t lxa_html_template_context_foster_text(lxa_html_template_context_t *context,
    lxa_html_text_context_t *text, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, uint64_t *identity, bool *skip_newline);
typedef enum {
    LXA_HTML_TEMPLATE_TABLE_RESULT_CONTINUE,
    LXA_HTML_TEMPLATE_TABLE_RESULT_IGNORE,
    LXA_HTML_TEMPLATE_TABLE_RESULT_PARAGRAPH,
    LXA_HTML_TEMPLATE_TABLE_RESULT_REPROCESS
} lxa_html_template_table_end_result_t;
/* Route an end through current actual-table rules after earlier dispatch.
 * Read active content once, apply colgroup/ordinary pops, or report a closed
 * foster paragraph. Preserve mode and publish only on success; latch failures.
 * Engines own paragraph insertion and full reprocessing. Views/output must
 * not alias owners.
 */
lxa_status_t lxa_html_template_context_end_table(lxa_html_template_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_table_end_result_t *result);
typedef struct {
    size_t space_prefix;
    uint64_t column_group; /* removed owner for the prefix, zero when absent */
    bool foster;
} lxa_html_template_table_text_result_t;
/* Flush accepted pending table text before processing the next token. Borrow
 * bytes without retaining/copying them; precharge their WORK before recovery.
 * Null/space-only text leaves metadata unchanged. Mixed text pops a current
 * HTML colgroup and reconstructs formatting; report its leading space prefix
 * and removed owner identity plus foster rule only on success. Failures latch
 * completed effects.
 * Deliver space text/prefixes before pops, then mixed text after reconstruction
 * through the borrowed callback. NULL emit discards. Views expire on return;
 * do not retain them, reenter or mutate owners. Callback failures latch without
 * result publication or replay. Engines clear pending storage only after success.
 * Input/output storage must not alias owners or each other.
 */
lxa_status_t lxa_html_template_context_table_text(lxa_html_template_context_t *context,
    lxa_span_t data, lxa_html_table_text_emit_t emit, void *user,
    lxa_html_template_table_text_result_t *result);
/* First NUL in accepted HTML table DATA after queueing its preceding run.
 * Flush whitespace WORK before current-colgroup pop, excluding entries below
 * the active frame boundary. Pending nonspace waits for ordinary flush.
 * Publish removed group key or zero only on success; latch partial failure.
 * Engines own emission under that key and pending clearing before reuse.
 * Call once per DATA span; input/output must not alias owners or each other.
 */
lxa_status_t lxa_html_template_context_table_data_null(lxa_html_template_context_t *context,
    lxa_span_t pending, uint64_t *column_group);
/* Execute accepted HTML table DATA within the active frame over same-budget
 * pending storage. Share complete-input WORK, non-NUL queueing and first-NUL
 * recovery. Emit whitespace before clearing/pop; NULL emit discards it.
 * Callback views expire on return; do not retain them, reenter or mutate
 * context/stack/list/pending. Callback/queue failures latch completed effects.
 * Nonspace remains pending for later flush. Input must not alias storage or
 * owners; engines retain earlier routing and later flush scheduling.
 */
lxa_status_t lxa_html_template_context_table_data(lxa_html_template_context_t *context,
    lxa_span_t data, lxa_buffer_t *pending, lxa_html_data_emit_t emit, void *user);
/* Emit accepted non-table DATA after earlier gating/reconstruction.
 * Resolve suppression/replacement through the active entry's saved integration;
 * key zero denotes the active frame's virtual content, never an entry below
 * its boundary. Share complete-input WORK without insertion/NODE charges.
 * NULL emit discards runs; callback views expire on return. Do not retain them,
 * reenter or mutate context/stack/list. Input must not alias owners.
 * Failures latch emitted prefixes; modes/membership remain unchanged.
 * Engines retain earlier preparation, table routing and delivery.
 */
lxa_status_t lxa_html_template_context_emit_data(lxa_html_template_context_t *context,
    lxa_span_t data, lxa_html_data_emit_t emit, void *user);
/* Accepted HTML DATA after active-current/foreign routing, excluding actual
 * table entries and nonintegrated foreign content handled by earlier dispatch.
 * Borrow same-budget initialized pending storage before effects. Ignore empty/
 * NUL-only input; queue virtual-table content, otherwise reconstruct then emit.
 * Retain completed reconstruction/queue/emission effects on terminal failure.
 * NULL emit discards ordinary runs. Callback spans expire on return; do not
 * retain them, reenter or mutate owners. Input/storage must not alias owners.
 */
lxa_status_t lxa_html_template_context_data_token(lxa_html_template_context_t *context,
    lxa_span_t data, lxa_buffer_t *pending, lxa_html_data_emit_t emit, void *user);
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
