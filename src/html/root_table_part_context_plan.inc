/* Recover ordinary descendants to the first eligible actual table context. */
static inline lxa_status_t
LXA_ROOT_TABLE_PART_CONTEXT_NAME(const LXA_ROOT_TABLE_PART_CONTEXT_VIEW *view,
    size_t boundary, lxa_span_t top, lxa_span_t target,
    lxa_html_root_table_part_context_plan_t *out)
{
    size_t length = LXA_ROOT_TABLE_PART_CONTEXT_LENGTH(view);
    if (boundary > length || out == NULL || !lxa_html_tree_table_part(target)
        || lxa_html_tree_name_equals(target, "table", 5)) return LXA_ERROR_ARGUMENT;
    lxa_html_root_table_part_context_plan_t plan = {length, 0, 0, false};
    bool column = lxa_html_tree_name_equals(target, "col", 3)
        || lxa_html_tree_name_equals(target, "colgroup", 8);
    if (!lxa_html_tree_table_context(top)
        || (column && !lxa_html_tree_name_equals(top, "table", 5)
            && !lxa_html_tree_name_equals(top, "colgroup", 8))) {
        size_t i = length;
        while (i > boundary) {
            lxa_span_t name;
            lxa_dom_namespace_t ns;
            lxa_status_t status = lxa_budget_charge(LXA_ROOT_TABLE_PART_CONTEXT_BUDGET(view),
                LXA_BUDGET_WORK, 1, LXA_ROOT_TABLE_PART_CONTEXT_REASON(view));
            if (status == LXA_OK)
                status = LXA_ROOT_TABLE_PART_CONTEXT_READ(view, --i, &name, &ns);
            if (status != LXA_OK) return status;
            if (ns != LXA_DOM_NS_HTML) continue;
            bool cell = lxa_html_tree_name_equals(name, "td", 2)
                || lxa_html_tree_name_equals(name, "th", 2);
            bool section = lxa_html_tree_table_section(name);
            bool row = lxa_html_tree_name_equals(name, "tr", 2);
            if (!lxa_html_tree_table_context(name) && !cell
                && !lxa_html_tree_name_equals(name, "caption", 7)) continue;
            if (column && (section || row || cell)) continue;
            if (lxa_html_tree_name_equals(name, "table", 5) || section || row) {
                lxa_html_virtual_table_start_plan_t geometry;
                lxa_html_template_mode_t mode = section ? LXA_TEMPLATE_TABLE_BODY
                    : row                               ? LXA_TEMPLATE_ROW
                                                        : LXA_TEMPLATE_TABLE;
                status = lxa_html_tree_virtual_table_start_plan(mode, target, &geometry);
                if (status != LXA_OK) return status;
                plan.keep = i + 1;
                size_t slots = lxa_html_tree_name_equals(target, "col", 3) ? 1 : geometry.extra;
                size_t removed = length - plan.keep;
                plan.extra = slots > removed ? slots - removed : 0;
                plan.nodes_before_pop = geometry.extra
                    + (mode == LXA_TEMPLATE_TABLE && lxa_html_tree_name_equals(target, "col", 3));
                plan.pop = true;
            }
            break;
        }
    }
    *out = plan;
    return LXA_OK;
}
