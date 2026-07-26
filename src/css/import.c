#include "css/blocks.h"

static bool
ascii_equal(lxa_span_t text, const char *ascii, size_t size)
{
    if (text.length != size) return false;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = text.data[i];
        if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != ascii[i])
            return false;
    }
    return true;
}

static lxa_status_t
read(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
     size_t at, lxa_css_token_t *token, lxa_span_t *text)
{
    lxa_status_t status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK,
                                            1, &blocks->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_css_tokens_get(tokens, at, token, text);
    return status == LXA_ERROR_ARGUMENT ? LXA_ERROR_INTERNAL : status;
}

lxa_status_t
lxa_css_import_parse(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
                     const lxa_css_rule_t *rule, lxa_css_import_t *out)
{
    lxa_css_token_t token;
    lxa_span_t text, uri;
    lxa_css_component_range_t component;
    lxa_status_t status;
    size_t at;
    if (tokens == NULL || blocks == NULL || rule == NULL || out == NULL
        || !blocks->ready || lxa_css_tokens_length(tokens) < blocks->end
        || rule->kind != LXA_CSS_RULE_AT || rule->has_block
        || !rule->valid || rule->name >= rule->prelude_first
        || rule->prelude_first > rule->prelude_after
        || rule->prelude_after > blocks->end)
        return LXA_ERROR_ARGUMENT;
    *out = (lxa_css_import_t) {0};
    blocks->limit_reason = LXA_BUDGET_COUNT;
    status = read(tokens, blocks, rule->name, &token, &text);
    if (status != LXA_OK) return status;
    if (token.kind != LXA_CSS_AT_KEYWORD) return LXA_ERROR_ARGUMENT;
    if (!ascii_equal(text, "import", 6)) {
        out->syntax = LXA_CSS_SYNTAX_UNSUPPORTED;
        return LXA_OK;
    }
    for (at = rule->prelude_first; at < rule->prelude_after; at++) {
        status = read(tokens, blocks, at, &token, &text);
        if (status != LXA_OK) return status;
        if (token.kind != LXA_CSS_WHITESPACE) break;
    }
    if (at == rule->prelude_after) return LXA_OK;
    if (token.kind == LXA_CSS_STRING || token.kind == LXA_CSS_URL) {
        uri = text;
        at++;
    }
    else if (token.kind == LXA_CSS_FUNCTION
             && ascii_equal(text, "url", 3)) {
        status = lxa_css_blocks_next(blocks, at, &component);
        if (status != LXA_OK) return status;
        if (!component.closed || component.after > rule->prelude_after)
            return LXA_OK;
        at++;
        while (at < component.after - 1) {
            status = read(tokens, blocks, at, &token, &text);
            if (status != LXA_OK) return status;
            if (token.kind != LXA_CSS_WHITESPACE) break;
            at++;
        }
        if (at == component.after - 1 || token.kind != LXA_CSS_STRING)
            return LXA_OK;
        uri = text;
        for (at++; at < component.after - 1; at++) {
            status = read(tokens, blocks, at, &token, &text);
            if (status != LXA_OK) return status;
            if (token.kind != LXA_CSS_WHITESPACE) {
                out->syntax = LXA_CSS_SYNTAX_UNSUPPORTED;
                return LXA_OK;
            }
        }
        at = component.after;
    }
    else {
        if (token.kind == LXA_CSS_FUNCTION)
            out->syntax = LXA_CSS_SYNTAX_UNSUPPORTED;
        return LXA_OK;
    }
    if (uri.length == 0) {
        out->syntax = LXA_CSS_SYNTAX_UNSUPPORTED;
        return LXA_OK;
    }
    while (at < rule->prelude_after) {
        status = read(tokens, blocks, at, &token, &text);
        if (status != LXA_OK) return status;
        if (token.kind != LXA_CSS_WHITESPACE) break;
        at++;
    }
    *out = (lxa_css_import_t) {
        uri, at, rule->prelude_after, LXA_CSS_SYNTAX_SUPPORTED
    };
    return LXA_OK;
}
