#include <string.h>
#include "base/budget_internal.h"

lxa_limits_t
lxa_limits_default(void)
{
    lxa_limits_t limits = {
        .max_input_bytes = 256u * 1024u * 1024u,
        .max_memory_bytes = 256u * 1024u * 1024u,
        .max_nodes = 2000000,
        .max_attributes = 2000000,
        .max_attributes_per_element = 65535,
        .max_string_bytes = 256u * 1024u * 1024u,
        .max_token_bytes = 16u * 1024u * 1024u,
        .max_name_bytes = 1024u * 1024u,
        .max_tree_depth = 4096,
        .max_open_elements = 4096,
        .max_active_formatting = 4096,
        .max_css_nesting = 4096,
        .max_tokens = 2000000,
        .initial_work_credit = UINT64_C(1000000),
        .work_per_input_byte = 128,
        .max_work = UINT64_MAX};
    return limits;
}

lxa_limits_t
lxa_limits_unbounded(void)
{
    lxa_limits_t limits = {
        .max_input_bytes = SIZE_MAX,
        .max_memory_bytes = SIZE_MAX,
        .max_nodes = UINT32_MAX,
        .max_attributes = UINT32_MAX,
        .max_attributes_per_element = UINT32_MAX,
        .max_string_bytes = SIZE_MAX,
        .max_token_bytes = SIZE_MAX,
        .max_name_bytes = SIZE_MAX,
        .max_tree_depth = UINT32_MAX,
        .max_open_elements = UINT32_MAX,
        .max_active_formatting = UINT32_MAX,
        .max_css_nesting = UINT32_MAX,
        .max_tokens = UINT32_MAX,
        .initial_work_credit = UINT64_MAX,
        .work_per_input_byte = 0,
        .max_work = UINT64_MAX};
    return limits;
}

lxa_status_t
lxa_budget_init(lxa_budget_t *budget, const lxa_limits_t *limits)
{
    if (budget == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    budget->limits = limits == NULL ? lxa_limits_default() : *limits;
    memset(budget->used, 0, sizeof(budget->used));
    budget->work_credit = budget->limits.initial_work_credit;
    return LXA_OK;
}

void
lxa_budget_reset(lxa_budget_t *budget)
{
    if (budget != NULL) {
        memset(budget->used, 0, sizeof(budget->used));
        budget->work_credit = budget->limits.initial_work_credit;
    }
}

void
lxa_budget_reset_session(lxa_budget_t *budget)
{
    if (budget != NULL) {
        budget->used[LXA_BUDGET_INPUT] = 0;
        budget->used[LXA_BUDGET_WORK] = 0;
        budget->work_credit = budget->limits.initial_work_credit;
    }
}

lxa_status_t
lxa_budget_check(const lxa_budget_t *budget, lxa_budget_kind_t kind,
    uint64_t amount, lxa_budget_kind_t *reason)
{
    return lxa_budget_check_impl(budget, kind, amount, reason);
}

lxa_status_t
lxa_budget_charge(lxa_budget_t *budget, lxa_budget_kind_t kind,
    uint64_t amount, lxa_budget_kind_t *reason)
{
    return lxa_budget_charge_impl(budget, kind, amount, reason);
}

lxa_status_t
lxa_budget_accept_input(lxa_budget_t *budget, size_t bytes,
    lxa_budget_kind_t *reason)
{
    lxa_status_t status;
    uint64_t credit;
#if SIZE_MAX > UINT64_MAX
    if (bytes > UINT64_MAX) {
        if (reason != NULL) {
            *reason = LXA_BUDGET_COUNT;
        }
        return LXA_ERROR_OVERFLOW;
    }
#endif
    status = lxa_budget_check(budget, LXA_BUDGET_INPUT, bytes, reason);
    if (status != LXA_OK) {
        return status;
    }
    if (budget->limits.work_per_input_byte != 0
        && bytes > (UINT64_MAX - budget->work_credit)
                / budget->limits.work_per_input_byte) {
        return LXA_ERROR_OVERFLOW;
    }
    credit = (uint64_t) bytes * budget->limits.work_per_input_byte;
    budget->used[LXA_BUDGET_INPUT] += bytes;
    budget->work_credit += credit;
    return LXA_OK;
}

lxa_status_t
lxa_budget_check_item(const lxa_budget_t *budget, lxa_budget_kind_t kind,
    uint64_t size, lxa_budget_kind_t *reason)
{
    return lxa_budget_check_item_impl(budget, kind, size, reason);
}

lxa_status_t
lxa_budget_release(lxa_budget_t *budget, lxa_budget_kind_t kind,
    uint64_t amount)
{
    if (budget == NULL || (kind != LXA_BUDGET_MEMORY && (kind < LXA_BUDGET_TREE_DEPTH || kind > LXA_BUDGET_CSS_NESTING))
        || amount > budget->used[kind]) {
        return LXA_ERROR_ARGUMENT;
    }
    budget->used[kind] -= amount;
    return LXA_OK;
}
