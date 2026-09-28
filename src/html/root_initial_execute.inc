/* Execute accepted INITIAL effects at the engine's existing dispatch point. */
static inline lxa_status_t
LXA_ROOT_INITIAL_NAME(LXA_ROOT_INITIAL_VIEW *view, const lxa_html_token_t *token,
    lxa_html_initial_action_t action)
{
    switch (action) {
        case LXA_HTML_INITIAL_COMMENT:
        case LXA_HTML_INITIAL_IGNORE:
            return LXA_OK;
        case LXA_HTML_INITIAL_DOCTYPE: {
            lxa_html_document_mode_t document_mode;
            lxa_status_t status = LXA_ROOT_INITIAL_RESOLVE(view, token, &document_mode);
            if (status == LXA_OK) status = LXA_ROOT_INITIAL_INSERT(view, token);
            if (status != LXA_OK) return status;
            LXA_ROOT_INITIAL_DOCUMENT_MODE(view) = document_mode;
            break;
        }
        case LXA_HTML_INITIAL_QUIRKS_REPROCESS:
            LXA_ROOT_INITIAL_DOCUMENT_MODE(view) = LXA_HTML_QUIRKS;
            break;
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
    LXA_ROOT_INITIAL_MODE(view) = LXA_TREE_BEFORE_HTML;
    return LXA_OK;
}
