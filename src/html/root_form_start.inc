/* Accepted form starts after earlier select/table/foreign routing. */
static inline lxa_status_t
LXA_ROOT_FORM_NAME(LXA_ROOT_FORM_VIEW *view, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode)
{
    lxa_status_t status = LXA_ROOT_FORM_BODY(view);
    if (status != LXA_OK) return status;
    *mode = LXA_TREE_IN_BODY;
    lxa_html_table_context_plan_t plan;
    status = LXA_ROOT_FORM_PLAN(view, &plan);
    if (status != LXA_OK) return status;
    if (LXA_ROOT_FORM_OWNED(view)) return LXA_ROOT_FORM_POP(view, plan.keep);
    if (!plan.in_table) return LXA_ROOT_FORM_OPEN(view, token);
    if (plan.keep == 0) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return LXA_ROOT_FORM_CLOSED(view, token, plan.keep);
}
