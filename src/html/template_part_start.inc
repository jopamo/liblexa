/* Continue START after earlier routing and the current-entry phase. */
static inline lxa_status_t
LXA_TEMPLATE_PART_START_NAME(LXA_TEMPLATE_PART_START_VIEW *view,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_status_t status;
    if (lxa_html_tree_name_equals(token->name, "col", 3))
        status = LXA_TEMPLATE_PART_START_COLUMN(view, token);
    else if (lxa_html_tree_table_part(token->name)
        && !lxa_html_tree_name_equals(token->name, "table", 5))
        status = LXA_TEMPLATE_PART_START_PART(view, token);
    else {
        *handled = false;
        return LXA_OK;
    }
    if (status == LXA_OK) *handled = true;
    return status;
}
