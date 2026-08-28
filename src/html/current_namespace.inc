/* Select tokenizer namespace feedback after effects. Empty root stacks and
 * exhausted template slices represent virtual HTML contexts. Integration
 * eligibility does not change the current element's namespace.
 */
static inline lxa_status_t
LXA_CURRENT_NAMESPACE_NAME(const LXA_CURRENT_NAMESPACE_VIEW *view,
    size_t boundary, lxa_dom_namespace_t *out)
{
    size_t length = LXA_CURRENT_NAMESPACE_LENGTH(view);
    if (out == NULL || boundary > length) return LXA_ERROR_ARGUMENT;
    lxa_dom_namespace_t ns = LXA_DOM_NS_HTML;
    lxa_status_t status = LXA_OK;
    if (length > boundary)
        status = LXA_CURRENT_NAMESPACE_READ(view, length - 1, &ns);
    if (status == LXA_OK) *out = ns;
    return status;
}
