/* Earlier foreign/mode routing has accepted an HTML root start. */
static inline lxa_status_t
LXA_ROOT_START_PRELUDE_NAME(LXA_ROOT_START_PRELUDE_VIEW *view,
    const lxa_html_token_t *token, lxa_html_root_start_prelude_action_t *result)
{
    lxa_status_t status = LXA_OK;
    lxa_html_root_mode_t mode = LXA_ROOT_START_PRELUDE_MODE(view);
    if (mode == LXA_TREE_IN_BODY
        && lxa_html_tree_root_control_group(token->kind, token->name))
        status = LXA_ROOT_START_PRELUDE_GROUP(view);
    if (status != LXA_OK) return status;
    lxa_html_root_start_action_t action;
    status = lxa_html_tree_root_start_action(mode, token->name, &action);
    if (status != LXA_OK) return status;
    if (mode == LXA_TREE_IN_BODY) {
        if (lxa_html_tree_name_equals(token->name, "frameset", 8)) {
            bool eligible;
            status = LXA_ROOT_START_PRELUDE_ELIGIBLE(view, &eligible);
            if (status == LXA_OK && eligible)
                status = LXA_ROOT_START_PRELUDE_FRAMESET(view, token);
            if (status == LXA_OK) *result = eligible
                ? LXA_HTML_ROOT_PRELUDE_FRAMESET : LXA_HTML_ROOT_PRELUDE_IGNORE;
            return status;
        }
        if (lxa_html_tree_name_equals(token->name, "frame", 5)) {
            *result = LXA_HTML_ROOT_PRELUDE_IGNORE;
            return LXA_OK;
        }
    }
    if (action != LXA_HTML_ROOT_START_OTHER) {
        status = LXA_ROOT_START_PRELUDE_ROOT(view, action, token);
        if (status == LXA_OK) *result = action == LXA_HTML_ROOT_START_IGNORE_HEAD
            ? LXA_HTML_ROOT_PRELUDE_IGNORE : LXA_HTML_ROOT_PRELUDE_ROOT;
        return status;
    }
    *result = LXA_HTML_ROOT_PRELUDE_CONTINUE;
    return LXA_OK;
}
