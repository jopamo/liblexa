/* Route once, then apply foreign effects or continue through HTML rules. */
static inline lxa_status_t
LXA_ROOT_FOREIGN_DISPATCH_NAME(LXA_ROOT_FOREIGN_DISPATCH_VIEW *view,
    const lxa_html_token_t *token, bool *handled)
{
    lxa_html_foreign_root_route_t route;
    lxa_status_t status = LXA_ROOT_FOREIGN_DISPATCH_ROUTE(view, token, &route);
    if (status != LXA_OK) return status;
    bool consumed = false;
    switch (route) {
        case LXA_HTML_FOREIGN_ROOT_HTML:
            break;
        case LXA_HTML_FOREIGN_ROOT_BREAKOUT:
            status = LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT(view, token);
            break;
        case LXA_HTML_FOREIGN_ROOT_CONSUME: {
            bool reprocess;
            status = LXA_ROOT_FOREIGN_DISPATCH_CONSUME(view, token, &reprocess);
            if (status == LXA_OK) consumed = !reprocess;
            break;
        }
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
    if (status == LXA_OK) *handled = consumed;
    return status;
}
