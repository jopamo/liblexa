/* Instantiate accepted root form-end effects over a prepared owner/view. */
static inline lxa_status_t
LXA_FORM_END_EXECUTE_NAME(const LXA_FORM_END_VIEW *view, LXA_FORM_END_IDENTITY_TYPE *owner)
{
    lxa_html_form_end_plan_t plan;
    lxa_status_t status = LXA_FORM_END_PLAN(view, *owner, &plan);
    if (status != LXA_OK) return status;
    if (plan.found) LXA_FORM_END_CLEAR(view, owner);
    status = LXA_FORM_END_POP(view, plan.keep);
    if (status != LXA_OK) return status;
    if (!plan.found) {
        LXA_FORM_END_CLEAR(view, owner);
        return LXA_OK;
    }
    return LXA_FORM_END_REMOVE(view, plan.index);
}
