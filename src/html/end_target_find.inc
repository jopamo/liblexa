#include "base/budget_internal.h"

/* Instantiate end-target searches with a borrowed stack and static access.
 * LXA_SCOPE_{NAME,VIEW,LENGTH,BUDGET,REASON,READ,READ_NAME,NAMESPACE,PLAN_NAME,IDENTITY},
 * LXA_ORDINARY_{NAME,PLAN_NAME}, LXA_{OPEN,SCOPED}_REFERENCE_NAME and
 * LXA_COLUMN_GROUP_NAME and LXA_{IMPLIED,RUBY,SELECT,TABLE_CONTEXT,LIST_ITEM}_PLAN_NAME
 * and LXA_TEMPLATE_{MISSING_TABLE,END,TABLE_SCAN,MATHML_TABLE}_PLAN_NAME,
 * LXA_FOSTERED_SUFFIX_CHECK_NAME, LXA_VIRTUAL_TABLE_SUFFIX_CHECK_NAME,
 * LXA_VIRTUAL_TABLE_ITEM_PLAN_NAME, LXA_TEMPLATE_TABLE_START_PLAN_NAME,
 * LXA_TEMPLATE_CLOSE_PLAN_NAME, LXA_COLUMN_START_PLAN_NAME, LXA_FORM_END_PLAN_NAME,
 * LXA_TABLE_MARKERS_PLAN_NAME
 * belong to the includer.
 */
static inline lxa_status_t
LXA_TABLE_MARKERS_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t keep, size_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view), markers = 0;
    if (keep > length) return LXA_ERROR_ARGUMENT;
    lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
        length - keep, LXA_SCOPE_REASON(view));
    if (status != LXA_OK) return status;
    for (size_t i = keep; i < length; ++i) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = LXA_SCOPE_READ(view, i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML
            && (lxa_html_tree_name_equals(name, "td", 2)
                || lxa_html_tree_name_equals(name, "th", 2)
                || lxa_html_tree_name_equals(name, "caption", 7)))
            ++markers;
    }
    *out = markers;
    return LXA_OK;
}

static inline lxa_status_t
LXA_COLUMN_START_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    lxa_html_template_mode_t mode, lxa_html_column_start_plan_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    if (boundary > length || (unsigned) mode > LXA_TEMPLATE_BODY)
        return LXA_ERROR_ARGUMENT;
    lxa_html_column_start_plan_t plan = {LXA_HTML_COLUMN_START_TABLE_PART, length, 0};
    if (mode == LXA_TEMPLATE_COLUMN_GROUP) {
        plan.action = LXA_HTML_COLUMN_START_VIRTUAL;
        goto publish;
    }
    if (length == boundary) {
        if (mode == LXA_TEMPLATE_ROW || mode == LXA_TEMPLATE_TABLE_BODY)
            plan.action = LXA_HTML_COLUMN_START_IGNORE;
        goto publish;
    }
    lxa_span_t top;
    lxa_dom_namespace_t ns;
    lxa_status_t status = LXA_SCOPE_READ(view, length - 1, &top, &ns);
    if (status != LXA_OK) return status;
    if (length - boundary == 1 && mode == LXA_TEMPLATE_TABLE_BODY
        && lxa_html_tree_name_equals(top, "tr", 2)) {
        plan.action = LXA_HTML_COLUMN_START_POP_IGNORE;
        plan.keep = boundary;
        goto publish;
    }
    bool top_table = lxa_html_tree_name_equals(top, "table", 5);
    bool top_group = lxa_html_tree_name_equals(top, "colgroup", 8);
    if (mode == LXA_TEMPLATE_TABLE && !top_table && !top_group) {
        size_t i = length;
        bool table = false, formatting = false;
        while (i > boundary) {
            lxa_span_t name;
            status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                1, LXA_SCOPE_REASON(view));
            if (status == LXA_OK) status = LXA_SCOPE_READ(view, --i, &name, &ns);
            if (status != LXA_OK) return status;
            if (lxa_html_tree_name_equals(name, "table", 5)) {
                table = true;
                break;
            }
            if (lxa_html_tree_formatting_tag(name)) formatting = true;
        }
        if (!table && !formatting) {
            plan.action = LXA_HTML_COLUMN_START_POP_REPROCESS;
            plan.keep = boundary;
            plan.nodes_before_pop = 2;
            goto publish;
        }
    }
    if (top_table) plan.action = LXA_HTML_COLUMN_START_IMPLY_GROUP;
    else if (top_group) plan.action = LXA_HTML_COLUMN_START_APPEND;
publish:
    *out = plan;
    return LXA_OK;
}

static inline lxa_status_t
LXA_TEMPLATE_CLOSE_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    bool explicit_end, bool has_patch, size_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view), keep = length;
    if (boundary > length) return LXA_ERROR_ARGUMENT;
    if (explicit_end) {
        size_t work;
        lxa_status_t status = lxa_size_add(length - boundary, 1, &work);
        if (status == LXA_OK)
            status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                work, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
    }
    if (explicit_end && has_patch) {
        while (keep > boundary) {
            lxa_span_t name;
            lxa_dom_namespace_t ns;
            lxa_status_t status = LXA_SCOPE_READ(view, keep - 1, &name, &ns);
            if (status != LXA_OK) return status;
            if (ns != LXA_DOM_NS_HTML
                || !(lxa_html_tree_implied_end_tag(name)
                    || (lxa_html_tree_table_part(name)
                        && !lxa_html_tree_name_equals(name, "table", 5)
                        && !lxa_html_tree_name_equals(name, "col", 3))))
                break;
            --keep;
        }
    }
    *out = keep;
    return LXA_OK;
}

static inline lxa_status_t
LXA_FOSTERED_SUFFIX_CHECK_NAME(const LXA_SCOPE_VIEW *view, size_t start,
    bool first_cell)
{
    for (size_t i = start; i < LXA_SCOPE_LENGTH(view); ++i) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        bool clearable;
        lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            2, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, i, &name, &ns);
        if (status != LXA_OK) return status;
        if (first_cell && i == start
            && (lxa_html_tree_name_equals(name, "td", 2)
                || lxa_html_tree_name_equals(name, "th", 2)))
            continue;
        status = lxa_html_tree_fostered_descendant(name, ns,
            LXA_SCOPE_BUDGET(view), LXA_SCOPE_REASON(view), &clearable);
        if (status != LXA_OK) return status;
        if (!clearable) return LXA_ERROR_INTERNAL;
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_VIRTUAL_TABLE_SUFFIX_CHECK_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    bool section_root)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    if (boundary > length || length - boundary < (section_root ? 2u : 1u))
        return LXA_ERROR_ARGUMENT;
    lxa_span_t root, row;
    lxa_dom_namespace_t ns;
    lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
        section_root ? 2 : 1, LXA_SCOPE_REASON(view));
    if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary, &root, &ns);
    if (status == LXA_OK && section_root)
        status = LXA_SCOPE_READ(view, boundary + 1, &row, &ns);
    if (status != LXA_OK) return status;
    if (!section_root) {
        if (!lxa_html_tree_name_equals(root, "tr", 2)) return LXA_ERROR_INTERNAL;
        return LXA_FOSTERED_SUFFIX_CHECK_NAME(view, boundary + 1, true);
    }
    if (!lxa_html_tree_table_section(root)) return LXA_ERROR_INTERNAL;
    size_t start = boundary + 1;
    if (lxa_html_tree_name_equals(row, "tr", 2)) {
        ++start;
        if (start < length) {
            lxa_span_t cell;
            status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                1, LXA_SCOPE_REASON(view));
            if (status == LXA_OK) status = LXA_SCOPE_READ(view, start, &cell, &ns);
            if (status != LXA_OK) return status;
            if (lxa_html_tree_name_equals(cell, "td", 2)
                || lxa_html_tree_name_equals(cell, "th", 2))
                ++start;
        }
    }
    return LXA_FOSTERED_SUFFIX_CHECK_NAME(view, start, false);
}

static inline lxa_status_t
LXA_VIRTUAL_TABLE_ITEM_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    lxa_html_template_mode_t mode, lxa_span_t target, lxa_html_table_start_plan_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    bool cell = lxa_html_tree_name_equals(target, "td", 2)
        || lxa_html_tree_name_equals(target, "th", 2);
    if (boundary > length || length - boundary < 2
        || (mode != LXA_TEMPLATE_TABLE && mode != LXA_TEMPLATE_TABLE_BODY)
        || (!cell && !lxa_html_tree_name_equals(target, "tr", 2)))
        return LXA_ERROR_ARGUMENT;
    size_t active = length - boundary, start = boundary + 1;
    bool direct_cell = false, fostered_row = false, fostered_cell = false;
    lxa_dom_namespace_t ns;
    lxa_span_t root, row, item;
    lxa_status_t status;
    if (mode == LXA_TEMPLATE_TABLE_BODY && active >= 3) {
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            2, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary, &row, &ns);
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, start, &item, &ns);
        if (status != LXA_OK) return status;
        direct_cell = lxa_html_tree_name_equals(row, "tr", 2)
            && (lxa_html_tree_name_equals(item, "td", 2)
                || lxa_html_tree_name_equals(item, "th", 2));
        if (direct_cell) ++start;
    }
    if (mode == LXA_TEMPLATE_TABLE && active >= 3) {
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            2, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary, &root, &ns);
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, start, &row, &ns);
        if (status != LXA_OK) return status;
        fostered_row = lxa_html_tree_table_section(root)
            && lxa_html_tree_name_equals(row, "tr", 2);
        if (fostered_row) ++start;
    }
    if (mode == LXA_TEMPLATE_TABLE && active >= 4) {
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            3, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary, &root, &ns);
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary + 1, &row, &ns);
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary + 2, &item, &ns);
        if (status != LXA_OK) return status;
        fostered_cell = lxa_html_tree_table_section(root)
            && lxa_html_tree_name_equals(row, "tr", 2)
            && (lxa_html_tree_name_equals(item, "td", 2)
                || lxa_html_tree_name_equals(item, "th", 2));
        if (fostered_cell) start = boundary + 3;
    }
    status = LXA_FOSTERED_SUFFIX_CHECK_NAME(view, start, false);
    if (status != LXA_OK) return status;
    lxa_html_table_start_plan_t plan = {boundary, 1, false, false};
    if (direct_cell) plan.keep += cell ? 1 : 0;
    else if (fostered_cell || fostered_row) plan.keep += cell ? 2 : 1;
    else {
        status = LXA_SCOPE_READ(view, boundary, &root, &ns);
        if (status != LXA_OK) return status;
        bool section = mode == LXA_TEMPLATE_TABLE && lxa_html_tree_table_section(root);
        if (!section && !(mode == LXA_TEMPLATE_TABLE_BODY
            && lxa_html_tree_name_equals(root, "tr", 2)))
            return LXA_ERROR_INTERNAL;
        plan.keep += cell || section ? 1 : 0;
        if (section && cell) {
            plan.implied_row = true;
            plan.extra = 2;
        }
    }
    *out = plan;
    return LXA_OK;
}

static inline lxa_status_t
LXA_TEMPLATE_MATHML_TABLE_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    lxa_html_template_mode_t mode, lxa_span_t target,
    lxa_html_template_mathml_table_action_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    if (boundary >= length || (unsigned) mode > LXA_TEMPLATE_BODY
        || target.data == NULL || target.length == 0
        || !lxa_html_tree_table_part(target) || lxa_html_tree_name_equals(target, "table", 5))
        return LXA_ERROR_ARGUMENT;
    lxa_html_template_mathml_table_action_t action = LXA_HTML_TEMPLATE_MATHML_TABLE_CONTINUE;
    for (size_t i = length; i > boundary;) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ_NAME(view, --i, &name);
        if (status != LXA_OK) return status;
        if (lxa_html_tree_name_equals(name, "table", 5)) {
            status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                1, LXA_SCOPE_REASON(view));
            if (status == LXA_OK) status = LXA_SCOPE_NAMESPACE(view, i, &ns);
            if (status != LXA_OK) return status;
            if (ns == LXA_DOM_NS_HTML) {
                *out = LXA_HTML_TEMPLATE_MATHML_TABLE_PART;
                return LXA_OK;
            }
        }
    }
    if (mode == LXA_TEMPLATE_TABLE || mode == LXA_TEMPLATE_TABLE_BODY) {
        lxa_span_t root;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            2, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ_NAME(view, boundary, &root);
        if (status == LXA_OK) status = LXA_SCOPE_NAMESPACE(view, boundary, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML) {
            if (mode == LXA_TEMPLATE_TABLE && lxa_html_tree_table_section(root))
                action = LXA_HTML_TEMPLATE_MATHML_TABLE_PART;
            else if (mode == LXA_TEMPLATE_TABLE_BODY && lxa_html_tree_name_equals(root, "tr", 2))
                action = lxa_html_tree_name_equals(target, "tr", 2)
                    || lxa_html_tree_name_equals(target, "td", 2)
                    || lxa_html_tree_name_equals(target, "th", 2)
                    ? LXA_HTML_TEMPLATE_MATHML_TABLE_PART : LXA_HTML_TEMPLATE_MATHML_TABLE_POP_IGNORE;
        }
    }
    *out = action;
    return LXA_OK;
}

static inline lxa_status_t
LXA_TEMPLATE_TABLE_SCAN_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    lxa_html_template_table_scan_t *out)
{
    lxa_html_template_table_scan_t plan = {
        SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, false, false
    };
    size_t i = LXA_SCOPE_LENGTH(view);
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) {
            plan.ordinary_above = true;
            continue;
        }
        if (plan.context == SIZE_MAX && lxa_html_tree_table_context(name))
            plan.context = i;
        if (lxa_html_tree_name_equals(name, "table", 5)) {
            plan.table = i;
            break;
        }
        if (lxa_html_tree_name_equals(name, "caption", 7)) {
            if (plan.caption != SIZE_MAX) return LXA_ERROR_INTERNAL;
            plan.caption = i;
        }
        else if (plan.caption != SIZE_MAX) return LXA_ERROR_INTERNAL;
        else if (lxa_html_tree_name_equals(name, "colgroup", 8))
            plan.group_open = true;
        else if (lxa_html_tree_name_equals(name, "td", 2)
            || lxa_html_tree_name_equals(name, "th", 2))
            plan.cell = i;
        else if (!lxa_html_tree_table_section(name)
            && !lxa_html_tree_name_equals(name, "tr", 2))
            plan.ordinary_above = true;
    }
    *out = plan;
    return LXA_OK;
}

static inline lxa_status_t
LXA_TEMPLATE_TABLE_START_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    lxa_html_template_mode_t mode, lxa_span_t target, lxa_html_template_table_start_plan_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    if (boundary > length || (unsigned) mode > LXA_TEMPLATE_BODY
        || !lxa_html_tree_table_part(target)
        || lxa_html_tree_name_equals(target, "table", 5))
        return LXA_ERROR_ARGUMENT;
    lxa_html_template_table_scan_t scan;
    lxa_status_t status = LXA_TEMPLATE_TABLE_SCAN_PLAN_NAME(view, boundary, &scan);
    if (status != LXA_OK) return status;
    lxa_html_template_table_start_plan_t plan = {
        {length, 1, false, false}, LXA_HTML_TABLE_START_INSERT, 0, false
    };
    lxa_span_t top, root, parent;
    lxa_dom_namespace_t ns;
    bool caption_open = scan.caption != SIZE_MAX;
    if (scan.table != SIZE_MAX) {
        bool clear_ordinary = scan.ordinary_above && !caption_open && scan.cell == SIZE_MAX;
        if (caption_open && scan.caption != scan.table + 1) return LXA_ERROR_INTERNAL;
        status = LXA_SCOPE_READ(view, clear_ordinary ? scan.context : length - 1, &top, &ns);
        if (status == LXA_OK)
            status = lxa_html_tree_table_start_plan(&scan, length, top, target, &plan.insertion);
        if (status != LXA_OK) return status;
        goto publish;
    }
    size_t active = length - boundary;
    bool section = lxa_html_tree_table_section(target);
    bool row = lxa_html_tree_name_equals(target, "tr", 2);
    bool cell = lxa_html_tree_name_equals(target, "td", 2)
        || lxa_html_tree_name_equals(target, "th", 2);
    bool caption = lxa_html_tree_name_equals(target, "caption", 7);
    bool column = lxa_html_tree_name_equals(target, "col", 3);
    bool group = column || lxa_html_tree_name_equals(target, "colgroup", 8);
    if (scan.context == SIZE_MAX && scan.cell == SIZE_MAX && !caption_open
        && (active == 0 || mode == LXA_TEMPLATE_ROW || mode == LXA_TEMPLATE_TABLE_BODY)) {
        lxa_html_virtual_table_start_plan_t admission;
        status = lxa_html_tree_virtual_table_start_plan(mode, target, &admission);
        if (status != LXA_OK) return status;
        plan.insertion = (lxa_html_table_start_plan_t){
            boundary, admission.extra, admission.implied_section, admission.implied_row
        };
        if (admission.ignore) plan.action = LXA_HTML_TABLE_START_IGNORE;
        goto publish;
    }
    if (active != 0 && mode == LXA_TEMPLATE_TABLE && (scan.ordinary_above || caption_open)) {
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary, &root, &ns);
        if (status != LXA_OK) return status;
        if (lxa_html_tree_name_equals(root, "caption", 7) || !lxa_html_tree_table_context(root)) {
            plan.action = LXA_HTML_TABLE_START_POP_REPROCESS;
            plan.insertion.keep = boundary;
            plan.nodes_before_pop = cell ? 3 : row || column ? 2 : 1;
            plan.table_pop = true;
            goto publish;
        }
    }
    if (scan.cell == boundary && mode == LXA_TEMPLATE_ROW) {
        plan.insertion.keep = boundary;
        if (!cell) {
            plan.action = LXA_HTML_TABLE_START_POP_IGNORE;
            plan.table_pop = true;
        }
        goto publish;
    }
    if (mode == LXA_TEMPLATE_TABLE_BODY && (section || caption || group) && active > 1) {
        status = LXA_VIRTUAL_TABLE_SUFFIX_CHECK_NAME(view, boundary, false);
        if (status != LXA_OK) return status;
        plan.action = LXA_HTML_TABLE_START_POP_IGNORE;
        plan.insertion.keep = boundary;
        plan.table_pop = true;
        goto publish;
    }
    if (active > 2 && !((cell || row)
        && (mode == LXA_TEMPLATE_TABLE_BODY || mode == LXA_TEMPLATE_TABLE))
        && !(mode == LXA_TEMPLATE_TABLE && (section || caption || group)))
        return LXA_ERROR_INTERNAL;
    if (active == 0) return LXA_ERROR_INTERNAL;
    status = LXA_SCOPE_READ(view, length - 1, &top, &ns);
    if (status != LXA_OK) return status;
    if (active == 1 && mode == LXA_TEMPLATE_TABLE_BODY
        && lxa_html_tree_name_equals(top, "tr", 2) && (section || caption || group)) {
        plan.action = LXA_HTML_TABLE_START_POP_IGNORE;
        plan.insertion.keep = boundary;
        goto publish;
    }
    if (active == 1 && mode == LXA_TEMPLATE_TABLE
        && lxa_html_tree_name_equals(top, "colgroup", 8)) {
        plan.action = LXA_HTML_TABLE_START_POP_REPROCESS;
        plan.insertion.keep = boundary;
        goto publish;
    }
    if (active == 1 && mode == LXA_TEMPLATE_TABLE && lxa_html_tree_table_section(top)) {
        if (cell) {
            plan.insertion.implied_row = true;
            plan.insertion.extra = 2;
        }
        if (section || caption || group) plan.insertion.keep = boundary;
    }
    else if (active == 2 && mode == LXA_TEMPLATE_TABLE
        && lxa_html_tree_name_equals(top, "tr", 2) && (row || cell)) {
        status = LXA_SCOPE_READ(view, boundary, &root, &ns);
        if (status != LXA_OK) return status;
        if (!lxa_html_tree_table_section(root)) return LXA_ERROR_INTERNAL;
        if (row) plan.insertion.keep = length - 1;
    }
    else if (active == 2 && mode == LXA_TEMPLATE_TABLE_BODY
        && (lxa_html_tree_name_equals(top, "td", 2) || lxa_html_tree_name_equals(top, "th", 2))
        && (row || cell)) {
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary, &parent, &ns);
        if (status != LXA_OK) return status;
        if (!lxa_html_tree_name_equals(parent, "tr", 2)) return LXA_ERROR_INTERNAL;
        plan.insertion.keep = boundary + (cell ? 1 : 0);
    }
    else if (active == 3 && mode == LXA_TEMPLATE_TABLE
        && (lxa_html_tree_name_equals(top, "td", 2) || lxa_html_tree_name_equals(top, "th", 2))
        && (row || cell)) {
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            2, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, length - 2, &parent, &ns);
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, boundary, &root, &ns);
        if (status != LXA_OK) return status;
        if (!lxa_html_tree_name_equals(parent, "tr", 2) || !lxa_html_tree_table_section(root))
            return LXA_ERROR_INTERNAL;
        plan.insertion.keep = length - (row ? 2 : 1);
    }
    else if (mode == LXA_TEMPLATE_TABLE && (section || caption || group) && active >= 2) {
        status = LXA_VIRTUAL_TABLE_SUFFIX_CHECK_NAME(view, boundary, true);
        if (status != LXA_OK) return status;
        plan.insertion.keep = boundary;
    }
    else if (active >= 2 && (cell || row)
        && (mode == LXA_TEMPLATE_TABLE_BODY || mode == LXA_TEMPLATE_TABLE)) {
        status = LXA_VIRTUAL_TABLE_ITEM_PLAN_NAME(view, boundary, mode, target, &plan.insertion);
        if (status != LXA_OK) return status;
    }
    else if (row && active == 1 && mode == LXA_TEMPLATE_TABLE_BODY
        && lxa_html_tree_name_equals(top, "tr", 2))
        plan.insertion.keep = boundary;
    else if (!lxa_html_tree_name_equals(top, "tr", 2) || (active == 1 && !cell)
        || (active == 2 && (mode != LXA_TEMPLATE_TABLE || (!cell && !section && !caption && !group))))
        return LXA_ERROR_INTERNAL;
    else if (active == 2) {
        status = LXA_SCOPE_READ(view, length - 2, &root, &ns);
        if (status != LXA_OK) return status;
        if (!lxa_html_tree_table_section(root)) return LXA_ERROR_INTERNAL;
        if (section || caption || group) plan.insertion.keep = boundary;
    }
publish:
    *out = plan;
    return LXA_OK;
}

static inline lxa_status_t
LXA_SCOPE_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, lxa_span_t target,
               lxa_html_scope_kind_t scope, size_t *index)
{
    bool any_heading = lxa_html_tree_heading(target);
    size_t i = LXA_SCOPE_LENGTH(view);
    *index = i;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(
            LXA_SCOPE_BUDGET(view), 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML
            && (any_heading ? lxa_html_tree_heading(name)
                : lxa_html_tree_name_equals(name, (const char *) target.data,
                                             target.length))) {
            *index = i;
            return LXA_OK;
        }
        if (lxa_html_tree_scope_boundary(name, ns, scope)) break;
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_COLUMN_GROUP_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, bool *found)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    *found = false;
    if (length > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = LXA_SCOPE_READ(view, length - 1, &name, &ns);
        if (status != LXA_OK) return status;
        *found = ns == LXA_DOM_NS_HTML
            && lxa_html_tree_name_equals(name, "colgroup", 8);
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_SCOPE_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, lxa_span_t target,
                    lxa_html_scope_kind_t scope, lxa_html_end_target_plan_t *out)
{
    LXA_SCOPE_VIEW search = *view;
    lxa_html_end_target_plan_t plan;
    bool column_group;
    lxa_status_t status = LXA_COLUMN_GROUP_NAME(view, boundary, &column_group);
    if (status != LXA_OK) return status;
    column_group = column_group && !lxa_html_tree_name_equals(target, "colgroup", 8);
    if (column_group) --LXA_SCOPE_LENGTH(&search);
    plan.search_length = LXA_SCOPE_LENGTH(&search);
    status = LXA_SCOPE_NAME(&search, boundary, target, scope, &plan.index);
    if (status == LXA_OK && column_group)
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                                   1, LXA_SCOPE_REASON(view));
    if (status == LXA_OK) *out = plan;
    return status;
}

static inline lxa_status_t
LXA_OPEN_REFERENCE_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                        uint64_t target, size_t *index)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    *index = length;
    for (size_t i = boundary; i < length; ++i) {
        uint64_t identity;
        lxa_status_t status = lxa_budget_charge_work(
            LXA_SCOPE_BUDGET(view), 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_IDENTITY(view, i, &identity);
        if (status != LXA_OK) return status;
        if (identity == target) {
            *index = i;
            break;
        }
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_SCOPED_REFERENCE_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                          uint64_t target, size_t *index)
{
    size_t i = LXA_SCOPE_LENGTH(view);
    *index = i;
    while (i > boundary) {
        uint64_t identity;
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(
            LXA_SCOPE_BUDGET(view), 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_IDENTITY(view, --i, &identity);
        if (status != LXA_OK) return status;
        if (identity == target) {
            *index = i;
            break;
        }
        status = LXA_SCOPE_READ(view, i, &name, &ns);
        if (status != LXA_OK) return status;
        if (lxa_html_tree_scope_boundary(name, ns, LXA_HTML_SCOPE_NORMAL)) break;
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_ORDINARY_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, lxa_span_t target,
                  size_t *index)
{
    size_t i = LXA_SCOPE_LENGTH(view);
    *index = i;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(
            LXA_SCOPE_BUDGET(view), 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML && name.length == target.length) {
            size_t blocks = name.length / 8 + (name.length % 8 != 0);
            status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                                       blocks, LXA_SCOPE_REASON(view));
            if (status != LXA_OK) return status;
            if (lxa_html_tree_name_equals(name, (const char *) target.data,
                                          target.length)) {
                *index = i;
                return LXA_OK;
            }
        }
        if (lxa_html_tree_special_element(name, ns)) break;
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_ORDINARY_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                       lxa_span_t target, lxa_html_end_target_plan_t *out)
{
    LXA_SCOPE_VIEW search = *view;
    lxa_html_end_target_plan_t plan;
    bool column_group;
    lxa_status_t status = LXA_COLUMN_GROUP_NAME(view, boundary, &column_group);
    if (status != LXA_OK) return status;
    if (column_group) --LXA_SCOPE_LENGTH(&search);
    plan.search_length = LXA_SCOPE_LENGTH(&search);
    status = LXA_ORDINARY_NAME(&search, boundary, target, &plan.index);
    if (status == LXA_OK && column_group)
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                                   1, LXA_SCOPE_REASON(view));
    if (status == LXA_OK) *out = plan;
    return status;
}
static inline lxa_status_t
LXA_TEMPLATE_MISSING_TABLE_PLAN_NAME(const LXA_SCOPE_VIEW *view,
    size_t boundary, size_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view), keep = length;
    lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
        length - boundary, LXA_SCOPE_REASON(view));
    if (status != LXA_OK) return status;
    for (size_t i = length; i > boundary; ) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) continue;
        if (lxa_html_tree_name_equals(name, "td", 2)
            || lxa_html_tree_name_equals(name, "th", 2))
            break;
        if (lxa_html_tree_name_equals(name, "tr", 2)) keep = i;
        else if (lxa_html_tree_table_section(name)
                 || lxa_html_tree_name_equals(name, "caption", 7)
                 || lxa_html_tree_name_equals(name, "colgroup", 8)) {
            keep = i;
            break;
        }
    }
    *out = keep;
    return LXA_OK;
}

static inline lxa_status_t
LXA_TEMPLATE_END_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    lxa_html_token_kind_t kind, lxa_span_t target, lxa_html_template_end_plan_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view), index;
    lxa_html_template_end_plan_t plan = {length, LXA_HTML_TEMPLATE_END_CONTINUE};
    if (kind != LXA_HTML_END_TAG) goto publish;
    bool part = lxa_html_tree_table_part(target);
    if (!part && !lxa_html_tree_formatting_marker_tag(target)) goto publish;
    lxa_status_t status = LXA_SCOPE_NAME(view, boundary, target,
        part ? LXA_HTML_SCOPE_TABLE : LXA_HTML_SCOPE_NORMAL, &index);
    if (status != LXA_OK) return status;
    plan.action = LXA_HTML_TEMPLATE_END_IGNORE;
    if (index != length) {
        plan.keep = index;
        plan.action = part ? LXA_HTML_TEMPLATE_END_POP_TABLE
                           : LXA_HTML_TEMPLATE_END_POP_MARKER;
    }
    else if (lxa_html_tree_name_equals(target, "table", 5)) {
        status = LXA_TEMPLATE_MISSING_TABLE_PLAN_NAME(view, boundary, &plan.keep);
        if (status != LXA_OK) return status;
        if (plan.keep != length) plan.action = LXA_HTML_TEMPLATE_END_POP_TABLE;
    }
    else if (!lxa_html_tree_name_equals(target, "col", 3) && length > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                                   1, LXA_SCOPE_REASON(view));
        if (status == LXA_OK) status = LXA_SCOPE_READ(view, length - 1, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML && lxa_html_tree_name_equals(name, "colgroup", 8)) {
            plan.keep = length - 1;
            plan.action = LXA_HTML_TEMPLATE_END_POP_TABLE;
        }
    }
publish:
    *out = plan;
    return LXA_OK;
}

static inline lxa_status_t
LXA_LIST_ITEM_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
    bool definition, size_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view), index = length;
    for (size_t i = length; i > boundary; ) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML
            && (lxa_html_tree_name_equals(name, "ul", 2)
                || lxa_html_tree_name_equals(name, "ol", 2)
                || lxa_html_tree_name_equals(name, "dl", 2)
                || lxa_html_tree_name_equals(name, "menu", 4)))
            break;
        if (ns == LXA_DOM_NS_HTML
            && (definition ? lxa_html_tree_name_equals(name, "dt", 2)
                             || lxa_html_tree_name_equals(name, "dd", 2)
                           : lxa_html_tree_name_equals(name, "li", 2))) {
            index = i;
            break;
        }
        if (lxa_html_tree_special_element(name, ns)
            && !(ns == LXA_DOM_NS_HTML
                 && (lxa_html_tree_name_equals(name, "address", 7)
                     || lxa_html_tree_name_equals(name, "div", 3)
                     || lxa_html_tree_name_equals(name, "p", 1))))
            break;
    }
    *out = index;
    return LXA_OK;
}

static inline lxa_status_t
LXA_IMPLIED_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                      lxa_span_t except, size_t *out)
{
    size_t keep = LXA_SCOPE_LENGTH(view);
    while (keep > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(
            LXA_SCOPE_BUDGET(view), 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, keep - 1, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML || !lxa_html_tree_implied_end_tag(name)
            || (except.length != 0
                && lxa_html_tree_name_equals(name, (const char *) except.data,
                                             except.length)))
            break;
        --keep;
    }
    *out = keep;
    return LXA_OK;
}

static inline lxa_status_t
LXA_RUBY_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                   bool except_rtc, size_t *out)
{
    static const uint8_t ruby[] = "ruby", rtc[] = "rtc";
    size_t index, keep = LXA_SCOPE_LENGTH(view);
    lxa_status_t status = LXA_SCOPE_NAME(view, boundary,
        (lxa_span_t){ruby, sizeof(ruby) - 1}, LXA_HTML_SCOPE_NORMAL, &index);
    if (status != LXA_OK) return status;
    if (index != keep) {
        lxa_span_t except = except_rtc
            ? (lxa_span_t){rtc, sizeof(rtc) - 1} : (lxa_span_t){NULL, 0};
        status = LXA_IMPLIED_PLAN_NAME(view, index + 1, except, &keep);
        if (status != LXA_OK) return status;
    }
    *out = keep;
    return LXA_OK;
}

static inline lxa_status_t
LXA_SELECT_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                     lxa_html_token_kind_t kind, lxa_span_t token_name,
                     lxa_html_select_plan_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view), select = length, i = length;
    lxa_html_select_plan_t plan = {length, false};
    bool option = lxa_html_tree_name_equals(token_name, "option", 6);
    bool group = lxa_html_tree_name_equals(token_name, "optgroup", 8);
    bool select_token = lxa_html_tree_name_equals(token_name, "select", 6);
    bool input = lxa_html_tree_name_equals(token_name, "input", 5);
    bool hr = lxa_html_tree_name_equals(token_name, "hr", 2);
    bool foreign_end = kind == LXA_HTML_END_TAG, foreign_boundary = false;
    lxa_status_t status;
    if (kind == LXA_HTML_END_TAG) {
        if (!select_token) goto publish;
        plan.ignore = true;
    }
    else if (kind != LXA_HTML_START_TAG
             || (!select_token && !input && !hr && !option && !group))
        goto publish;
    if (kind == LXA_HTML_START_TAG && length > boundary) {
        lxa_dom_namespace_t ns;
        status = LXA_SCOPE_NAMESPACE(view, length - 1, &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) goto publish;
    }
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (kind == LXA_HTML_END_TAG && i == length - 1
            && ns == LXA_DOM_NS_HTML && lxa_html_tree_name_equals(name, "colgroup", 8)) {
            plan.keep = i;
            goto publish;
        }
        if (foreign_end && ns != LXA_DOM_NS_HTML) {
            foreign_boundary |= lxa_html_tree_scope_boundary(name, ns, LXA_HTML_SCOPE_NORMAL);
            continue;
        }
        foreign_end = false;
        if (foreign_boundary) goto publish;
        if (ns == LXA_DOM_NS_HTML && lxa_html_tree_name_equals(name, "select", 6)) {
            select = i;
            break;
        }
        if (lxa_html_tree_scope_boundary(name, ns, LXA_HTML_SCOPE_NORMAL)) break;
    }
    if (select != length && (select_token || input)) {
        plan.keep = select;
        plan.ignore = select_token;
        goto publish;
    }
    if ((!option && !group && !hr) || (select == length && !option && !group))
        goto publish;
    if (select != length) {
        static const uint8_t optgroup[] = "optgroup";
        lxa_span_t except = option ? (lxa_span_t){optgroup, sizeof(optgroup) - 1}
                                   : (lxa_span_t){NULL, 0};
        status = LXA_IMPLIED_PLAN_NAME(view, boundary, except, &plan.keep);
        if (status != LXA_OK) return status;
    }
    else if (length > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, length - 1, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML && lxa_html_tree_name_equals(name, "option", 6))
            --plan.keep;
    }
publish:
    *out = plan;
    return LXA_OK;
}

static inline lxa_status_t
LXA_TABLE_CONTEXT_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                            bool virtual_table, lxa_html_table_context_plan_t *out)
{
    size_t length = LXA_SCOPE_LENGTH(view), i = length;
    lxa_html_table_context_plan_t plan = {length, virtual_table};
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) continue;
        if (lxa_html_tree_name_equals(name, "td", 2)
            || lxa_html_tree_name_equals(name, "th", 2)
            || lxa_html_tree_name_equals(name, "caption", 7)) {
            plan.in_table = false;
            break;
        }
        if (lxa_html_tree_table_context(name)) {
            plan.in_table = true;
            if (i == length - 1 && lxa_html_tree_name_equals(name, "colgroup", 8)) {
                status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
                    1, LXA_SCOPE_REASON(view));
                if (status != LXA_OK) return status;
                plan.keep = i;
            }
            break;
        }
    }
    *out = plan;
    return LXA_OK;
}

static inline lxa_status_t
LXA_FORM_END_PLAN_NAME(const LXA_SCOPE_VIEW *view, uint64_t identity,
    lxa_html_form_end_plan_t *out)
{
    bool column_group;
    lxa_status_t status = LXA_COLUMN_GROUP_NAME(view, 0, &column_group);
    if (status != LXA_OK) return status;
    LXA_SCOPE_VIEW search = *view;
    if (column_group) --LXA_SCOPE_LENGTH(&search);
    size_t length = LXA_SCOPE_LENGTH(&search), index = length;
    if (identity != 0)
        status = LXA_SCOPED_REFERENCE_NAME(&search, 0, identity, &index);
    if (status == LXA_OK && column_group)
        status = lxa_budget_charge_work(LXA_SCOPE_BUDGET(view),
            1, LXA_SCOPE_REASON(view));
    if (status != LXA_OK) return status;
    lxa_html_form_end_plan_t plan = {length, index, index != length};
    if (plan.found) {
        status = LXA_IMPLIED_PLAN_NAME(&search, index + 1,
            (lxa_span_t){NULL, 0}, &plan.keep);
        if (status != LXA_OK) return status;
    }
    *out = plan;
    return LXA_OK;
}
