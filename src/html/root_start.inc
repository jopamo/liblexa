/* Accepted explicit root starts; attribute effects remain owner-specific. */
static inline lxa_status_t
LXA_ROOT_START_NAME(LXA_ROOT_START_VIEW *view, lxa_html_root_element_t element,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode)
{
    lxa_status_t status;
    lxa_html_root_mode_t next = *mode;
    switch (element) {
        case LXA_HTML_ROOT_ELEMENT_HTML:
            status = LXA_ROOT_START_HTML(view);
            if (*mode == LXA_TREE_BEFORE_HTML) next = LXA_TREE_BEFORE_HEAD;
            break;
        case LXA_HTML_ROOT_ELEMENT_HEAD:
            status = LXA_ROOT_START_HEAD(view);
            next = LXA_TREE_IN_HEAD;
            break;
        case LXA_HTML_ROOT_ELEMENT_BODY:
            status = LXA_ROOT_START_BODY(view);
            if (status == LXA_OK) LXA_ROOT_START_FRAMESET(view);
            next = LXA_TREE_IN_BODY;
            break;
        default:
            return LXA_ERROR_ARGUMENT;
    }
    if (status == LXA_OK) status = LXA_ROOT_START_ATTRIBUTES(view, element, token);
    if (status == LXA_OK) *mode = next;
    return status;
}
