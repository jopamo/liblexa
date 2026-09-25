/* Pending bytes belong to the engine; clear them only after successful flush. */
static inline lxa_status_t
LXA_TABLE_TEXT_FLUSH_NAME(LXA_TABLE_TEXT_FLUSH_VIEW *view, lxa_span_t bytes)
{
    if (bytes.length == 0) return LXA_OK;
    lxa_status_t status = lxa_budget_charge(LXA_TABLE_TEXT_FLUSH_BUDGET(view),
        LXA_BUDGET_WORK, bytes.length, LXA_TABLE_TEXT_FLUSH_REASON(view));
    if (status != LXA_OK) return status;
    if (lxa_html_tree_null_or_space_only(bytes))
        return LXA_TABLE_TEXT_FLUSH_APPEND(view, bytes, false);
    bool group;
    status = LXA_TABLE_TEXT_FLUSH_GROUP(view, &group);
    if (status != LXA_OK) return status;
    if (group) {
        size_t prefix = lxa_html_tree_space_prefix(bytes);
        status = LXA_TABLE_TEXT_FLUSH_PREFLIGHT(view);
        if (status != LXA_OK) return status;
        if (prefix != 0) {
            status = LXA_TABLE_TEXT_FLUSH_APPEND(view,
                ((lxa_span_t){bytes.data, prefix}), true);
            if (status != LXA_OK) return status;
        }
        status = LXA_TABLE_TEXT_FLUSH_POP(view);
        if (status != LXA_OK) return status;
        bytes.data += prefix;
        bytes.length -= prefix;
    }
    status = LXA_TABLE_TEXT_FLUSH_RECONSTRUCT(view);
    return status == LXA_OK ? LXA_TABLE_TEXT_FLUSH_FOSTER(view, bytes) : status;
}
