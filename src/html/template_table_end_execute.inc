/* Consume one copied current-entry END route after earlier recovery. */
static inline lxa_status_t
LXA_TEMPLATE_TABLE_END_NAME(LXA_TEMPLATE_TABLE_END_VIEW *view,
    const lxa_html_token_t *token, lxa_html_template_table_end_result_t *result)
{
    lxa_html_template_current_route_t route;
    lxa_status_t status = LXA_TEMPLATE_TABLE_END_READ(view, token, &route);
    if (status != LXA_OK) return status;
    lxa_html_template_table_end_result_t out = LXA_HTML_TEMPLATE_TABLE_RESULT_CONTINUE;
    switch (route.action) {
    case LXA_HTML_TEMPLATE_CURRENT_CONTINUE:
        break;
    case LXA_HTML_TEMPLATE_CURRENT_REPROCESS_END:
        status = LXA_TEMPLATE_TABLE_END_POP(view);
        out = LXA_HTML_TEMPLATE_TABLE_RESULT_REPROCESS;
        break;
    case LXA_HTML_TEMPLATE_CURRENT_TABLE:
        switch (lxa_html_tree_template_table_end_action(token->name, route.column_group)) {
        case LXA_HTML_TEMPLATE_TABLE_END_CONTINUE:
            break;
        case LXA_HTML_TEMPLATE_TABLE_END_IGNORE:
            out = LXA_HTML_TEMPLATE_TABLE_RESULT_IGNORE;
            break;
        case LXA_HTML_TEMPLATE_TABLE_END_POP_IGNORE:
            status = LXA_TEMPLATE_TABLE_END_POP(view);
            out = LXA_HTML_TEMPLATE_TABLE_RESULT_IGNORE;
            break;
        case LXA_HTML_TEMPLATE_TABLE_END_PARAGRAPH: {
            if (route.column_group) status = LXA_TEMPLATE_TABLE_END_POP(view);
            lxa_html_token_t adjusted = *token;
            adjusted.kind = LXA_HTML_START_TAG;
            if (status == LXA_OK) status = LXA_TEMPLATE_TABLE_END_PARAGRAPH(view, &adjusted);
            out = LXA_HTML_TEMPLATE_TABLE_RESULT_PARAGRAPH;
            break;
        }
        case LXA_HTML_TEMPLATE_TABLE_END_ORDINARY:
            status = LXA_TEMPLATE_TABLE_END_ORDINARY(view, token);
            out = LXA_HTML_TEMPLATE_TABLE_RESULT_IGNORE;
            break;
        case LXA_HTML_TEMPLATE_TABLE_END_FORMATTING:
            status = LXA_TEMPLATE_TABLE_END_WORK(view);
            if (status == LXA_OK) status = LXA_TEMPLATE_TABLE_END_POP(view);
            out = LXA_HTML_TEMPLATE_TABLE_RESULT_REPROCESS;
            break;
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        break;
    default:
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    if (status == LXA_OK) *result = out;
    return status;
}
