/* In-body ends after earlier foreign/template/head/br/select routing. */
static inline lxa_status_t
LXA_ROOT_BODY_END_NAME(LXA_ROOT_BODY_END_VIEW *view, lxa_span_t name,
    lxa_html_root_body_end_plan_t plan, lxa_html_root_mode_t *mode)
{
    switch (plan.action) {
        case LXA_HTML_ROOT_BODY_END_BODY:
        case LXA_HTML_ROOT_BODY_END_HTML:
            return LXA_ROOT_BODY_END_BODY(view, plan.action == LXA_HTML_ROOT_BODY_END_HTML, mode);
        case LXA_HTML_ROOT_BODY_END_COL: return LXA_OK;
        case LXA_HTML_ROOT_BODY_END_FORMATTING: return LXA_ROOT_BODY_END_FORMATTING(view, name);
        case LXA_HTML_ROOT_BODY_END_FORM: return LXA_ROOT_BODY_END_FORM(view);
        case LXA_HTML_ROOT_BODY_END_PARAGRAPH: return LXA_ROOT_BODY_END_PARAGRAPH(view);
        case LXA_HTML_ROOT_BODY_END_SCOPED: return LXA_ROOT_BODY_END_SCOPED(view, name, plan.scope);
        case LXA_HTML_ROOT_BODY_END_ORDINARY: return LXA_ROOT_BODY_END_ORDINARY(view, name);
        default: return LXA_ERROR_ARGUMENT;
    }
}
