#include "css/tokens.h"
#include "css/blocks.h"

static lxa_css_token_kind_t
closing(lxa_css_token_kind_t kind)
{
    switch (kind) {
    case LXA_CSS_LEFT_BRACE: return LXA_CSS_RIGHT_BRACE;
    case LXA_CSS_LEFT_SQUARE: return LXA_CSS_RIGHT_SQUARE;
    case LXA_CSS_LEFT_PAREN:
    case LXA_CSS_FUNCTION: return LXA_CSS_RIGHT_PAREN;
    default: return LXA_CSS_TOKEN_COUNT;
    }
}

static lxa_status_t
release_depth(lxa_budget_t *budget, size_t depth)
{
    return lxa_css_internal_status(lxa_budget_release(budget,
        LXA_BUDGET_CSS_NESTING, depth));
}

lxa_status_t
lxa_css_blocks_create(const lxa_allocator_t *allocator, lxa_budget_t *budget,
                      lxa_css_blocks_t **out)
{
    lxa_css_blocks_t *blocks;
    lxa_vector_t entries;
    lxa_budget_t local;
    lxa_budget_t *active = budget;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    *out = NULL;
    status = lxa_vector_init(&entries, allocator, sizeof(lxa_css_block_t));
    if (status != LXA_OK) return status;
    if (active == NULL) {
        status = lxa_budget_init(&local, NULL);
        if (status != LXA_OK) return status;
        active = &local;
    }
    status = lxa_budget_charge(active, LXA_BUDGET_MEMORY, sizeof(*blocks), NULL);
    if (status != LXA_OK) return status;
    status = lxa_alloc(allocator, sizeof(*blocks), (void **) &blocks);
    if (status != LXA_OK) {
        lxa_budget_release(active, LXA_BUDGET_MEMORY, sizeof(*blocks));
        return status;
    }
    *blocks = (lxa_css_blocks_t) {
        .entries = entries, .limit_reason = LXA_BUDGET_COUNT
    };
    if (budget == NULL) blocks->local_budget = local;
    blocks->budget = budget == NULL ? &blocks->local_budget : budget;
    status = lxa_vector_bind_budget(&blocks->entries, blocks->budget);
    if (status != LXA_OK) {
        lxa_css_blocks_destroy(blocks);
        return status;
    }
    *out = blocks;
    return LXA_OK;
}

size_t
lxa_css_blocks_end(const lxa_css_blocks_t *blocks)
{
    return blocks == NULL || !blocks->ready ? 0 : blocks->end;
}

lxa_budget_t *
lxa_css_blocks_budget(lxa_css_blocks_t *blocks)
{
    return blocks == NULL ? NULL : blocks->budget;
}

lxa_budget_kind_t
lxa_css_blocks_limit_reason(const lxa_css_blocks_t *blocks)
{
    return blocks == NULL ? LXA_BUDGET_COUNT : blocks->limit_reason;
}

lxa_status_t
lxa_css_blocks_build(lxa_css_blocks_t *blocks, lxa_css_tokens_t *tokens)
{
    lxa_vector_t stack;
    size_t count, i;
    lxa_status_t status;
    if (blocks == NULL || tokens == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    blocks->end = 0;
    blocks->ready = false;
    blocks->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_css_blocks_check(blocks);
    if (status != LXA_OK) {
        lxa_vector_clear(&blocks->entries);
        return status;
    }
    status = lxa_css_vector_check(&tokens->entries,
                                  sizeof(lxa_css_token_entry_t));
    if (status != LXA_OK) {
        lxa_vector_clear(&blocks->entries);
        return status;
    }
    lxa_vector_clear(&blocks->entries);
    if (blocks->budget != &blocks->local_budget
        && (tokens->text.budget != blocks->budget
            || tokens->entries.bytes.budget != blocks->budget)) {
        return LXA_ERROR_ARGUMENT;
    }
    count = lxa_css_tokens_length(tokens);
    if (count > UINT32_MAX) {
        return LXA_ERROR_OVERFLOW;
    }
    status = lxa_vector_init(&stack, &blocks->entries.bytes.allocator,
                             sizeof(uint32_t));
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_vector_bind_budget(&stack, blocks->budget);
    if (status != LXA_OK) {
        goto done;
    }
    for (i = 0; i < count; i++) {
        lxa_css_token_t token;
        lxa_span_t text;
        size_t depth = lxa_vector_length(&stack);
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) {
            break;
        }
        status = lxa_css_internal_status(
            lxa_css_tokens_get(tokens, i, &token, &text));
        if (status != LXA_OK) {
            break;
        }
        if (token.kind == LXA_CSS_EOF) {
            if (i + 1 != count) {
                status = lxa_css_invariant(false);
            }
            break;
        }
        if (depth != 0) {
            void *slot;
            uint32_t id;
            lxa_css_block_t *block;
            status = lxa_css_vector_at(&stack, depth - 1, &slot);
            if (status != LXA_OK) {
                break;
            }
            id = *(const uint32_t *) slot;
            status = lxa_css_vector_at(&blocks->entries, id, &slot);
            if (status != LXA_OK) {
                break;
            }
            block = slot;
            if (closing(block->opener) == token.kind) {
                block->close = i;
                status = lxa_css_internal_status(
                    lxa_vector_erase(&stack, depth - 1, 1));
                if (status != LXA_OK) break;
                status = release_depth(blocks->budget, 1);
                if (status != LXA_OK) break;
                continue;
            }
        }
        if (closing(token.kind) != LXA_CSS_TOKEN_COUNT) {
            uint32_t parent = LXA_CSS_BLOCK_NONE;
            uint32_t id = (uint32_t) lxa_vector_length(&blocks->entries);
            lxa_css_block_t block = {i, count, parent, token.kind};
            void *slot;
            if (depth != 0) {
                status = lxa_css_vector_at(&stack, depth - 1, &slot);
                if (status != LXA_OK) break;
                block.parent = *(const uint32_t *) slot;
            }
            /* Depth rejection must not grow retained arrays first. */
            status = lxa_budget_check(blocks->budget, LXA_BUDGET_CSS_NESTING,
                                      1, &blocks->limit_reason);
            if (status != LXA_OK) {
                break;
            }
            status = lxa_vector_reserve(&blocks->entries, (size_t) id + 1);
            if (status == LXA_OK) {
                status = lxa_vector_reserve(&stack, depth + 1);
            }
            if (status == LXA_ERROR_LIMIT) {
                blocks->limit_reason = LXA_BUDGET_MEMORY;
            }
            if (status != LXA_OK) {
                break;
            }
            status = lxa_budget_charge(blocks->budget, LXA_BUDGET_CSS_NESTING,
                                       1, &blocks->limit_reason);
            if (status != LXA_OK) {
                break;
            }
            status = lxa_css_internal_status(
                lxa_vector_push(&blocks->entries, &block));
            if (status != LXA_OK) {
                lxa_status_t cleanup = release_depth(blocks->budget, 1);
                if (cleanup != LXA_OK) status = cleanup;
                break;
            }
            status = lxa_css_internal_status(lxa_vector_push(&stack, &id));
            if (status != LXA_OK) {
                lxa_status_t cleanup = lxa_css_internal_status(
                    lxa_vector_erase(&blocks->entries, id, 1));
                if (cleanup != LXA_OK) status = cleanup;
                cleanup = release_depth(blocks->budget, 1);
                if (cleanup != LXA_OK) status = cleanup;
                break;
            }
        }
    }
    if (status == LXA_OK) {
        /* EOF closes all still-open blocks without synthesizing tokens. */
        while (lxa_vector_length(&stack) != 0) {
            void *slot;
            uint32_t id;
            /* Cleanup must release every live depth charge even when work
             * runs out during implicit EOF closure. */
            if (status == LXA_OK) {
                status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK,
                                           1, &blocks->limit_reason);
            }
            lxa_status_t access = lxa_css_vector_at(
                &stack, lxa_vector_length(&stack) - 1, &slot);
            if (access != LXA_OK) {
                status = access;
                break;
            }
            id = *(const uint32_t *) slot;
            access = lxa_css_vector_at(&blocks->entries, id, &slot);
            if (access != LXA_OK) {
                status = access;
                break;
            }
            ((lxa_css_block_t *) slot)->close = i;
            access = lxa_css_internal_status(lxa_vector_erase(
                &stack, lxa_vector_length(&stack) - 1, 1));
            if (access != LXA_OK) {
                status = access;
                break;
            }
            access = release_depth(blocks->budget, 1);
            if (access != LXA_OK) {
                status = access;
                break;
            }
        }
    }
done:
    if (status != LXA_OK) {
        lxa_status_t cleanup = release_depth(blocks->budget,
                                             lxa_vector_length(&stack));
        if (cleanup != LXA_OK) status = cleanup;
        lxa_vector_clear(&blocks->entries);
    }
    else {
        blocks->end = i;
        blocks->ready = true;
    }
    lxa_vector_destroy(&stack);
    return status;
}

lxa_status_t
lxa_css_blocks_get(lxa_css_blocks_t *blocks, size_t index,
                   lxa_css_block_t *out)
{
    void *entry;
    lxa_status_t status;
    if (out == NULL || blocks == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_css_vector_check(&blocks->entries, sizeof(lxa_css_block_t));
    if (status != LXA_OK) return status;
    status = lxa_vector_at(&blocks->entries, index, &entry);
    if (status == LXA_OK) {
        *out = *(const lxa_css_block_t *) entry;
    }
    return status;
}

lxa_status_t
lxa_css_blocks_next(lxa_css_blocks_t *blocks, size_t from,
                    lxa_css_component_range_t *out)
{
    size_t lo, hi;
    lxa_status_t status;
    if (blocks == NULL || out == NULL || !blocks->ready || from >= blocks->end) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_css_blocks_check(blocks);
    if (status != LXA_OK) return status;
    blocks->limit_reason = LXA_BUDGET_COUNT;
    /* Block entries are appended in token order. Search the stable opener
     * offsets; no pointer into the index survives a later build. */
    lo = 0;
    hi = lxa_vector_length(&blocks->entries);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        void *slot;
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) {
            return status;
        }
        status = lxa_css_vector_at(&blocks->entries, mid, &slot);
        if (status != LXA_OK) {
            return status;
        }
        if (((const lxa_css_block_t *) slot)->open < from) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                               &blocks->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    if (lo < lxa_vector_length(&blocks->entries)) {
        void *slot;
        status = lxa_css_vector_at(&blocks->entries, lo, &slot);
        if (status != LXA_OK) {
            return status;
        }
        const lxa_css_block_t *block = slot;
        if (block->open == from) {
            status = lxa_css_invariant(block->close > block->open
                                       && block->close <= blocks->end
                                       && closing(block->opener)
                                          != LXA_CSS_TOKEN_COUNT);
            if (status != LXA_OK) return status;
            *out = (lxa_css_component_range_t) {
                from, block->close < blocks->end ? block->close + 1 : blocks->end,
                block->close < blocks->end
            };
            return LXA_OK;
        }
    }
    *out = (lxa_css_component_range_t) {from, from + 1, false};
    return LXA_OK;
}

void
lxa_css_blocks_destroy(lxa_css_blocks_t *blocks)
{
    if (blocks != NULL) {
        lxa_allocator_t allocator = blocks->entries.bytes.allocator;
        lxa_vector_destroy(&blocks->entries);
        lxa_budget_release(blocks->budget, LXA_BUDGET_MEMORY, sizeof(*blocks));
        lxa_free(&allocator, blocks);
    }
}
