/* Continue START after early routing, current-entry dispatch and table parts. */
static inline lxa_status_t
LXA_TEMPLATE_HTML_START_NAME(LXA_TEMPLATE_HTML_START_VIEW *view,
    const lxa_html_token_t *token, lxa_html_template_html_start_action_t *action)
{
    lxa_status_t status;
    lxa_html_template_html_start_action_t out;
    if (lxa_html_tree_name_equals(token->name, "template", 8)) {
        status = LXA_TEMPLATE_HTML_START_TEMPLATE(view, token);
        out = LXA_HTML_TEMPLATE_HTML_START_TEMPLATE;
    } else if (lxa_html_tree_head_void(token->name) || lxa_html_tree_body_void(token->name)
        || lxa_html_tree_name_equals(token->name, "image", 5)) {
        status = LXA_TEMPLATE_HTML_START_CLOSED(view, token);
        out = LXA_HTML_TEMPLATE_HTML_START_CLOSED;
    } else if (lxa_html_tree_returning_text_element(token->name)
        || lxa_html_tree_name_equals(token->name, "xmp", 3)) {
        status = LXA_TEMPLATE_HTML_START_TEXT(view, token);
        out = LXA_HTML_TEMPLATE_HTML_START_TEXT;
    } else {
        status = LXA_OK;
        out = lxa_html_tree_name_equals(token->name, "plaintext", 9)
            ? LXA_HTML_TEMPLATE_HTML_START_PLAINTEXT
            : LXA_HTML_TEMPLATE_HTML_START_ORDINARY;
    }
    if (status == LXA_OK) *action = out;
    return status;
}
