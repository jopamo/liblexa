/* HTML-path SVG/MathML starts reconstruct formatting only at eligible parents. */
static inline lxa_status_t
LXA_TEMPLATE_HTML_FOREIGN_NAME(LXA_TEMPLATE_HTML_FOREIGN_VIEW *view,
    const lxa_html_token_t *token, bool *handled)
{
    bool eligible = true;
    lxa_status_t status = LXA_OK;
    if (LXA_TEMPLATE_HTML_FOREIGN_CURRENT(view)) {
        lxa_dom_namespace_t ns;
        status = lxa_budget_charge(LXA_TEMPLATE_HTML_FOREIGN_BUDGET(view),
            LXA_BUDGET_WORK, 1, LXA_TEMPLATE_HTML_FOREIGN_REASON(view));
        if (status == LXA_OK) status = LXA_TEMPLATE_HTML_FOREIGN_NAMESPACE(view, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML) {
            lxa_span_t name;
            status = LXA_TEMPLATE_HTML_FOREIGN_CURRENT_NAME(view, &name);
            if (status == LXA_OK) eligible = !lxa_html_tree_table_context(name);
        }
        else if (ns == LXA_DOM_NS_SVG || ns == LXA_DOM_NS_MATHML)
            status = LXA_TEMPLATE_HTML_FOREIGN_INTEGRATION(view, &eligible);
        else {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
    }
    if (status == LXA_OK && eligible) {
        status = LXA_TEMPLATE_HTML_FOREIGN_RECONSTRUCT(view);
        if (status == LXA_OK)
            status = LXA_TEMPLATE_HTML_FOREIGN_START(view, token,
                lxa_html_tree_name_equals(token->name, "svg", 3)
                    ? LXA_DOM_NS_SVG : LXA_DOM_NS_MATHML);
    }
    if (status == LXA_OK) *handled = eligible;
    return status;
}
