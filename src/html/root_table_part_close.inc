/* Preflight before removing the planned table part and its marker suffix. */
static inline lxa_status_t
LXA_ROOT_PART_CLOSE_EXEC_NAME(LXA_ROOT_PART_CLOSE_EXEC_VIEW *view,
    lxa_html_table_group_t group)
{
    size_t index;
    lxa_status_t status = LXA_ROOT_PART_CLOSE_EXEC_PLAN(view, group, &index);
    if (status != LXA_OK) return status;
    if (index == LXA_ROOT_PART_CLOSE_EXEC_LENGTH(view)) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    status = LXA_ROOT_PART_CLOSE_EXEC_NODE(view);
    return status == LXA_OK ? LXA_ROOT_PART_CLOSE_EXEC_POP(view, index) : status;
}
