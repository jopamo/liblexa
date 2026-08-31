#ifndef LIBLEXA_HTML_TREE_POLICY_H
#define LIBLEXA_HTML_TREE_POLICY_H

#include <string.h>
#include "html/tokenizer.h"
#include "dom/namespace.h"

typedef enum {
    LXA_HTML_NO_QUIRKS, LXA_HTML_LIMITED_QUIRKS, LXA_HTML_QUIRKS
} lxa_html_document_mode_t;

typedef enum {
    LXA_TEMPLATE_INITIAL, LXA_TEMPLATE_TABLE, LXA_TEMPLATE_TABLE_BODY,
    LXA_TEMPLATE_ROW, LXA_TEMPLATE_COLUMN_GROUP, LXA_TEMPLATE_BODY
} lxa_html_template_mode_t;

static inline bool
lxa_html_tree_name_equals(lxa_span_t name, const char *text, size_t length)
{
    return name.length == length && name.data != NULL
        && memcmp(name.data, text, length) == 0;
}

static inline bool
lxa_html_tree_heading(lxa_span_t name)
{
    return name.length == 2 && name.data != NULL && name.data[0] == 'h'
        && name.data[1] >= '1' && name.data[1] <= '6';
}

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

/* Template geometry includes html/head-or-body, two slots per active template
 * (element and virtual content root), and retained content elements. For a body
 * context, root_ancestors is the root open-stack length minus its body entry;
 * head contexts use zero. Check the predicted depth without charging live quota.
 */
lxa_status_t
lxa_html_tree_template_depth_check(lxa_budget_t *budget, size_t root_ancestors,
    size_t templates, size_t elements, lxa_limit_reason_t *reason);

/* Select the mode for an accepted start in an active template context.
 * Head-rule starts retain INITIAL; table parts choose their virtual table
 * mode; other starts leave INITIAL for BODY. Noninitial modes are unchanged.
 * This does not dispatch, admit elements or apply tokenizer text feedback.
 * The engine owns effect ordering and nested-template save/restore.
 */
lxa_status_t
lxa_html_tree_template_start_mode(lxa_html_template_mode_t current,
    lxa_span_t name, lxa_dom_namespace_t ns, lxa_html_template_mode_t *out);

typedef enum {
    LXA_HTML_COLUMN_GROUP_CONTINUE, LXA_HTML_COLUMN_GROUP_IGNORE,
    LXA_HTML_COLUMN_GROUP_SPACE_TEXT
} lxa_html_column_group_action_t;

/* Apply virtual column-group gating after earlier template dispatch.
 * The engine supplies the current HTML-colgroup predicate and owns text effects.
 */
lxa_status_t
lxa_html_tree_template_column_group(lxa_html_template_mode_t mode,
    bool group_open, lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_html_column_group_action_t *out);

/* Query decoded, normalized start attributes in source order. Charge one WORK
 * per visited attribute and publish only on success. Engines own dispatch.
 */
lxa_status_t
lxa_html_tree_hidden_input(const lxa_html_token_t *token, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *out);
lxa_status_t
lxa_html_tree_font_breakout(const lxa_html_token_t *token, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *out);

#endif
