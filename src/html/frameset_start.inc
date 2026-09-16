/* Prepare roots and admission before replacement, open publication and mode. */
static inline lxa_status_t
LXA_FRAMESET_START_NAME(LXA_FRAMESET_START_VIEW *view, const lxa_html_token_t *token)
{
    lxa_status_t status = LXA_FRAMESET_START_PREPARE(view);
    if (status == LXA_OK) status = LXA_FRAMESET_START_HEAD(view);
    if (status == LXA_OK) status = LXA_FRAMESET_START_ADMIT(view, token);
    if (status == LXA_OK) status = LXA_FRAMESET_START_REPLACE(view);
    if (status == LXA_OK) status = LXA_FRAMESET_START_PUSH(view);
    if (status == LXA_OK) LXA_FRAMESET_START_MODE(view) = LXA_TREE_IN_FRAMESET;
    return status;
}
