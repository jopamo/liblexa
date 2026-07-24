/* Copyright (C) 2026 Paul Moses */
#include <string.h>

#include "liblexa/base.h"
#include "liblexa/css.h"

lxa_status_t
lxa_css_tokens_init(lxa_css_tokens_t *tokens, const lxa_allocator_t *allocator)
{
    lxa_status_t status;
    if (tokens == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    tokens->charged_tokens = 0;
    tokens->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_buffer_init(&tokens->text, allocator);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_vector_init(&tokens->entries, allocator, sizeof(lxa_css_token_t));
    if (status != LXA_OK) {
        lxa_buffer_destroy(&tokens->text);
    }
    return status;
}

lxa_status_t
lxa_css_tokens_bind_budget(lxa_css_tokens_t *tokens, lxa_budget_t *budget)
{
    if (tokens == NULL || budget == NULL || tokens->entries.element_size == 0
        || tokens->text.budget != NULL || tokens->entries.bytes.budget != NULL
        || tokens->text.capacity != 0 || tokens->entries.bytes.capacity != 0
        || tokens->text.length != 0 || tokens->entries.bytes.length != 0
        || tokens->text.data != NULL || tokens->entries.bytes.data != NULL
        || tokens->charged_tokens != 0) {
        return LXA_ERROR_ARGUMENT;
    }
    /* Both binds are infallible after validating the initialized buffers. */
    if (lxa_buffer_bind_budget(&tokens->text, budget) != LXA_OK) {
        return LXA_ERROR_ARGUMENT;
    }
    return lxa_vector_bind_budget(&tokens->entries, budget);
}

void
lxa_css_tokens_destroy(lxa_css_tokens_t *tokens)
{
    if (tokens != NULL) {
        lxa_vector_destroy(&tokens->entries);
        lxa_buffer_destroy(&tokens->text);
        tokens->charged_tokens = 0;
        tokens->limit_reason = LXA_BUDGET_COUNT;
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
    token.text_offset = original;
    token.text_length = text.length;
    status = lxa_vector_push(&tokens->entries, &token);
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
    status = lxa_vector_at(&tokens->entries, index, &entry);
    if (status == LXA_OK) {
        *token = *(const lxa_css_token_t *) entry;
        if (token->text_length != 0) {
            text->data = tokens->text.data + token->text_offset;
            text->length = token->text_length;
        }
    }
    return status;
}
