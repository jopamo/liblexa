/* Insert an accepted hidden table input before removing its colgroup suffix. */
static inline lxa_status_t
LXA_HIDDEN_TABLE_NAME(LXA_HIDDEN_TABLE_VIEW *view,
    const lxa_html_token_t *token, bool *handled)
{
    *handled = false;
    if (token->kind != LXA_HTML_START_TAG
        || !lxa_html_tree_name_equals(token->name, "input", 5)) return LXA_OK;
    bool hidden;
    lxa_status_t status = lxa_html_tree_hidden_input(token,
        LXA_HIDDEN_TABLE_BUDGET(view), LXA_HIDDEN_TABLE_REASON(view), &hidden);
    if (status != LXA_OK || !hidden) return status;
    lxa_html_table_context_plan_t plan;
    status = LXA_HIDDEN_TABLE_PLAN(view, &plan);
    if (status != LXA_OK || !plan.in_table) return status;
    status = LXA_HIDDEN_TABLE_INSERT(view, token, plan.keep);
    if (status == LXA_OK) status = LXA_HIDDEN_TABLE_POP(view, plan.keep);
    if (status == LXA_OK) *handled = true;
    return status;
}
