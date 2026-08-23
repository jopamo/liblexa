#ifndef LIBLEXA_HTML_CONTROL_SCOPE_H
#define LIBLEXA_HTML_CONTROL_SCOPE_H

#include "html/tree_categories.h"

typedef struct {
    lxa_span_t name;
    lxa_dom_namespace_t namespace_id;
} lxa_html_scope_entry_t;

typedef struct {
    const lxa_html_scope_entry_t *entries;
    size_t length;
    lxa_budget_t *budget;
    lxa_limit_reason_t *reason;
} lxa_html_control_scope_view_t;

static inline lxa_status_t
lxa_html_control_scope_read(const lxa_html_control_scope_view_t *view,
                            size_t index, lxa_span_t *name,
                            lxa_dom_namespace_t *ns)
{
    const lxa_html_scope_entry_t *entry = &view->entries[index];
    if (entry->name.data == NULL || entry->name.length == 0
        || (unsigned) entry->namespace_id < LXA_DOM_NS_HTML
        || (unsigned) entry->namespace_id > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    *name = entry->name;
    *ns = entry->namespace_id;
    return LXA_OK;
}

#define LXA_SCOPE_NAME lxa_html_control_scope_search
#define LXA_SCOPE_VIEW lxa_html_control_scope_view_t
#define LXA_SCOPE_LENGTH(view) ((view)->length)
#define LXA_SCOPE_BUDGET(view) ((view)->budget)
#define LXA_SCOPE_REASON(view) ((view)->reason)
#define LXA_SCOPE_READ lxa_html_control_scope_read
#include "html/scope_find.inc"
#undef LXA_SCOPE_READ
#undef LXA_SCOPE_REASON
#undef LXA_SCOPE_BUDGET
#undef LXA_SCOPE_LENGTH
#undef LXA_SCOPE_VIEW
#undef LXA_SCOPE_NAME

/* Find an HTML target; heading targets match any HTML h1-h6. Borrow entries
 * and their names only until return. No stack mutation or allocation; length
 * is the successful not-found result. Ignore index on failure. The boundary
 * excludes all entries below it.
 */
static inline lxa_status_t
lxa_html_control_scope_find(const lxa_html_scope_entry_t *entries, size_t length,
                            size_t boundary, lxa_span_t target,
                            lxa_html_scope_kind_t scope, lxa_budget_t *budget,
                            lxa_limit_reason_t *reason, size_t *index)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (index != NULL) *index = 0;
    if (index == NULL || budget == NULL || (length != 0 && entries == NULL)
        || length > SIZE_MAX / sizeof(*entries) || boundary > length
        || target.data == NULL || target.length == 0
        || (scope != LXA_HTML_SCOPE_NORMAL && scope != LXA_HTML_SCOPE_LIST_ITEM
            && scope != LXA_HTML_SCOPE_BUTTON && scope != LXA_HTML_SCOPE_TABLE))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_control_scope_view_t view = {
        entries, length, budget, reason
    };
    return lxa_html_control_scope_search(&view, boundary, target, scope, index);
}

#endif
