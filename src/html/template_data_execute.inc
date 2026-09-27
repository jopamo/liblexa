/* Accepted HTML DATA after active-current and foreign routing. */
static inline lxa_status_t
LXA_TEMPLATE_DATA_NAME(LXA_TEMPLATE_DATA_VIEW *view, lxa_span_t bytes)
{
    if (lxa_html_tree_null_only(bytes)) return LXA_OK;
    lxa_status_t status = LXA_TEMPLATE_DATA_VALID(view);
    if (status != LXA_OK) return status;
    bool table = LXA_TEMPLATE_DATA_TABLE(view);
    status = table ? LXA_OK : LXA_TEMPLATE_DATA_RECONSTRUCT(view);
    if (status != LXA_OK) return status;
    return LXA_TEMPLATE_DATA_EMIT(view, bytes, table);
}
