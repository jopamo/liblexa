/* Copyright (C) 2026 Paul Moses */
#include "css/tokens.h"
#include <math.h>
#include <string.h>

#include "liblexa/base.h"
#include "css/scan.h"

enum { CSS_MAX_UNITS = 1024 * 1024 };

typedef struct {
    const lxa_css_unit_t *units;
    size_t count;
    size_t pos;
    size_t end;
    lxa_buffer_t text;
    lxa_css_tokens_t *tokens;
    lxa_budget_t *budget;
    lxa_budget_kind_t *limit_reason;
    bool name_text;
    lxa_status_t work_status;
} scanner_t;

/* All scanner lookahead, including failed lookahead at EOF, is charged before
 * inspection. Latch exhaustion so inner loops cannot resume or publish a
 * partially scanned token after a limit failure. */
static uint32_t
peek(scanner_t *s, size_t ahead)
{
    if (s->work_status != LXA_OK) {
        return 0xffffffffu;
    }
    s->work_status = lxa_budget_charge(s->budget, LXA_BUDGET_WORK, 1,
                                       s->limit_reason);
    if (s->work_status != LXA_OK) {
        return 0xffffffffu;
    }
    return ahead < s->count - s->pos ? s->units[s->pos + ahead].value : 0xffffffffu;
}

static int
space(uint32_t c)
{
    return c == ' ' || c == '\n' || c == '\t';
}

static int
name_start(uint32_t c)
{
    return c == '_' || (c >= 'a' && c <= 'z')
           || (c >= 'A' && c <= 'Z') || (c >= 0x80 && c <= 0x10ffff);
}

static int
name(uint32_t c)
{
    return name_start(c) || (c >= '0' && c <= '9') || c == '-';
}

static int
digit(uint32_t c)
{
    return c >= '0' && c <= '9';
}

static int
escape(scanner_t *s, size_t ahead)
{
    return peek(s, ahead) == '\\' && peek(s, ahead + 1) != 0xffffffffu
           && peek(s, ahead + 1) != '\n';
}

static int
hex(uint32_t c)
{
    if (c >= '0' && c <= '9') {
        return (int) (c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int) (c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return (int) (c - 'A' + 10);
    }
    return -1;
}

/* Consume a valid escape beginning at the backslash, without retaining
 * lookahead; CSS Syntax caps hexadecimal escapes at six digits. */
static uint32_t
read_escape(scanner_t *s)
{
    uint32_t c;
    unsigned digits = 0;
    s->pos++;
    c = peek(s, 0);
    if (s->work_status != LXA_OK) {
        return 0;
    }
    if (hex(c) < 0) {
        s->pos++;
        return c;
    }
    c = 0;
    while (digits < 6 && hex(peek(s, 0)) >= 0) {
        c = c * 16 + (uint32_t) hex(peek(s, 0));
        s->pos++;
        digits++;
    }
    if (space(peek(s, 0))) {
        s->pos++;
    }
    return c == 0 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)
           ? 0xfffd : c;
}

static int
ident(scanner_t *s, size_t ahead)
{
    uint32_t a = peek(s, ahead), b = peek(s, ahead + 1);
    return name_start(a) || escape(s, ahead)
           || (a == '-' && (name_start(b) || b == '-'
                            || escape(s, ahead + 1)));
}

static int
number_start(scanner_t *s)
{
    uint32_t a = peek(s, 0), b = peek(s, 1), c = peek(s, 2);
    if (a == '+' || a == '-') {
        return digit(b) || (b == '.' && digit(c));
    }
    return digit(a) || (a == '.' && digit(b));
}

static lxa_status_t
put(scanner_t *s, uint32_t c)
{
    uint8_t bytes[4];
    size_t n;
    if (s->work_status != LXA_OK) {
        return s->work_status;
    }
    if (c < 0x80) {
        bytes[0] = (uint8_t) c;
        n = 1;
    }
    else if (c < 0x800) {
        bytes[0] = (uint8_t) (0xc0 | c >> 6);
        bytes[1] = (uint8_t) (0x80 | (c & 0x3f));
        n = 2;
    }
    else if (c < 0x10000) {
        bytes[0] = (uint8_t) (0xe0 | c >> 12);
        bytes[1] = (uint8_t) (0x80 | ((c >> 6) & 0x3f));
        bytes[2] = (uint8_t) (0x80 | (c & 0x3f));
        n = 3;
    }
    else {
        bytes[0] = (uint8_t) (0xf0 | c >> 18);
        bytes[1] = (uint8_t) (0x80 | ((c >> 12) & 0x3f));
        bytes[2] = (uint8_t) (0x80 | ((c >> 6) & 0x3f));
        bytes[3] = (uint8_t) (0x80 | (c & 0x3f));
        n = 4;
    }
    {
        size_t length;
        lxa_status_t status = lxa_size_add(s->text.length, n, &length);
        if (status != LXA_OK) {
            return status;
        }
        status = lxa_budget_check_item(s->budget, LXA_BUDGET_TOKEN_BYTES,
                                       length, s->limit_reason);
        if (status == LXA_OK && s->name_text) {
            status = lxa_budget_check_item(s->budget, LXA_BUDGET_NAME_BYTES,
                                           length, s->limit_reason);
        }
        if (status == LXA_OK) {
            status = lxa_buffer_append(&s->text, (lxa_span_t) {bytes, n});
            if (status == LXA_ERROR_LIMIT) {
                *s->limit_reason = LXA_BUDGET_MEMORY;
            }
        }
        return status;
    }
}

static lxa_status_t
read_name(scanner_t *s)
{
    lxa_status_t status;
    s->name_text = true;
    while (name(peek(s, 0)) || escape(s, 0)) {
        uint32_t cp = escape(s, 0) ? read_escape(s) : s->units[s->pos++].value;
        status = put(s, cp);
        if (status != LXA_OK) {
            return status;
        }
    }
    return LXA_OK;
}

static int
nonprint(uint32_t c)
{
    return c <= 8 || c == 11 || (c >= 14 && c <= 31) || c == 127;
}

static void
bad_url(scanner_t *s)
{
    while (s->pos < s->count) {
        if (peek(s, 0) == ')') {
            s->pos++;
            break;
        }
        if (escape(s, 0)) {
            (void) read_escape(s);
        }
        else {
            s->pos++;
        }
    }
    lxa_buffer_clear(&s->text);
}

/* The opening parenthesis was consumed. Quoted URLs are function tokens
 * instead, so only unquoted URL content reaches this function. */
static lxa_status_t
read_url(scanner_t *s, lxa_css_token_t *token)
{
    lxa_status_t status;
    s->name_text = false;
    lxa_buffer_clear(&s->text);
    token->kind = LXA_CSS_URL;
    while (space(peek(s, 0))) {
        s->pos++;
    }
    while (s->pos < s->count) {
        uint32_t c = peek(s, 0);
        if (c == ')') {
            s->pos++;
            break;
        }
        if (space(c)) {
            while (space(peek(s, 0))) {
                s->pos++;
            }
            if (peek(s, 0) == ')') {
                s->pos++;
            }
            else if (s->pos < s->count) {
                bad_url(s);
                token->kind = LXA_CSS_BAD_URL;
            }
            break;
        }
        if (c == '"' || c == '\'' || c == '(' || nonprint(c)
            || (c == '\\' && !escape(s, 0))) {
            s->pos++;
            bad_url(s);
            token->kind = LXA_CSS_BAD_URL;
            break;
        }
        status = put(s, c == '\\' ? read_escape(s)
                           : s->units[s->pos++].value);
        if (status != LXA_OK) {
            return status;
        }
    }
    return LXA_OK;
}

static lxa_status_t
emit(scanner_t *s, lxa_css_token_t token, size_t start)
{
    size_t end = s->pos == s->count ? s->end
                 : s->units[s->pos].offset;
    /* Gaps between source units belong to the preceding token only when
     * included in its last unit; do not claim unrelated source bytes. */
    if (s->pos > start) {
        const lxa_css_unit_t *last = &s->units[s->pos - 1];
        end = last->offset + last->length;
    }
    token.offset = start == s->count ? s->end : s->units[start].offset;
    token.length = end - token.offset;
    {
        lxa_status_t limit = lxa_budget_check_item(s->budget,
            LXA_BUDGET_TOKEN_BYTES, token.length, s->limit_reason);
        if (limit != LXA_OK) {
            return limit;
        }
    }
    {
        lxa_status_t status = lxa_css_tokens_append(s->tokens, token,
            (lxa_span_t) {s->text.data, s->text.length});
        if (status == LXA_ERROR_LIMIT) {
            *s->limit_reason = s->tokens->limit_reason;
        }
        return status;
    }
}

/* Retain 17 significant decimal digits and their base-10 order, rather
 * than overflowing the accumulator before a compensating negative exponent.
 * Work remains linear in the bounded input; exponent arithmetic is capped.
 */
static double
read_number(scanner_t *s, unsigned *flags)
{
    double mantissa = 0, value;
    size_t significant = 0;
    int64_t scale = 0, exponent = 0;
    int negative = 0, negative_exponent = 0;
    uint32_t c = peek(s, 0);
    *flags = LXA_CSS_INTEGER;
    if (c == '+' || c == '-') {
        negative = c == '-';
        *flags |= LXA_CSS_SIGNED;
        s->pos++;
    }
    while (digit(peek(s, 0))) {
        uint32_t d = peek(s, 0) - '0';
        s->pos++;
        if (mantissa == 0 && d == 0) {
            continue;
        }
        if (significant < 17) {
            mantissa = mantissa * 10 + d;
            significant++;
        }
        else {
            scale++;
        }
    }
    if (peek(s, 0) == '.' && digit(peek(s, 1))) {
        *flags &= ~LXA_CSS_INTEGER;
        s->pos++;
        while (digit(peek(s, 0))) {
            uint32_t d = peek(s, 0) - '0';
            s->pos++;
            if (mantissa == 0 && d == 0) {
                scale--;
            }
            else if (significant < 17) {
                mantissa = mantissa * 10 + d;
                significant++;
                scale--;
            }
        }
    }
    if ((peek(s, 0) == 'e' || peek(s, 0) == 'E')
        && (digit(peek(s, 1))
            || ((peek(s, 1) == '+' || peek(s, 1) == '-')
                && digit(peek(s, 2))))) {
        *flags &= ~LXA_CSS_INTEGER;
        s->pos++;
        if (peek(s, 0) == '+' || peek(s, 0) == '-') {
            negative_exponent = peek(s, 0) == '-';
            s->pos++;
        }
        while (digit(peek(s, 0))) {
            if (exponent < CSS_MAX_UNITS) {
                exponent = exponent * 10 + (peek(s, 0) - '0');
                if (exponent > CSS_MAX_UNITS) {
                    exponent = CSS_MAX_UNITS;
                }
            }
            s->pos++;
        }
    }
    scale += negative_exponent ? -exponent : exponent;
    if (mantissa == 0 || scale < -350) {
        value = 0;
    }
    else if (scale > 308) {
        value = INFINITY;
    }
    else if (scale < -308) {
        value = mantissa * pow(10.0, -308.0);
        value *= pow(10.0, (double) (scale + 308));
    }
    else {
        value = mantissa * pow(10.0, (double) scale);
    }
    return negative ? -value : value;
}

static lxa_css_scan_result_t
scan(scanner_t *s, size_t capacity, size_t *produced)
{
    while (s->pos < s->count) {
        if (*produced == capacity) {
            return LXA_CSS_SCAN_OUTPUT_FULL;
        }
        lxa_css_token_t t = {0};
        size_t start = s->pos;
        uint32_t c = peek(s, 0);
        lxa_status_t status = LXA_OK;
        lxa_buffer_clear(&s->text);
        s->name_text = false;
        if (c == '/' && peek(s, 1) == '*') {
            s->pos += 2;
            while (s->pos < s->count
                   && !(peek(s, 0) == '*' && peek(s, 1) == '/')) {
                s->pos++;
            }
            if (s->work_status != LXA_OK) {
                if (s->work_status == LXA_ERROR_LIMIT) {
                    *s->limit_reason = LXA_BUDGET_WORK;
                }
                return lxa_css_scan_status(s->work_status);
            }
            if (s->pos < s->count) {
                s->pos += 2;
            }
            continue;
        }
        if (space(c)) {
            do { s->pos++; } while (space(peek(s, 0)));
            t.kind = LXA_CSS_WHITESPACE;
        }
        else if (c == '<' && peek(s, 1) == '!' && peek(s, 2) == '-'
                 && peek(s, 3) == '-') {
            s->pos += 4;
            t.kind = LXA_CSS_CDO;
        }
        else if (c == '-' && peek(s, 1) == '-' && peek(s, 2) == '>') {
            s->pos += 3;
            t.kind = LXA_CSS_CDC;
        }
        else if (number_start(s)) {
            t.kind = LXA_CSS_NUMBER;
            t.number = read_number(s, &t.flags);
            if (ident(s, 0)) {
                t.kind = LXA_CSS_DIMENSION;
                status = read_name(s);
            }
            else if (peek(s, 0) == '%') {
                t.kind = LXA_CSS_PERCENTAGE;
                t.flags &= ~LXA_CSS_INTEGER;
                s->pos++;
            }
        }
        else if (c == '#' && (name(peek(s, 1)) || escape(s, 1))) {
            s->pos++;
            t.kind = LXA_CSS_HASH;
            if (ident(s, 0)) {
                t.flags = LXA_CSS_ID_HASH;
            }
            status = read_name(s);
        }
        else if (c == '@' && ident(s, 1)) {
            s->pos++;
            t.kind = LXA_CSS_AT_KEYWORD;
            status = read_name(s);
        }
        else if (ident(s, 0)) {
            t.kind = LXA_CSS_IDENT;
            status = read_name(s);
            if (status == LXA_OK && peek(s, 0) == '(') {
                int url = s->text.length == 3
                    && (s->text.data[0] | 32) == 'u'
                    && (s->text.data[1] | 32) == 'r'
                    && (s->text.data[2] | 32) == 'l';
                s->pos++;
                t.kind = LXA_CSS_FUNCTION;
                if (url) {
                    while (space(peek(s, 0)) && space(peek(s, 1))) {
                        s->pos++;
                    }
                    if (peek(s, 0) != '"' && peek(s, 0) != '\''
                        && !(space(peek(s, 0))
                             && (peek(s, 1) == '"' || peek(s, 1) == '\''))) {
                        status = read_url(s, &t);
                    }
                }
            }
        }
        else if (c == '"' || c == '\'') {
            uint32_t quote = c;
            t.kind = LXA_CSS_STRING;
            s->pos++;
            while (status == LXA_OK && s->pos < s->count
                   && peek(s, 0) != quote && peek(s, 0) != '\n') {
                if (peek(s, 0) == '\\') {
                    if (escape(s, 0)) {
                        status = put(s, read_escape(s));
                    }
                    else {
                        s->pos++;
                        if (peek(s, 0) == '\n') {
                            s->pos++;
                        }
                    }
                }
                else {
                    status = put(s, s->units[s->pos++].value);
                }
            }
            if (status == LXA_OK) {
                if (peek(s, 0) == '\n') {
                    t.kind = LXA_CSS_BAD_STRING;
                }
                else if (s->pos < s->count) {
                    s->pos++;
                }
            }
        }
        else {
            s->pos++;
            switch (c) {
            case ':': t.kind = LXA_CSS_COLON; break;
            case ';': t.kind = LXA_CSS_SEMICOLON; break;
            case ',': t.kind = LXA_CSS_COMMA; break;
            case '(': t.kind = LXA_CSS_LEFT_PAREN; break;
            case ')': t.kind = LXA_CSS_RIGHT_PAREN; break;
            case '[': t.kind = LXA_CSS_LEFT_SQUARE; break;
            case ']': t.kind = LXA_CSS_RIGHT_SQUARE; break;
            case '{': t.kind = LXA_CSS_LEFT_BRACE; break;
            case '}': t.kind = LXA_CSS_RIGHT_BRACE; break;
            default:
                t.kind = LXA_CSS_DELIM;
                t.codepoint = c;
                break;
            }
        }
        if (s->work_status != LXA_OK) {
            return lxa_css_scan_status(s->work_status);
        }
        if (status == LXA_OK) {
            status = emit(s, t, start);
        }
        if (status != LXA_OK) {
            return lxa_css_scan_status(status);
        }
        (*produced)++;
    }
    if (*produced == capacity) {
        return LXA_CSS_SCAN_OUTPUT_FULL;
    }
    {
        lxa_css_token_t eof = {0};
        eof.kind = LXA_CSS_EOF;
        lxa_buffer_clear(&s->text);
        lxa_status_t status = emit(s, eof, s->count);
        if (status == LXA_OK) {
            (*produced)++;
            return LXA_CSS_SCAN_OK;
        }
        return lxa_css_scan_status(status);
    }
}

lxa_css_scan_result_t
lxa_css_scan_cursor_init_budget(lxa_css_scan_cursor_t *cursor,
                                const lxa_css_unit_t *input, size_t count,
                                size_t source_length, lxa_budget_t *budget)
{
    size_t prev = 0;
    if (cursor == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    *cursor = (lxa_css_scan_cursor_t) {0};
    cursor->closed = true;
    if (count > CSS_MAX_UNITS) {
        return LXA_CSS_SCAN_OVERFLOW;
    }
    if (input == NULL && count != 0) {
        return LXA_CSS_SCAN_INVALID;
    }
    lxa_budget_init(&cursor->local_budget, NULL);
    cursor->budget = budget;
    cursor->limit_reason = LXA_BUDGET_COUNT;
    for (size_t i = 0; i < count; i++) {
        lxa_status_t work = lxa_budget_charge(budget == NULL
            ? &cursor->local_budget : budget, LXA_BUDGET_WORK, 1,
            &cursor->limit_reason);
        if (work != LXA_OK) {
            return lxa_css_scan_status(work);
        }
        size_t end;
        uint32_t c = input[i].value;
        if (input[i].length == 0 || input[i].offset < prev
            || lxa_size_add(input[i].offset, input[i].length, &end) != LXA_OK
            || end > source_length || c > 0x10ffff
            || c == 0 || c == '\r' || c == '\f'
            || (c >= 0xd800 && c <= 0xdfff)) {
            return LXA_CSS_SCAN_INVALID;
        }
        prev = end;
    }
    cursor->count = count;
    cursor->source_length = source_length;
    cursor->closed = false;
    return LXA_CSS_SCAN_OK;
}

lxa_css_scan_result_t
lxa_css_scan_cursor_init(lxa_css_scan_cursor_t *cursor,
                         const lxa_css_unit_t *input, size_t count,
                         size_t source_length)
{
    return lxa_css_scan_cursor_init_budget(cursor, input, count,
                                            source_length, NULL);
}

lxa_css_scan_result_t
lxa_css_scan_cursor_batch(lxa_css_scan_cursor_t *cursor,
                          const lxa_css_unit_t *input, lxa_css_tokens_t *tokens,
                          size_t capacity, size_t *produced)
{
    scanner_t s = {0};
    lxa_css_scan_result_t result;
    if (produced == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    *produced = 0;
    if (cursor == NULL || cursor->closed || tokens == NULL
        || (input == NULL && cursor->count != 0)) {
        return LXA_CSS_SCAN_INVALID;
    }
    if (capacity == 0) {
        return LXA_CSS_SCAN_OUTPUT_FULL;
    }
    s.units = input;
    s.count = cursor->count;
    s.pos = cursor->position;
    s.end = cursor->source_length;
    s.tokens = tokens;
    s.budget = cursor->budget == NULL ? &cursor->local_budget : cursor->budget;
    s.limit_reason = &cursor->limit_reason;
    if (lxa_buffer_init(&s.text, &tokens->text.allocator) != LXA_OK) {
        cursor->closed = true;
        return lxa_css_scan_status(LXA_ERROR_INTERNAL);
    }
    if (lxa_buffer_bind_budget(&s.text, s.budget) != LXA_OK) {
        lxa_buffer_destroy(&s.text);
        cursor->closed = true;
        return lxa_css_scan_status(LXA_ERROR_INTERNAL);
    }
    result = scan(&s, capacity, produced);
    lxa_buffer_destroy(&s.text);
    cursor->position = s.pos;
    if (result != LXA_CSS_SCAN_OUTPUT_FULL) {
        cursor->closed = true;
    }
    return result;
}

lxa_css_scan_result_t
lxa_css_scan_units(const lxa_css_unit_t *input, size_t count,
                   size_t source_length, lxa_css_tokens_t *tokens)
{
    lxa_css_scan_cursor_t cursor;
    lxa_css_scan_result_t result;
    size_t produced;
    if (tokens == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    lxa_css_tokens_clear(tokens);
    result = lxa_css_scan_cursor_init(&cursor, input, count, source_length);
    if (result == LXA_CSS_SCAN_OK) {
        result = lxa_css_scan_cursor_batch(&cursor, input, tokens, count + 1,
                                            &produced);
    }
    if (result != LXA_CSS_SCAN_OK) {
        lxa_css_tokens_clear(tokens);
    }
    return result;
}
