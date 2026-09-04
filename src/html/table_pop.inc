/* Instantiate accepted table recovery over prepared membership owners. */
static inline lxa_status_t
LXA_TABLE_POP_NAME(const LXA_TABLE_POP_VIEW *view, size_t keep, bool template_context)
{
    size_t markers = 0;
    lxa_status_t status = LXA_OK;
    if (template_context || LXA_TABLE_POP_HAS_MARKER(view))
        status = LXA_TABLE_POP_PLAN(view, keep, &markers);
    if (status != LXA_OK) return status;
    status = LXA_TABLE_POP_ELEMENTS(view, keep, template_context);
    if (status != LXA_OK) return status;
    while (markers != 0) {
        status = LXA_TABLE_POP_CLEAR(view);
        if (status != LXA_OK) return status;
        --markers;
    }
    return LXA_OK;
}
