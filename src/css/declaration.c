/* Copyright (C) 2026 Paul Moses */
#include "liblexa/css.h"

static lxa_status_t
read_token(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks, size_t at,
           lxa_css_token_t *token, lxa_span_t *text)
{
    lxa_status_t status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK,
                                             1, &blocks->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    return lxa_css_tokens_get(tokens, at, token, text);
}

static bool
important_name(lxa_span_t text)
{
    static const char word[] = "important";
    if (text.length != sizeof(word) - 1) {
        return false;
    }
    for (size_t i = 0; i < sizeof(word) - 1; i++) {
        uint8_t c = text.data[i];
        if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != word[i]) {
            return false;
        }
    }
    return true;
}

lxa_status_t
lxa_css_declaration_parse(lxa_css_tokens_t *tokens,
                          lxa_css_blocks_t *blocks, size_t first, size_t after,
                          lxa_css_declaration_t *out)
{
    lxa_css_token_t token;
    lxa_span_t text;
    lxa_css_component_range_t range;
    lxa_status_t status;
    size_t at = first, value_first, value_after, last = after, previous = after;
    size_t significant = 0, brace = after;
    bool custom;
    if (out == NULL || tokens == NULL || blocks == NULL || !blocks->ready
        || first >= after || after > blocks->end
        || lxa_css_tokens_length(tokens) < blocks->end) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = (lxa_css_declaration_t) {0};
    blocks->limit_reason = LXA_BUDGET_COUNT;
    while (at < after) {
        status = read_token(tokens, blocks, at, &token, &text);
        if (status != LXA_OK) return status;
        if (token.kind != LXA_CSS_WHITESPACE) break;
        at++;
    }
    if (at == after || token.kind != LXA_CSS_IDENT) return LXA_OK;
    size_t name = at++;
    custom = text.length >= 2 && text.data[0] == '-' && text.data[1] == '-';
    while (at < after) {
        status = read_token(tokens, blocks, at, &token, &text);
        if (status != LXA_OK) return status;
        if (token.kind != LXA_CSS_WHITESPACE) break;
        at++;
    }
    if (at == after || token.kind != LXA_CSS_COLON) return LXA_OK;
    at++;
    while (at < after) {
        status = read_token(tokens, blocks, at, &token, &text);
        if (status != LXA_OK) return status;
        if (token.kind != LXA_CSS_WHITESPACE) break;
        at++;
    }
    value_first = at;
    value_after = at;
    for (; at < after; at = range.after) {
        status = read_token(tokens, blocks, at, &token, &text);
        if (status != LXA_OK) return status;
        if (token.kind == LXA_CSS_SEMICOLON || token.kind == LXA_CSS_EOF) {
            return LXA_OK;
        }
        status = lxa_css_blocks_next(blocks, at, &range);
        if (status != LXA_OK) return status;
        if (range.after > after) return LXA_ERROR_ARGUMENT;
        if (token.kind == LXA_CSS_WHITESPACE) continue;
        if (token.kind == LXA_CSS_LEFT_BRACE) brace = at;
        previous = last;
        last = at;
        significant++;
        value_after = range.after;
    }
    bool important = false;
    if (significant >= 2) {
        lxa_css_token_t tail, bang;
        lxa_span_t spelling, ignored;
        status = read_token(tokens, blocks, last, &tail, &spelling);
        if (status != LXA_OK) return status;
        status = read_token(tokens, blocks, previous, &bang, &ignored);
        if (status != LXA_OK) return status;
        if (tail.kind == LXA_CSS_IDENT && important_name(spelling)
            && bang.kind == LXA_CSS_DELIM && bang.codepoint == '!') {
            important = true;
            significant -= 2;
            value_after = previous;
            while (value_after > value_first) {
                status = read_token(tokens, blocks, value_after - 1,
                                    &token, &text);
                if (status != LXA_OK) return status;
                if (token.kind != LXA_CSS_WHITESPACE) break;
                value_after--;
            }
        }
    }
    if (!custom && brace != after && significant != 1) return LXA_OK;
    *out = (lxa_css_declaration_t) {name, value_first, value_after,
                                    important, true};
    return LXA_OK;
}
