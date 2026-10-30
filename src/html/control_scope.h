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
lxa_html_control_scope_namespace(const lxa_html_control_scope_view_t *view,
    size_t index, lxa_dom_namespace_t *ns)
{
    *ns = view->entries[index].namespace_id;
    if ((unsigned) *ns < LXA_DOM_NS_HTML || (unsigned) *ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    return LXA_OK;
}

#define LXA_CURRENT_NAMESPACE_NAME lxa_html_control_current_namespace_read
#define LXA_CURRENT_NAMESPACE_VIEW lxa_html_control_scope_view_t
#define LXA_CURRENT_NAMESPACE_LENGTH(view) ((view)->length)
#define LXA_CURRENT_NAMESPACE_READ lxa_html_control_scope_namespace
#include "html/current_namespace.inc"
#undef LXA_CURRENT_NAMESPACE_NAME
#undef LXA_CURRENT_NAMESPACE_VIEW
#undef LXA_CURRENT_NAMESPACE_LENGTH
#undef LXA_CURRENT_NAMESPACE_READ

/* Constant-time post-effect namespace feedback. An exhausted slice is virtual
 * HTML; inspect only its current entry otherwise. Publish only on success.
 * Borrow entries for this call; no names, allocation or WORK charges.
 */
static inline lxa_status_t
lxa_html_control_current_namespace(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_dom_namespace_t *out)
{
    if (out == NULL || boundary > length || (length != 0 && entries == NULL)
        || length > SIZE_MAX / sizeof(*entries))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_control_scope_view_t view = {entries, length, NULL, NULL};
    return lxa_html_control_current_namespace_read(&view, boundary, out);
}

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
lxa_html_control_scope_name(const lxa_html_control_scope_view_t *view,
    size_t index, lxa_span_t *name)
{
    *name = view->entries[index].name;
    return name->data == NULL || name->length == 0 ? LXA_ERROR_ARGUMENT : LXA_OK;
}

static inline lxa_status_t
lxa_html_control_scope_identity(const lxa_html_control_scope_view_t *view,
    size_t index, uint64_t *identity)
{
    *identity = view->entries[index].identity;
    return *identity == 0 ? LXA_ERROR_ARGUMENT : LXA_OK;
}

#define LXA_SCOPE_NAME lxa_html_control_scope_search
#define LXA_SCOPE_FIRST_NAME lxa_html_control_scope_search_first
#define LXA_ORDINARY_NAME lxa_html_control_ordinary_end_search
#define LXA_ORDINARY_FIRST_NAME lxa_html_control_ordinary_end_search_first
#define LXA_COLUMN_GROUP_NAME lxa_html_control_column_group
#define LXA_COLUMN_GROUP_READ_NAME lxa_html_control_column_group_read
#define LXA_SCOPE_PLAN_NAME lxa_html_control_scope_plan_search
#define LXA_ORDINARY_PLAN_NAME lxa_html_control_ordinary_plan_search
#define LXA_OPEN_REFERENCE_NAME lxa_html_control_open_reference_search
#define LXA_SCOPED_REFERENCE_NAME lxa_html_control_scoped_reference_search
#define LXA_IMPLIED_PLAN_NAME lxa_html_control_implied_plan_search
#define LXA_RUBY_PLAN_NAME lxa_html_control_ruby_plan_search
#define LXA_SELECT_PLAN_NAME lxa_html_control_select_plan_search
#define LXA_TABLE_CONTEXT_PLAN_NAME lxa_html_control_table_context_plan_search
#define LXA_TEMPLATE_MISSING_TABLE_PLAN_NAME lxa_html_control_template_missing_table_search
#define LXA_TEMPLATE_END_PLAN_NAME lxa_html_control_template_end_search
#define LXA_TEMPLATE_TABLE_SCAN_PLAN_NAME lxa_html_control_template_table_search
#define LXA_TEMPLATE_MATHML_TABLE_PLAN_NAME lxa_html_control_template_mathml_table_search
#define LXA_FOSTERED_SUFFIX_CHECK_NAME lxa_html_control_fostered_suffix_search
#define LXA_VIRTUAL_TABLE_SUFFIX_CHECK_NAME lxa_html_control_virtual_table_suffix_search
#define LXA_VIRTUAL_TABLE_ITEM_PLAN_NAME lxa_html_control_virtual_table_item_search
#define LXA_TEMPLATE_TABLE_START_PLAN_NAME lxa_html_control_template_table_start_search
#define LXA_TEMPLATE_CLOSE_PLAN_NAME lxa_html_control_template_close_search
#define LXA_TABLE_MARKERS_PLAN_NAME lxa_html_control_table_markers_search
#define LXA_COLUMN_START_PLAN_NAME lxa_html_control_column_start_search
#define LXA_FORM_END_PLAN_NAME lxa_html_control_form_end_search
#define LXA_LIST_ITEM_PLAN_NAME lxa_html_control_list_item_search
#define LXA_SCOPE_NAMESPACE lxa_html_control_scope_namespace
#define LXA_SCOPE_IDENTITY lxa_html_control_scope_identity
#define LXA_SCOPE_VIEW lxa_html_control_scope_view_t
#define LXA_SCOPE_LENGTH(view) ((view)->length)
#define LXA_SCOPE_BUDGET(view) ((view)->budget)
#define LXA_SCOPE_REASON(view) ((view)->reason)
#define LXA_SCOPE_READ lxa_html_control_scope_read
#define LXA_SCOPE_TAG_ID(view, index, name, ns) lxa_html_tree_tag_id(name, ns)
#define LXA_SCOPE_READ_NAME lxa_html_control_scope_name
#define LXA_SCOPE_IDENTITY_TYPE uint64_t
#include "html/end_target_find.inc"
#undef LXA_SCOPE_IDENTITY_TYPE
#undef LXA_SCOPE_READ
#undef LXA_SCOPE_TAG_ID
#undef LXA_SCOPE_READ_NAME
#undef LXA_SCOPE_REASON
#undef LXA_SCOPE_BUDGET
#undef LXA_SCOPE_LENGTH
#undef LXA_SCOPE_VIEW
#undef LXA_SCOPE_NAME
#undef LXA_SCOPE_FIRST_NAME
#undef LXA_ORDINARY_NAME
#undef LXA_ORDINARY_FIRST_NAME
#undef LXA_COLUMN_GROUP_NAME
#undef LXA_COLUMN_GROUP_READ_NAME
#undef LXA_SCOPE_PLAN_NAME
#undef LXA_ORDINARY_PLAN_NAME
#undef LXA_OPEN_REFERENCE_NAME
#undef LXA_SCOPED_REFERENCE_NAME
#undef LXA_IMPLIED_PLAN_NAME
#undef LXA_RUBY_PLAN_NAME
#undef LXA_SELECT_PLAN_NAME
#undef LXA_TABLE_CONTEXT_PLAN_NAME
#undef LXA_TEMPLATE_MISSING_TABLE_PLAN_NAME
#undef LXA_TEMPLATE_END_PLAN_NAME
#undef LXA_TEMPLATE_TABLE_SCAN_PLAN_NAME
#undef LXA_TEMPLATE_MATHML_TABLE_PLAN_NAME
#undef LXA_FOSTERED_SUFFIX_CHECK_NAME
#undef LXA_VIRTUAL_TABLE_SUFFIX_CHECK_NAME
#undef LXA_VIRTUAL_TABLE_ITEM_PLAN_NAME
#undef LXA_TEMPLATE_TABLE_START_PLAN_NAME
#undef LXA_TEMPLATE_CLOSE_PLAN_NAME
#undef LXA_TABLE_MARKERS_PLAN_NAME
#undef LXA_COLUMN_START_PLAN_NAME
#undef LXA_FORM_END_PLAN_NAME
#undef LXA_LIST_ITEM_PLAN_NAME
#undef LXA_SCOPE_NAMESPACE
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
    *view = (lxa_html_control_scope_view_t) {entries, length, budget, reason};
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

/* Plan implied-end trimming without mutation; an empty exception excludes no
 * name. Ruby plans first find an HTML ruby in normal scope, preserving rtc
 * when requested for rp/rt. Both publish a retained length only on success.
 */
static inline lxa_status_t
lxa_html_control_implied_end_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_span_t except, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    if (except.length != 0 && except.data == NULL) return LXA_ERROR_ARGUMENT;
    return lxa_html_control_implied_plan_search(&view, boundary, except, out);
}

static inline lxa_status_t
lxa_html_control_ruby_start_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, bool except_rtc, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_ruby_plan_search(&view, boundary, except_rtc, out);
}

/* After foreign-end dispatch and earlier insertion-mode exceptions (such as
 * hidden table inputs), plan select actions without retaining token fields.
 */
static inline lxa_status_t
lxa_html_control_select_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, lxa_html_select_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    if (!lxa_html_select_arguments_valid(kind, name)) return LXA_ERROR_ARGUMENT;
    return lxa_html_control_select_plan_search(&view, boundary, kind, name, out);
}

/* Preserve virtual table context unless an HTML table part or cell/caption
 * overrides it. Preflight a current colgroup close before publishing.
 */
static inline lxa_status_t
lxa_html_control_table_context_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, bool virtual_table, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, lxa_html_table_context_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_table_context_plan_search(&view, boundary, virtual_table, out);
}

/* Plan recovery for a template table end without a scoped table. Precharge
 * the entire active slice before reading it. Never close an HTML cell;
 * trim an intervening row or nearest section/caption/colgroup. Publish only
 * on success. The engine owns pops and formatting-marker cleanup.
 */
static inline lxa_status_t
lxa_html_control_template_missing_table_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_template_missing_table_search(&view, boundary, out);
}

/* Validate a clearable fostered suffix without applying pops. Charge two WORK
 * per entry plus classification lookup. first_cell skips classification for
 * initial td/th spelling in any namespace, matching virtual table-body recovery.
 */
static inline lxa_status_t
lxa_html_control_fostered_suffix_check(const lxa_html_scope_entry_t *entries,
    size_t length, size_t start, bool first_cell, lxa_budget_t *budget,
    lxa_limit_reason_t *reason)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        start, budget, reason, &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_fostered_suffix_search(&view, start, first_cell);
}

/* Validate a retained virtual row or section prefix and its clearable suffix.
 * Preserve name-based prefix matching and its WORK charges. Apply no pops.
 */
static inline lxa_status_t
lxa_html_control_virtual_table_suffix_check(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, bool section_root, lxa_budget_t *budget,
    lxa_limit_reason_t *reason)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_virtual_table_suffix_search(&view, boundary, section_root);
}

/* Plan row/cell replacement in a retained virtual table/table-body context
 * after earlier start dispatch. Preserve prefix probes and suffix WORK.
 * Publish only on success; the engine owns quota preflight and pops/insertion.
 */
static inline lxa_status_t
lxa_html_control_virtual_table_item_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_html_template_mode_t mode, lxa_span_t target,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, lxa_html_table_start_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_virtual_table_item_search(&view, boundary, mode, target, out);
}

/* Route accepted MathML integration-point table-part starts. Borrow only the
 * active slice; preserve name, table-namespace and virtual-root WORK charges.
 * Publish only on success; engines apply table-part admission or ordinary pops.
 */
static inline lxa_status_t
lxa_html_control_template_mathml_table_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_html_template_mode_t mode, lxa_span_t target,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, lxa_html_template_mathml_table_action_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_template_mathml_table_search(&view, boundary, mode, target, out);
}

/* Plan accepted template table-part starts after earlier token dispatch.
 * Apply no effects; POP_REPROCESS preserves the pre-pop node-admission count.
 */
static inline lxa_status_t
lxa_html_control_template_table_start_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_html_template_mode_t mode, lxa_span_t target,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, lxa_html_template_table_start_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_template_table_start_search(&view, boundary, mode, target, out);
}

/* Plan a root in-body form end. Zero identity means no active form owner.
 * A found owner clears before pops/removal; an absent owner clears after
 * successful colgroup popping. Preserve failed outputs and apply no effects.
 */
static inline lxa_status_t
lxa_html_control_form_end_plan(const lxa_html_scope_entry_t *entries,
    size_t length, uint64_t identity, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, lxa_html_form_end_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        0, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_form_end_search(&view, identity, out);
}

/* Plan an accepted template col start after earlier dispatch. Preserve the
 * name-based table/formatting scan and its WORK charges; apply no effects.
 */
static inline lxa_status_t
lxa_html_control_column_start_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_html_template_mode_t mode,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, lxa_html_column_start_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_column_start_search(&view, boundary, mode, out);
}

/* Count popped HTML cell/caption markers. Precharge length - keep WORK,
 * then scan the suffix. Publish only on success; engines own pops and clears.
 */
static inline lxa_status_t
lxa_html_control_table_markers_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t keep, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        keep, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_table_markers_search(&view, keep, out);
}

/* Precharge active length + one WORK for explicit ends. For explicit patched
 * closes, trim trailing HTML implied/table-part names before patch detachment.
 * EOF does neither. Publish only on success; engines own all close effects.
 */
static inline lxa_status_t
lxa_html_control_template_close_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, bool explicit_end, bool has_patch,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, size_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_template_close_search(&view, boundary, explicit_end, has_patch, out);
}

/* Scan through the nearest HTML table. Charge one WORK per visited entry and
 * publish classification only on success, without applying recovery.
 */
static inline lxa_status_t
lxa_html_control_template_table_scan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, lxa_html_template_table_scan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_template_table_search(&view, boundary, out);
}

/* After foreign-end dispatch, plan template table/marker end recovery.
 * POP_TABLE requires table-marker cleanup; POP_MARKER clears one formatting
 * marker after popping. CONTINUE is unhandled; IGNORE is handled without pops.
 * Preserve output on failure and leave all effects to the engine.
 */
static inline lxa_status_t
lxa_html_control_template_end_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, lxa_html_template_end_plan_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    if (!lxa_html_select_arguments_valid(kind, name)) return LXA_ERROR_ARGUMENT;
    return lxa_html_control_template_end_search(&view, boundary, kind, name, out);
}

/* Find a preceding HTML li, or either dt/dd for a definition item. Stop at a
 * special element other than HTML address/div/p. Charge one WORK per visited
 * entry, exclude entries before boundary and publish only on success.
 * Return length when absent; the engine owns pops and paragraph recovery.
 */
static inline lxa_status_t
lxa_html_control_list_item_plan(const lxa_html_scope_entry_t *entries,
    size_t length, size_t boundary, bool definition, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, size_t *out)
{
    lxa_html_control_scope_view_t view;
    size_t index;
    lxa_status_t status = lxa_html_control_scope_view_init(entries, length,
        boundary, budget, reason, out == NULL ? NULL : &index, &view);
    if (status != LXA_OK) return status;
    return lxa_html_control_list_item_search(&view, boundary, definition, out);
}

#endif
