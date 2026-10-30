/* Consume one accepted root table-start action after earlier token routing. */
static inline lxa_status_t
LXA_ROOT_TABLE_EXEC_NAME(LXA_ROOT_TABLE_EXEC_VIEW *view, const lxa_html_token_t *token,
    lxa_html_root_table_start_action_t action, bool column_group)
{
    switch (action) {
        case LXA_HTML_ROOT_TABLE_TEMPLATE:
            /* Retain template content under the table scope, not before the table. */
            return LXA_ROOT_TABLE_EXEC_TEMPLATE(view, token);
        case LXA_HTML_ROOT_TABLE_TEXT_CURRENT:
            return LXA_ROOT_TABLE_EXEC_TEXT(view, token, column_group);
        case LXA_HTML_ROOT_TABLE_PLAINTEXT:
            return LXA_ROOT_TABLE_EXEC_PLAINTEXT(view, token);
        case LXA_HTML_ROOT_TABLE_XMP:
            return LXA_ROOT_TABLE_EXEC_XMP(view, token);
        case LXA_HTML_ROOT_TABLE_TEXT_FOSTER:
            return LXA_ROOT_TABLE_EXEC_FOSTER_TEXT(view, token, column_group);
        case LXA_HTML_ROOT_TABLE_VOID:
        case LXA_HTML_ROOT_TABLE_SVG:
        case LXA_HTML_ROOT_TABLE_MATH:
        case LXA_HTML_ROOT_TABLE_CONTAINER: {
            lxa_status_t status = column_group
                ? LXA_ROOT_TABLE_EXEC_GROUP(view, action == LXA_HTML_ROOT_TABLE_CONTAINER)
                : LXA_OK;
            if (status != LXA_OK) return status;
            return action == LXA_HTML_ROOT_TABLE_SVG || action == LXA_HTML_ROOT_TABLE_MATH
                ? LXA_ROOT_TABLE_EXEC_FOREIGN(view, token)
                : LXA_ROOT_TABLE_EXEC_HTML(view, token, action == LXA_HTML_ROOT_TABLE_CONTAINER);
        }
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
}
