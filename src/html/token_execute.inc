/* Select the post-intake owner from the updated mode, not the entry mode. */
static inline lxa_status_t
LXA_TOKEN_EXEC_NAME(LXA_TOKEN_EXEC_VIEW *view,
    const lxa_html_token_t *token)
{
    lxa_html_token_t adjusted;
    bool handled;
    lxa_status_t status = LXA_TOKEN_EXEC_INTAKE(view, &token, &adjusted, &handled);
    if (status != LXA_OK || handled) return status;
    if (LXA_TOKEN_EXEC_MODE(view) == LXA_TREE_TEMPLATE_TEXT)
        return LXA_TOKEN_EXEC_TEMPLATE(view, &token, &adjusted);
    if (token->kind == LXA_HTML_EOF) return LXA_TOKEN_EXEC_EOF(view);
    if (lxa_html_tree_frameset_mode(LXA_TOKEN_EXEC_MODE(view)))
        return LXA_TOKEN_EXEC_FRAMESET(view, token);
    return LXA_TOKEN_EXEC_ROOT(view, &token, &adjusted);
}
