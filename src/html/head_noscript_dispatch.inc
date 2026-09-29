/* Consume ignored/closed tokens or borrow the suffix after emitted spaces. */
static inline lxa_status_t
LXA_HEAD_NOSCRIPT_DISPATCH_NAME(LXA_HEAD_NOSCRIPT_DISPATCH_VIEW *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted, bool *handled)
{
    if (LXA_HEAD_NOSCRIPT_DISPATCH_MODE(view) == LXA_TREE_HEAD_NOSCRIPT) {
        lxa_html_head_noscript_plan_t plan;
        lxa_status_t status = LXA_HEAD_NOSCRIPT_DISPATCH_EXEC(view, *token, &plan);
        if (status != LXA_OK) return status;
        if (plan.action == LXA_HTML_HEAD_NOSCRIPT_IGNORE
            || plan.action == LXA_HTML_HEAD_NOSCRIPT_CLOSE) {
            *handled = true;
            return LXA_OK;
        }
        if (plan.space_prefix != 0) {
            *adjusted = **token;
            adjusted->data.data += plan.space_prefix;
            adjusted->data.length -= plan.space_prefix;
            *token = adjusted;
        }
    }
    *handled = false;
    return LXA_OK;
}
