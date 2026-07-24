/* Copyright (C) 2026 Paul Moses */
#include "liblexa/css.h"

lxa_status_t
lxa_css_declarations_init(lxa_css_declarations_t *list,
                          const lxa_allocator_t *allocator,
                          lxa_css_blocks_t *blocks)
{
    lxa_status_t status;
    if (list == NULL || blocks == NULL || blocks->budget == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *list = (lxa_css_declarations_t) {0};
    list->budget = blocks->budget;
    list->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_vector_init(&list->entries, allocator,
                             sizeof(lxa_css_declaration_t));
    if (status != LXA_OK) return status;
    status = lxa_vector_bind_budget(&list->entries, list->budget);
    if (status != LXA_OK) lxa_vector_destroy(&list->entries);
    return status;
}

lxa_status_t
lxa_css_declarations_build(lxa_css_declarations_t *list,
                           lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
                           size_t first, size_t after, size_t max_count)
{
    size_t at = first;
    lxa_status_t status = LXA_OK;
    if (list == NULL || tokens == NULL || blocks == NULL || !blocks->ready
        || list->budget != blocks->budget || list->entries.element_size == 0
        || first > after || after > blocks->end
        || lxa_css_tokens_length(tokens) < blocks->end) {
        return LXA_ERROR_ARGUMENT;
    }
    list->ready = false;
    list->count_limit = false;
    list->limit_reason = LXA_BUDGET_COUNT;
    lxa_vector_clear(&list->entries);
    while (at < after) {
        lxa_css_token_t token;
        lxa_span_t text;
        size_t end;
        lxa_css_component_range_t range;
        lxa_css_declaration_t decl;
        lxa_css_rule_t rule;
        status = lxa_budget_charge(list->budget, LXA_BUDGET_WORK, 1,
                                   &list->limit_reason);
        if (status != LXA_OK) break;
        status = lxa_css_tokens_get(tokens, at, &token, &text);
        if (status != LXA_OK) break;
        if (token.kind == LXA_CSS_WHITESPACE
            || token.kind == LXA_CSS_SEMICOLON) {
            at++;
            continue;
        }
        if (token.kind == LXA_CSS_AT_KEYWORD) {
            status = lxa_css_rule_parse(tokens, blocks, at, after, &rule);
            if (status != LXA_OK) {
                list->limit_reason = blocks->limit_reason;
                break;
            }
            if (rule.after <= at || rule.after > after) {
                status = LXA_ERROR_ARGUMENT;
                break;
            }
            at = rule.after;
            continue;
        }
        /* A semicolon inside a function or block belongs to that component.
         * The resulting segment is tried once as a declaration. */
        end = at;
        while (end < after) {
            status = lxa_budget_charge(list->budget, LXA_BUDGET_WORK, 1,
                                       &list->limit_reason);
            if (status != LXA_OK) break;
            status = lxa_css_tokens_get(tokens, end, &token, &text);
            if (status != LXA_OK) break;
            if (token.kind == LXA_CSS_SEMICOLON) break;
            status = lxa_css_blocks_next(blocks, end, &range);
            if (status != LXA_OK) {
                list->limit_reason = blocks->limit_reason;
                break;
            }
            if (range.after > after) {
                status = LXA_ERROR_ARGUMENT;
                break;
            }
            end = range.after;
        }
        if (status != LXA_OK) break;
        if (at == end) {
            at++;
            continue;
        }
        status = lxa_css_declaration_parse(tokens, blocks, at, end, &decl);
        if (status != LXA_OK) {
            list->limit_reason = blocks->limit_reason;
            break;
        }
        if (decl.valid) {
            if (lxa_vector_length(&list->entries) >= max_count) {
                list->count_limit = true;
                status = LXA_ERROR_LIMIT;
                break;
            }
            status = lxa_vector_push(&list->entries, &decl);
            if (status == LXA_ERROR_LIMIT) {
                list->limit_reason = LXA_BUDGET_MEMORY;
            }
            if (status != LXA_OK) break;
            at = end < after ? end + 1 : end;
            continue;
        }
        /* A nested qualified rule can end before the next semicolon. Skip it
         * whole, then resume at its following declaration. */
        status = lxa_css_rule_parse(tokens, blocks, at, after, &rule);
        if (status != LXA_OK) {
            list->limit_reason = blocks->limit_reason;
            break;
        }
        if (rule.has_block && rule.after > at && rule.after <= after) {
            at = rule.after;
        }
        else {
            at = end < after ? end + 1 : end;
        }
    }
    if (status != LXA_OK) {
        lxa_vector_clear(&list->entries);
    }
    else {
        list->ready = true;
    }
    return status;
}

lxa_status_t
lxa_css_declarations_get(lxa_css_declarations_t *list, size_t at,
                         lxa_css_declaration_t *out)
{
    void *slot;
    lxa_status_t status;
    if (list == NULL || out == NULL || !list->ready) return LXA_ERROR_ARGUMENT;
    status = lxa_vector_at(&list->entries, at, &slot);
    if (status == LXA_OK) *out = *(const lxa_css_declaration_t *) slot;
    return status;
}

void
lxa_css_declarations_destroy(lxa_css_declarations_t *list)
{
    if (list != NULL) {
        lxa_vector_destroy(&list->entries);
        list->budget = NULL;
        list->ready = false;
    }
}
