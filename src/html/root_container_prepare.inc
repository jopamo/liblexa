/* Prepare accepted root containers before reconstruction and admission. */
static inline lxa_status_t
LXA_ROOT_CONTAINER_NAME(LXA_ROOT_CONTAINER_VIEW *view, lxa_span_t name, bool formatting)
{
    bool heading = lxa_html_tree_heading(name);
    bool ruby = lxa_html_tree_ruby_annotation(name);
    bool button = lxa_html_tree_name_equals(name, "button", 6);
    bool paragraph = lxa_html_tree_root_container_closes_paragraph(name);
    lxa_status_t status;
    if (!formatting && !ruby && !button && (!paragraph || heading)) {
        status = LXA_ROOT_CONTAINER_RESERVE(view, heading ? 0 : 1);
        if (status != LXA_OK) return status;
    }
    if (ruby) {
        size_t keep;
        status = LXA_ROOT_CONTAINER_NODE(view);
        if (status == LXA_OK) status = LXA_ROOT_CONTAINER_RUBY(view, name, &keep);
        if (status == LXA_OK) status = LXA_ROOT_CONTAINER_POP(view, keep);
        if (status == LXA_OK) status = LXA_ROOT_CONTAINER_RESERVE(view, 1);
        if (status != LXA_OK) return status;
    }
    if (lxa_html_tree_template_list_item(name)) {
        status = LXA_ROOT_CONTAINER_LIST(view, !lxa_html_tree_name_equals(name, "li", 2));
        if (status != LXA_OK) return status;
    }
    if (paragraph) {
        status = LXA_ROOT_CONTAINER_PARAGRAPH(view);
        if (status == LXA_OK && !heading) status = LXA_ROOT_CONTAINER_RESERVE(view, 1);
        if (status != LXA_OK) return status;
    }
    if (heading) {
        status = LXA_ROOT_CONTAINER_HEADING(view);
        if (status == LXA_OK) status = LXA_ROOT_CONTAINER_RESERVE(view, 1);
        if (status != LXA_OK) return status;
    }
    if (button) {
        status = LXA_ROOT_CONTAINER_BUTTON(view);
        if (status == LXA_OK) status = LXA_ROOT_CONTAINER_RESERVE(view, 1);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}
