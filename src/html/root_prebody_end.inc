static lxa_status_t
LXA_ROOT_END_NAME(LXA_ROOT_END_VIEW *view, lxa_html_prebody_end_action_t action,
    lxa_html_root_mode_t *mode)
{
    lxa_status_t status;
    if (action == LXA_HTML_PREBODY_END_IGNORE) return LXA_OK;
    if (action == LXA_HTML_PREBODY_END_HTML_HEAD) {
        status = LXA_ROOT_END_HTML(view);
        if (status != LXA_OK) return status;
        *mode = LXA_TREE_BEFORE_HEAD;
    }
    if (action == LXA_HTML_PREBODY_END_BODY) {
        status = LXA_ROOT_END_BODY(view);
        if (status != LXA_OK) return status;
        *mode = LXA_TREE_IN_BODY;
    }
    else {
        status = LXA_ROOT_END_HEAD(view);
        if (status != LXA_OK) return status;
        *mode = LXA_TREE_IN_HEAD;
    }
    return LXA_OK;
}
