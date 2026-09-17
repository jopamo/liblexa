static lxa_status_t
LXA_FRAMESET_END_NAME(LXA_FRAMESET_END_VIEW *view,
    lxa_html_frameset_action_t action, lxa_html_root_mode_t *mode)
{
    if (action == LXA_HTML_FRAMESET_HTML_END) {
        *mode = LXA_TREE_AFTER_AFTER_FRAMESET;
        return LXA_OK;
    }
    if (action != LXA_HTML_FRAMESET_END) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    lxa_status_t status = LXA_FRAMESET_END_POP(view);
    if (status == LXA_OK && LXA_FRAMESET_END_LENGTH(view) == 0)
        *mode = LXA_TREE_AFTER_FRAMESET;
    return status;
}
