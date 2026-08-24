#ifndef LIBLEXA_HTML_CONTROL_SCOPE_H
#define LIBLEXA_HTML_CONTROL_SCOPE_H

#include "html/tree_categories.h"

typedef struct {
    lxa_span_t name;
    lxa_dom_namespace_t namespace_id;
    uint64_t identity; /* nonzero for reference queries; ignored by name queries */
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

static inline lxa_status_t
lxa_html_control_scope_identity(const lxa_html_control_scope_view_t *view,
                                size_t index, uint64_t *identity)
{
    *identity = view->entries[index].identity;
    return *identity == 0 ? LXA_ERROR_ARGUMENT : LXA_OK;
}

#define LXA_SCOPE_NAME lxa_html_control_scope_search
#define LXA_ORDINARY_NAME lxa_html_control_ordinary_end_search
#define LXA_COLUMN_GROUP_NAME lxa_html_control_column_group
#define LXA_SCOPE_PLAN_NAME lxa_html_control_scope_plan_search
#define LXA_ORDINARY_PLAN_NAME lxa_html_control_ordinary_plan_search
#define LXA_OPEN_REFERENCE_NAME lxa_html_control_open_reference_search
#define LXA_SCOPED_REFERENCE_NAME lxa_html_control_scoped_reference_search
#define LXA_SCOPE_IDENTITY lxa_html_control_scope_identity
#define LXA_SCOPE_VIEW lxa_html_control_scope_view_t
#define LXA_SCOPE_LENGTH(view) ((view)->length)
#define LXA_SCOPE_BUDGET(view) ((view)->budget)
#define LXA_SCOPE_REASON(view) ((view)->reason)
#define LXA_SCOPE_READ lxa_html_control_scope_read
#include "html/end_target_find.inc"
#undef LXA_SCOPE_READ
#undef LXA_SCOPE_REASON
#undef LXA_SCOPE_BUDGET
#undef LXA_SCOPE_LENGTH
#undef LXA_SCOPE_VIEW
#undef LXA_SCOPE_NAME
#undef LXA_ORDINARY_NAME
#undef LXA_COLUMN_GROUP_NAME
#undef LXA_SCOPE_PLAN_NAME
#undef LXA_ORDINARY_PLAN_NAME
#undef LXA_OPEN_REFERENCE_NAME
#undef LXA_SCOPED_REFERENCE_NAME
#undef LXA_SCOPE_IDENTITY

static inline lxa_status_t
lxa_html_control_scope_view_init(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *index, lxa_html_control_scope_view_t *view)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (index != NULL) *index = 0;
    if (index == NULL || budget == NULL || (length != 0 && entries == NULL)
        || length > SIZE_MAX / sizeof(*entries) || boundary > length)
        return LXA_ERROR_ARGUMENT;
    *view = (lxa_html_control_scope_view_t){entries, length, budget, reason};
    return LXA_OK;
}

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
    lxa_html_control_scope_view_t view;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, index, &view);
    if (status != LXA_OK) return status;
    if (target.data == NULL || target.length == 0
        || !lxa_html_scope_kind_valid(scope))
        return LXA_ERROR_ARGUMENT;
    return lxa_html_control_scope_search(&view, boundary, target, scope, index);
}

/* Same borrowing and result contract as scope_find; match names exactly,
 * including headings, and stop at a special element after checking it.
 */
static inline lxa_status_t
lxa_html_control_ordinary_end_find(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_span_t target, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *index)
{
    lxa_html_control_scope_view_t view;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, index, &view);
    if (status != LXA_OK) return status;
    if (target.data == NULL || target.length == 0) return LXA_ERROR_ARGUMENT;
    return lxa_html_control_ordinary_end_search(&view, boundary, target, index);
}

/* Plan against the post-colgroup view without changing entries. Publish only
 * on success, including the final WORK charge for the planned colgroup close.
 */
static inline lxa_status_t
lxa_html_control_scope_plan(const lxa_html_scope_entry_t *entries, size_t length,
    size_t boundary, lxa_span_t target, lxa_html_scope_kind_t scope,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, lxa_html_end_target_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    if (target.data == NULL || target.length == 0
        || !lxa_html_scope_kind_valid(scope))
        return LXA_ERROR_ARGUMENT;
    return lxa_html_control_scope_plan_search(&view, boundary, target, scope, out);
}

static inline lxa_status_t
lxa_html_control_ordinary_end_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_span_t target, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, lxa_html_end_target_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    if (target.data == NULL || target.length == 0) return LXA_ERROR_ARGUMENT;
    return lxa_html_control_ordinary_plan_search(&view, boundary, target, out);
}

/* Reference queries borrow caller-owned nonzero identities. Open lookup scans
 * forward without reading names; scoped lookup scans backward and checks the
 * identity before reading names or stopping at a normal-scope boundary.
 */
static inline lxa_status_t
lxa_html_control_open_reference_find(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, uint64_t identity, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *index)
{
    lxa_html_control_scope_view_t view;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, index, &view);
    if (status != LXA_OK) return status;
    if (identity == 0) return LXA_ERROR_ARGUMENT;
    return lxa_html_control_open_reference_search(&view, boundary, identity, index);
}

static inline lxa_status_t
lxa_html_control_scoped_reference_find(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, uint64_t identity, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *index)
{
    lxa_html_control_scope_view_t view;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, index, &view);
    if (status != LXA_OK) return status;
    if (identity == 0) return LXA_ERROR_ARGUMENT;
    return lxa_html_control_scoped_reference_search(&view, boundary, identity, index);
}

#endif
