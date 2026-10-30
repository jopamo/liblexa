/* Run non-EOF template tokens after TEXT/flush/newline intake. */
static inline lxa_status_t
LXA_TEMPLATE_TOKEN_NAME(LXA_TEMPLATE_TOKEN_VIEW *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted)
{
    for (;;) {
        size_t length = LXA_TEMPLATE_TOKEN_LENGTH(view);
        if ((*token)->kind == LXA_HTML_DOCTYPE) return LXA_OK;
        if ((*token)->kind == LXA_HTML_COMMENT
            || (*token)->kind == LXA_HTML_PROCESSING_INSTRUCTION)
            return LXA_TEMPLATE_TOKEN_MISC(view, *token);
        lxa_status_t status;
        if ((*token)->kind == LXA_HTML_START_TAG || (*token)->kind == LXA_HTML_TEXT) {
            lxa_html_template_foreign_action_t foreign;
            status = LXA_TEMPLATE_TOKEN_FOREIGN(view, *token, &foreign);
            if (status != LXA_OK) return status;
            switch (foreign) {
                case LXA_HTML_TEMPLATE_FOREIGN_CONSUMED:
                    return LXA_OK;
                case LXA_HTML_TEMPLATE_FOREIGN_REPROCESS:
                    goto reprocess;
                case LXA_HTML_TEMPLATE_FOREIGN_CONTINUE:
                    break;
                default:
                    assert(false);
                    return LXA_ERROR_INTERNAL;
            }
        }
        bool handled;
        status = (*token)->kind == LXA_HTML_END_TAG
            ? LXA_TEMPLATE_TOKEN_END(view, token, adjusted, &handled)
            : LXA_TEMPLATE_TOKEN_BOUNDARY(view, token, adjusted, &handled);
        if (status != LXA_OK || handled) return status;
        if ((*token)->kind == LXA_HTML_START_TAG) {
            lxa_html_template_start_action_t start;
            status = LXA_TEMPLATE_TOKEN_START(view, token, &start);
            if (status != LXA_OK) return status;
            switch (start) {
                case LXA_HTML_TEMPLATE_START_CONSUMED:
                    return LXA_OK;
                case LXA_HTML_TEMPLATE_START_REPROCESS:
                    goto reprocess;
                case LXA_HTML_TEMPLATE_START_CONTINUE:
                    return LXA_TEMPLATE_TOKEN_ORDINARY(view, *token);
                default:
                    assert(false);
                    return LXA_ERROR_INTERNAL;
            }
        }
        if ((*token)->kind != LXA_HTML_TEXT) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        status = LXA_TEMPLATE_TOKEN_DATA_PRELUDE(view, *token, &handled);
        if (status != LXA_OK || handled) return status;
        return LXA_TEMPLATE_TOKEN_DATA(view, *token);
reprocess:
        if (LXA_TEMPLATE_TOKEN_LENGTH(view) >= length) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
    }
}
