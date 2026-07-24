/* Copyright (C) 2026 Paul Moses */
#include "liblexa/css.h"

static bool
word(lxa_span_t text, const char *ascii, size_t size)
{
    if (text.length != size) return false;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = text.data[i];
        if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != ascii[i])
            return false;
    }
    return true;
}

/* Invalid/unsupported query terms do not apply. A later comma-separated
 * query is still evaluated independently. */
static lxa_status_t
query(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
      size_t first, size_t after, bool *out)
{
    size_t terms = 0;
    bool negated = false, recognized = false, match = false;
    lxa_status_t status;
    *out = false;
    for (size_t i = first; i < after; i++) {
        lxa_css_token_t token;
        lxa_span_t text;
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_css_tokens_get(tokens, i, &token, &text);
        if (status != LXA_OK) return status;
        if (token.kind == LXA_CSS_WHITESPACE) continue;
        if (token.kind != LXA_CSS_IDENT) return LXA_OK;
        if (terms == 0 && (word(text, "only", 4) || word(text, "not", 3))) {
            negated = word(text, "not", 3);
            terms++;
            continue;
        }
        if (terms > 1 || (terms == 1 && recognized)) return LXA_OK;
        recognized = true; /* Unknown media types are false, then NOT flips. */
        match = word(text, "screen", 6) || word(text, "all", 3);
        terms++;
    }
    *out = recognized && (negated ? !match : match);
    return LXA_OK;
}

lxa_status_t
lxa_css_media_screen(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
                     size_t first, size_t after, bool *applies)
{
    size_t start = first, at = first;
    bool match = false, group;
    lxa_status_t status;
    if (tokens == NULL || blocks == NULL || applies == NULL || !blocks->ready
        || first > after || after > blocks->end
        || lxa_css_tokens_length(tokens) < blocks->end)
        return LXA_ERROR_ARGUMENT;
    *applies = false;
    blocks->limit_reason = LXA_BUDGET_COUNT;
    if (first == after) {
        *applies = true;
        return LXA_OK;
    }
    while (at < after) {
        lxa_css_token_t token;
        lxa_span_t text;
        lxa_css_component_range_t component;
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_css_tokens_get(tokens, at, &token, &text);
        if (status != LXA_OK) return status;
        if (token.kind == LXA_CSS_COMMA) {
            status = query(tokens, blocks, start, at, &group);
            if (status != LXA_OK) return status;
            match |= group;
            start = ++at;
            continue;
        }
        status = lxa_css_blocks_next(blocks, at, &component);
        if (status != LXA_OK) return status;
        if (component.after > after) return LXA_ERROR_ARGUMENT;
        at = component.after;
    }
    status = query(tokens, blocks, start, after, &group);
    if (status != LXA_OK) return status;
    match |= group;
    *applies = match;
    return LXA_OK;
}
