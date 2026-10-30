/* Instantiate admission planning with static list access.
 * LXA_NOAH_{PLAN_NAME,VIEW,LENGTH,EQUAL,BUDGET,REASON} belong to the includer.
 * The owner supplies the boundary after its last marker and performs admission.
 */
static inline lxa_status_t
LXA_NOAH_PLAN_NAME(const LXA_NOAH_VIEW *view, size_t start,
    const lxa_html_token_t *token, size_t *out)
{
    if (!lxa_html_formatting_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    size_t i = LXA_NOAH_LENGTH(view), matches = 0, drop = SIZE_MAX;
    while (i > start) {
        bool equal;
        lxa_status_t status = lxa_budget_charge(LXA_NOAH_BUDGET(view),
            LXA_BUDGET_WORK, 1, LXA_NOAH_REASON(view));
        if (status == LXA_OK) status = LXA_NOAH_EQUAL(view, --i, token, &equal);
        if (status != LXA_OK) return status;
        if (equal && ++matches >= 3) drop = i;
    }
    *out = drop;
    return LXA_OK;
}
