/* Accepted root intake after early/template/EOF/frameset-family routing. */
static inline lxa_status_t
LXA_ROOT_DISPATCH_NAME(LXA_ROOT_DISPATCH_VIEW *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted)
{
    lxa_status_t status;
    bool handled;
    if (LXA_ROOT_DISPATCH_MODE(view) == LXA_TREE_HEAD_NOSCRIPT) {
        status = LXA_ROOT_DISPATCH_NOSCRIPT(view, token, adjusted, &handled);
        if (status != LXA_OK || handled) return status;
    }
    if ((*token)->kind == LXA_HTML_START_TAG
        && lxa_html_tree_name_equals((*token)->name, "frameset", 8)
        && (LXA_ROOT_DISPATCH_MODE(view) == LXA_TREE_INITIAL
            || LXA_ROOT_DISPATCH_MODE(view) == LXA_TREE_BEFORE_HTML
            || LXA_ROOT_DISPATCH_MODE(view) == LXA_TREE_BEFORE_HEAD
            || LXA_ROOT_DISPATCH_MODE(view) == LXA_TREE_IN_HEAD
            || LXA_ROOT_DISPATCH_MODE(view) == LXA_TREE_AFTER_HEAD)
        && !LXA_ROOT_DISPATCH_HAS_BODY(view))
        return LXA_ROOT_DISPATCH_FRAMESET(view, *token);
    status = LXA_ROOT_DISPATCH_FOREIGN(view, *token, &handled);
    if (status != LXA_OK || handled) return status;
    if ((*token)->kind == LXA_HTML_START_TAG
        && lxa_html_tree_name_equals((*token)->name, "image", 5)) {
        *adjusted = **token;
        adjusted->name = (lxa_span_t) {(const uint8_t *) "img", 3};
        *token = adjusted;
    }
    if ((*token)->kind == LXA_HTML_DOCTYPE && LXA_ROOT_DISPATCH_MODE(view) == LXA_TREE_INITIAL)
        return LXA_ROOT_DISPATCH_INITIAL(view, *token);
    return LXA_ROOT_DISPATCH_HTML(view, *token);
}
