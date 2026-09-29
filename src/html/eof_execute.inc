/* Accepted EOF after initial/TEXT intake, pending flush and template gating. */
static inline lxa_status_t
LXA_EOF_EXEC_NAME(LXA_EOF_EXEC_VIEW *view)
{
    if (LXA_EOF_EXEC_MODE(view) == LXA_TREE_TEMPLATE_TEXT) {
        lxa_status_t status = LXA_EOF_EXEC_TEMPLATES(view);
        if (status != LXA_OK) return status;
    }
    return LXA_EOF_EXEC_ROOT(view);
}
