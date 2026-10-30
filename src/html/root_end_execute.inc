/* Accepted root ends after earlier mode/foreign/after-body routing. */
static inline lxa_status_t
LXA_ROOT_END_EXEC_NAME(LXA_ROOT_END_EXEC_VIEW *view, const lxa_html_token_t *token)
{
    lxa_status_t status;
    lxa_html_root_mode_t mode = LXA_ROOT_END_EXEC_MODE(view);
    if (mode == LXA_TREE_BEFORE_HTML || mode == LXA_TREE_BEFORE_HEAD || mode == LXA_TREE_AFTER_HEAD) {
        lxa_html_prebody_end_action_t action;
        status = lxa_html_tree_prebody_end_action(mode, token->name, &action);
        if (status != LXA_OK) return status;
        if (action == LXA_HTML_PREBODY_END_IGNORE) {
            LXA_ROOT_END_EXEC_IGNORE(view);
            return LXA_OK;
        }
        status = LXA_ROOT_END_EXEC_PREBODY(view, action);
        if (status != LXA_OK) return status;
    }
    lxa_html_root_end_prelude_action_t prelude;
    status = LXA_ROOT_END_EXEC_PRELUDE(view, token, &prelude);
    if (status != LXA_OK) return status;
    if (prelude == LXA_HTML_ROOT_END_IGNORE) {
        LXA_ROOT_END_EXEC_IGNORE(view);
        return LXA_OK;
    }
    if (prelude == LXA_HTML_ROOT_END_BR_START) goto br_start;
    if (LXA_ROOT_END_EXEC_MODE(view) == LXA_TREE_IN_BODY) {
        bool ignore;
        status = LXA_ROOT_END_EXEC_SELECT(view, token, &ignore);
        if (status != LXA_OK) return status;
        if (ignore) {
            LXA_ROOT_END_EXEC_IGNORE(view);
            return LXA_OK;
        }
    }
    if (LXA_ROOT_END_EXEC_MODE(view) == LXA_TREE_IN_HEAD) {
        lxa_html_head_end_action_t action;
        status = lxa_html_tree_head_end_action(token->name, &action);
        if (status == LXA_OK) status = LXA_ROOT_END_EXEC_HEAD(view, action);
        if (status != LXA_OK) return status;
        if (action == LXA_HTML_HEAD_END_BR) goto br_start;
        if (action == LXA_HTML_HEAD_END_IGNORE) LXA_ROOT_END_EXEC_IGNORE(view);
        return LXA_OK;
    }
    if (LXA_ROOT_END_EXEC_MODE(view) == LXA_TREE_IN_BODY)
        return LXA_ROOT_END_EXEC_BODY(view, token);
    return LXA_ERROR_ARGUMENT;
br_start:
    {
        lxa_html_token_t adjusted = *token;
        adjusted.kind = LXA_HTML_START_TAG;
        adjusted.attributes = NULL;
        adjusted.attribute_count = 0;
        adjusted.self_closing = false;
        LXA_ROOT_END_EXEC_BR(view);
        return LXA_ROOT_END_EXEC_START(view, &adjusted);
    }
}
