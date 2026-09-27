/* Accepted foreign descendants after integration and breakout dispatch. */
static inline lxa_status_t
LXA_FOREIGN_EXEC_NAME(LXA_FOREIGN_EXEC_VIEW *view, const lxa_html_token_t *token,
    bool *reprocess)
{
    *reprocess = false;
    if (token->kind == LXA_HTML_COMMENT || token->kind == LXA_HTML_PROCESSING_INSTRUCTION)
        return LXA_FOREIGN_EXEC_MISC(view, token);
    if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
    if (token->kind == LXA_HTML_TEXT) return LXA_FOREIGN_EXEC_DATA(view, token->data);
    if (token->kind == LXA_HTML_END_TAG) {
        size_t keep;
        lxa_status_t status = LXA_FOREIGN_EXEC_END(view, token->name, &keep, reprocess);
        return status == LXA_OK ? LXA_FOREIGN_EXEC_POP(view, keep) : status;
    }
    if (token->kind != LXA_HTML_START_TAG) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    lxa_status_t status = LXA_FOREIGN_EXEC_START(view, token);
    if (status == LXA_OK && token->self_closing)
        status = LXA_FOREIGN_EXEC_POP(view, LXA_FOREIGN_EXEC_LENGTH(view) - 1);
    return status;
}
