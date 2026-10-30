/* Accepted HTML start after earlier mode/foreign/image routing. */
static inline lxa_status_t
LXA_ROOT_START_EXEC_NAME(LXA_ROOT_START_EXEC_VIEW *view, const lxa_html_token_t *token)
{
    lxa_html_root_start_prelude_action_t prelude;
    lxa_status_t status = LXA_ROOT_START_EXEC_PRELUDE(view, token, &prelude);
    if (status != LXA_OK) return status;
    if (prelude != LXA_HTML_ROOT_PRELUDE_CONTINUE)
        return prelude == LXA_HTML_ROOT_PRELUDE_IGNORE ? LXA_ROOT_START_EXEC_IGNORE(view) : LXA_OK;
    if (LXA_ROOT_START_EXEC_MODE(view) == LXA_TREE_IN_BODY) {
        bool handled, ignore;
        status = LXA_ROOT_START_EXEC_HIDDEN(view, token, &handled);
        if (status != LXA_OK || handled) return status;
        status = LXA_ROOT_START_EXEC_SELECT(view, token, &ignore);
        if (status != LXA_OK) return status;
        if (ignore) return LXA_ROOT_START_EXEC_IGNORE(view);
        bool table, group;
        status = LXA_ROOT_START_EXEC_TABLE_QUERY(view, &table, &group);
        if (status != LXA_OK) return status;
        if (table && !lxa_html_tree_table_part(token->name)
            && !lxa_html_tree_name_equals(token->name, "form", 4))
            return LXA_ROOT_START_EXEC_TABLE(view, token, group);
    }
    lxa_html_root_special_start_action_t special;
    status = lxa_html_tree_root_special_start_action(LXA_ROOT_START_EXEC_MODE(view), token->name, &special);
    if (status != LXA_OK) return status;
    return special != LXA_HTML_ROOT_SPECIAL_ORDINARY
        ? LXA_ROOT_START_EXEC_SPECIAL(view, token, special)
        : LXA_ROOT_START_EXEC_ORDINARY(view, token);
}
