#ifndef LIBLEXA_HTML_H
#define LIBLEXA_HTML_H

#include "liblexa/base.h"
#include "liblexa/dom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parser results are separate from core allocation statuses. Unsupported
 * input is a candidate-implementation limit, not an HTML parse error.
 * CALLBACK_FAILURE retains the callback's exact lxa_status_t for inspection.
 */
typedef enum {
    LXA_HTML_OK = 0,
    LXA_HTML_BAD_ARGUMENT,
    LXA_HTML_UNSUPPORTED,
    LXA_HTML_OVERFLOW,
    LXA_HTML_NO_MEMORY,
    LXA_HTML_CALLBACK_FAILURE,
    LXA_HTML_LIMIT,
    LXA_HTML_ENTROPY,
    LXA_HTML_INTERNAL
} lxa_html_result_t;

/* Malformed raw-input byte offset; a non-OK return aborts publication. */
typedef lxa_status_t (*lxa_html_input_error_callback_t)(void *context,
    size_t offset);

/* Private consumer candidate. A successful parse owns
 * one ID store; all returned references and borrowed spans expire at destroy.
 * The allocator context must remain alive until then. The complete input is
 * borrowed only during this call. The configured input limit applies to raw
 * complete input before BOM/declaration selection; unsupported explicit encodings
 * fail instead of falling back to UTF-8. Decoded UTF-8 is parser-owned until
 * tokenization finishes. Scripting mode is disabled. A failed parse publishes
 * no document.
 *
 * status and parser_result == UNSUPPORTED identify unrepresentable names,
 * unknown explicit encodings or an encoding unsettled after one reparse;
 * BAD_ARGUMENT identifies invalid arguments.
 * INTERNAL identifies a violated implementation invariant.
 * status and parser_result distinguish LIMIT/NO_MEMORY/OVERFLOW/ENTROPY;
 * CALLBACK_FAILURE instead identifies a diagnostic callback rejection,
 * whose exact status is returned. reason identifies the
 * limited resource when known. parser_result and reason may be NULL;
 * out is required.
 */
typedef struct lxa_html_document lxa_html_document_t;

lxa_status_t lxa_html_document_parse(const lxa_allocator_t *allocator,
    const lxa_limits_t *limits, lxa_span_t input,
    lxa_html_result_t *parser_result, lxa_limit_reason_t *reason,
    lxa_html_document_t **out);
/* Optional diagnostic callback reports original raw-input byte offsets,
 * including a BOM. It runs after the final tree parse, before publication;
 * a non-OK return is propagated unchanged and destroys all parse state.
 * Previous callbacks cannot be undone. The callback must not reenter the
 * parser. Other results and ownership match lxa_html_document_parse.
 */
lxa_status_t lxa_html_document_parse_report(const lxa_allocator_t *allocator,
    const lxa_limits_t *limits, lxa_span_t input,
    lxa_html_input_error_callback_t on_malformed, void *context,
    lxa_html_result_t *parser_result, lxa_limit_reason_t *reason,
    lxa_html_document_t **out);
void lxa_html_document_destroy(lxa_html_document_t *document);

/* The store is owned by document. Use its checked traversal, name, attribute,
 * text and serialization operations; callers never own or reset it.
 * Missing head/body is represented by a zero handle.
 */
lxa_dom_nodes_t *lxa_html_document_nodes(lxa_html_document_t *document);
lxa_dom_ref_t lxa_html_document_root(const lxa_html_document_t *document);
lxa_dom_ref_t lxa_html_document_head(const lxa_html_document_t *document);
lxa_dom_ref_t lxa_html_document_body(const lxa_html_document_t *document);

#ifdef __cplusplus
}
#endif

#endif /* LIBLEXA_HTML_H */
