/* Final in-body template end dispatch, after earlier recovery rules.
 * LXA_TEMPLATE_BODY_END_{NAME,VIEW,LENGTH,BOUNDARY,MODE,SCOPED,ORDINARY,
 * POP,ADOPT,PARAGRAPH} belong to the includer.
 */
static inline lxa_status_t
LXA_TEMPLATE_BODY_END_NAME(LXA_TEMPLATE_BODY_END_VIEW *view, lxa_span_t name)
{
    size_t index, length = LXA_TEMPLATE_BODY_END_LENGTH(view);
    bool paragraph = lxa_html_tree_name_equals(name, "p", 1);
    lxa_status_t status;
    if (lxa_html_tree_template_scoped_end(name) && !paragraph) {
        status = LXA_TEMPLATE_BODY_END_SCOPED(view, name,
            lxa_html_tree_name_equals(name, "li", 2)
                ? LXA_HTML_SCOPE_LIST_ITEM
                : LXA_HTML_SCOPE_NORMAL,
            &index);
    } else if (paragraph && (LXA_TEMPLATE_BODY_END_MODE(view) != LXA_TEMPLATE_INITIAL || length > LXA_TEMPLATE_BODY_END_BOUNDARY(view))) {
        status = LXA_TEMPLATE_BODY_END_SCOPED(view, name, LXA_HTML_SCOPE_BUTTON, &index);
        if (status != LXA_OK) return status;
        if (index == length) return LXA_TEMPLATE_BODY_END_PARAGRAPH(view);
    } else if (lxa_html_tree_template_ordinary_end(name))
        status = LXA_TEMPLATE_BODY_END_ORDINARY(view, name, &index);
    else if (lxa_html_tree_formatting_tag(name))
        return LXA_TEMPLATE_BODY_END_ADOPT(view, name);
    else return LXA_OK;
    return status != LXA_OK || index == length
        ? status
        : LXA_TEMPLATE_BODY_END_POP(view, index);
}
