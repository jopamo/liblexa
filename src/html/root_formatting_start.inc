/* Accepted formatting starts consume one preprocessing/Noah plan. */
static inline lxa_status_t
LXA_ROOT_FORMATTING_START_NAME(LXA_ROOT_FORMATTING_START_VIEW *view,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = LXA_ROOT_FORMATTING_START_BODY(view);
    if (status != LXA_OK) return status;
    *mode = LXA_TREE_IN_BODY;
    if (LXA_ROOT_FORMATTING_START_FRAMESET(view)
        && lxa_html_tree_root_container_disables_frameset(token->name))
        LXA_ROOT_FORMATTING_START_FRAMESET(view) = false;
    size_t drop;
    status = LXA_ROOT_FORMATTING_START_PREPARE(view, token, &drop);
    return status == LXA_OK ? LXA_ROOT_FORMATTING_START_ADMIT(view, token, drop) : status;
}
