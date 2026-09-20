/* Accepted root table admission; reserve before recovery, then after shrinking. */
static inline lxa_status_t
LXA_ROOT_TABLE_START_NAME(LXA_ROOT_TABLE_START_VIEW *view,
    const lxa_html_token_t *token, bool quirks, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = LXA_ROOT_TABLE_START_BODY(view);
    if (status != LXA_OK) return status;
    *mode = LXA_TREE_IN_BODY;
    lxa_span_t top;
    status = LXA_ROOT_TABLE_START_TOP(view, &top);
    if (status != LXA_OK) return status;
    LXA_ROOT_TABLE_START_FRAMESET(view) = false;
    bool close_existing;
    status = LXA_ROOT_TABLE_START_PLAN(view, top, &close_existing);
    if (status != LXA_OK) return status;
    bool reserve_after_close = close_existing || !quirks;
    status = LXA_ROOT_TABLE_START_RESERVE(view, reserve_after_close ? 0 : 1);
    if (status == LXA_OK) status = LXA_ROOT_TABLE_START_NODE(view);
    if (status == LXA_OK && close_existing) status = LXA_ROOT_TABLE_START_CLOSE(view);
    if (status == LXA_OK && !quirks) status = LXA_ROOT_TABLE_START_PARAGRAPH(view);
    if (status == LXA_OK && reserve_after_close) status = LXA_ROOT_TABLE_START_RESERVE(view, 1);
    return status == LXA_OK ? LXA_ROOT_TABLE_START_INSERT(view, token) : status;
}
