/* Accepted fostered HTML: do not apply ordinary paragraph/frameset recovery. */
static inline lxa_status_t
LXA_FOSTER_HTML_NAME(LXA_FOSTER_HTML_VIEW *view, const lxa_html_token_t *token,
    bool keep_open, bool *skip_newline)
{
    bool formatting = lxa_html_tree_formatting_tag(token->name);
    bool marker = lxa_html_tree_formatting_marker_tag(token->name);
    size_t drop = SIZE_MAX;
    lxa_status_t status = LXA_OK;
    if (formatting) status = LXA_FOSTER_HTML_PREPARE(view, token, &drop);
    else if (lxa_html_tree_foster_reconstructs(token->name, LXA_DOM_NS_HTML, keep_open))
        status = LXA_FOSTER_HTML_RECONSTRUCT(view);
    if (status == LXA_OK && marker) status = LXA_FOSTER_HTML_MARKER(view);
    if (status == LXA_OK) status = LXA_FOSTER_HTML_ADMIT(view, token, keep_open, formatting, marker, drop);
    if (status == LXA_OK)
        *skip_newline = lxa_html_tree_name_equals(token->name, "pre", 3)
            || lxa_html_tree_name_equals(token->name, "listing", 7);
    return status;
}
