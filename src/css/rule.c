/* Copyright (C) 2026 Paul Moses */
#include "liblexa/css.h"

lxa_status_t
lxa_css_rule_parse(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
                   size_t first, size_t limit, lxa_css_rule_t *out)
{
    lxa_css_token_t token;
    lxa_span_t text;
    lxa_css_component_range_t range;
    lxa_css_rule_t rule = {0};
    lxa_status_t status;
    size_t at, significant = 0;
    bool custom = false, colon = false;
    if (out == NULL || tokens == NULL || blocks == NULL || !blocks->ready
        || first >= limit || limit > blocks->end
        || lxa_css_tokens_length(tokens) < blocks->end) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = (lxa_css_rule_t) {0};
    blocks->limit_reason = LXA_BUDGET_COUNT;
    for (at = first; at < limit; at = range.after) {
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_css_tokens_get(tokens, at, &token, &text);
        if (status != LXA_OK) return status;
        if (at == first) {
            rule.kind = token.kind == LXA_CSS_AT_KEYWORD
                        ? LXA_CSS_RULE_AT : LXA_CSS_RULE_QUALIFIED;
            rule.name = rule.kind == LXA_CSS_RULE_AT ? at : 0;
            rule.prelude_first = at + (rule.kind == LXA_CSS_RULE_AT);
        }
        if (token.kind == LXA_CSS_LEFT_BRACE) {
            status = lxa_css_blocks_next(blocks, at, &range);
            if (status != LXA_OK) return status;
            if (range.after > limit) return LXA_ERROR_ARGUMENT;
            rule.prelude_after = at;
            rule.block_open = at;
            rule.block_after = range.closed ? range.after - 1
                                            : blocks->end;
            rule.has_block = true;
            rule.after = range.after;
            rule.valid = rule.kind == LXA_CSS_RULE_AT || !(custom && colon);
            *out = rule;
            return LXA_OK;
        }
        if (rule.kind == LXA_CSS_RULE_AT && token.kind == LXA_CSS_SEMICOLON) {
            rule.prelude_after = at;
            rule.after = at + 1;
            rule.valid = true;
            *out = rule;
            return LXA_OK;
        }
        if (rule.kind == LXA_CSS_RULE_QUALIFIED
            && token.kind != LXA_CSS_WHITESPACE && significant < 2) {
            if (significant == 0) {
                custom = token.kind == LXA_CSS_IDENT && text.length >= 2
                         && text.data[0] == '-' && text.data[1] == '-';
            }
            else {
                colon = token.kind == LXA_CSS_COLON;
            }
            significant++;
        }
        status = lxa_css_blocks_next(blocks, at, &range);
        if (status != LXA_OK) return status;
        if (range.after > limit) return LXA_ERROR_ARGUMENT;
    }
    rule.prelude_after = limit;
    rule.after = limit;
    if (rule.kind == LXA_CSS_RULE_AT) {
        rule.valid = true;
    }
    *out = rule;
    return LXA_OK;
}
