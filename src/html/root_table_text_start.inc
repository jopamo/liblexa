/* Table script/style preflight precedes colgroup recovery and text admission. */
static inline lxa_status_t
LXA_ROOT_TABLE_TEXT_NAME(LXA_ROOT_TABLE_TEXT_VIEW *view,
    const lxa_html_token_t *token, bool column_group)
{
    lxa_status_t status = LXA_ROOT_TABLE_TEXT_PREPARE(view);
    if (status == LXA_OK && column_group) status = LXA_ROOT_TABLE_TEXT_POP(view);
    return status == LXA_OK ? LXA_ROOT_TABLE_TEXT_ENTER(view, token) : status;
}
