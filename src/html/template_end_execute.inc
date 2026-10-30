/* Execute END after intake. Converted br continues through START separately. */
static inline lxa_status_t
LXA_TEMPLATE_END_EXECUTE_NAME(LXA_TEMPLATE_END_EXECUTE_VIEW *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted, bool *handled)
{
    for (;;) {
        size_t length = LXA_TEMPLATE_END_EXECUTE_LENGTH(view);
        bool consumed;
        lxa_status_t status = LXA_TEMPLATE_END_EXECUTE_DISPATCH(view, token, adjusted, &consumed);
        if (status != LXA_OK) return status;
        if (consumed) break;
        if ((*token)->kind == LXA_HTML_START_TAG) {
            *handled = false;
            return LXA_OK;
        }
        status = LXA_TEMPLATE_END_EXECUTE_PRELUDE(view, *token, &consumed);
        if (status != LXA_OK) return status;
        if (consumed) break;
        lxa_html_template_table_end_result_t table;
        status = LXA_TEMPLATE_END_EXECUTE_TABLE(view, *token, &table);
        if (status != LXA_OK) return status;
        switch (table) {
            case LXA_HTML_TEMPLATE_TABLE_RESULT_REPROCESS:
                if (LXA_TEMPLATE_END_EXECUTE_LENGTH(view) >= length) {
                    assert(false);
                    return LXA_ERROR_INTERNAL;
                }
                continue;
            case LXA_HTML_TEMPLATE_TABLE_RESULT_PARAGRAPH:
                LXA_TEMPLATE_END_EXECUTE_PARAGRAPH(view);
                break;
            case LXA_HTML_TEMPLATE_TABLE_RESULT_CONTINUE:
                status = LXA_TEMPLATE_END_EXECUTE_BODY(view, *token);
                if (status != LXA_OK) return status;
                break;
            case LXA_HTML_TEMPLATE_TABLE_RESULT_IGNORE:
                break;
            default:
                assert(false);
                return LXA_ERROR_INTERNAL;
        }
        break;
    }
    *handled = true;
    return LXA_OK;
}
