/* Execute one copied current-entry route after early START routing. */
static inline lxa_status_t
LXA_TEMPLATE_CURRENT_START_NAME(LXA_TEMPLATE_CURRENT_START_VIEW *view,
    const lxa_html_token_t *token, lxa_html_template_start_action_t *action)
{
    lxa_html_template_current_route_t route;
    lxa_status_t status = LXA_TEMPLATE_CURRENT_START_READ(view, token, &route);
    if (status != LXA_OK) return status;
    lxa_html_template_start_action_t out = LXA_HTML_TEMPLATE_START_CONTINUE;
    switch (route.action) {
        case LXA_HTML_TEMPLATE_CURRENT_CONTINUE:
            break;
        case LXA_HTML_TEMPLATE_CURRENT_REPROCESS_START:
            if (lxa_html_tree_template_html_text_element(token->name))
                status = LXA_TEMPLATE_CURRENT_START_TEXT_CHECK(view);
            if (status == LXA_OK) status = LXA_TEMPLATE_CURRENT_START_PREFLIGHT(view);
            if (status == LXA_OK) status = LXA_TEMPLATE_CURRENT_START_POP(view);
            out = LXA_HTML_TEMPLATE_START_REPROCESS;
            break;
        case LXA_HTML_TEMPLATE_CURRENT_TABLE: {
            bool handled;
            status = LXA_TEMPLATE_CURRENT_START_TABLE(view, token, route.column_group, &handled);
            if (status == LXA_OK && handled) out = LXA_HTML_TEMPLATE_START_CONSUMED;
            break;
        }
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
    if (status == LXA_OK) *action = out;
    return status;
}
