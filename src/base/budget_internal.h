#ifndef LIBLEXA_BASE_BUDGET_INTERNAL_H
#define LIBLEXA_BASE_BUDGET_INTERNAL_H

#include "liblexa/base.h"

static inline uint64_t
lxa_budget_maximum(const lxa_limits_t *limits, lxa_budget_kind_t kind)
{
    switch (kind) {
        case LXA_BUDGET_INPUT: return limits->max_input_bytes;
        case LXA_BUDGET_MEMORY: return limits->max_memory_bytes;
        case LXA_BUDGET_NODES: return limits->max_nodes;
        case LXA_BUDGET_ATTRIBUTES: return limits->max_attributes;
        case LXA_BUDGET_STRINGS: return limits->max_string_bytes;
        case LXA_BUDGET_TREE_DEPTH: return limits->max_tree_depth;
        case LXA_BUDGET_OPEN_ELEMENTS: return limits->max_open_elements;
        case LXA_BUDGET_ACTIVE_FORMATTING:
            return limits->max_active_formatting;
        case LXA_BUDGET_CSS_NESTING: return limits->max_css_nesting;
        case LXA_BUDGET_TOKENS: return limits->max_tokens;
        case LXA_BUDGET_WORK: return limits->max_work;
        case LXA_BUDGET_ATTRS_PER_ELEMENT:
            return limits->max_attributes_per_element;
        case LXA_BUDGET_TOKEN_BYTES: return limits->max_token_bytes;
        case LXA_BUDGET_NAME_BYTES: return limits->max_name_bytes;
        default: return 0;
    }
}

static inline lxa_status_t
lxa_budget_check_impl(const lxa_budget_t *budget, lxa_budget_kind_t kind,
                 uint64_t amount, lxa_budget_kind_t *reason)
{
    uint64_t used, limit;
    if (reason != NULL) {
        *reason = LXA_BUDGET_COUNT;
    }
    if (budget == NULL || kind < 0 || kind > LXA_BUDGET_WORK) {
        return LXA_ERROR_ARGUMENT;
    }
    used = budget->used[kind];
    if (amount > UINT64_MAX - used) {
        return LXA_ERROR_OVERFLOW;
    }
    limit = lxa_budget_maximum(&budget->limits, kind);
    if (kind == LXA_BUDGET_WORK && budget->work_credit < limit) {
        limit = budget->work_credit;
    }
    if (used > limit || amount > limit - used) {
        if (reason != NULL) {
            *reason = kind;
        }
        return LXA_ERROR_LIMIT;
    }
    return LXA_OK;
}

static inline lxa_status_t
lxa_budget_charge_impl(lxa_budget_t *budget, lxa_budget_kind_t kind,
                  uint64_t amount, lxa_budget_kind_t *reason)
{
    lxa_status_t status;
    if (kind == LXA_BUDGET_INPUT) {
        if (reason != NULL) {
            *reason = LXA_BUDGET_COUNT;
        }
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_budget_check_impl(budget, kind, amount, reason);
    if (status == LXA_OK) {
        budget->used[kind] += amount;
    }
    return status;
}

/* Constant-kind charging uses the same checks as the installed APIs. */
static inline lxa_status_t
lxa_budget_charge_work(lxa_budget_t *budget, uint64_t amount,
                       lxa_budget_kind_t *reason)
{
    return lxa_budget_charge_impl(budget, LXA_BUDGET_WORK, amount, reason);
}

/* Equivalent to accepting one byte, then charging one WORK, repeatedly.
 * Aggregate only when every step succeeds; retain ordered partial failure. */
static inline lxa_status_t
lxa_budget_accept_work_run(lxa_budget_t *budget, size_t bytes,
    lxa_budget_kind_t *reason)
{
    if (budget != NULL && bytes != 0) {
        uint64_t rate = budget->limits.work_per_input_byte;
        uint64_t credit = budget->work_credit;
        uint64_t input = budget->used[LXA_BUDGET_INPUT];
        uint64_t work = budget->used[LXA_BUDGET_WORK];
        if (input <= budget->limits.max_input_bytes
            && bytes <= budget->limits.max_input_bytes - input
            && bytes <= UINT64_MAX - input
            && (rate == 0 || bytes <= (UINT64_MAX - credit) / rate)
            && work <= budget->limits.max_work
            && bytes <= budget->limits.max_work - work
            && (rate != 0 ? work < credit + rate
                          : work <= credit && bytes <= credit - work)) {
            lxa_status_t status = lxa_budget_accept_input(budget, bytes, reason);
            return status == LXA_OK ? lxa_budget_charge_work(budget, bytes, reason) : status;
        }
    }
    while (bytes-- != 0) {
        lxa_status_t status = lxa_budget_accept_input(budget, 1, reason);
        if (status == LXA_OK) status = lxa_budget_charge_work(budget, 1, reason);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

#endif
