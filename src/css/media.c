#include "css/blocks.h"

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
      size_t first, size_t after, lxa_css_media_t *out)
{
    enum { QUERY_START, QUERY_TYPE, QUERY_END } state = QUERY_START;
    bool negated = false, match = false;
    lxa_status_t status;
    *out = (lxa_css_media_t) {0};
    for (size_t i = first; i < after; i++) {
        lxa_css_token_t token;
        lxa_span_t text;
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_css_internal_status(
            lxa_css_tokens_get(tokens, i, &token, &text));
        if (status != LXA_OK) return status;
        if (token.kind == LXA_CSS_WHITESPACE) continue;
        if (token.kind != LXA_CSS_IDENT) {
            if (token.kind == LXA_CSS_LEFT_PAREN
                || token.kind == LXA_CSS_FUNCTION)
                out->syntax = LXA_CSS_SYNTAX_UNSUPPORTED;
            return LXA_OK;
        }
        if (state == QUERY_START
            && (word(text, "only", 4) || word(text, "not", 3))) {
            negated = word(text, "not", 3);
            state = QUERY_TYPE;
            continue;
        }
        if (state == QUERY_END) {
            if (word(text, "and", 3) || word(text, "or", 2))
                out->syntax = LXA_CSS_SYNTAX_UNSUPPORTED;
            return LXA_OK;
        }
        /* These grammar keywords are not unknown media types that NOT
         * may turn into a match. */
        if (word(text, "not", 3) || word(text, "only", 4)
            || word(text, "and", 3) || word(text, "or", 2))
            return LXA_OK;
        /* Unknown media types are false, then NOT flips. */
        match = word(text, "screen", 6) || word(text, "all", 3);
        state = QUERY_END;
    }
    if (state == QUERY_END) {
        out->syntax = LXA_CSS_SYNTAX_SUPPORTED;
        out->applies = negated ? !match : match;
    }
    return LXA_OK;
}

static void
merge(lxa_css_media_t *out, lxa_css_media_t group)
{
    out->applies |= group.applies;
    if (group.syntax == LXA_CSS_SYNTAX_UNSUPPORTED)
        out->syntax = LXA_CSS_SYNTAX_UNSUPPORTED;
    else if (group.syntax == LXA_CSS_SYNTAX_INVALID
             && out->syntax != LXA_CSS_SYNTAX_UNSUPPORTED)
        out->syntax = LXA_CSS_SYNTAX_INVALID;
}

lxa_status_t
lxa_css_media_screen(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
                     size_t first, size_t after, lxa_css_media_t *out)
{
    size_t start = first, at = first;
    lxa_css_media_t result = {.syntax = LXA_CSS_SYNTAX_SUPPORTED}, group;
    lxa_status_t status;
    if (tokens == NULL || blocks == NULL || out == NULL || !blocks->ready
        || first > after || after > blocks->end
        || lxa_css_tokens_length(tokens) < blocks->end)
        return LXA_ERROR_ARGUMENT;
    *out = (lxa_css_media_t) {0};
    status = lxa_css_blocks_check(blocks);
    if (status != LXA_OK) return status;
    blocks->limit_reason = LXA_BUDGET_COUNT;
    if (first == after) {
        *out = (lxa_css_media_t) {LXA_CSS_SYNTAX_SUPPORTED, true};
        return LXA_OK;
    }
    while (at < after) {
        lxa_css_token_t token;
        lxa_span_t text;
        lxa_css_component_range_t component;
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_css_internal_status(
            lxa_css_tokens_get(tokens, at, &token, &text));
        if (status != LXA_OK) return status;
        if (token.kind == LXA_CSS_COMMA) {
            status = query(tokens, blocks, start, at, &group);
            if (status != LXA_OK) return status;
            merge(&result, group);
            start = ++at;
            continue;
        }
        status = lxa_css_internal_status(
            lxa_css_blocks_next(blocks, at, &component));
        if (status != LXA_OK) return status;
        if (component.after > after) return LXA_ERROR_ARGUMENT;
        at = component.after;
    }
    status = query(tokens, blocks, start, after, &group);
    if (status != LXA_OK) return status;
    merge(&result, group);
    *out = result;
    return LXA_OK;
}
