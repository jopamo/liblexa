/* Root comment/PI preparation and insertion; late DOCTYPE has no effects. */
static inline lxa_status_t
LXA_ROOT_MISC_NAME(LXA_ROOT_MISC_VIEW *view, const lxa_html_token_t *token,
    lxa_html_root_mode_t mode)
{
    lxa_html_root_misc_plan_t plan;
    lxa_status_t status = lxa_html_tree_root_misc_plan(mode, token->kind, &plan);
    if (status != LXA_OK) return status;
    if (plan.ensure_html) {
        status = LXA_ROOT_MISC_HTML(view);
        if (status != LXA_OK) return status;
    }
    return LXA_ROOT_MISC_INSERT(view, token, plan.destination);
}
