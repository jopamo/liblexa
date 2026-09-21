/* Prepare root parts through actual-table scope gating and context recovery. */
static inline lxa_status_t
LXA_ROOT_TABLE_PART_RECOVER_NAME(LXA_ROOT_TABLE_PART_RECOVER_VIEW *view,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode, bool *in_scope)
{
    lxa_status_t status = LXA_ROOT_TABLE_PART_RECOVER_BODY(view);
    if (status != LXA_OK) return status;
    *mode = LXA_TREE_IN_BODY;
    bool found;
    status = LXA_ROOT_TABLE_PART_RECOVER_SCOPE(view, &found);
    if (status != LXA_OK) return status;
    if (found) {
        lxa_html_root_table_part_context_plan_t plan;
        status = LXA_ROOT_TABLE_PART_RECOVER_PLAN(view, token->name, &plan);
        if (status != LXA_OK) return status;
        if (plan.pop) {
            status = LXA_ROOT_TABLE_PART_RECOVER_RESERVE(view, plan.extra);
            if (status == LXA_OK) status = LXA_ROOT_TABLE_PART_RECOVER_NODE(view, plan.nodes_before_pop);
            if (status == LXA_OK) status = LXA_ROOT_TABLE_PART_RECOVER_POP(view, plan.keep);
            if (status == LXA_OK) status = LXA_ROOT_TABLE_PART_RECOVER_POSTPOP(view);
            if (status != LXA_OK) return status;
        }
    }
    *in_scope = found;
    return LXA_OK;
}
