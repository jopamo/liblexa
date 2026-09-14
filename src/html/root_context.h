#ifndef LIBLEXA_HTML_ROOT_CONTEXT_H
#define LIBLEXA_HTML_ROOT_CONTEXT_H

#include "html/control_stack.h"
#include "html/formatting_list.h"

/* Private virtual-root ledger over a borrowed complete-document stack.
 * HTML/head own no open entries. BODY enters the stack only when it is empty.
 * Only frameset_ok is engine-writable. The engine owns mode changes and
 * accepted-rule dispatch; call outside active template/text rules.
 */
typedef struct {
    lxa_html_control_stack_t *stack;
    bool html, head, body, frameset_ok;
    lxa_status_t failure;
    lxa_limit_reason_t limit_reason;
} lxa_html_root_context_t;

/* Initialize against an empty, live stack. Destroy the ledger before the
 * borrowed stack; destruction releases no stack entries or live credits.
 */
lxa_status_t lxa_html_root_context_init(lxa_html_root_context_t *context,
    lxa_html_control_stack_t *stack);
void lxa_html_root_context_destroy(lxa_html_root_context_t *context);
/* Ensure virtual roots in canonical order. Check static-name/depth limits;
 * no DOM-node quota or HTML/head name storage. Retain completed virtual creation
 * on later failure and latch it. Successful existing roots perform no WORK.
 */
lxa_status_t lxa_html_root_context_ensure_html(lxa_html_root_context_t *context);
lxa_status_t lxa_html_root_context_ensure_head(lxa_html_root_context_t *context);
lxa_status_t lxa_html_root_context_ensure_body(lxa_html_root_context_t *context);
/* Apply accepted prebody end recovery over an empty stack before BODY.
 * Publish each intermediate mode after construction, retaining it on later
 * failure. Engines continue remaining dispatch after success. No retained name
 * views; mode storage must not alias the borrowed owners.
 */
lxa_status_t lxa_html_root_context_prebody_end(lxa_html_root_context_t *context,
    lxa_span_t name, lxa_html_root_mode_t *mode);
/* Finish accepted EOF after INITIAL intake and all template closures.
 * Ensure ordinary BODY, skip it for framesets, then release open membership.
 * Publish DONE only after success; retain completed roots and latch failure.
 * Text/formatting owners remain engine-owned. Mode must not alias owners.
 */
lxa_status_t lxa_html_root_context_finish(lxa_html_root_context_t *context,
    lxa_html_root_mode_t *mode);
/* Apply accepted IN_HEAD ends over the virtual head and empty open stack.
 * Publish mode/action after required BODY admission succeeds. Engines reprocess
 * BR as an attribute-free start. Failures retain roots and latch; no name views.
 * Input/output storage must not alias owners or each other.
 */
lxa_status_t lxa_html_root_context_head_end(lxa_html_root_context_t *context,
    lxa_span_t name, lxa_html_root_mode_t *mode, lxa_html_head_end_action_t *action);
/* Coordinate accepted ordinary returning-text starts after earlier routing.
 * Borrow an inactive text context; create roots, publish preparatory mode and
 * frameset effects, then apply tokenizer feedback. Publish TEXT/newline only
 * on success. Failed feedback retains preparatory effects and latches.
 * Engines own insertion/text destination; storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_start_text(lxa_html_root_context_t *context,
    lxa_html_text_context_t *text, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, bool *skip_newline);
/* Apply accepted ordinary plaintext recovery after earlier routing.
 * Close paragraph, recover current colgroup, admit open metadata and publish
 * IN_BODY before feedback. Retain completed effects and clear identity on
 * failure; no returning text context. Token/output must not alias owners.
 */
lxa_status_t lxa_html_root_context_start_plaintext(lxa_html_root_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity);
/* Reconstruct root BODY formatting from one saved closed-suffix plan.
 * Borrow a live list sharing the stack budget; preflight the entire root depth
 * before reopening. Failures retain completed pushes and latch. Call outside
 * active templates; list ownership and mode remain engine-owned.
 */
lxa_status_t lxa_html_root_context_reconstruct(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting);
/* Coordinate accepted root xmp after earlier routing: recover current colgroup,
 * close paragraph, reconstruct formatting, then publish IN_BODY/frameset effects
 * and apply returning-text feedback. TEXT publishes only on success. Borrow an
 * inactive text context and same-budget list; engines retain insertion effects.
 * Input/output storage must not alias owners or each other.
 */
lxa_status_t lxa_html_root_context_start_xmp(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_html_text_context_t *text,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode);
/* Apply accepted HTML head/body void starts after earlier routing and image
 * normalization. Share root, frameset/hidden-input, hr paragraph and formatting
 * effects without retaining void-token entries. Borrow a same-budget list when reopening
 * is required; otherwise NULL is permitted. Engines own closed insertion and
 * accepted-meta observation. Retain completed effects and latch failures.
 * Token/mode storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_start_void(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode);
/* Admit accepted root formatting starts after earlier routing.
 * Validate token ownership before effects; ensure BODY and publish IN_BODY,
 * then share anchor/nobr preprocessing, depth-bounded reconstruction and one
 * Noah plan. Preflight admission depth before consuming the saved drop index.
 * Borrow a same-budget list. Retain completed effects, clear identity and latch
 * ordinary failure; engines own insertion. Storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_start_formatting(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity);
/* Admit accepted svg/math roots after earlier foreign/table routing.
 * Ensure BODY, reserve an open slot, reconstruct formatting, then publish
 * IN_BODY and admit effective metadata. Self-closing starts pop that entry.
 * Borrow a same-budget list; failures retain completed effects and latch.
 * Engines own insertion/attribute actions. Storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_start_foreign(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode);
/* Admit accepted object/marquee/applet roots after earlier routing.
 * Publish BODY/frameset effects and reserve an open slot before reconstruction;
 * then preflight depth and admit marker membership without attribute snapshots.
 * Borrow a same-budget list. Retain completed effects and clear identity/latch
 * on failure. Engines own insertion; storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_start_marker(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity);
/* Accepted final in-body scoped end after table/form/paragraph dispatch.
 * Borrow a same-budget list; shared scope planning precedes pops and clearing.
 * Retain completed effects and latch failure without changing mode/framesets.
 */
lxa_status_t lxa_html_root_context_end_scoped(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_span_t name,
    lxa_html_root_mode_t mode);
/* Accepted body/html end in IN_BODY over an admitted BODY. Shared recovery
 * retains ordinary descendants and publishes after-body/html only on success.
 * Failed planning/pops preserve mode and latch; engines own earlier routing.
 */
lxa_status_t lxa_html_root_context_end_body(lxa_html_root_context_t *context,
    bool html_end, lxa_html_root_mode_t *mode);
/* Accepted fallback end after special/scoped/formatting rules. Plan once and
 * apply its retained index, including missing-target colgroup recovery.
 * Require admitted BODY/IN_BODY; preserve mode/framesets and latch failure.
 */
lxa_status_t lxa_html_root_context_end_ordinary(lxa_html_root_context_t *context,
    lxa_span_t name, lxa_html_root_mode_t mode);
/* Accepted in-body formatting end: validate subject before HTML-colgroup
 * recovery, then adopt over the root slice. Borrow a same-budget list; latch
 * failed recovery/adoption and retain completed effects. No mode publication.
 */
lxa_status_t lxa_html_root_context_end_formatting(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_span_t name,
    lxa_html_root_mode_t mode);
/* Accepted in-body form end over the engine's retained identity. Borrow owner
 * only for this call; it must not alias context/stack headers or storage.
 * Shared clearing/pop/middle-removal ordering retains partial effects; latch
 * failure without replay. Form admission/identity lifetime remain engine-owned.
 */
lxa_status_t lxa_html_root_context_end_form(lxa_html_root_context_t *context,
    uint64_t *owner, lxa_html_root_mode_t mode);
typedef struct {
    lxa_html_initial_action_t action;
    lxa_html_document_mode_t document_mode; /* only DOCTYPE/QUIRKS_REPROCESS */
} lxa_html_root_initial_result_t;
/* Process accepted INITIAL intake before virtual roots/open entries exist.
 * Share classification and budgeted DOCTYPE resolution without retained views.
 * Publish only on success; latch resolver failure. Engines emit comments and
 * publish BEFORE_HTML/document mode for DOCTYPE or QUIRKS_REPROCESS, then rerun
 * the latter through remaining dispatch. Token/output must not alias owners.
 */
lxa_status_t lxa_html_root_context_initial_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token,
    lxa_html_root_initial_result_t *result);
typedef struct {
    lxa_html_prebody_text_plan_t plan;
    lxa_html_root_mode_t next_mode;
} lxa_html_root_prebody_text_result_t;
/* Prepare accepted prebody text over an empty stack before BODY construction.
 * Apply shared prefix WORK and required virtual head creation, preserving
 * completed roots on failure. Publish only on success; no retained text views.
 * Engines emit the prefix before publishing next_mode and process the suffix.
 * Input/output storage must not alias owners or each other.
 */
lxa_status_t lxa_html_root_context_prebody_text(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_span_t data,
    lxa_html_root_prebody_text_result_t *result);

#endif
