/* Execute closure over validated owners and a prepared enclosing frame. */
static inline lxa_status_t
LXA_TEMPLATE_CLOSE_NAME(const LXA_TEMPLATE_CLOSE_VIEW *view, size_t boundary,
    bool explicit_end, bool has_patch)
{
    size_t keep;
    lxa_status_t status = LXA_TEMPLATE_CLOSE_PLAN(view, boundary, explicit_end, has_patch, &keep);
    if (status != LXA_OK) return status;
    if (explicit_end && has_patch) {
        status = LXA_TEMPLATE_CLOSE_POP(view, keep);
        if (status != LXA_OK) return status;
        status = LXA_TEMPLATE_CLOSE_PATCH(view);
        if (status != LXA_OK) return status;
    }
    status = LXA_TEMPLATE_CLOSE_POP(view, boundary);
    if (status != LXA_OK) return status;
    status = LXA_TEMPLATE_CLOSE_CLEAR(view);
    if (status != LXA_OK) return status;
    status = LXA_TEMPLATE_CLOSE_RELEASE(view, boundary);
    if (status != LXA_OK) return status;
    return LXA_TEMPLATE_CLOSE_PUBLISH(view);
}
