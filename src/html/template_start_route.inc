/* Continue START after earlier misc/foreign/end dispatch and intake. */
static inline lxa_status_t
LXA_TEMPLATE_START_ROUTE_NAME(LXA_TEMPLATE_START_ROUTE_VIEW *view,
    const lxa_html_token_t *token, lxa_html_template_start_action_t *action)
{
    bool handled;
    lxa_status_t status = LXA_TEMPLATE_START_ROUTE_PRELUDE(view, token, &handled);
    if (status != LXA_OK) return status;
    if (handled) goto consumed;
    if (lxa_html_tree_name_equals(token->name, "form", 4)) {
        status = LXA_TEMPLATE_START_ROUTE_FORM(view, token, &handled);
        if (status != LXA_OK) return status;
        if (handled) goto consumed;
    }
    if (lxa_html_tree_name_equals(token->name, "svg", 3)
        || lxa_html_tree_name_equals(token->name, "math", 4)) {
        status = LXA_TEMPLATE_START_ROUTE_FOREIGN(view, token, &handled);
        if (status != LXA_OK) return status;
        if (handled) goto consumed;
    }
    if (lxa_html_tree_name_equals(token->name, "col", 3)
        && !LXA_TEMPLATE_START_ROUTE_CURRENT(view)) {
        status = LXA_TEMPLATE_START_ROUTE_COLUMN(view, token);
        if (status != LXA_OK) return status;
        goto consumed;
    }
    if (lxa_html_tree_name_equals(token->name, "table", 5)) {
        lxa_html_template_table_action_t table;
        status = LXA_TEMPLATE_START_ROUTE_TABLE(view, token, &table);
        if (status != LXA_OK) return status;
        switch (table) {
        case LXA_HTML_TEMPLATE_TABLE_CONTINUE: break;
        case LXA_HTML_TEMPLATE_TABLE_IGNORE: goto consumed;
        case LXA_HTML_TEMPLATE_TABLE_REPROCESS:
            *action = LXA_HTML_TEMPLATE_START_REPROCESS;
            return LXA_OK;
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
    }
    *action = LXA_HTML_TEMPLATE_START_CONTINUE;
    return LXA_OK;
consumed:
    *action = LXA_HTML_TEMPLATE_START_CONSUMED;
    return LXA_OK;
}
