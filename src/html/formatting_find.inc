/* Instantiate active-formatting name lookup with static list access.
 * LXA_FORMATTING_FIND_{NAME,VIEW,LENGTH,READ,BUDGET,REASON} belong to the includer.
 */
static inline lxa_status_t
LXA_FORMATTING_FIND_NAME(const LXA_FORMATTING_FIND_VIEW *view, size_t start,
    lxa_span_t target, size_t *out)
{
    size_t i = LXA_FORMATTING_FIND_LENGTH(view), found = i;
    while (i > start) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(LXA_FORMATTING_FIND_BUDGET(view),
            LXA_BUDGET_WORK, 1, LXA_FORMATTING_FIND_REASON(view));
        if (status == LXA_OK) status = LXA_FORMATTING_FIND_READ(view, --i, &name);
        if (status != LXA_OK) return status;
        if (name.length == target.length
            && memcmp(name.data, target.data, name.length) == 0) {
            found = i;
            break;
        }
    }
    *out = found;
    return LXA_OK;
}
