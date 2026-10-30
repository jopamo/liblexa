/* Continue START after intake/misc/foreign/end dispatch. Reprocess outside. */
static inline lxa_status_t
LXA_TEMPLATE_START_NAME(LXA_TEMPLATE_START_VIEW *view,
    const lxa_html_token_t *token, lxa_html_template_start_action_t *action)
{
    lxa_html_template_start_action_t out;
    lxa_status_t status = LXA_TEMPLATE_START_EARLY(view, token, &out);
    if (status != LXA_OK) return status;
    if (out != LXA_HTML_TEMPLATE_START_CONTINUE) goto done;
    status = LXA_TEMPLATE_START_CURRENT(view, token, &out);
    if (status != LXA_OK) return status;
    if (out != LXA_HTML_TEMPLATE_START_CONTINUE) goto done;
    bool handled;
    status = LXA_TEMPLATE_START_PART(view, token, &handled);
    if (status != LXA_OK) return status;
    if (!handled) {
        status = LXA_TEMPLATE_START_LATE(view, token, &handled);
        if (status != LXA_OK) return status;
    }
    out = handled ? LXA_HTML_TEMPLATE_START_CONSUMED : LXA_HTML_TEMPLATE_START_CONTINUE;
done:
    switch (out) {
        case LXA_HTML_TEMPLATE_START_CONTINUE:
        case LXA_HTML_TEMPLATE_START_CONSUMED:
        case LXA_HTML_TEMPLATE_START_REPROCESS:
            *action = out;
            return LXA_OK;
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
}
