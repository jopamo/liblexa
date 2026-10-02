/* Ordinary closed starts retain head exceptions and publish after all effects. */
static inline lxa_status_t
LXA_TEMPLATE_VOID_NAME(LXA_TEMPLATE_VOID_VIEW *view, const lxa_html_token_t *token)
{
    lxa_status_t status;
    if (lxa_html_tree_head_void(token->name)) {
        status = LXA_TEMPLATE_VOID_HEAD(view, token);
    }
    else {
        if (lxa_html_tree_body_void_reconstructs(token->name)) {
            status = LXA_TEMPLATE_VOID_RECONSTRUCT(view);
            if (status != LXA_OK) return status;
        }
        if (lxa_html_tree_name_equals(token->name, "hr", 2)) {
            size_t keep;
            status = LXA_TEMPLATE_VOID_PARAGRAPH(view, &keep);
            if (status == LXA_OK) status = LXA_TEMPLATE_VOID_PREFLIGHT(view);
            if (status != LXA_OK) return status;
            if (keep != LXA_TEMPLATE_VOID_LENGTH(view)) {
                status = LXA_TEMPLATE_VOID_POP(view, keep);
                if (status != LXA_OK) return status;
            }
        }
        lxa_html_token_t adjusted;
        if (lxa_html_tree_name_equals(token->name, "image", 5)) {
            adjusted = *token;
            adjusted.name = (lxa_span_t){(const uint8_t *) "img", 3};
            token = &adjusted;
        }
        status = LXA_TEMPLATE_VOID_INSERT(view, token);
        if (status == LXA_OK) status = LXA_TEMPLATE_VOID_MODE(view, token);
        if (status == LXA_OK) LXA_TEMPLATE_VOID_RESULT(view, token);
        return status;
    }
    if (status == LXA_OK) LXA_TEMPLATE_VOID_RESULT(view, token);
    return status;
}
