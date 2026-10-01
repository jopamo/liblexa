/* Execute an already-read actual HTML table context after earlier routing. */
static inline lxa_status_t
LXA_TEMPLATE_TABLE_START_NAME(LXA_TEMPLATE_TABLE_START_VIEW *view,
    const lxa_html_token_t *token, bool group, bool *handled)
{
    lxa_status_t status = LXA_OK;
    lxa_html_token_t adjusted;
    switch (lxa_html_tree_template_table_start_action(token->name, group)) {
    case LXA_HTML_TEMPLATE_TABLE_START_CONTINUE:
        *handled = false;
        return LXA_OK;
    case LXA_HTML_TEMPLATE_TABLE_START_COLUMN:
        status = LXA_TEMPLATE_TABLE_START_COLUMN(view, token);
        break;
    case LXA_HTML_TEMPLATE_TABLE_START_FOREIGN:
        if (group) status = LXA_TEMPLATE_TABLE_START_POP(view);
        if (status == LXA_OK)
            status = LXA_TEMPLATE_TABLE_START_FOSTER(view, token, !token->self_closing,
                lxa_html_tree_name_equals(token->name, "svg", 3) ? LXA_DOM_NS_SVG : LXA_DOM_NS_MATHML);
        break;
    case LXA_HTML_TEMPLATE_TABLE_START_HEAD_VOID:
        if (group) status = LXA_TEMPLATE_TABLE_START_POP(view);
        if (status == LXA_OK)
            status = LXA_TEMPLATE_TABLE_START_FOSTER(view, token, false, LXA_DOM_NS_HTML);
        break;
    case LXA_HTML_TEMPLATE_TABLE_START_TEXT:
        status = LXA_TEMPLATE_TABLE_START_TEXT_CHECK(view);
        if (status == LXA_OK && group) status = LXA_TEMPLATE_TABLE_START_POP(view);
        if (status == LXA_OK) status = LXA_TEMPLATE_TABLE_START_TEXT(view, token, true);
        break;
    case LXA_HTML_TEMPLATE_TABLE_START_POP_GROUP:
        status = LXA_TEMPLATE_TABLE_START_POP(view);
        if (status == LXA_OK) *handled = false;
        return status;
    case LXA_HTML_TEMPLATE_TABLE_START_XMP:
        status = LXA_TEMPLATE_TABLE_START_TEXT(view, token, false);
        break;
    case LXA_HTML_TEMPLATE_TABLE_START_VOID:
        if (group) {
            status = LXA_TEMPLATE_TABLE_START_VOID_PREFLIGHT(view);
            if (status == LXA_OK) status = LXA_TEMPLATE_TABLE_START_POP(view);
        }
        if (status == LXA_OK) {
            if (lxa_html_tree_name_equals(token->name, "image", 5)) {
                adjusted = *token;
                adjusted.name = (lxa_span_t){(const uint8_t *) "img", 3};
                token = &adjusted;
            }
            status = LXA_TEMPLATE_TABLE_START_FOSTER(view, token, false, LXA_DOM_NS_HTML);
        }
        break;
    case LXA_HTML_TEMPLATE_TABLE_START_OPEN:
        if (group) status = LXA_TEMPLATE_TABLE_START_POP(view);
        if (status == LXA_OK)
            status = LXA_TEMPLATE_TABLE_START_FOSTER(view, token, true, LXA_DOM_NS_HTML);
        if (status == LXA_OK && (lxa_html_tree_name_equals(token->name, "pre", 3)
                || lxa_html_tree_name_equals(token->name, "listing", 7)))
            LXA_TEMPLATE_TABLE_START_NEWLINE(view) = true;
        break;
    case LXA_HTML_TEMPLATE_TABLE_START_PART:
        status = LXA_TEMPLATE_TABLE_START_PART(view, token);
        break;
    case LXA_HTML_TEMPLATE_TABLE_START_FALLBACK: {
        bool ordinary;
        status = LXA_TEMPLATE_TABLE_START_SUPPORTED(view, token->name, &ordinary);
        if (status == LXA_OK && !ordinary) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        if (status == LXA_OK && group) status = LXA_TEMPLATE_TABLE_START_POP(view);
        if (status == LXA_OK)
            status = LXA_TEMPLATE_TABLE_START_FOSTER(view, token, true, LXA_DOM_NS_HTML);
        break;
    }
    default:
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    if (status == LXA_OK) *handled = true;
    return status;
}
