/* Returning-text intake; owners supply target lookup, append and restoration. */
static inline lxa_status_t
LXA_TEXT_TOKEN_NAME(LXA_TEXT_TOKEN_VIEW *view, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode, lxa_html_text_action_t *out)
{
    lxa_span_t target = {0};
    lxa_status_t status = LXA_OK;
    if (token->kind == LXA_HTML_END_TAG) status = LXA_TEXT_TOKEN_TARGET(view, &target);
    if (status != LXA_OK) return status;
    lxa_html_text_action_t action;
    status = lxa_html_tree_text_action(token->kind, token->name, target, &action);
    if (status != LXA_OK) return status;
    status = action == LXA_HTML_TEXT_APPEND ? LXA_TEXT_TOKEN_APPEND(view, token)
                                            : LXA_TEXT_TOKEN_RETURN(view, mode);
    if (status == LXA_OK && out != NULL) *out = action;
    return status;
}
