/* Instantiate foreign stack plans with static metadata access.
 * LXA_FOREIGN_{VIEW,LENGTH,BUDGET,REASON,NAMESPACE,NAME,BREAKOUT_BOUNDARY,
 * END_PLAN_NAME,BREAKOUT_PLAN_NAME} belong to the includer.
 */
static inline lxa_status_t
LXA_FOREIGN_BREAKOUT_PLAN_NAME(const LXA_FOREIGN_VIEW *view, size_t boundary,
                              size_t *out)
{
    size_t keep = LXA_FOREIGN_LENGTH(view);
    while (keep > boundary) {
        bool integrated;
        lxa_status_t status = lxa_budget_charge(
            LXA_FOREIGN_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_FOREIGN_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_FOREIGN_BREAKOUT_BOUNDARY(view, keep - 1, &integrated);
        if (status != LXA_OK) return status;
        if (integrated) break;
        --keep;
    }
    *out = keep;
    return LXA_OK;
}

static inline lxa_status_t
LXA_FOREIGN_END_PLAN_NAME(const LXA_FOREIGN_VIEW *view, size_t boundary,
                          lxa_span_t name, lxa_html_foreign_end_plan_t *out)
{
    size_t i = LXA_FOREIGN_LENGTH(view);
    lxa_html_foreign_end_plan_t plan = {i, false};
    while (i > boundary) {
        lxa_dom_namespace_t ns;
        lxa_span_t top, target = name;
        lxa_status_t status = lxa_budget_charge(
            LXA_FOREIGN_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_FOREIGN_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_FOREIGN_NAMESPACE(view, --i, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML) {
            plan.reprocess = true;
            *out = plan;
            return LXA_OK;
        }
        status = LXA_FOREIGN_NAME(view, i, &top);
        if (status == LXA_OK && ns == LXA_DOM_NS_SVG)
            status = lxa_html_svg_tag_name(target, &target);
        if (status != LXA_OK) return status;
        if (top.length == target.length) {
            size_t blocks = top.length / 8 + (top.length % 8 != 0);
            status = lxa_budget_charge(LXA_FOREIGN_BUDGET(view),
                LXA_BUDGET_WORK, blocks, LXA_FOREIGN_REASON(view));
            if (status != LXA_OK) return status;
            if (memcmp(top.data, target.data, top.length) == 0) {
                plan.keep = i;
                *out = plan;
                return LXA_OK;
            }
        }
    }
    plan.reprocess = true;
    *out = plan;
    return LXA_OK;
}
