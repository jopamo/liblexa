/* Consume one select plan; owners retain pop failure semantics. */
static inline lxa_status_t
LXA_SELECT_EXECUTE_NAME(const LXA_SELECT_EXECUTE_VIEW *view,
    lxa_html_token_kind_t kind, lxa_span_t name, bool *ignore)
{
    lxa_html_select_plan_t plan;
    lxa_status_t status = LXA_SELECT_EXECUTE_PLAN(view, kind, name, &plan);
    if (status == LXA_OK) status = LXA_SELECT_EXECUTE_POP(view, plan.keep);
    if (status == LXA_OK) *ignore = plan.ignore;
    return status;
}
