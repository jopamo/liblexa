/* Accepted root breakout: plan and preflight before releasing foreign entries. */
static inline lxa_status_t
LXA_ROOT_BREAKOUT_NAME(LXA_ROOT_BREAKOUT_VIEW *view, const lxa_html_token_t *token)
{
    size_t keep;
    lxa_status_t status = LXA_ROOT_BREAKOUT_PLAN(view, &keep);
    if (status != LXA_OK) return status;
    if (keep == 0) return LXA_ERROR_INTERNAL;
    bool needs_node = !lxa_html_tree_name_equals(token->name, "head", 4)
        && !lxa_html_tree_name_equals(token->name, "body", 4);
    if (token->kind == LXA_HTML_END_TAG && lxa_html_tree_name_equals(token->name, "p", 1)) {
        size_t index;
        status = LXA_ROOT_BREAKOUT_PARAGRAPH(view, keep, token->name, &index);
        if (status != LXA_OK) return status;
        needs_node = index == keep;
    }
    if (needs_node) {
        status = LXA_ROOT_BREAKOUT_NODE(view);
        if (status != LXA_OK) return status;
    }
    return LXA_ROOT_BREAKOUT_POP(view, keep);
}
