/* Accepted HTML DATA after earlier mode and foreign routing. */
static inline lxa_status_t
LXA_ROOT_DATA_EXEC_NAME(LXA_ROOT_DATA_EXEC_VIEW *view, lxa_span_t bytes)
{
    lxa_html_root_mode_t mode = LXA_ROOT_DATA_EXEC_MODE(view);
    lxa_status_t status;
    if (mode == LXA_TREE_BEFORE_HTML || mode == LXA_TREE_BEFORE_HEAD
        || mode == LXA_TREE_IN_HEAD || mode == LXA_TREE_AFTER_HEAD) {
        status = LXA_ROOT_DATA_EXEC_PREBODY(view, &bytes);
        if (status != LXA_OK) return status;
    }
    lxa_html_root_data_action_t action;
    status = LXA_ROOT_DATA_EXEC_PREPARE(view, bytes, &action);
    if (status != LXA_OK || action == LXA_HTML_ROOT_DATA_IGNORE) return status;
    return LXA_ROOT_DATA_EXEC_EMIT(view, bytes, action);
}
