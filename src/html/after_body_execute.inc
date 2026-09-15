/* Execute accepted after-body actions; engines retain insertion/reprocessing. */
static inline lxa_status_t
LXA_AFTER_BODY_NAME(LXA_AFTER_BODY_VIEW *view, lxa_html_after_body_action_t action)
{
    switch (action) {
        case LXA_HTML_AFTER_BODY_SPACE_TEXT:
            return LXA_AFTER_BODY_RECONSTRUCT(view);
        case LXA_HTML_AFTER_BODY_HTML_START:
            return LXA_OK;
        case LXA_HTML_AFTER_BODY_REPROCESS:
            LXA_AFTER_BODY_MODE(view) = LXA_TREE_IN_BODY;
            return LXA_OK;
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
}
