/* Admit returning HTML text after earlier template routing. */
static inline lxa_status_t
LXA_TEMPLATE_TEXT_START_NAME(LXA_TEMPLATE_TEXT_START_VIEW *view,
    const lxa_html_token_t *token)
{
    lxa_status_t status = LXA_TEMPLATE_TEXT_START_CHECK(view);
    if (status != LXA_OK) return status;
    bool xmp = lxa_html_tree_name_equals(token->name, "xmp", 3);
    if (xmp) {
        size_t keep;
        status = LXA_TEMPLATE_TEXT_START_PARAGRAPH(view, &keep);
        if (status == LXA_OK) status = LXA_TEMPLATE_TEXT_START_PREFLIGHT(view);
        if (status != LXA_OK) return status;
        if (keep != LXA_TEMPLATE_TEXT_START_LENGTH(view)) {
            status = LXA_TEMPLATE_TEXT_START_POP(view, keep);
            if (status != LXA_OK) return status;
        }
        bool group;
        status = LXA_TEMPLATE_TEXT_START_GROUP(view, &group);
        if (status != LXA_OK) return status;
        if (group) {
            status = LXA_TEMPLATE_TEXT_START_POP(view, LXA_TEMPLATE_TEXT_START_LENGTH(view) - 1);
            if (status != LXA_OK) return status;
        }
        status = LXA_TEMPLATE_TEXT_START_RECONSTRUCT(view);
        if (status != LXA_OK) return status;
    }
    status = LXA_TEMPLATE_TEXT_START_ENTER(view, token, xmp);
    if (status != LXA_OK) return status;
    if (lxa_html_tree_name_equals(token->name, "textarea", 8))
        LXA_TEMPLATE_TEXT_START_NEWLINE(view);
    return LXA_TEMPLATE_TEXT_START_MODE(view, token);
}
