/* Instantiate accepted in-body body/html ends over a prepared root owner. */
static inline lxa_status_t
LXA_BODY_END_NAME(const LXA_BODY_END_VIEW *view, bool html_end,
    lxa_html_root_mode_t *mode)
{
    lxa_html_end_target_plan_t plan;
    lxa_status_t status = LXA_BODY_END_PLAN(view, &plan);
    if (status != LXA_OK) return status;
    if (plan.search_length != LXA_BODY_END_LENGTH(view)) {
        status = LXA_BODY_END_POP(view, plan.search_length);
        if (status != LXA_OK) return status;
    }
    if (plan.index != plan.search_length)
        *mode = html_end ? LXA_TREE_AFTER_HTML : LXA_TREE_AFTER_BODY;
    return LXA_OK;
}
