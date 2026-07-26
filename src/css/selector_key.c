#include "css/blocks.h"
#include "liblexa/base.h"

static lxa_status_t
unsupported(lxa_css_syntax_t *syntax)
{
    *syntax = LXA_CSS_SYNTAX_UNSUPPORTED;
    return LXA_OK;
}

lxa_status_t
lxa_css_selector_key(lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
                     size_t first, size_t after, size_t max_bytes,
                     lxa_buffer_t *output, lxa_css_syntax_t *syntax)
{
    lxa_span_t tag = {0}, klass = {0}, id = {0}, text;
    bool universal = false, dot = false, started = false;
    size_t size = 0;
    lxa_status_t status = LXA_OK;
    if (tokens == NULL || blocks == NULL || !blocks->ready || output == NULL
        || syntax == NULL || output->budget != blocks->budget
        || first > after || after > blocks->end
        || lxa_css_tokens_length(tokens) < blocks->end) {
        return LXA_ERROR_ARGUMENT;
    }
    *syntax = LXA_CSS_SYNTAX_INVALID;
    lxa_buffer_clear(output);
    blocks->limit_reason = LXA_BUDGET_COUNT;
    for (size_t at = first; at < after; at++) {
        lxa_css_token_t token;
        status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK, 1,
                                   &blocks->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_css_tokens_get(tokens, at, &token, &text);
        if (status != LXA_OK)
            return status == LXA_ERROR_ARGUMENT ? LXA_ERROR_INTERNAL : status;
        if (dot && token.kind != LXA_CSS_IDENT) return LXA_OK;
        if (token.kind == LXA_CSS_WHITESPACE) {
            /* Whitespace is allowed at the edges, never between parts. */
            size_t next = at + 1;
            while (next < after) {
                lxa_css_token_t look;
                lxa_span_t ignored;
                status = lxa_budget_charge(blocks->budget, LXA_BUDGET_WORK,
                                           1, &blocks->limit_reason);
                if (status != LXA_OK) return status;
                status = lxa_css_tokens_get(tokens, next, &look, &ignored);
                if (status != LXA_OK)
                    return status == LXA_ERROR_ARGUMENT
                           ? LXA_ERROR_INTERNAL : status;
                if (look.kind != LXA_CSS_WHITESPACE) break;
                next++;
            }
            if (next < after && started) return unsupported(syntax);
            at = next - 1;
            continue;
        }
        if (token.kind == LXA_CSS_IDENT) {
            if (dot) {
                if (klass.length != 0) return unsupported(syntax);
                klass = text;
                dot = false;
            }
            else if (!started && tag.length == 0 && !universal) tag = text;
            else return LXA_OK;
        }
        else if (token.kind == LXA_CSS_DELIM && token.codepoint == '.') {
            dot = true;
        }
        else if (token.kind == LXA_CSS_DELIM && token.codepoint == '*') {
            if (started || universal) return LXA_OK;
            universal = true;
        }
        else if (token.kind == LXA_CSS_HASH && (token.flags & LXA_CSS_ID_HASH)) {
            if (id.length != 0) return unsupported(syntax);
            id = text;
        }
        else if (token.kind == LXA_CSS_COLON
                 || token.kind == LXA_CSS_LEFT_SQUARE
                 || token.kind == LXA_CSS_COMMA
                 || (token.kind == LXA_CSS_DELIM
                     && (token.codepoint == '>' || token.codepoint == '+'
                         || token.codepoint == '~' || token.codepoint == '|'
                         || token.codepoint == '&')))
            return unsupported(syntax);
        else return LXA_OK;
        started = true;
    }
    if (!started || dot) {
        return LXA_OK;
    }
    if (universal && (klass.length || id.length)) return unsupported(syntax);
    size = tag.length;
    if (universal) size = 1;
    if (lxa_size_add(size, klass.length, &size) != LXA_OK
        || lxa_size_add(size, id.length, &size) != LXA_OK
        || lxa_size_add(size, (klass.length != 0) + (id.length != 0), &size)
           != LXA_OK) return LXA_ERROR_OVERFLOW;
    if (size > max_bytes) {
        blocks->limit_reason = LXA_BUDGET_NAME_BYTES;
        return LXA_ERROR_LIMIT;
    }
    if (lxa_size_add(size, 1, &size) != LXA_OK) return LXA_ERROR_OVERFLOW;
    status = lxa_buffer_reserve(output, size);
    if (status != LXA_OK) goto fail;
    if (universal) status = lxa_buffer_append(output, (lxa_span_t) {(const uint8_t *) "*", 1});
    else if (tag.length) status = lxa_buffer_append(output, tag);
    if (status == LXA_OK && klass.length) {
        status = lxa_buffer_append(output, (lxa_span_t) {(const uint8_t *) ".", 1});
        if (status == LXA_OK) status = lxa_buffer_append(output, klass);
    }
    if (status == LXA_OK && id.length) {
        status = lxa_buffer_append(output, (lxa_span_t) {(const uint8_t *) "#", 1});
        if (status == LXA_OK) status = lxa_buffer_append(output, id);
    }
    if (status == LXA_OK) status = lxa_buffer_terminate(output);
fail:
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT) blocks->limit_reason = LXA_BUDGET_MEMORY;
        lxa_buffer_clear(output);
    }
    else *syntax = LXA_CSS_SYNTAX_SUPPORTED;
    return status;
}
