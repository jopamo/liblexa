/* Execute one scoped-end plan; owners retain their pop failure semantics. */
static inline lxa_status_t
LXA_SCOPED_END_NAME(const LXA_SCOPED_END_VIEW *view, lxa_span_t target,
    lxa_html_scope_kind_t scope)
{
    lxa_html_end_target_plan_t plan;
    lxa_status_t status = LXA_SCOPED_END_PLAN(view, target, scope, &plan);
    if (status != LXA_OK) return status;
    if (plan.index == plan.search_length) {
        return plan.search_length != LXA_SCOPED_END_LENGTH(view)
            ? LXA_SCOPED_END_POP(view, plan.search_length)
            : LXA_OK;
    }
    if (scope == LXA_HTML_SCOPE_TABLE)
        return LXA_SCOPED_END_TABLE(view, plan.index);
    status = LXA_SCOPED_END_POP(view, plan.index);
    if (status != LXA_OK) return status;
    return lxa_html_tree_formatting_marker_tag(target)
        ? LXA_SCOPED_END_CLEAR(view)
        : LXA_OK;
}
