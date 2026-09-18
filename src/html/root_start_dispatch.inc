/* Explicit root starts after control-group recovery and earlier mode routing. */
static inline lxa_status_t
LXA_ROOT_START_DISPATCH_NAME(LXA_ROOT_START_DISPATCH_VIEW *view,
    lxa_html_root_start_action_t action, const lxa_html_token_t *token,
    lxa_html_root_mode_t *mode)
{
    lxa_html_root_element_t element;
    switch (action) {
        case LXA_HTML_ROOT_START_IGNORE_HEAD: return LXA_OK;
        case LXA_HTML_ROOT_START_HTML: element = LXA_HTML_ROOT_ELEMENT_HTML; break;
        case LXA_HTML_ROOT_START_HEAD: element = LXA_HTML_ROOT_ELEMENT_HEAD; break;
        case LXA_HTML_ROOT_START_BODY: element = LXA_HTML_ROOT_ELEMENT_BODY; break;
        default: return LXA_ERROR_ARGUMENT;
    }
    /* Select planning publishes continue without reads or WORK for root names. */
    return LXA_ROOT_START_DISPATCH_EXECUTE(view, element, token, mode);
}
