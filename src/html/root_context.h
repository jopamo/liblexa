#ifndef LIBLEXA_HTML_ROOT_CONTEXT_H
#define LIBLEXA_HTML_ROOT_CONTEXT_H

#include "html/control_stack.h"
#include "html/formatting_list.h"

/* Private virtual-root ledger over a borrowed complete-document stack.
 * HTML/head own no open entries. BODY enters the stack only when it is empty;
 * its key records the last successful admission, survives borrowed pops and
 * clears when frameset replacement removes the BODY owner.
 * Only frameset_ok is engine-writable. The engine owns mode changes and
 * accepted-rule dispatch; call outside active template/text rules.
 */
typedef struct {
    lxa_html_control_stack_t *stack;
    uint64_t body_identity;
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
/* Root html/head/body dispatch after earlier mode/foreign routing. Recover current
 * HTML colgroup for head/body in IN_BODY, then ignore HEAD before select routing.
 * Root names bypass the select plan's unconditional continue path.
 * Reuse root construction without capturing attributes; publish mode after
 * admission, disable framesets for explicit BODY. Latch failures while retaining
 * completed effects. Call outside templates, text and framesets.
 * Mode storage must not alias the context, stack or token.
 */
lxa_status_t lxa_html_root_context_start_root(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode);
/* In-body end dispatch after foreign/template/head/br routing. Apply select
 * recovery before the shared body/formatting/form/paragraph/scoped/fallback
 * dispatch. Borrow same-budget formatting for formatting/scoped ends and a form
 * key only for form ends; other names permit NULL borrowers. Publish body/html
 * modes and implied-paragraph output only after recovery succeeds. Latch
 * execution failures without replay. Engines insert a reported closed paragraph.
 * Token/mode/form-key/output storage must not alias the borrowed owners or each other.
 */
lxa_status_t lxa_html_root_context_body_end_token(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, uint64_t *form_owner,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, bool *implied_paragraph);
/* Root end prelude after earlier mode/foreign routing and prebody recovery.
 * Recover current HTML colgroup before ignoring an in-body head end. Ignore
 * template/unmatched head, or report in-body br reprocessing as a start with
 * attributes/self-closing cleared by the engine. Publish only on success and
 * latch recovery failure; do not change mode/framesets or retain token views.
 * Output must not alias context/stack/token storage.
 */
lxa_status_t lxa_html_root_context_end_prelude(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t mode,
    lxa_html_root_end_prelude_action_t *action);
/* Apply accepted prebody end recovery over an empty stack before BODY.
 * Publish each intermediate mode after construction, retaining it on later
 * failure. Engines continue remaining dispatch after success. No retained name
 * views; mode storage must not alias the borrowed owners.
 */
lxa_status_t lxa_html_root_context_prebody_end(lxa_html_root_context_t *context,
    lxa_span_t name, lxa_html_root_mode_t *mode);
/* Finish accepted EOF after INITIAL intake, TEXT restoration and template closures.
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
/* Coordinate accepted root returning-text starts after earlier routing,
 * including frameset-dispatched noframes without resurrecting BODY.
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
/* Reconstruct accepted AFTER_AFTER_FRAMESET whitespace under HTML/current
 * formatting without BODY admission. Borrow a same-budget list and consume
 * one closed-suffix plan, retaining partial reopen effects on latched failure.
 * Engines own mode, filtered text and insertion; require HEAD without BODY.
 */
lxa_status_t lxa_html_root_context_reconstruct_after_frameset(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting);
/* Filter accepted frameset-family text into a borrowed empty initialized buffer
 * sharing the stack budget. Reconstruct only nonempty AFTER_AFTER_FRAMESET
 * output using a same-budget formatting borrower. Retain partial output and
 * reopen effects on latched failure. Engines own filtered storage/insertion;
 * destroy the buffer before its budget. Input/storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_prepare_frameset_data(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_html_root_mode_t mode,
    lxa_span_t data, lxa_buffer_t *spaces);
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
 * normalization. Frameset frame starts share family classification and check
 * static name/depth with one virtual creation WORK, without BODY or open entries.
 * Share root, frameset/hidden-input, hr paragraph and formatting
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
/* Accepted in-body scoped end after col/form/paragraph dispatch.
 * Borrow a same-budget list; shared scope planning precedes pops and clearing.
 * Table parts use table scope and clear removed cell/caption markers.
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
 * failure without replay. Form-start dispatch/identity lifetime remain engine-owned.
 */
lxa_status_t lxa_html_root_context_end_form(lxa_html_root_context_t *context,
    uint64_t *owner, lxa_html_root_mode_t mode);
typedef enum {
    LXA_HTML_ROOT_FORM_IGNORE, LXA_HTML_ROOT_FORM_OPEN, LXA_HTML_ROOT_FORM_CLOSED
} lxa_html_root_form_action_t;
/* Accepted form start after earlier routing. Borrow the engine's stable owner
 * scalar and publish action only on success. Table forms reserve a closed key
 * before colgroup pops; ordinary forms close paragraphs before open admission.
 * Retain completed roots/pops/keys and latch failure. Token/output storage must
 * not alias context/stack headers/storage or each other. Engines retain insertion
 * and form identity lifetime.
 */
lxa_status_t lxa_html_root_context_start_form(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, uint64_t *owner, lxa_html_root_mode_t *mode,
    lxa_html_root_form_action_t *action);
/* Accepted root table start after earlier select/foreign/table routing. Borrow
 * a same-budget formatting list for table-marker cleanup and document mode for
 * paragraph recovery. Preserve completed roots/pops/marker cleanup on failure, clear
 * identity and latch. HTML self-close syntax retains the table. Token/output
 * storage must not alias context/stack/list storage or each other.
 */
lxa_status_t lxa_html_root_context_start_table(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_document_mode_t document_mode, lxa_html_root_mode_t *mode, uint64_t *identity);
/* Prepared non-table part start at an actual table's HTML insertion context,
 * after the engine's scope checks/recovery. Borrow a same-budget formatting list
 * for cell markers; preflight the entire OPEN suffix before implied admission.
 * Check depth per entry, preserve completed implied entries on failure, clear
 * identity and latch. Invalid prepared geometry has no effects and does not
 * latch. COL retains no entry and reports identity zero, including after implied
 * colgroup admission; an existing colgroup needs no OPEN reservation. HTML
 * self-close retains other new parts. Token/output storage must not
 * alias context/stack/list storage or each other.
 */
lxa_status_t lxa_html_root_context_admit_table_part(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t mode, uint64_t *identity);
/* First accepted non-table part phase after earlier select/foreign dispatch.
 * Ensure BODY/IN_BODY, gate on actual-table scope and recover ordinary suffixes
 * to context with pre-pop OPEN checks and same-budget table-marker cleanup.
 * Publish in_scope only on success; false ignores admission after root effects.
 * True still requires caption/group/part recovery before prepared admission.
 * Preserve completed effects and latch failures. Token/mode/output storage must
 * not alias context/stack/list storage or each other.
 */
lxa_status_t lxa_html_root_context_recover_table_part_context(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, bool *in_scope);
/* Accepted ordinary HTML container after earlier specialized start dispatch.
 * Share recovery/reservation order, then reconstruct when required and check
 * child depth before admission. Borrow a same-budget list only when needed.
 * Clear identity/latch on failure; publish newline only on success. Token/
 * output storage must not alias context/stack/list storage or each other.
 * Engines retain insertion, select effects and earlier foreign/table routing.
 */
lxa_status_t lxa_html_root_context_start_container(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, uint64_t *identity, bool *skip_newline);
/* Accepted in-body paragraph end. Close a scoped paragraph, or recover an
 * empty BODY/current HTML colgroup and report a closed insertion. No retained
 * paragraph entry/name/key or DOM-node quota. Publish implied only on success;
 * latch failed recovery. Engines retain insertion/destination and earlier
 * routing. Output must not alias context/stack headers or storage.
 */
lxa_status_t lxa_html_root_context_end_paragraph(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, bool *implied);
/* Accepted root data after prebody/foreign/template dispatch. Borrow a
 * same-budget list; charge byte scanning before effects, then share BODY/mode,
 * frameset, table routing and conditional reconstruction. No text metadata or
 * DOM-node quota. Publish action only on success; latch completed preparation.
 * Engines own insertion, destination and token delivery. Storage must not alias
 * context/stack/list owners.
 */
lxa_status_t lxa_html_root_context_prepare_data(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, lxa_span_t data,
    lxa_html_root_mode_t *mode, lxa_html_root_data_action_t *action);
/* Accepted root select routing after foreign/hidden-table-input dispatch.
 * Consume one shared plan, pop its retained suffix, then publish ignore.
 * Latch failures without replay; modes, framesets and formatting are unchanged.
 * Output/name storage must not alias context/stack owners.
 */
lxa_status_t lxa_html_root_context_select_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_html_token_kind_t kind, lxa_span_t name,
    bool *ignore);
/* Accepted in-body control prelude: head/body/frameset/frame starts or head
 * end, not html start. Recover current HTML colgroup with the shared two-WORK
 * pre-pop charge. Latch failure; engines retain subsequent control dispatch.
 * Borrowed name must not alias owners.
 */
lxa_status_t lxa_html_root_context_control_group(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, lxa_html_token_kind_t kind, lxa_span_t name);
/* Accepted after-body/after-html token after earlier EOF/foreign/comment/
 * DOCTYPE dispatch. Preflight text scan WORK, share whitespace reconstruction
 * or in-body reprocessing publication; html starts preserve the current mode.
 * Borrow a same-budget list for whitespace, including empty text. Publish action
 * only on success and latch failed preparation. Engines retain text insertion
 * and subsequent dispatch. Input/output storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_after_body(lxa_html_root_context_t *context,
    lxa_html_formatting_list_t *formatting, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, lxa_html_after_body_action_t *action);
/* Root hidden-input table rule before select routing. Share budgeted attribute
 * classification and table planning, then remove the planned suffix. Closed
 * input retains no metadata/name/key or DOM-node quota; engines own insertion
 * and destination effects. Publish handled only on success and latch failure.
 * Roots/framesets/formatting remain unchanged; storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_hidden_table_input(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token, bool *handled);
/* Accepted disabled-scripting head noscript start before/in head. Share HEAD
 * construction before admission and publish HEAD_NOSCRIPT only on success.
 * Metadata checks the static name/depth and charges one virtual creation WORK,
 * without OPEN/name/attribute/key storage or DOM-node quota. Latch failures;
 * engines retain insertion and subsequent prefix/closure dispatch.
 * Token/mode storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_start_head_noscript(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode);
typedef struct {
    lxa_html_head_noscript_plan_t plan;
    lxa_html_root_mode_t next_mode;
} lxa_html_root_head_noscript_result_t;
/* Prepare accepted HEAD_NOSCRIPT intake after EOF/text dispatch. Preflight the
 * shared prefix scan and publish a result only on success; latch failed WORK.
 * Engines emit the prefix before publishing next_mode and reprocessing suffix.
 * Retain no token views; token/output must not alias borrowed owners.
 */
lxa_status_t lxa_html_root_context_head_noscript_token(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, const lxa_html_token_t *token,
    lxa_html_root_head_noscript_result_t *result);
/* Plan accepted in-body frameset replacement against the admitted BODY key,
 * not merely a body-shaped entry. Read the root entry only when other gates
 * pass, publish eligible only on success and latch failed WORK.
 * This query does not admit framesets, replace BODY or change mode.
 * Output must not alias context/stack owners.
 */
lxa_status_t lxa_html_root_context_frameset_body(lxa_html_root_context_t *context,
    lxa_html_root_mode_t mode, bool *eligible);
/* Accepted frameset start after root control/foreign dispatch. Nested starts
 * retain the current frame owner and check depth from open membership.
 * Prebody starts
 * preflight OPEN before HEAD. In-body starts consume eligibility once, account
 * released OPEN credits, and preflight name/work/storage/key admission before
 * BODY pops/owner clearing. Push then publish IN_FRAMESET; identity zero with
 * success means ignored. No attribute snapshots or DOM-node quota. Retain
 * completed effects/capacity, clear identity and latch failure. Engines own
 * insertion; token/mode/output storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_start_frameset(lxa_html_root_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, uint64_t *identity);
typedef struct {
    lxa_html_formatting_list_t *formatting;
    lxa_html_text_context_t *text;
    lxa_html_tokenizer_t *tokenizer;
    lxa_buffer_t *spaces;
} lxa_html_root_frameset_borrowers_t;
typedef struct {
    lxa_html_frameset_action_t action;
    uint64_t identity; /* nested frameset admission only */
    bool skip_newline; /* returning-text admission only */
} lxa_html_root_frameset_result_t;
/* Consume accepted frameset-family intake through shared action execution.
 * Borrow spaces for text, formatting for post-frameset spaces and inactive
 * text/tokenizer owners for noframes. Other actions permit NULL borrowers.
 * Engines retain comment/html attributes and insertion; publish result only
 * on success. Retain completed owner effects on failure without replay.
 * Token/mode/result/borrowers must not alias context or borrowed storage.
 */
lxa_status_t lxa_html_root_context_frameset_token(lxa_html_root_context_t *context,
    const lxa_html_root_frameset_borrowers_t *borrowers, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, lxa_html_root_frameset_result_t *result);
/* Accepted frameset/html family ends after classification. Pop one frameset,
 * publish AFTER_FRAMESET only when membership empties, or publish
 * AFTER_AFTER_FRAMESET for html ends. Latch failed pops without mode changes.
 * Require HEAD without BODY; mode storage must not alias owners.
 */
lxa_status_t lxa_html_root_context_end_frameset(lxa_html_root_context_t *context,
    lxa_span_t name, lxa_html_root_mode_t *mode);
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
