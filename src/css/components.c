/* Copyright (C) 2026 Paul Moses */
#include "css/tokens.h"
#include <assert.h>
#include "liblexa/css.h"

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

static void
release_depth(lxa_budget_t *budget, size_t depth)
{
    lxa_status_t status = lxa_budget_release(budget,
        LXA_BUDGET_CSS_NESTING, depth);
    assert(status == LXA_OK);
    (void) status;
}

lxa_status_t
lxa_css_blocks_init(lxa_css_blocks_t *blocks,
                    const lxa_allocator_t *allocator, lxa_budget_t *budget)
{
    lxa_status_t status;
    if (blocks == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *blocks = (lxa_css_blocks_t) {0};
    lxa_budget_init(&blocks->local_budget, NULL);
    blocks->budget = budget == NULL ? &blocks->local_budget : budget;
    blocks->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_vector_init(&blocks->entries, allocator,
                             sizeof(lxa_css_block_t));
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_vector_bind_budget(&blocks->entries, blocks->budget);
    if (status != LXA_OK) {
        lxa_vector_destroy(&blocks->entries);
    }
    return status;
}

lxa_status_t
lxa_css_blocks_build(lxa_css_blocks_t *blocks, lxa_css_tokens_t *tokens)
{
    lxa_vector_t stack;
    size_t count, i;
    lxa_status_t status;
    if (blocks == NULL || tokens == NULL || blocks->budget == NULL
        || blocks->entries.element_size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    lxa_vector_clear(&blocks->entries);
    blocks->end = 0;
    blocks->ready = false;
    blocks->limit_reason = LXA_BUDGET_COUNT;
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
        status = lxa_css_tokens_get(tokens, i, &token, &text);
        if (status != LXA_OK) {
            break;
        }
        if (token.kind == LXA_CSS_EOF) {
            if (i + 1 != count) {
                status = LXA_ERROR_ARGUMENT;
            }
            break;
        }
        if (depth != 0) {
            void *slot;
            uint32_t id;
            lxa_css_block_t *block;
            status = lxa_vector_at(&stack, depth - 1, &slot);
            if (status != LXA_OK) {
                break;
            }
            id = *(const uint32_t *) slot;
            status = lxa_vector_at(&blocks->entries, id, &slot);
            if (status != LXA_OK) {
                break;
            }
            block = slot;
            if (closing(block->opener) == token.kind) {
                block->close = i;
                lxa_vector_erase(&stack, depth - 1, 1);
                release_depth(blocks->budget, 1);
                continue;
            }
        }
        if (closing(token.kind) != LXA_CSS_TOKEN_COUNT) {
            uint32_t parent = LXA_CSS_BLOCK_NONE;
            uint32_t id = (uint32_t) lxa_vector_length(&blocks->entries);
            lxa_css_block_t block = {i, count, parent, token.kind};
            void *slot;
            if (depth != 0) {
                lxa_vector_at(&stack, depth - 1, &slot);
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
            status = lxa_vector_push(&blocks->entries, &block);
            if (status != LXA_OK) {
                release_depth(blocks->budget, 1);
                break;
            }
            status = lxa_vector_push(&stack, &id);
            if (status != LXA_OK) {
                lxa_vector_erase(&blocks->entries, id, 1);
                release_depth(blocks->budget, 1);
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
            lxa_vector_at(&stack, lxa_vector_length(&stack) - 1, &slot);
            id = *(const uint32_t *) slot;
            lxa_vector_at(&blocks->entries, id, &slot);
            ((lxa_css_block_t *) slot)->close = i;
            lxa_vector_erase(&stack, lxa_vector_length(&stack) - 1, 1);
            release_depth(blocks->budget, 1);
        }
    }
done:
    if (status != LXA_OK) {
        release_depth(blocks->budget, lxa_vector_length(&stack));
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
        status = lxa_vector_at(&blocks->entries, mid, &slot);
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
        status = lxa_vector_at(&blocks->entries, lo, &slot);
        if (status != LXA_OK) {
            return status;
        }
        const lxa_css_block_t *block = slot;
        if (block->open == from) {
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
        lxa_vector_destroy(&blocks->entries);
        blocks->budget = NULL;
        blocks->ready = false;
        blocks->end = 0;
    }
}
