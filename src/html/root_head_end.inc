/* Accepted in-head end execution; owners supply node preflight and BODY. */
static inline lxa_status_t
LXA_ROOT_HEAD_END_NAME(LXA_ROOT_HEAD_END_VIEW *view,
    lxa_html_head_end_action_t action, lxa_html_root_mode_t *mode)
{
    lxa_html_root_mode_t next;
    switch (action) {
        case LXA_HTML_HEAD_END_IGNORE: return LXA_OK;
        case LXA_HTML_HEAD_END_CLOSE: *mode = LXA_TREE_AFTER_HEAD; return LXA_OK;
        case LXA_HTML_HEAD_END_BODY: next = LXA_TREE_AFTER_BODY; break;
        case LXA_HTML_HEAD_END_HTML: next = LXA_TREE_AFTER_HTML; break;
        case LXA_HTML_HEAD_END_BR: next = LXA_TREE_IN_BODY; break;
        default: return LXA_ERROR_ARGUMENT;
    }
    lxa_status_t status = LXA_ROOT_HEAD_END_PREPARE(view, action);
    if (status == LXA_OK) status = LXA_ROOT_HEAD_END_BODY(view);
    if (status == LXA_OK) *mode = next;
    return status;
}
