/* Execute one classified family action over statically selected owners. */
static lxa_status_t
LXA_FRAMESET_EXECUTE_NAME(LXA_FRAMESET_EXECUTE_VIEW *view,
    const lxa_html_token_t *token, lxa_html_frameset_action_t action)
{
    switch (action) {
    case LXA_HTML_FRAMESET_IGNORE: return LXA_OK;
    case LXA_HTML_FRAMESET_EOF: return LXA_FRAMESET_EXECUTE_FINISH(view);
    case LXA_HTML_FRAMESET_COMMENT: return LXA_FRAMESET_EXECUTE_COMMENT(view, token);
    case LXA_HTML_FRAMESET_HTML_START: return LXA_FRAMESET_EXECUTE_HTML(view, token);
    case LXA_HTML_FRAMESET_TEXT: return LXA_FRAMESET_EXECUTE_SPACES(view, token);
    case LXA_HTML_FRAMESET_NOFRAMES_START: return LXA_FRAMESET_EXECUTE_TEXT(view, token);
    case LXA_HTML_FRAMESET_START: return LXA_FRAMESET_EXECUTE_START(view, token);
    case LXA_HTML_FRAMESET_FRAME_START: return LXA_FRAMESET_EXECUTE_FRAME(view, token);
    case LXA_HTML_FRAMESET_END:
    case LXA_HTML_FRAMESET_HTML_END: return LXA_FRAMESET_EXECUTE_END(view, token, action);
    }
    assert(false);
    return LXA_ERROR_INTERNAL;
}
