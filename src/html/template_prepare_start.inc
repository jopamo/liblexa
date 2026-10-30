/* Prepare accepted HTML starts before group/hidden-input/select recovery. */
static inline lxa_status_t
LXA_TEMPLATE_PREPARE_START_NAME(LXA_TEMPLATE_PREPARE_START_VIEW *view,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_status_t status = LXA_OK;
    bool out = false;
    if (lxa_html_tree_body_control_start(token->name)) {
        if (!lxa_html_tree_name_equals(token->name, "html", 4)
            && LXA_TEMPLATE_PREPARE_START_CURRENT(view)) {
            lxa_span_t name;
            status = LXA_TEMPLATE_PREPARE_START_NAME_READ(view, &name);
            if (status == LXA_OK && lxa_html_tree_name_equals(name, "colgroup", 8))
                status = LXA_TEMPLATE_PREPARE_START_POP(view);
        }
        if (status == LXA_OK) status = LXA_TEMPLATE_PREPARE_START_MODE(view, token);
        out = true;
    } else if (lxa_html_tree_table_part(token->name)
        && !lxa_html_tree_name_equals(token->name, "table", 5)) {
        if (LXA_TEMPLATE_PREPARE_START_MODE_GET(view) == LXA_TEMPLATE_INITIAL)
            status = LXA_TEMPLATE_PREPARE_START_MODE(view, token);
        if (status == LXA_OK && LXA_TEMPLATE_PREPARE_START_MODE_GET(view) == LXA_TEMPLATE_BODY) {
            bool found;
            status = LXA_TEMPLATE_PREPARE_START_TABLE(view, &found);
            if (status == LXA_OK) out = !found;
        }
    }
    if (status == LXA_OK) *handled = out;
    return status;
}
