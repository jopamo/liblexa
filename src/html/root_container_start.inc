/* Accepted non-formatting containers after specialized start dispatch. */
static inline lxa_status_t
LXA_ROOT_CONTAINER_START_NAME(LXA_ROOT_CONTAINER_START_VIEW *view,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, bool *skip_newline)
{
    lxa_status_t status = LXA_ROOT_CONTAINER_START_BODY(view);
    if (status != LXA_OK) return status;
    *mode = LXA_TREE_IN_BODY;
    if (LXA_ROOT_CONTAINER_START_FRAMESET(view)
        && lxa_html_tree_root_container_disables_frameset(token->name))
        LXA_ROOT_CONTAINER_START_FRAMESET(view) = false;
    status = LXA_ROOT_CONTAINER_START_PREPARE(view, token->name);
    if (status == LXA_OK && lxa_html_tree_container_reconstructs(token->name))
        status = LXA_ROOT_CONTAINER_START_RECONSTRUCT(view);
    if (status == LXA_OK) status = LXA_ROOT_CONTAINER_START_INSERT(view, token);
    if (status == LXA_OK)
        *skip_newline = lxa_html_tree_name_equals(token->name, "pre", 3)
            || lxa_html_tree_name_equals(token->name, "listing", 7);
    return status;
}
