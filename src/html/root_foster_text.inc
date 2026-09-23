/* Publish text feedback/newline before recovering the saved column group. */
static inline lxa_status_t
LXA_ROOT_FOSTER_TEXT_NAME(LXA_ROOT_FOSTER_TEXT_VIEW *view,
    const lxa_html_token_t *token, bool column_group, bool *pending_newline)
{
    lxa_status_t status = LXA_ROOT_FOSTER_TEXT_INSERT(view, token);
    if (status == LXA_OK) status = LXA_ROOT_FOSTER_TEXT_ENTER(view, token);
    if (status != LXA_OK) return status;
    if (lxa_html_tree_name_equals(token->name, "textarea", 8)) *pending_newline = true;
    return column_group ? LXA_ROOT_FOSTER_TEXT_POP(view) : LXA_OK;
}
