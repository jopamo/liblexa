/* Root end intake after earlier mode/foreign routing and prebody recovery. */
static inline lxa_status_t
LXA_ROOT_END_PRELUDE_NAME(LXA_ROOT_END_PRELUDE_VIEW *view,
    lxa_html_root_mode_t mode, lxa_span_t name, lxa_html_root_end_prelude_action_t *out)
{
    if (mode == LXA_TREE_IN_BODY && lxa_html_tree_root_control_group(LXA_HTML_END_TAG, name)) {
        lxa_status_t status = LXA_ROOT_END_PRELUDE_GROUP(view);
        if (status != LXA_OK) return status;
    }
    lxa_html_root_end_prelude_action_t action = LXA_HTML_ROOT_END_CONTINUE;
    if (lxa_html_tree_name_equals(name, "template", 8)
        || (mode >= LXA_TREE_AFTER_HEAD && lxa_html_tree_name_equals(name, "head", 4)))
        action = LXA_HTML_ROOT_END_IGNORE;
    else if (mode == LXA_TREE_IN_BODY && lxa_html_tree_name_equals(name, "br", 2))
        action = LXA_HTML_ROOT_END_BR_START;
    *out = action;
    return LXA_OK;
}
