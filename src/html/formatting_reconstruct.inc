/* Instantiate suffix planning with static open-identity access.
 * LXA_RECONSTRUCT_{PLAN_NAME,VIEW,LENGTH,OPEN} belong to the includer.
 * Open lookup owns its WORK charges; there is no extra formatting visit charge.
 */
static inline lxa_status_t
LXA_RECONSTRUCT_PLAN_NAME(const LXA_RECONSTRUCT_VIEW *view, size_t start, size_t *out)
{
    size_t i = LXA_RECONSTRUCT_LENGTH(view);
    while (i > start) {
        bool open;
        lxa_status_t status = LXA_RECONSTRUCT_OPEN(view, i - 1, &open);
        if (status != LXA_OK) return status;
        if (open) break;
        --i;
    }
    *out = i;
    return LXA_OK;
}
