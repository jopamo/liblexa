/* Gate accepted template tokens, then deliver virtual-group spaces. */
static inline lxa_status_t
LXA_TEMPLATE_COLUMN_GROUP_NAME(LXA_TEMPLATE_COLUMN_GROUP_VIEW *view,
    const lxa_html_token_t *token, lxa_html_column_group_action_t *out)
{
    lxa_html_column_group_action_t action;
    lxa_status_t status = LXA_TEMPLATE_COLUMN_GROUP_PLAN(view, token, &action);
    if (status == LXA_OK && action == LXA_HTML_COLUMN_GROUP_SPACE_TEXT)
        status = LXA_TEMPLATE_COLUMN_GROUP_EMIT(view, token->data);
    if (status == LXA_OK) *out = action;
    return status;
}
