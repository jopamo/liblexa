#include "css/blocks.h"

struct lxa_css_declarations {
    lxa_vector_t entries;
    lxa_budget_t *budget;
    lxa_budget_kind_t limit_reason;
    bool count_limit;
    bool ready;
};

lxa_status_t
lxa_css_declarations_create(const lxa_allocator_t *allocator,
    lxa_css_blocks_t *blocks, lxa_css_declarations_t **out)
{
    lxa_css_declarations_t *list;
    lxa_vector_t storage;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    *out = NULL;
    if (blocks == NULL) return LXA_ERROR_ARGUMENT;
    status = lxa_css_blocks_check(blocks);
    if (status != LXA_OK) return status;
    status = lxa_vector_init(&storage, allocator, sizeof(lxa_css_declaration_t));
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
    *list = (lxa_css_declarations_t){
        .entries = storage, .budget = blocks->budget, .limit_reason = LXA_BUDGET_COUNT};
    *out = list;
    return LXA_OK;
}

size_t
lxa_css_declarations_length(const lxa_css_declarations_t *list)
{
    return list == NULL || !list->ready ? 0 : lxa_vector_length(&list->entries);
}

lxa_budget_kind_t
lxa_css_declarations_limit_reason(const lxa_css_declarations_t *list)
{
    return list == NULL ? LXA_BUDGET_COUNT : list->limit_reason;
}

bool
lxa_css_declarations_count_limit(const lxa_css_declarations_t *list)
{
    return list != NULL && list->count_limit;
}

lxa_status_t
lxa_css_declarations_build(lxa_css_declarations_t *list,
    lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
    size_t first, size_t after, size_t max_count)
{
    size_t at = first;
    lxa_status_t status = LXA_OK;
    if (list == NULL || tokens == NULL || blocks == NULL || !blocks->ready
        || list->budget != blocks->budget
        || first > after || after > blocks->end
        || lxa_css_tokens_length(tokens) < blocks->end) {
        return LXA_ERROR_ARGUMENT;
    }
    list->ready = false;
    list->count_limit = false;
    list->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_css_blocks_check(blocks);
    if (status == LXA_OK)
        status = lxa_css_vector_check(&list->entries,
            sizeof(lxa_css_declaration_t));
    if (status != LXA_OK) {
        lxa_vector_clear(&list->entries);
        return status;
    }
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
        status = lxa_css_internal_status(
            lxa_css_tokens_get(tokens, at, &token, &text));
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
                status = lxa_css_invariant(false);
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
            status = lxa_css_internal_status(
                lxa_css_tokens_get(tokens, end, &token, &text));
            if (status != LXA_OK) break;
            if (token.kind == LXA_CSS_SEMICOLON) break;
            status = lxa_css_internal_status(
                lxa_css_blocks_next(blocks, end, &range));
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
            status = lxa_css_internal_status(
                lxa_vector_push(&list->entries, &decl));
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
        } else {
            at = end < after ? end + 1 : end;
        }
    }
    if (status != LXA_OK) {
        lxa_vector_clear(&list->entries);
    } else {
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
    status = lxa_css_vector_check(&list->entries, sizeof(lxa_css_declaration_t));
    if (status != LXA_OK) return status;
    status = lxa_vector_at(&list->entries, at, &slot);
    if (status == LXA_OK) *out = *(const lxa_css_declaration_t *) slot;
    return status;
}

void
lxa_css_declarations_destroy(lxa_css_declarations_t *list)
{
    if (list != NULL) {
        lxa_allocator_t allocator = list->entries.bytes.allocator;
        lxa_vector_destroy(&list->entries);
        lxa_budget_release(list->budget, LXA_BUDGET_MEMORY, sizeof(*list));
        lxa_free(&allocator, list);
    }
}
