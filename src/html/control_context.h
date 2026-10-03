#ifndef LIBLEXA_HTML_CONTROL_CONTEXT_H
#define LIBLEXA_HTML_CONTROL_CONTEXT_H

#include "html/root_context.h"
#include "html/template_context.h"

typedef struct {
    lxa_html_control_stack_t stack;
    lxa_html_formatting_store_t snapshots;
    lxa_html_formatting_list_t formatting;
    lxa_html_root_context_t root;
    lxa_html_template_context_t templates;
    lxa_html_text_context_t text;
    lxa_buffer_t pending_table_text, spaces;
    lxa_html_root_mode_t mode;
    lxa_html_document_mode_t document_mode;
    uint64_t form_owner;
    bool pending_newline;
    lxa_status_t failure;
    lxa_limit_reason_t limit_reason;
} lxa_html_control_context_t;

/* Address-stable, single-thread-confined owner of control-only parser state.
 * Initialize fresh storage; destroy before reuse. Borrowed budget and allocator
 * context outlive destruction and must not alias this owner. No tokenizer or
 * input views are owned here. Destroy releases live metadata/scratch credits,
 * not cumulative WORK. Failed init leaves safely destructible empty storage.
 */
lxa_status_t lxa_html_control_context_init(lxa_html_control_context_t *context,
    const lxa_allocator_t *allocator, lxa_budget_t *budget);
void lxa_html_control_context_destroy(lxa_html_control_context_t *context);
typedef struct {
    lxa_html_text_emit_t text_emit;
    lxa_html_table_text_emit_t table_emit;
    void *user;
} lxa_html_control_intake_sink_t;
typedef struct {
    bool handled;
    lxa_html_token_t token;
} lxa_html_control_intake_result_t;
/* Run initial/TEXT/flush/newline intake before template/frameset/foreign routing.
 * Initial comments/whitespace consume without metadata. Defer initial DOCTYPE
 * effects to their late dispatch point; publish quirks fallback before continuing.
 * EOF restores TEXT before flushing and continues for later completion. Other
 * TEXT tokens consume after flushing/newline handling. Strip one pending LF from
 * the borrowed DATA view, never from caller bytes. NULL sinks discard delivery.
 * Publish handled and the borrowed adjusted token only after success. Callback
 * views and result token views expire with caller input; do not retain, reenter
 * or mutate owners/sink. Token/sink/result must not alias owners. Latch failure
 * without replay, retaining completed restoration/flush/newline effects.
 */
lxa_status_t lxa_html_control_context_intake(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, const lxa_html_control_intake_sink_t *sink,
    lxa_html_control_intake_result_t *result);
/* Run head-noscript scheduling after early intake/template/EOF/frameset routing.
 * Other accepted root modes pass through without WORK. Emit spaces before mode
 * closure; consume ignored/closed tokens and borrow any remaining DATA suffix.
 * NULL emit discards spaces. Callback/result views expire with caller input;
 * do not retain, reenter or mutate owners. Token/result must not alias owners.
 * Publish handled/token only on success; latch execution failure without replay.
 * This phase does not execute the remaining root rules or namespace feedback.
 */
lxa_status_t lxa_html_control_context_head_noscript_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_prebody_emit_t emit, void *user,
    lxa_html_control_intake_result_t *result);
/* Consume non-EOF frameset-family intake after early TEXT/flush/newline handling.
 * Use owned formatting/text/scratch; require a tokenizer only for noframes.
 * Deliver nonempty filtered spaces after reconstruction, then clear scratch
 * only on success. NULL emit discards. Callback spans expire on return; do not
 * retain, reenter or mutate owners. Token/result must not alias owned storage.
 * Publish results only on success; latch failures and retain completed effects/
 * scratch for destruction without replay. EOF uses the separate finish phase.
 */
lxa_status_t lxa_html_control_context_frameset_token(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_text_emit_t emit, void *user, lxa_html_root_frameset_result_t *result);
/* Gate active-template column-group intake after earlier foreign/end/control/
 * table routing. Other insertion modes continue without WORK. Use owned scratch
 * for filtered spaces; clear it only after successful delivery/discard. Key zero
 * denotes active virtual content, never an enclosing entry. NULL emit discards.
 * Callback spans expire on return; do not retain, reenter or mutate owners.
 * Token/result must not alias owners or scratch. Publish action only on success;
 * latch failure and retain scratch/effects for destruction without replay.
 * This phase neither admits continuing starts nor finalizes tokenizer feedback.
 */
lxa_status_t lxa_html_control_context_template_column_group_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_data_emit_t emit, void *user,
    lxa_html_column_group_action_t *result);
/* Execute accepted template HTML DATA after earlier foreign/column-group/select
 * routing. Consume one current-entry plan, then use owned pending storage for
 * actual/virtual table DATA or reconstruct/deliver ordinary DATA. NULL emit
 * discards ordinary runs, not required table buffering. Input cannot alias
 * owners or pending storage. Callback spans expire on return; do not retain,
 * reenter or mutate owners. Latch failure without replay, retaining completed
 * queue/delivery/pop/reconstruction effects for later destruction. The driver
 * still flushes pending text and finalizes namespace feedback.
 */
lxa_status_t lxa_html_control_context_template_data_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_data_emit_t emit, void *user);
/* Execute an accepted template HTML text start after earlier routing. Foster
 * selects the saved table route, not a second current-entry query. Publish TEXT
 * only after returning-text feedback succeeds; plaintext keeps TEMPLATE_TEXT
 * and admitted metadata. Set pending newline only for successful textarea.
 * Identity is zero for returning text, otherwise the admitted plaintext key.
 * Publish identity only on success. Latch feedback/admission failure, retaining
 * completed recovery for teardown without replay. Tokenizer shares the budget;
 * token/output must not alias owners. Intake handles later text/end/EOF.
 */
lxa_status_t lxa_html_control_context_template_text_start(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool foster, uint64_t *identity);
typedef struct {
    lxa_html_token_t token;
    bool handled, implied_paragraph, foster;
} lxa_html_control_template_end_result_t;
/* Coordinate accepted template END after intake has flushed pending text and
 * cleared newline state. Order foreign/BR/closure/boundary, column group,
 * select and current-table/body recovery as in the canonical driver. Bound
 * reprocessing by completed content pops. Only converted BR continues as START;
 * its views remain borrowed and attributes/self-closing state are cleared.
 * Report closed implied-paragraph insertion and foster placement without NODE
 * charges or retained paragraph metadata. Publish only on success; latch
 * completed effects on failure without replay. Token/result cannot alias
 * owners. Later START execution, insertion and namespace feedback are separate.
 */
lxa_status_t lxa_html_control_context_template_end_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_control_template_end_result_t *result);
/* Prepare template HTML START after misc/foreign/end routing and intake.
 * Order body-control/initial-part/BODY-scope preparation, column-group gating,
 * hidden table input and select recovery as in the canonical driver. Hidden
 * inputs retain no metadata/NODE charge. Publish handled only after success;
 * retain completed mode/pops and latch failure without replay. Prior intake
 * flushes pending bytes and clears newline state. Token/output cannot alias
 * owners. Continuing form/foreign/table/current/ordinary execution is separate;
 * subsequent part/column execution consumes this prepared phase exactly once.
 */
lxa_status_t lxa_html_control_context_template_start_prelude(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled);
/* Route an accepted form after START prelude. Closed table forms retain no
 * metadata/NODE charge; ordinary forms continue through later current-element
 * routing before admission. Prior intake clears pending bytes/newline state.
 * Publish handled only on success; latch failure without replay.
 */
lxa_status_t lxa_html_control_context_template_form_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled);
/* Route SVG/MathML on the continuing HTML path after prelude/form dispatch.
 * Apply eligible-parent reconstruction/admission, not inherited foreign starts.
 * Prior intake clears pending bytes/newline state. Publish handled only on
 * success; latch completed effects on failure without replay.
 */
lxa_status_t lxa_html_control_context_template_html_foreign_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled);
/* Recover table START after prelude/form/ordinary-foreign/empty-column routing.
 * Virtual tables ignore; actual table cleanup requests full token reprocessing
 * and strictly reduces active length. Retain no new metadata/NODE charge.
 * Prior intake clears pending bytes/newline state. Publish action only on
 * success; latch retained cleanup on failure without replay.
 */
lxa_status_t lxa_html_control_context_template_table_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_table_action_t *action);
/* Schedule continuing START after intake/misc/foreign/end dispatch. Order
 * prelude, form, HTML-path foreign, empty-content column and nested-table routes.
 * CONSUMED completes an earlier rule; REPROCESS requires full earlier dispatch
 * after a strictly shrinking table pop. CONTINUE proceeds to current-entry
 * routing and remaining admission. Publish only on success; latch failures
 * without replay. Prior intake clears pending bytes/newline state; token/output
 * must not alias owners.
 */
lxa_status_t lxa_html_control_context_template_start_route(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_start_action_t *action);
/* Continue START after early routing and current-entry dispatch. Select column
 * or non-table part admission without repeating mode/scope preparation. A
 * handled result includes grammar-defined ignores. Publish only on success;
 * latch retained effects on failure. Prior intake clears pending bytes/newline.
 */
lxa_status_t lxa_html_control_context_template_part_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, bool *handled);
typedef struct {
    lxa_html_template_start_action_t action;
    lxa_html_token_t token;
    uint64_t identity;
    bool foster;
} lxa_html_control_template_table_start_result_t;
/* Execute current-template HTML table START after earlier foreign/control/
 * initial-mode/BODY-scope/group/select/form/nested-table routing and intake.
 * Read current content once; publish CONTINUE or completed colgroup REPROCESS
 * when selected. Table rules share canonical effect ordering, including text
 * feedback, foster starts, image normalization and prepared part/column admission.
 * Require a same-budget tokenizer only for selected text routes. Publish output
 * only on success; latch failed effects without replay. Borrow token views until
 * caller input expires; token/result cannot alias owners. Later continuing HTML
 * execution and namespace feedback remain separate.
 */
lxa_status_t lxa_html_control_context_template_table_start(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_control_template_table_start_result_t *result);
/* Bind accepted MathML integration-point table-part execution to this owner.
 * Earlier routing establishes integration/nonempty content and flushes pending
 * text. Publish handled/inserted only on success; latch failures without replay.
 * Shape/empty-content argument rejection preserves state.
 * Token/result cannot alias owners. Continuing HTML dispatch and namespace
 * feedback remain separate.
 */
lxa_status_t lxa_html_control_context_template_mathml_table_token(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_template_mathml_table_result_t *result);
/* Route template START/DATA after early intake/misc handling. Exhausted active
 * slices continue as virtual HTML. Use saved integration for DATA, annotation
 * SVG and MathML text exceptions, table parts and conditional font breakout.
 * REPROCESS follows completed breakout pops; reroute the unchanged token.
 * NULL emit discards ordinary runs, not required table buffering. Callback
 * spans expire on return; do not retain, reenter or mutate owners. Token/output
 * cannot alias owners/pending storage. Publish action only on success; latch
 * failed scans/admission/delivery without replay. Later HTML/end dispatch,
 * namespace feedback and encoding observation remain driver-owned.
 */
lxa_status_t lxa_html_control_context_template_foreign_dispatch(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_data_emit_t emit, void *user,
    lxa_html_template_foreign_action_t *result);
/* Execute accepted ordinary head/body void or image START after earlier routing.
 * Share reconstruction, hr recovery and image normalization without retained
 * closed metadata/NODE charge. Publish borrowed adjusted token only on success;
 * latch failed effects without replay. Prior intake clears pending bytes/newline
 * state. Token/result must not alias owners or each other; views expire with input.
 * Encoding observation and namespace feedback remain driver-owned.
 */
lxa_status_t lxa_html_control_context_template_void_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_token_t *result);
typedef struct {
    uint64_t identity;
    bool foster;
} lxa_html_control_template_start_result_t;
/* Admit an accepted ordinary template HTML start after earlier foreign/select/
 * table/text routing. Dispatch formatting and object-family markers through
 * owned membership coordinators; publish initial formatting mode after admission.
 * Containers bind quirks recovery to document_mode. Arm pre/listing newline only
 * after successful non-foster admission. Publish identity/foster only on success
 * and latch completed recovery on failure without replay. Unsupported arguments
 * preserve state but may charge classification WORK. Token/result cannot alias
 * owners. Text starts and later feedback remain separate.
 */
lxa_status_t lxa_html_control_context_template_ordinary_start(lxa_html_control_context_t *context,
    const lxa_html_token_t *token, lxa_html_control_template_start_result_t *result);
typedef struct {
    lxa_html_template_html_start_action_t action;
    lxa_html_token_t token;
    uint64_t identity;
    bool foster;
} lxa_html_control_template_html_start_result_t;
/* Execute remaining START after early/current-entry/part routing and intake.
 * Share template, closed and returning-text precedence; bind continuing
 * plaintext/ordinary admission to the owned coordinators. The caller supplies
 * patch state: reject has_patch before effects, without inferring attribute
 * semantics. Nested unpatched entry publishes its owner identity after success.
 * Require a tokenizer for text starts. Result tokens/attributes remain borrowed;
 * only image changes name. Publish only on success; latch retained effects on
 * execution failure. Pending bytes/newline must be clear. Views/results must
 * not alias owners. Encoding observation and final feedback remain driver-owned.
 */
lxa_status_t lxa_html_control_context_template_html_start(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, lxa_html_control_template_html_start_result_t *result);
typedef struct {
    lxa_html_data_emit_t emit;
    lxa_html_root_prebody_emit_t prebody_emit;
    void *user;
} lxa_html_control_root_sink_t;
typedef struct {
    lxa_html_token_kind_t kind;
    union {
        lxa_html_root_start_result_t start;
        lxa_html_root_end_result_t end;
        lxa_html_root_misc_destination_t destination;
    } value;
} lxa_html_control_root_result_t;
/* Execute accepted HTML root intake after initial/text/template/frameset,
 * head-noscript, foreign and image routing. Share misc/after-body/end/start/DATA
 * ordering; use the owned formatting, form, mode, newline and pending text.
 * Start/end intake requires a borrowed tokenizer for the complete owner set.
 * NULL sink/callbacks discard DATA. Borrow callback spans only until return;
 * callbacks must not retain spans, reenter or mutate owners/sink.
 * Admit deferred unpatched templates before publishing TEMPLATE_TEXT and
 * disabling framesets. Reject has_patch requests before effects, as the
 * metadata template contract requires. Publish result only on success; latch
 * execution failure without replay. Shape/context/patch argument rejection
 * preserves state. The result kind selects start/end/destination; TEXT has no
 * value. Token/sink/result must not alias owners. Earlier non-DATA intake must
 * flush pending table text. This phase does not finalize namespace feedback or
 * observe encoding; the driver does that later. EOF remains driver-owned.
 */
lxa_status_t lxa_html_control_context_root_token(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, const lxa_html_control_root_sink_t *sink,
    lxa_html_control_root_result_t *result);
typedef enum {
    LXA_HTML_CONTROL_ROOT_NOSCRIPT, LXA_HTML_CONTROL_ROOT_FRAMESET,
    LXA_HTML_CONTROL_ROOT_FOREIGN, LXA_HTML_CONTROL_ROOT_INITIAL,
    LXA_HTML_CONTROL_ROOT_HTML
} lxa_html_control_root_dispatch_action_t;
typedef struct {
    lxa_html_control_root_dispatch_action_t action;
    lxa_html_token_t token;
    union {
        uint64_t frameset_identity;
        lxa_html_root_foreign_dispatch_result_t foreign;
        lxa_html_control_root_result_t html;
    } value;
} lxa_html_control_root_dispatch_result_t;
/* Schedule late root intake after early/template/EOF/frameset-family routing.
 * Order head-noscript, prebody frameset, foreign, HTML image normalization,
 * initial DOCTYPE and accepted HTML execution as in the canonical driver.
 * Require a tokenizer for tags; reject patches and unrouted initial tokens.
 * Result token/attributes remain borrowed; only the HTML image name changes.
 * Token/sink/result must not alias owners or owned storage.
 * Sink callbacks follow root_token's lifetime/reentry contract. Publish the
 * selected action/token/value only on success; latch effects without replay.
 * The driver still observes encoding and finalizes namespace feedback.
 */
lxa_status_t lxa_html_control_context_root_dispatch(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, const lxa_html_control_root_sink_t *sink,
    lxa_html_control_root_dispatch_result_t *result);
/* Flush owned pending table text before non-DATA intake, after any EOF TEXT
 * restoration. Select the active template slice or root ledger. NULL emit
 * discards; callback views expire on return and cannot mutate/reenter owners.
 * Clear bytes only after successful delivery/recovery. Retain completed callback,
 * pop and reconstruction effects on terminal failure, leaving bytes for teardown,
 * not retry. An empty buffer applies no grammar WORK or allocation.
 */
lxa_status_t lxa_html_control_context_flush_table_text(lxa_html_control_context_t *context,
    lxa_html_table_text_emit_t emit, void *user);
/* Complete accepted EOF after intake has restored TEXT/flushed pending bytes.
 * Run template column-group/current reads before cleanup, preserving canonical
 * WORK and excluding enclosing content at empty virtual slices.
 * Unwind active templates before root
 * completion; ensure ordinary BODY but never resurrect it for framesets. Publish
 * DONE only after cleanup succeeds. Retain partial cleanup on terminal failure
 * without replay; remaining root formatting/form state belongs to this owner
 * until destruction. Premature INITIAL/TEXT/pending bytes/newline or repeated
 * DONE requests reject before effects. No encoding or namespace feedback here.
 */
lxa_status_t lxa_html_control_context_finish(lxa_html_control_context_t *context);
/* Finalize accepted token effects with their status/reason. Latch the first
 * failure before feedback; retain completed state without replay. After success,
 * apply namespace feedback over the active template slice or root stack.
 * Skip feedback for EOF or NULL tokenizer, as canonical dispatch does. The
 * borrowed tokenizer uses the same budget. No WORK, allocation or membership
 * effects. Grammar dispatch, text delivery and EOF completion remain separate.
 */
lxa_status_t lxa_html_control_context_after_token(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, lxa_html_token_kind_t kind,
    lxa_status_t status, lxa_limit_reason_t reason);

#endif
