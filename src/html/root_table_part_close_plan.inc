/* Locate the part removed during accepted root table-part reprocessing. */
static inline lxa_status_t
LXA_ROOT_PART_CLOSE_NAME(const LXA_ROOT_PART_CLOSE_VIEW *view,
    size_t boundary, lxa_html_table_group_t group, size_t *out)
{
    size_t length = LXA_ROOT_PART_CLOSE_LENGTH(view);
    if (boundary > length || out == NULL || (unsigned) group > LXA_HTML_TABLE_SECTION)
        return LXA_ERROR_ARGUMENT;
    size_t index = length;
    size_t i = length;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(LXA_ROOT_PART_CLOSE_BUDGET(view),
            LXA_BUDGET_WORK, 1, LXA_ROOT_PART_CLOSE_REASON(view));
        if (status == LXA_OK) status = LXA_ROOT_PART_CLOSE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) continue;
        if ((group == LXA_HTML_TABLE_CELL
                && (lxa_html_tree_name_equals(name, "td", 2)
                    || lxa_html_tree_name_equals(name, "th", 2)))
            || (group == LXA_HTML_TABLE_ROW && lxa_html_tree_name_equals(name, "tr", 2))
            || (group == LXA_HTML_TABLE_SECTION && lxa_html_tree_table_section(name))) {
            index = i;
            break;
        }
        if (lxa_html_tree_name_equals(name, "table", 5)
            || (group == LXA_HTML_TABLE_CELL && lxa_html_tree_table_context(name))
            || (group == LXA_HTML_TABLE_ROW && lxa_html_tree_table_section(name)))
            break;
    }
    *out = index;
    return LXA_OK;
}
