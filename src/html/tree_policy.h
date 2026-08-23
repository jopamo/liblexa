#ifndef LIBLEXA_HTML_TREE_POLICY_H
#define LIBLEXA_HTML_TREE_POLICY_H

#include "html/tokenizer.h"

typedef enum {
    LXA_HTML_NO_QUIRKS, LXA_HTML_LIMITED_QUIRKS, LXA_HTML_QUIRKS
} lxa_html_document_mode_t;

static inline bool
lxa_html_tree_ascii_case_equals(lxa_span_t value, const char *expected,
                                size_t length)
{
    if (value.length != length || value.data == NULL) return false;
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = value.data[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (uint8_t) expected[i]) return false;
    }
    return true;
}

/* Classify a borrowed DOCTYPE token without constructing a node. The caller
 * decides whether the declaration applies and commits the mode on success.
 */
lxa_status_t
lxa_html_tree_doctype_mode(const lxa_html_token_t *data, lxa_budget_t *budget,
                           lxa_limit_reason_t *reason,
                           lxa_html_document_mode_t *mode);

#endif
