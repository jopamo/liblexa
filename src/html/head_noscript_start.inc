/* Admit disabled-scripting head noscript before publishing its mode. */
static inline lxa_status_t
LXA_HEAD_NOSCRIPT_NAME(LXA_HEAD_NOSCRIPT_VIEW *view, const lxa_html_token_t *token)
{
    lxa_status_t status = LXA_HEAD_NOSCRIPT_HEAD(view);
    if (status == LXA_OK) status = LXA_HEAD_NOSCRIPT_ADMIT(view, token);
    if (status == LXA_OK) LXA_HEAD_NOSCRIPT_MODE(view) = LXA_TREE_HEAD_NOSCRIPT;
    return status;
}
