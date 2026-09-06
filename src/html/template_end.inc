/* Table/marker end effects after foreign dispatch. Queries and effects own
 * WORK. LXA_TEMPLATE_END_{NAME,VIEW,PLAN,POP_TABLE,POP,CLEAR} belong to includer.
 */
static inline lxa_status_t
LXA_TEMPLATE_END_NAME(LXA_TEMPLATE_END_VIEW *view, lxa_html_token_kind_t kind,
    lxa_span_t name, bool *handled)
{
    lxa_html_template_end_plan_t plan;
    *handled = false;
    lxa_status_t status = LXA_TEMPLATE_END_PLAN(view, kind, name, &plan);
    if (status != LXA_OK) return status;
    switch (plan.action) {
        case LXA_HTML_TEMPLATE_END_CONTINUE: return LXA_OK;
        case LXA_HTML_TEMPLATE_END_IGNORE: break;
        case LXA_HTML_TEMPLATE_END_POP_TABLE:
            status = LXA_TEMPLATE_END_POP_TABLE(view, plan.keep);
            break;
        case LXA_HTML_TEMPLATE_END_POP_MARKER:
            status = LXA_TEMPLATE_END_POP(view, plan.keep);
            if (status == LXA_OK) status = LXA_TEMPLATE_END_CLEAR(view);
            break;
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
    if (status == LXA_OK) *handled = true;
    return status;
}
