/* Instantiate marker-bounded formatting identity lookup.
 * LXA_FORMATTING_REFERENCE_{NAME,VIEW,LENGTH,READ,BUDGET,REASON} belong to the includer.
 */
static inline lxa_status_t
LXA_FORMATTING_REFERENCE_NAME(const LXA_FORMATTING_REFERENCE_VIEW *view, size_t start,
    uint64_t identity, size_t *out)
{
    size_t i = LXA_FORMATTING_REFERENCE_LENGTH(view), found = i;
    while (i > start) {
        uint64_t current;
        lxa_status_t status = lxa_budget_charge(LXA_FORMATTING_REFERENCE_BUDGET(view),
            LXA_BUDGET_WORK, 1, LXA_FORMATTING_REFERENCE_REASON(view));
        if (status == LXA_OK) status = LXA_FORMATTING_REFERENCE_READ(view, --i, &current);
        if (status != LXA_OK) return status;
        if (current == identity) {
            found = i;
            break;
        }
    }
    *out = found;
    return LXA_OK;
}
