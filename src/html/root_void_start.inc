/* Accepted root void starts after earlier table/foreign/control routing. */
static inline lxa_status_t
LXA_ROOT_VOID_NAME(LXA_ROOT_VOID_VIEW *view, const lxa_html_token_t *token,
    bool head, bool reconstruct, lxa_html_root_mode_t *mode)
{
    lxa_status_t status;
    if (head) {
        status = *mode == LXA_TREE_IN_BODY ? LXA_ROOT_VOID_BODY(view) : LXA_ROOT_VOID_HEAD(view);
        if (status == LXA_OK) status = LXA_ROOT_VOID_INSERT(view, token, true, false, *mode);
        if (status == LXA_OK && *mode < LXA_TREE_IN_HEAD) *mode = LXA_TREE_IN_HEAD;
        return status;
    }
    status = LXA_ROOT_VOID_BODY(view);
    if (status != LXA_OK) return status;
    *mode = LXA_TREE_IN_BODY;
    if (LXA_ROOT_VOID_FRAMESET(view)) {
        if (lxa_html_tree_name_equals(token->name, "input", 5)) {
            bool hidden;
            status = lxa_html_tree_hidden_input(token, LXA_ROOT_VOID_BUDGET(view),
                LXA_ROOT_VOID_REASON(view), &hidden);
            if (status != LXA_OK) return status;
            if (!hidden) LXA_ROOT_VOID_FRAMESET(view) = false;
        }
        else if (!lxa_html_tree_body_void_frameset_exempt(token->name))
            LXA_ROOT_VOID_FRAMESET(view) = false;
    }
    bool hr = lxa_html_tree_name_equals(token->name, "hr", 2);
    if (hr) status = LXA_ROOT_VOID_PARAGRAPH(view);
    else if (reconstruct) status = LXA_ROOT_VOID_RECONSTRUCT(view);
    return status == LXA_OK ? LXA_ROOT_VOID_INSERT(view, token, false, hr, *mode) : status;
}
