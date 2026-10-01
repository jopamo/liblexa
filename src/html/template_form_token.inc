/* Insert a closed table form before removing its ordinary colgroup suffix. */
static inline lxa_status_t
LXA_TEMPLATE_FORM_NAME(LXA_TEMPLATE_FORM_VIEW *view,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_html_table_context_plan_t plan;
    lxa_status_t status = LXA_TEMPLATE_FORM_PLAN(view, &plan);
    if (status != LXA_OK) return status;
    if (plan.in_table) {
        status = LXA_TEMPLATE_FORM_INSERT(view, token, plan.keep);
        if (status == LXA_OK) status = LXA_TEMPLATE_FORM_POP(view, plan.keep);
    }
    if (status == LXA_OK) *handled = plan.in_table;
    return status;
}
