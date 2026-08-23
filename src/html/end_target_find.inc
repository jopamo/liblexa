/* Instantiate end-target searches with a borrowed stack and static access.
 * LXA_SCOPE_{NAME,VIEW,LENGTH,BUDGET,REASON,READ} and LXA_ORDINARY_NAME
 * belong to the includer.
 */
static inline lxa_status_t
LXA_SCOPE_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, lxa_span_t target,
               lxa_html_scope_kind_t scope, size_t *index)
{
    bool any_heading = lxa_html_tree_heading(target);
    size_t i = LXA_SCOPE_LENGTH(view);
    *index = i;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(
            LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML
            && (any_heading ? lxa_html_tree_heading(name)
                : lxa_html_tree_name_equals(name, (const char *) target.data,
                                             target.length))) {
            *index = i;
            return LXA_OK;
        }
        if (lxa_html_tree_scope_boundary(name, ns, scope)) break;
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_ORDINARY_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, lxa_span_t target,
                  size_t *index)
{
    size_t i = LXA_SCOPE_LENGTH(view);
    *index = i;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(
            LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML && name.length == target.length) {
            size_t blocks = name.length / 8 + (name.length % 8 != 0);
            status = lxa_budget_charge(LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK,
                                       blocks, LXA_SCOPE_REASON(view));
            if (status != LXA_OK) return status;
            if (lxa_html_tree_name_equals(name, (const char *) target.data,
                                          target.length)) {
                *index = i;
                return LXA_OK;
            }
        }
        if (lxa_html_tree_special_element(name, ns)) break;
    }
    return LXA_OK;
}
