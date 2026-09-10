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

#endif
