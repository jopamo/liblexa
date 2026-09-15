/* Prepare accepted root data; owners retain insertion and earlier routing. */
static inline lxa_status_t
LXA_ROOT_DATA_NAME(LXA_ROOT_DATA_VIEW *view, lxa_span_t data,
    lxa_html_root_data_action_t *action)
{
    if (data.length == 0) {
        *action = LXA_HTML_ROOT_DATA_IGNORE;
        return LXA_OK;
    }
    lxa_status_t status = LXA_ROOT_DATA_BODY(view);
    if (status != LXA_OK) return status;
    LXA_ROOT_DATA_MODE(view) = LXA_TREE_IN_BODY;
    if (LXA_ROOT_DATA_FRAMESET(view) && !lxa_html_tree_null_or_space_only(data))
        LXA_ROOT_DATA_FRAMESET(view) = false;
    bool table = false;
    if (LXA_ROOT_DATA_LENGTH(view) > 1)
        status = LXA_ROOT_DATA_TABLE_QUERY(view, &table);
    if (status != LXA_OK) return status;
    if (table) {
        *action = LXA_HTML_ROOT_DATA_TABLE;
        return LXA_OK;
    }
    if (lxa_html_tree_null_only(data)) {
        *action = LXA_HTML_ROOT_DATA_IGNORE;
        return LXA_OK;
    }
    status = LXA_ROOT_DATA_RECONSTRUCT(view);
    if (status == LXA_OK) *action = LXA_HTML_ROOT_DATA_NORMAL;
    return status;
}
