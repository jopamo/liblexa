#include "css/blocks.h"

struct lxa_css_selector_list {
    lxa_vector_t ranges;
    lxa_budget_t *budget;
    lxa_budget_kind_t limit_reason;
    bool count_limit;
    bool valid;
};

lxa_status_t
lxa_css_selector_list_create(const lxa_allocator_t *allocator,
    lxa_css_blocks_t *blocks, lxa_css_selector_list_t **out)
{
    lxa_css_selector_list_t *list;
    lxa_vector_t storage;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    *out = NULL;
    if (blocks == NULL) return LXA_ERROR_ARGUMENT;
    status = lxa_css_blocks_check(blocks);
    if (status != LXA_OK) return status;
    status = lxa_vector_init(&storage, allocator, sizeof(lxa_css_selector_range_t));
    if (status != LXA_OK) return status;
    status = lxa_vector_bind_budget(&storage, blocks->budget);
    if (status != LXA_OK) return status;
    status = lxa_budget_charge(blocks->budget, LXA_BUDGET_MEMORY,
                               sizeof(*list), NULL);
    if (status != LXA_OK) return status;
    status = lxa_alloc(allocator, sizeof(*list), (void **) &list);
    if (status != LXA_OK) {
        lxa_budget_release(blocks->budget, LXA_BUDGET_MEMORY, sizeof(*list));
        return status;
    }
    *list = (lxa_css_selector_list_t) {
        .ranges = storage, .budget = blocks->budget,
        .limit_reason = LXA_BUDGET_COUNT
    };
    *out = list;
    return LXA_OK;
}

size_t
lxa_css_selector_list_length(const lxa_css_selector_list_t *list)
{
    return list == NULL || !list->valid ? 0 : lxa_vector_length(&list->ranges);
}

lxa_budget_kind_t
lxa_css_selector_list_limit_reason(const lxa_css_selector_list_t *list)
{
    return list == NULL ? LXA_BUDGET_COUNT : list->limit_reason;
}

bool
lxa_css_selector_list_count_limit(const lxa_css_selector_list_t *list)
{
    return list != NULL && list->count_limit;
}

lxa_status_t
lxa_css_selector_list_build(lxa_css_selector_list_t *list,
                            lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
                            size_t first, size_t after, size_t max_count)
{
    size_t start = first, at = first;
    bool nonempty = false;
    lxa_status_t status = LXA_OK;
    if (list == NULL || tokens == NULL || blocks == NULL || !blocks->ready
        || list->budget != blocks->budget
        || first > after || after > blocks->end
        || lxa_css_tokens_length(tokens) < blocks->end)
        return LXA_ERROR_ARGUMENT;
    list->valid = false;
    list->count_limit = false;
    list->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_css_blocks_check(blocks);
    if (status == LXA_OK)
        status = lxa_css_vector_check(&list->ranges,
                                      sizeof(lxa_css_selector_range_t));
    if (status != LXA_OK) goto fail;
    lxa_vector_clear(&list->ranges);
    while (at < after) {
        lxa_css_token_t token;
        lxa_span_t text;
        lxa_css_component_range_t component;
        status = lxa_budget_charge(list->budget, LXA_BUDGET_WORK, 1,
                                   &list->limit_reason);
        if (status != LXA_OK) goto fail;
        status = lxa_css_internal_status(
            lxa_css_tokens_get(tokens, at, &token, &text));
        if (status != LXA_OK) goto fail;
        if (token.kind == LXA_CSS_COMMA) {
            lxa_css_selector_range_t range = {start, at};
            if (!nonempty) goto fail;
            if (lxa_vector_length(&list->ranges) >= max_count) {
                list->count_limit = true;
                status = LXA_ERROR_LIMIT;
                goto fail;
            }
            status = lxa_css_internal_status(
                lxa_vector_push(&list->ranges, &range));
            if (status != LXA_OK) goto fail;
            start = ++at;
            nonempty = false;
            continue;
        }
        if (token.kind != LXA_CSS_WHITESPACE) nonempty = true;
        status = lxa_css_internal_status(
            lxa_css_blocks_next(blocks, at, &component));
        if (status != LXA_OK) {
            list->limit_reason = blocks->limit_reason;
            goto fail;
        }
        if (component.after > after) {
            status = LXA_ERROR_ARGUMENT;
            goto fail;
        }
        at = component.after;
    }
    if (!nonempty) goto fail;
    if (lxa_vector_length(&list->ranges) >= max_count) {
        list->count_limit = true;
        status = LXA_ERROR_LIMIT;
        goto fail;
    }
    lxa_css_selector_range_t range = {start, after};
    status = lxa_css_internal_status(lxa_vector_push(&list->ranges, &range));
    if (status != LXA_OK) goto fail;
    list->valid = true;
    return LXA_OK;
fail:
    if (status == LXA_ERROR_LIMIT && !list->count_limit
        && list->limit_reason == LXA_BUDGET_COUNT)
        list->limit_reason = LXA_BUDGET_MEMORY;
    lxa_vector_clear(&list->ranges);
    return status;
}

lxa_status_t
lxa_css_selector_list_get(lxa_css_selector_list_t *list, size_t index,
                          lxa_css_selector_range_t *out)
{
    void *slot;
    lxa_status_t status;
    if (list == NULL || out == NULL || !list->valid)
        return LXA_ERROR_ARGUMENT;
    status = lxa_css_vector_check(&list->ranges, sizeof(lxa_css_selector_range_t));
    if (status != LXA_OK) return status;
    status = lxa_vector_at(&list->ranges, index, &slot);
    if (status == LXA_OK) *out = *(const lxa_css_selector_range_t *) slot;
    return status;
}

void
lxa_css_selector_list_destroy(lxa_css_selector_list_t *list)
{
    if (list != NULL) {
        lxa_allocator_t allocator = list->ranges.bytes.allocator;
        lxa_vector_destroy(&list->ranges);
        lxa_budget_release(list->budget, LXA_BUDGET_MEMORY, sizeof(*list));
        lxa_free(&allocator, list);
    }
}
