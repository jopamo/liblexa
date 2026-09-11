#ifndef LIBLEXA_HTML_ROOT_CONTEXT_H
#define LIBLEXA_HTML_ROOT_CONTEXT_H

#include "html/control_stack.h"

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
