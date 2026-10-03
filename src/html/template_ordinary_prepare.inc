/* Prepare ordinary template starts after earlier specialized routing. */
static inline lxa_status_t
LXA_TEMPLATE_ORDINARY_PREPARE_NAME(LXA_TEMPLATE_ORDINARY_PREPARE_VIEW *view,
    lxa_span_t name, bool *accepted)
{
    lxa_status_t status;
    if (lxa_html_tree_name_equals(name, "button", 6)) {
        size_t keep;
        status = LXA_TEMPLATE_ORDINARY_PREPARE_BUTTON(view, name, &keep);
        if (status == LXA_OK) status = LXA_TEMPLATE_ORDINARY_PREPARE_PREFLIGHT(view);
        if (status == LXA_OK) status = LXA_TEMPLATE_ORDINARY_PREPARE_POP(view, keep);
        if (status == LXA_OK) status = LXA_TEMPLATE_ORDINARY_PREPARE_RECONSTRUCT(view);
        if (status != LXA_OK) return status;
    }
    bool out = lxa_html_tree_template_container_start(name);
    if (!out) {
        status = LXA_TEMPLATE_ORDINARY_PREPARE_SUPPORTED(view, name, &out);
        if (status != LXA_OK) return status;
    }
    *accepted = out;
    return LXA_OK;
}
