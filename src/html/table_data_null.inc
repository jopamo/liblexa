/* First HTML table DATA NUL only; pending nonspace recovers at later flush. */
static inline lxa_status_t
LXA_TABLE_DATA_NULL_NAME(LXA_TABLE_DATA_NULL_VIEW *view, lxa_span_t pending)
{
    bool group;
    lxa_status_t status = LXA_TABLE_DATA_NULL_GROUP(view, &group);
    if (status != LXA_OK || !group || !lxa_html_tree_space_only(pending))
        return status;
    status = LXA_TABLE_DATA_NULL_FLUSH(view, pending);
    return status == LXA_OK ? LXA_TABLE_DATA_NULL_POP(view) : status;
}
