/* Virtual tables ignore; actual table cleanup precedes full token reprocessing. */
static inline lxa_status_t
LXA_TEMPLATE_TABLE_TOKEN_NAME(LXA_TEMPLATE_TABLE_TOKEN_VIEW *view,
    lxa_html_template_table_action_t *action)
{
    lxa_html_table_context_plan_t plan;
    lxa_status_t status = LXA_TEMPLATE_TABLE_TOKEN_PLAN(view, &plan);
    if (status != LXA_OK) return status;
    lxa_html_template_table_action_t out = LXA_HTML_TEMPLATE_TABLE_CONTINUE;
    if (plan.in_table) {
        size_t index;
        status = LXA_TEMPLATE_TABLE_TOKEN_SCOPE(view, &index);
        if (status != LXA_OK) return status;
        if (index == LXA_TEMPLATE_TABLE_TOKEN_LENGTH(view)) {
            status = LXA_TEMPLATE_TABLE_TOKEN_POP(view, plan.keep);
            out = LXA_HTML_TEMPLATE_TABLE_IGNORE;
        }
        else {
            status = LXA_TEMPLATE_TABLE_TOKEN_PREFLIGHT(view);
            if (status == LXA_OK) status = LXA_TEMPLATE_TABLE_TOKEN_POP_TABLE(view, index);
            out = LXA_HTML_TEMPLATE_TABLE_REPROCESS;
        }
    }
    if (status == LXA_OK) *action = out;
    return status;
}
