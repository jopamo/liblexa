/* Recover an accepted foster-start colgroup before owner admission effects. */
static inline lxa_status_t
LXA_ROOT_FOSTER_GROUP_NAME(LXA_ROOT_FOSTER_GROUP_VIEW *view, bool container)
{
    lxa_status_t status = container ? LXA_ROOT_FOSTER_GROUP_RESERVE(view) : LXA_OK;
    if (status == LXA_OK) status = LXA_ROOT_FOSTER_GROUP_NODE(view);
    if (status == LXA_OK) status = LXA_ROOT_FOSTER_GROUP_WORK(view);
    return status == LXA_OK ? LXA_ROOT_FOSTER_GROUP_POP(view) : status;
}
