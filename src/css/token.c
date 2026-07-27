#include <string.h>

#include "liblexa/base.h"
#include "css/tokens.h"
#include "css/invariant.h"

static lxa_status_t
check_storage(lxa_css_tokens_t *tokens)
{
    lxa_status_t status = lxa_css_vector_check(&tokens->entries,
                                               sizeof(lxa_css_token_entry_t));
    if (status != LXA_OK) return status;
    return lxa_css_invariant(tokens->text.length <= tokens->text.capacity
        && (tokens->text.capacity == 0 || tokens->text.data != NULL));
}

lxa_status_t
lxa_css_tokens_create(const lxa_allocator_t *allocator, lxa_budget_t *budget,
                      lxa_css_tokens_t **out)
{
    lxa_css_tokens_t *tokens;
    lxa_buffer_t text;
    lxa_vector_t entries;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    *out = NULL;
    status = lxa_buffer_init(&text, allocator);
    if (status != LXA_OK) return status;
    status = lxa_vector_init(&entries, allocator, sizeof(lxa_css_token_entry_t));
    if (status != LXA_OK) return status;
    if (budget != NULL) {
        status = lxa_buffer_bind_budget(&text, budget);
        if (status == LXA_OK) status = lxa_vector_bind_budget(&entries, budget);
        if (status == LXA_OK)
            status = lxa_budget_charge(budget, LXA_BUDGET_MEMORY,
                                       sizeof(*tokens), NULL);
        if (status != LXA_OK) return status;
    }
    status = lxa_alloc(allocator, sizeof(*tokens), (void **) &tokens);
    if (status != LXA_OK) {
        if (budget != NULL)
            lxa_budget_release(budget, LXA_BUDGET_MEMORY, sizeof(*tokens));
        return status;
    }
    *tokens = (lxa_css_tokens_t) {
        .text = text, .entries = entries, .limit_reason = LXA_BUDGET_COUNT
    };
    *out = tokens;
    return LXA_OK;
}

lxa_budget_kind_t
lxa_css_tokens_limit_reason(const lxa_css_tokens_t *tokens)
{
    return tokens == NULL ? LXA_BUDGET_COUNT : tokens->limit_reason;
}

void
lxa_css_tokens_destroy(lxa_css_tokens_t *tokens)
{
    if (tokens != NULL) {
        lxa_allocator_t allocator = tokens->text.allocator;
        lxa_budget_t *budget = tokens->text.budget;
        lxa_vector_destroy(&tokens->entries);
        lxa_buffer_destroy(&tokens->text);
        if (budget != NULL)
            lxa_budget_release(budget, LXA_BUDGET_MEMORY, sizeof(*tokens));
        lxa_free(&allocator, tokens);
    }
}

void
lxa_css_tokens_clear(lxa_css_tokens_t *tokens)
{
    if (tokens != NULL) {
        lxa_vector_clear(&tokens->entries);
        lxa_buffer_clear(&tokens->text);
    }
}

size_t
lxa_css_tokens_length(const lxa_css_tokens_t *tokens)
{
    return tokens == NULL ? 0 : lxa_vector_length(&tokens->entries);
}

lxa_status_t
lxa_css_tokens_append(lxa_css_tokens_t *tokens, lxa_css_token_t token,
                      lxa_span_t text)
{
    size_t end, original, count;
    lxa_status_t status;
    if (tokens != NULL) {
        tokens->limit_reason = LXA_BUDGET_COUNT;
    }
    if (tokens == NULL || token.kind < LXA_CSS_IDENT
        || token.kind >= LXA_CSS_TOKEN_COUNT
        || (text.data == NULL && text.length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    status = check_storage(tokens);
    if (status != LXA_OK) return status;
    status = lxa_size_add(token.offset, token.length, &end);
    if (status != LXA_OK) {
        return status;
    }
    (void) end;
    status = lxa_size_add(lxa_vector_length(&tokens->entries), 1, &count);
    if (status != LXA_OK) {
        return status;
    }
    lxa_budget_t *budget = tokens->entries.bytes.budget;
    if (budget != NULL) {
        status = lxa_budget_check_item(budget, LXA_BUDGET_TOKEN_BYTES,
                                       token.length, &tokens->limit_reason);
        if (status == LXA_OK) {
            status = lxa_budget_check_item(budget, LXA_BUDGET_TOKEN_BYTES,
                                           text.length, &tokens->limit_reason);
        }
        if (status == LXA_OK && (token.kind == LXA_CSS_IDENT
            || token.kind == LXA_CSS_FUNCTION
            || token.kind == LXA_CSS_AT_KEYWORD
            || token.kind == LXA_CSS_HASH
            || token.kind == LXA_CSS_DIMENSION)) {
            status = lxa_budget_check_item(budget, LXA_BUDGET_NAME_BYTES,
                                           text.length, &tokens->limit_reason);
        }
        if (status != LXA_OK) {
            return status;
        }
    }
    if (budget != NULL && count > tokens->charged_tokens) {
        status = lxa_budget_check(budget, LXA_BUDGET_TOKENS,
                                  count - tokens->charged_tokens,
                                  &tokens->limit_reason);
        if (status != LXA_OK) {
            return status;
        }
    }
    status = lxa_vector_reserve(&tokens->entries, count);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT) {
            tokens->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    original = tokens->text.length;
    status = lxa_buffer_append(&tokens->text, text);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT) {
            tokens->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    if (budget != NULL && count > tokens->charged_tokens) {
        /* Reserve and copy may call the allocator. Check again before the
         * nonallocating push, then charge before publishing the result. */
        status = lxa_budget_check(budget, LXA_BUDGET_TOKENS,
                                  count - tokens->charged_tokens,
                                  &tokens->limit_reason);
        if (status != LXA_OK) {
            lxa_buffer_truncate(&tokens->text, original);
            return status;
        }
    }
    lxa_css_token_entry_t entry = {token, original, text.length};
    status = lxa_vector_push(&tokens->entries, &entry);
    if (status != LXA_OK) {
        lxa_buffer_truncate(&tokens->text, original);
        return status;
    }
    if (budget != NULL && count > tokens->charged_tokens) {
        status = lxa_budget_charge(budget, LXA_BUDGET_TOKENS,
                                   count - tokens->charged_tokens,
                                   &tokens->limit_reason);
        if (status != LXA_OK) {
            lxa_vector_erase(&tokens->entries, count - 1, 1);
            lxa_buffer_truncate(&tokens->text, original);
            return status;
        }
        tokens->charged_tokens = count;
    }
    return LXA_OK;
}

lxa_status_t
lxa_css_tokens_get(lxa_css_tokens_t *tokens, size_t index,
                   lxa_css_token_t *token, lxa_span_t *text)
{
    void *entry;
    lxa_status_t status;
    if (token == NULL || text == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(token, 0, sizeof(*token));
    text->data = NULL;
    text->length = 0;
    if (tokens == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    status = check_storage(tokens);
    if (status != LXA_OK) return status;
    status = lxa_vector_at(&tokens->entries, index, &entry);
    if (status == LXA_OK) {
        const lxa_css_token_entry_t *stored = entry;
        status = lxa_css_invariant(stored->token.kind >= LXA_CSS_IDENT
            && stored->token.kind < LXA_CSS_TOKEN_COUNT
            && stored->token.length <= SIZE_MAX - stored->token.offset
            && stored->text_offset <= tokens->text.length
            && stored->text_length <= tokens->text.length - stored->text_offset);
        if (status != LXA_OK) return status;
        *token = stored->token;
        if (stored->text_length != 0) {
            text->data = tokens->text.data + stored->text_offset;
            text->length = stored->text_length;
        }
    }
    return status;
}
