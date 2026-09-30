/* Instantiate accepted template START/DATA routing over the active slice. */
static inline lxa_status_t
LXA_TEMPLATE_FOREIGN_DISPATCH_NAME(LXA_TEMPLATE_FOREIGN_DISPATCH_VIEW *view,
    const lxa_html_token_t *token, lxa_html_template_foreign_action_t *result)
{
    lxa_dom_namespace_t ns;
    lxa_status_t status = LXA_TEMPLATE_FOREIGN_DISPATCH_NAMESPACE(view, &ns);
    if (status != LXA_OK) return status;
    if (ns == LXA_DOM_NS_HTML) {
        *result = LXA_HTML_TEMPLATE_FOREIGN_CONTINUE;
        return LXA_OK;
    }
    if (ns != LXA_DOM_NS_SVG && ns != LXA_DOM_NS_MATHML) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    lxa_span_t name;
    status = LXA_TEMPLATE_FOREIGN_DISPATCH_CURRENT_NAME(view, &name);
    if (status != LXA_OK) return status;
    lxa_html_foreign_context_t context = lxa_html_foreign_context(ns, name, false);
    if (ns == LXA_DOM_NS_MATHML && lxa_html_foreign_annotation_svg(context, token)) {
        status = LXA_TEMPLATE_FOREIGN_DISPATCH_START(view, token, LXA_DOM_NS_SVG);
        if (status == LXA_OK) *result = LXA_HTML_TEMPLATE_FOREIGN_CONSUMED;
        return status;
    }
    status = LXA_TEMPLATE_FOREIGN_DISPATCH_CONTEXT(view, ns, name, &context);
    if (status != LXA_OK) return status;
    bool integrated = lxa_html_foreign_integration(context);
    if (token->kind == LXA_HTML_TEXT) {
        status = LXA_TEMPLATE_FOREIGN_DISPATCH_DATA(view, token, integrated);
        if (status == LXA_OK) *result = LXA_HTML_TEMPLATE_FOREIGN_CONSUMED;
        return status;
    }
    bool html = lxa_html_foreign_uses_html(context, token);
    if (ns == LXA_DOM_NS_MATHML && integrated) {
        if (!html) {
            status = LXA_TEMPLATE_FOREIGN_DISPATCH_START(view, token, ns);
            if (status == LXA_OK) *result = LXA_HTML_TEMPLATE_FOREIGN_CONSUMED;
            return status;
        }
        if (lxa_html_tree_table_part(token->name)
            && !lxa_html_tree_name_equals(token->name, "table", 5)) {
            bool handled;
            status = LXA_TEMPLATE_FOREIGN_DISPATCH_TABLE(view, token, &handled);
            if (status != LXA_OK) return status;
            if (handled) {
                *result = LXA_HTML_TEMPLATE_FOREIGN_CONSUMED;
                return LXA_OK;
            }
        }
    }
    bool font = false;
    if ((ns == LXA_DOM_NS_SVG || !integrated)
        && lxa_html_tree_name_equals(token->name, "font", 4)) {
        status = LXA_TEMPLATE_FOREIGN_DISPATCH_FONT(view, token, &font);
        if (status != LXA_OK) return status;
    }
    if (!integrated && (lxa_html_foreign_html_start_breakout(token->name) || font)) {
        status = LXA_TEMPLATE_FOREIGN_DISPATCH_BREAKOUT(view, token);
        if (status == LXA_OK) *result = LXA_HTML_TEMPLATE_FOREIGN_REPROCESS;
        return status;
    }
    if (!html) {
        status = LXA_TEMPLATE_FOREIGN_DISPATCH_START(view, token, ns);
        if (status == LXA_OK) *result = LXA_HTML_TEMPLATE_FOREIGN_CONSUMED;
        return status;
    }
    *result = LXA_HTML_TEMPLATE_FOREIGN_CONTINUE;
    return LXA_OK;
}
