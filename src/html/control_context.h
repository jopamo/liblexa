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
/* Execute an accepted root HTML start after initial/text/template/frameset,
 * head-noscript/after-body, foreign and image routing. Borrow the tokenizer for
 * text feedback and use the owned formatting, form, mode and newline state.
 * Admit deferred unpatched templates before publishing TEMPLATE_TEXT and
 * disabling framesets. Reject has_patch requests before effects, as the
 * metadata template contract requires. Publish result only on success; latch
 * execution failure without replay. Shape/context/patch argument rejection
 * preserves state. Token/result must not alias owners. This phase does not
 * finalize namespace feedback or observe encoding; the driver does that later.
 */
lxa_status_t lxa_html_control_context_start_root(lxa_html_control_context_t *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    bool has_patch, lxa_html_root_start_result_t *result);
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
