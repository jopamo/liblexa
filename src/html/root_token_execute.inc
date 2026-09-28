/* Accepted HTML root intake after initial/text/template/frameset/foreign routing. */
static inline lxa_status_t
LXA_ROOT_TOKEN_NAME(LXA_ROOT_TOKEN_VIEW *view, const lxa_html_token_t *token)
{
    if (token->kind == LXA_HTML_COMMENT || token->kind == LXA_HTML_PROCESSING_INSTRUCTION
        || token->kind == LXA_HTML_DOCTYPE)
        return LXA_ROOT_TOKEN_MISC(view, token);
    if (LXA_ROOT_TOKEN_MODE(view) == LXA_TREE_AFTER_BODY
        || LXA_ROOT_TOKEN_MODE(view) == LXA_TREE_AFTER_HTML) {
        lxa_html_after_body_action_t action;
        lxa_status_t status = LXA_ROOT_TOKEN_AFTER_BODY(view, token, &action);
        if (status != LXA_OK || action == LXA_HTML_AFTER_BODY_SPACE_TEXT) return status;
    }
    switch (token->kind) {
        case LXA_HTML_END_TAG: return LXA_ROOT_TOKEN_END(view, token);
        case LXA_HTML_START_TAG: return LXA_ROOT_TOKEN_START(view, token);
        case LXA_HTML_TEXT: return LXA_ROOT_TOKEN_DATA(view, token->data);
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
}
