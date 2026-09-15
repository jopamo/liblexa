/* Close a scoped paragraph or prepare its owner's closed insertion action. */
static inline lxa_status_t
LXA_ROOT_PARAGRAPH_END_NAME(LXA_ROOT_PARAGRAPH_END_VIEW *view)
{
    bool closed;
    lxa_status_t status = LXA_ROOT_PARAGRAPH_END_CLOSE(view, &closed);
    if (status != LXA_OK || closed) return status;
    if (LXA_ROOT_PARAGRAPH_END_LENGTH(view) == 0) {
        status = LXA_ROOT_PARAGRAPH_END_BODY(view);
        if (status != LXA_OK) return status;
    }
    return LXA_ROOT_PARAGRAPH_END_MISSING(view);
}
