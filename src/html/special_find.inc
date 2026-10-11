/* Instantiate forward special-element lookup.
 * LXA_SPECIAL_{NAME,VIEW,LENGTH,READ,TAG_ID,BUDGET,REASON} belong to the includer.
 */
static inline lxa_status_t
LXA_SPECIAL_NAME(const LXA_SPECIAL_VIEW *view, size_t start, size_t *out)
{
    size_t i = start;
    while (i < LXA_SPECIAL_LENGTH(view)) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(LXA_SPECIAL_BUDGET(view),
            LXA_BUDGET_WORK, 1, LXA_SPECIAL_REASON(view));
        if (status == LXA_OK) status = LXA_SPECIAL_READ(view, i, &name, &ns);
        if (status != LXA_OK) return status;
        if (liblexa_html_tag_metadata_table[
                LXA_SPECIAL_TAG_ID(view, i, name, ns)].special != 0)
            break;
        ++i;
    }
    *out = i;
    return LXA_OK;
}
