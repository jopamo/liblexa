/* Initial effects, EOF TEXT restoration, pending flush and one-shot newline. */
static inline lxa_status_t
LXA_TOKEN_INTAKE_NAME(LXA_TOKEN_INTAKE_VIEW *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted, bool *handled)
{
    lxa_status_t status;
    if (LXA_TOKEN_INTAKE_MODE(view) == LXA_TREE_INITIAL) {
        switch (lxa_html_tree_initial_action((*token)->kind, (*token)->data)) {
            case LXA_HTML_INITIAL_COMMENT:
                status = LXA_TOKEN_INTAKE_COMMENT(view, *token);
                if (status != LXA_OK) return status;
                goto consumed;
            case LXA_HTML_INITIAL_IGNORE: goto consumed;
            case LXA_HTML_INITIAL_DOCTYPE: break;
            case LXA_HTML_INITIAL_QUIRKS_REPROCESS:
                status = LXA_TOKEN_INTAKE_QUIRKS(view, *token);
                if (status != LXA_OK) return status;
                break;
        }
    }
    if ((*token)->kind == LXA_HTML_EOF && LXA_TOKEN_INTAKE_MODE(view) == LXA_TREE_TEXT) {
        status = LXA_TOKEN_INTAKE_TEXT(view, *token);
        if (status != LXA_OK) return status;
    }
    if ((*token)->kind != LXA_HTML_TEXT) {
        status = LXA_TOKEN_INTAKE_FLUSH(view);
        if (status != LXA_OK) return status;
    }
    if (LXA_TOKEN_INTAKE_NEWLINE(view)) {
        *adjusted = **token;
        status = lxa_html_tree_leading_newline(&LXA_TOKEN_INTAKE_NEWLINE(view),
            (*token)->kind, (*token)->data, &adjusted->data);
        if (status != LXA_OK) return status;
        if ((*token)->kind == LXA_HTML_TEXT && adjusted->data.length != (*token)->data.length) {
            *token = adjusted;
            if (adjusted->data.length == 0) goto consumed;
        }
    }
    if (LXA_TOKEN_INTAKE_MODE(view) == LXA_TREE_TEXT) {
        status = LXA_TOKEN_INTAKE_TEXT(view, *token);
        if (status != LXA_OK) return status;
        goto consumed;
    }
    *handled = false;
    return LXA_OK;
consumed:
    *handled = true;
    return LXA_OK;
}
