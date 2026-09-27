/* Emit accepted head-noscript spaces before closing its owner and mode. */
static inline lxa_status_t
LXA_HEAD_NOSCRIPT_EXEC_NAME(LXA_HEAD_NOSCRIPT_EXEC_VIEW *view,
    const lxa_html_token_t *token, lxa_html_head_noscript_plan_t *out)
{
    lxa_html_head_noscript_plan_t plan;
    lxa_status_t status = LXA_HEAD_NOSCRIPT_EXEC_PLAN(view, token, &plan);
    if (status != LXA_OK) return status;
    if (plan.space_prefix != 0) {
        status = LXA_HEAD_NOSCRIPT_EXEC_EMIT(view,
            ((lxa_span_t){token->data.data, plan.space_prefix}));
        if (status != LXA_OK) return status;
    }
    lxa_html_root_mode_t next = lxa_html_tree_head_noscript_next_mode(plan.action);
    if (next != LXA_HEAD_NOSCRIPT_EXEC_MODE(view)) {
        LXA_HEAD_NOSCRIPT_EXEC_CLEAR(view);
        LXA_HEAD_NOSCRIPT_EXEC_MODE(view) = next;
    }
    *out = plan;
    return LXA_OK;
}
