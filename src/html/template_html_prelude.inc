/* Continue after earlier template misc/foreign/end dispatch. */
static inline lxa_status_t
LXA_TEMPLATE_HTML_PRELUDE_NAME(LXA_TEMPLATE_HTML_PRELUDE_VIEW *view,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_status_t status = LXA_OK;
    bool out = false;
    if (token->kind == LXA_HTML_START_TAG) {
        status = LXA_TEMPLATE_HTML_PRELUDE_PREPARE(view, token, &out);
        if (status != LXA_OK || out) goto done;
    }
    lxa_html_column_group_action_t group;
    status = LXA_TEMPLATE_HTML_PRELUDE_GROUP(view, token, &group);
    if (status != LXA_OK) return status;
    if (group != LXA_HTML_COLUMN_GROUP_CONTINUE) {
        out = true;
        goto done;
    }
    status = LXA_TEMPLATE_HTML_PRELUDE_HIDDEN(view, token, &out);
    if (status != LXA_OK || out) goto done;
    status = LXA_TEMPLATE_HTML_PRELUDE_SELECT(view, token, &out);
done:
    if (status == LXA_OK) *handled = out;
    return status;
}
