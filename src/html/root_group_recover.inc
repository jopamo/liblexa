/* Recover a current HTML colgroup before accepted root control/adoption. */
static inline lxa_status_t
LXA_ROOT_GROUP_NAME(LXA_ROOT_GROUP_VIEW *view)
{
    bool group;
    lxa_status_t status = LXA_ROOT_GROUP_QUERY(view, &group);
    if (status != LXA_OK || !group) return status;
    status = lxa_budget_charge(LXA_ROOT_GROUP_BUDGET(view), LXA_BUDGET_WORK,
        2, LXA_ROOT_GROUP_REASON(view));
    return status == LXA_OK ? LXA_ROOT_GROUP_POP(view) : status;
}
