/* A current table context needs no scan; cells/captions stop ancestor lookup. */
static inline lxa_status_t
LXA_ROOT_TABLE_CLOSE_NAME(const LXA_ROOT_TABLE_CLOSE_VIEW *view,
    size_t boundary, bool current_context, bool *out)
{
    size_t length = LXA_ROOT_TABLE_CLOSE_LENGTH(view);
    if (boundary > length || out == NULL) return LXA_ERROR_ARGUMENT;
    bool found = current_context;
    if (!found) {
        size_t i = length;
        while (i > boundary) {
            lxa_span_t name;
            lxa_dom_namespace_t ns;
            lxa_status_t status = lxa_budget_charge(LXA_ROOT_TABLE_CLOSE_BUDGET(view),
                LXA_BUDGET_WORK, 1, LXA_ROOT_TABLE_CLOSE_REASON(view));
            if (status == LXA_OK) status = LXA_ROOT_TABLE_CLOSE_READ(view, --i, &name, &ns);
            if (status != LXA_OK) return status;
            if (ns != LXA_DOM_NS_HTML) continue;
            if (lxa_html_tree_name_equals(name, "table", 5)) {
                found = true;
                break;
            }
            if (lxa_html_tree_name_equals(name, "td", 2)
                || lxa_html_tree_name_equals(name, "th", 2)
                || lxa_html_tree_name_equals(name, "caption", 7)) break;
        }
    }
    *out = found;
    return LXA_OK;
}
