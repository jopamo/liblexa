/* Accepted svg/math starts after earlier foreign/table/select dispatch. */
static inline lxa_status_t
LXA_ROOT_FOREIGN_START_NAME(LXA_ROOT_FOREIGN_START_VIEW *view,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = LXA_ROOT_FOREIGN_START_BODY(view);
    if (status == LXA_OK) status = LXA_ROOT_FOREIGN_START_RESERVE(view);
    if (status == LXA_OK) status = LXA_ROOT_FOREIGN_START_RECONSTRUCT(view);
    if (status != LXA_OK) return status;
    *mode = LXA_TREE_IN_BODY;
    lxa_dom_namespace_t ns = lxa_html_tree_name_equals(token->name, "svg", 3)
        ? LXA_DOM_NS_SVG : LXA_DOM_NS_MATHML;
    status = LXA_ROOT_FOREIGN_START_INSERT(view, token, ns);
    if (status == LXA_OK && token->self_closing) status = LXA_ROOT_FOREIGN_START_POP(view);
    return status;
}
