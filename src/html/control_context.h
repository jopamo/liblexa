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
/* Complete accepted EOF after intake has restored TEXT/flushed pending bytes
 * and template gating permits completion. Unwind active templates before root
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
