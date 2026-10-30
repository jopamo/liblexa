/* Consume one accepted special-start action; owners retain admission effects. */
static inline lxa_status_t
LXA_ROOT_SPECIAL_EXEC_NAME(LXA_ROOT_SPECIAL_EXEC_VIEW *view,
    const lxa_html_token_t *token, lxa_html_root_special_start_action_t action)
{
    lxa_status_t status;
    switch (action) {
        case LXA_HTML_ROOT_SPECIAL_FRAME:
            status = LXA_ROOT_SPECIAL_EXEC_BODY(view);
            if (status == LXA_OK) LXA_ROOT_SPECIAL_EXEC_MODE(view) = LXA_TREE_IN_BODY;
            return status;
        case LXA_HTML_ROOT_SPECIAL_TEMPLATE_HEAD:
            status = LXA_ROOT_SPECIAL_EXEC_HEAD(view);
            if (status != LXA_OK) return status;
            LXA_ROOT_SPECIAL_EXEC_MODE(view) = LXA_TREE_IN_HEAD;
            /* fall through */
        case LXA_HTML_ROOT_SPECIAL_TEMPLATE:
            return LXA_ROOT_SPECIAL_EXEC_TEMPLATE(view, token);
        case LXA_HTML_ROOT_SPECIAL_HEAD_NOSCRIPT:
            return LXA_ROOT_SPECIAL_EXEC_NOSCRIPT(view, token);
        case LXA_HTML_ROOT_SPECIAL_FORM:
            return LXA_ROOT_SPECIAL_EXEC_FORM(view, token);
        case LXA_HTML_ROOT_SPECIAL_TEXT:
            return LXA_ROOT_SPECIAL_EXEC_TEXT(view, token);
        case LXA_HTML_ROOT_SPECIAL_XMP:
            return LXA_ROOT_SPECIAL_EXEC_XMP(view, token);
        case LXA_HTML_ROOT_SPECIAL_PLAINTEXT:
            return LXA_ROOT_SPECIAL_EXEC_PLAINTEXT(view, token);
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
}
