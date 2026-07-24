/* Copyright (C) 2026 Paul Moses */
#include "liblexa/css.h"
#include "liblexa/base.h"

static lxa_status_t
append(lxa_css_blocks_t *blocks, lxa_buffer_t *output,
       lxa_span_t text, size_t max_bytes)
{
    size_t size;
    lxa_status_t status = lxa_size_add(output->length, text.length, &size);
    if (status != LXA_OK) return status;
    if (size > max_bytes) {
        blocks->limit_reason = LXA_BUDGET_TOKEN_BYTES;
        return LXA_ERROR_LIMIT;
    }
    status = lxa_buffer_append(output, text);
    if (status == LXA_ERROR_LIMIT) blocks->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static bool
digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

/* CSS number syntax is a contiguous ASCII prefix even when its dimension
 * unit contains CSS escapes. Preserve its spelling and append the cooked unit. */
static size_t
number_end(lxa_span_t raw)
{
    size_t i = 0, digits = 0;
    if (i < raw.length && (raw.data[i] == '+' || raw.data[i] == '-')) i++;
    while (i < raw.length && digit(raw.data[i])) { i++; digits++; }
    if (i + 1 < raw.length && raw.data[i] == '.'
        && digit(raw.data[i + 1])) {
        i++;
        while (i < raw.length && digit(raw.data[i])) { i++; digits++; }
    }
    if (digits == 0) return 0;
    if (i < raw.length && (raw.data[i] == 'e' || raw.data[i] == 'E')) {
        size_t at = i + 1;
        if (at < raw.length && (raw.data[at] == '+' || raw.data[at] == '-'))
            at++;
        if (at < raw.length && digit(raw.data[at])) {
            i = at + 1;
            while (i < raw.length && digit(raw.data[i])) i++;
        }
    }
    return i;
}

lxa_status_t
lxa_css_value_text(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
                   size_t first, size_t after, lxa_span_t source,
                   size_t max_bytes, lxa_buffer_t *output)
{
    size_t prior_end = 0;
    lxa_status_t status = LXA_OK;
    if (tokens == NULL || blocks == NULL || !blocks->ready || output == NULL
        || output->budget != blocks->budget || first > after
        || after > blocks->end || lxa_css_tokens_length(tokens) < blocks->end
        || (source.data == NULL && source.length != 0))
        return LXA_ERROR_ARGUMENT;
    blocks->limit_reason = LXA_BUDGET_COUNT;
    lxa_buffer_clear(output);
    for (size_t at = first; at < after; at++) {
        lxa_css_token_t token;
        lxa_span_t text, spelling;
        size_t end;
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) goto fail;
        status = lxa_css_tokens_get(tokens, at, &token, &text);
        if (status != LXA_OK) goto fail;
        status = lxa_size_add(token.offset, token.length, &end);
        if (status != LXA_OK) goto fail;
        if (end > source.length || (at != first && token.offset < prior_end)) {
            status = LXA_ERROR_ARGUMENT;
            goto fail;
        }
        /* A discarded comment separates two CSS tokens. Preserve the token
         * boundary without retaining the comment's untrusted source bytes. */
        if (at != first && token.offset != prior_end) {
            status = append(blocks, output, (lxa_span_t) {(const uint8_t *) " ", 1},
                            max_bytes);
            if (status != LXA_OK) goto fail;
        }
        spelling = (lxa_span_t) {source.data == NULL ? NULL : source.data + token.offset,
                                       token.length};
        if (token.kind == LXA_CSS_IDENT) spelling = text;
        else if (token.kind == LXA_CSS_HASH) {
            status = append(blocks, output, (lxa_span_t) {(const uint8_t *) "#", 1},
                            max_bytes);
            if (status != LXA_OK) goto fail;
            spelling = text;
        }
        else if (token.kind == LXA_CSS_FUNCTION) {
            if (text.length == 3
                && (text.data[0] == 'u' || text.data[0] == 'U')
                && (text.data[1] == 'r' || text.data[1] == 'R')
                && (text.data[2] == 'l' || text.data[2] == 'L'))
                text = (lxa_span_t) {(const uint8_t *) "url", 3};
            status = append(blocks, output, text, max_bytes);
            if (status != LXA_OK) goto fail;
            spelling = (lxa_span_t) {(const uint8_t *) "(", 1};
        }
        else if (token.kind == LXA_CSS_DIMENSION) {
            size_t prefix;
            status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK,
                                       spelling.length, &blocks->limit_reason);
            if (status != LXA_OK) goto fail;
            prefix = number_end(spelling);
            if (prefix == 0 || prefix == spelling.length) {
                status = LXA_ERROR_ARGUMENT;
                goto fail;
            }
            status = append(blocks, output,
                (lxa_span_t) {spelling.data, prefix}, max_bytes);
            if (status != LXA_OK) goto fail;
            spelling = text;
        }
        else if (token.kind == LXA_CSS_URL) {
            /* The consumer's URL property parser expects lower-case url().
             * A simple decoded URI is safe to emit unquoted. */
            bool simple = text.length != 0;
            status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK,
                                       text.length, &blocks->limit_reason);
            if (status != LXA_OK) goto fail;
            for (size_t i = 0; i < text.length; i++) {
                uint8_t c = text.data[i];
                if (c <= 0x20 || c == ')' || c == '(' || c == '\''
                    || c == '"' || c == '\\') simple = false;
            }
            if (simple) {
                status = append(blocks, output,
                    (lxa_span_t) {(const uint8_t *) "url(", 4}, max_bytes);
                if (status == LXA_OK)
                    status = append(blocks, output, text, max_bytes);
                if (status != LXA_OK) goto fail;
                spelling = (lxa_span_t) {(const uint8_t *) ")", 1};
            }
        }
        else if (token.kind == LXA_CSS_WHITESPACE)
            spelling = (lxa_span_t) {(const uint8_t *) " ", 1};
        status = append(blocks, output, spelling, max_bytes);
        if (status != LXA_OK) goto fail;
        prior_end = end;
    }
    status = lxa_buffer_terminate(output);
    if (status == LXA_OK) return LXA_OK;
fail:
    if (status == LXA_ERROR_LIMIT && blocks->limit_reason == LXA_BUDGET_COUNT)
        blocks->limit_reason = LXA_BUDGET_MEMORY;
    lxa_buffer_clear(output);
    return status;
}
