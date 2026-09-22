/* Accepted non-table root part after earlier token routing. */
static inline lxa_status_t
LXA_ROOT_PART_START_NAME(LXA_ROOT_PART_START_VIEW *view,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode)
{
    bool found;
    lxa_status_t status = LXA_ROOT_PART_START_CONTEXT(view, token, mode, &found);
    if (status != LXA_OK || !found) return status;
    status = LXA_ROOT_PART_START_CAPTION(view, token->name);
    if (status != LXA_OK) return status;
    lxa_span_t top;
    status = LXA_ROOT_PART_START_TOP(view, &top);
    if (status != LXA_OK) return status;
    if (lxa_html_tree_name_equals(token->name, "caption", 7)
        && !lxa_html_tree_name_equals(top, "table", 5)
        && !lxa_html_tree_name_equals(top, "colgroup", 8)) {
        status = LXA_ROOT_PART_START_CLOSE(view, LXA_HTML_TABLE_SECTION);
        if (status != LXA_OK) return status;
        top = (lxa_span_t){(const uint8_t *) "table", 5};
    }
    if (lxa_html_tree_name_equals(top, "colgroup", 8)
        && !lxa_html_tree_name_equals(token->name, "col", 3)) {
        size_t needed = lxa_html_tree_name_equals(token->name, "td", 2)
            || lxa_html_tree_name_equals(token->name, "th", 2) ? 3
            : lxa_html_tree_name_equals(token->name, "tr", 2) ? 2 : 1;
        status = LXA_ROOT_PART_START_RESERVE(view, needed - 1);
        if (status == LXA_OK) status = LXA_ROOT_PART_START_NODE(view, needed);
        if (status == LXA_OK) status = LXA_ROOT_PART_START_POP(view);
        if (status != LXA_OK) return status;
        top = (lxa_span_t){(const uint8_t *) "table", 5};
    }
    bool section = lxa_html_tree_table_section(token->name);
    bool row = lxa_html_tree_name_equals(token->name, "tr", 2);
    bool cell = lxa_html_tree_name_equals(token->name, "td", 2)
        || lxa_html_tree_name_equals(token->name, "th", 2);
    if ((cell && !lxa_html_tree_table_context(top))
        || (row && !lxa_html_tree_name_equals(top, "table", 5)
            && !lxa_html_tree_table_section(top))
        || (section && !lxa_html_tree_name_equals(top, "table", 5))) {
        status = LXA_ROOT_PART_START_CLOSE(view, cell ? LXA_HTML_TABLE_CELL
            : row ? LXA_HTML_TABLE_ROW : LXA_HTML_TABLE_SECTION);
        if (status == LXA_OK) status = LXA_ROOT_PART_START_TOP(view, &top);
        if (status != LXA_OK) return status;
    }
    lxa_html_prepared_table_part_plan_t plan;
    status = lxa_html_tree_prepared_table_part_plan(top, token->name, &plan);
    if (status == LXA_ERROR_ARGUMENT) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return status == LXA_OK ? LXA_ROOT_PART_START_ADMIT(view, token, &plan) : status;
}
