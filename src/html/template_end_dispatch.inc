/* Instantiate after earlier template misc/foreign-start intake. */
static inline lxa_status_t
LXA_TEMPLATE_END_DISPATCH_NAME(LXA_TEMPLATE_END_DISPATCH_VIEW *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted, bool *handled)
{
    lxa_status_t status;
    if ((*token)->kind == LXA_HTML_END_TAG) {
        status = LXA_TEMPLATE_END_DISPATCH_FOREIGN(view, *token, handled);
        if (status != LXA_OK || *handled) return status;
        if (lxa_html_tree_name_equals((*token)->name, "br", 2)
            && LXA_TEMPLATE_END_DISPATCH_BR(view)) {
            *adjusted = **token;
            adjusted->kind = LXA_HTML_START_TAG;
            adjusted->attributes = NULL;
            adjusted->attribute_count = 0;
            adjusted->self_closing = false;
            *token = adjusted;
        }
        if ((*token)->kind == LXA_HTML_END_TAG
            && lxa_html_tree_name_equals((*token)->name, "template", 8)) {
            status = LXA_TEMPLATE_END_DISPATCH_CLOSE(view);
            if (status == LXA_OK) *handled = true;
            return status;
        }
    }
    return LXA_TEMPLATE_END_DISPATCH_BOUNDARY(view, *token, handled);
}
