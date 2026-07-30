#include <string.h>
#include <assert.h>

#include "html/tokenizer.h"
#include "html/tokenizer_classes.h"
#include "liblexa/base.h"
#include "liblexa/encoding.h"
#include "html-charref-overrides.h"
#include "html-entities.h"

typedef enum {
#define LXA_HTML_STATE(name, id, action, ...) STATE_##name = id,
#include "html/tokenizer_states.def"
#undef LXA_HTML_STATE
} tokenizer_state_t;

typedef enum {
    EOF_TEXT, EOF_TAG_PREFIX, EOF_DECLARATION_DASH, EOF_COMMENT_OPEN,
    EOF_DECLARATION_PREFIX, EOF_CDATA, EOF_DOCTYPE, EOF_DOCTYPE_KEYWORD,
    EOF_BOGUS_DOCTYPE, EOF_PI, EOF_TAG, EOF_REFERENCE_MARK, EOF_REFERENCE_NAME,
    EOF_REFERENCE_LONG_NAME, EOF_REFERENCE_NO_DIGITS, EOF_REFERENCE_NUMBER,
    EOF_BOGUS_COMMENT, EOF_COMMENT, EOF_RAW_CANDIDATE, EOF_SCRIPT_ESCAPED,
    EOF_SCRIPT_ESCAPE_START, EOF_INVALID
} tokenizer_eof_action_t;

static tokenizer_eof_action_t
eof_action(tokenizer_state_t state)
{
    switch (state) {
#define LXA_HTML_STATE(name, id, action, ...) case STATE_##name: return EOF_##action;
#include "html/tokenizer_states.def"
#undef LXA_HTML_STATE
    }
    assert(!"invalid tokenizer state");
    return EOF_INVALID;
}

typedef struct {
    size_t name_start, name_length, value_start, value_length;
} attribute_slot_t;

/* A discarded duplicate remains the current parsing candidate for its value. */
typedef enum {
    ATTRIBUTE_NONE, ATTRIBUTE_KEEP, ATTRIBUTE_DISCARD
} attribute_state_t;

struct lxa_html_tokenizer {
    lxa_allocator_t allocator;
    lxa_buffer_t text, name, attribute_bytes, appropriate_end_tag, raw_candidate;
    lxa_buffer_t public_id, system_id;
    lxa_vector_t attribute_slots, attribute_views;
    lxa_table_t *attribute_names; /* Large tags only; owns normalized key copies. */
    attribute_slot_t attribute;
    attribute_state_t attribute_state;
    size_t source_attribute_count; /* Includes duplicates for the source limit. */
    lxa_budget_t owned_budget, *budget;
    lxa_decoder_t *decoder;
    lxa_encoder_t *encoder;
    lxa_html_token_callback_t on_token;
    lxa_html_error_callback_t on_error;
    void *context;
    tokenizer_state_t state;
    tokenizer_state_t raw_mode, raw_return_mode;
    tokenizer_state_t reference_return_state;
    lxa_html_result_t stopped;
    lxa_status_t callback_status;
    lxa_limit_reason_t limit_reason;
    size_t offset, utf8_start;
    size_t control_offset;
    uint32_t reference;
    uint8_t reference_hex_marker;
    char reference_name[34];
    unsigned reference_length;
    unsigned declaration_length;
    char declaration[7];
    char doctype_keyword[6];
    unsigned doctype_keyword_length;
    size_t doctype_keyword_offset;
    const liblexa_html_entity_entry *reference_match;
    unsigned reference_match_length;
    unsigned script_word_length;
    bool script_word_matches;
    bool feed_started, prepaid_input;
    bool allow_cdata;
    bool end_tag, closed, in_callback, in_error, utf8_pending, previous_cr;
    bool reference_digits;
    bool control_reported;
    bool doctype_quirks, has_public_id, has_system_id;
};

static lxa_html_result_t
budget_result(lxa_html_tokenizer_t *tokenizer, lxa_status_t status,
              lxa_limit_reason_t reason)
{
    if (status == LXA_ERROR_LIMIT) {
        tokenizer->limit_reason = reason;
        return LXA_HTML_LIMIT;
    }
    return status == LXA_OK ? LXA_HTML_OK
         : status == LXA_ERROR_OVERFLOW ? LXA_HTML_OVERFLOW
         : LXA_HTML_BAD_ARGUMENT;
}

static lxa_html_result_t
buffer_append(lxa_html_tokenizer_t *tokenizer, lxa_buffer_t *buffer,
              lxa_span_t bytes, lxa_budget_kind_t kind)
{
    lxa_status_t status;
    size_t length;
    lxa_limit_reason_t reason = LXA_BUDGET_COUNT;
    status = lxa_size_add(buffer->length, bytes.length, &length);
    if (status != LXA_OK) {
        return LXA_HTML_OVERFLOW;
    }
    status = lxa_budget_check_item(tokenizer->budget, kind, length, &reason);
    if (status == LXA_OK && buffer == &tokenizer->name) {
        status = lxa_budget_check_item(tokenizer->budget,
                                       LXA_BUDGET_TOKEN_BYTES, length, &reason);
    }
    else if (status == LXA_OK && buffer == &tokenizer->text
             && (tokenizer->state == STATE_PI_DATA
                 || tokenizer->state == STATE_PI_QUESTIONABLE)) {
        size_t total;
        status = lxa_size_add(tokenizer->name.length, length, &total);
        if (status == LXA_OK) {
            status = lxa_budget_check_item(tokenizer->budget,
                                           LXA_BUDGET_TOKEN_BYTES, total,
                                           &reason);
        }
    }
    if (status != LXA_OK) {
        return budget_result(tokenizer, status, reason);
    }
    status = lxa_buffer_append(buffer, bytes);
    if (status == LXA_ERROR_NO_MEMORY) {
        return LXA_HTML_NO_MEMORY;
    }
    if (status == LXA_ERROR_LIMIT) {
        return budget_result(tokenizer, status, LXA_BUDGET_MEMORY);
    }
    return budget_result(tokenizer, status, LXA_BUDGET_COUNT);
}

static lxa_html_result_t
accept_input(lxa_html_tokenizer_t *tokenizer, size_t length)
{
    lxa_limit_reason_t reason = LXA_BUDGET_COUNT;
    lxa_status_t status = lxa_budget_accept_input(tokenizer->budget, length,
                                                 &reason);
    return budget_result(tokenizer, status, reason);
}

static lxa_html_result_t
charge_work(lxa_html_tokenizer_t *tokenizer, size_t steps)
{
    lxa_limit_reason_t reason = LXA_BUDGET_COUNT;
    lxa_status_t status = lxa_budget_charge(tokenizer->budget, LXA_BUDGET_WORK,
                                            steps, &reason);
    return budget_result(tokenizer, status, reason);
}

static lxa_html_result_t
account_run(lxa_html_tokenizer_t *tokenizer, size_t bytes)
{
    uint64_t credit = tokenizer->budget->work_credit;
    uint64_t used = tokenizer->budget->used[LXA_BUDGET_WORK];
    uint64_t input_left = tokenizer->budget->limits.max_input_bytes;
    lxa_html_result_t result;
    if (tokenizer->prepaid_input) {
        return charge_work(tokenizer, bytes);
    }
    if (input_left >= tokenizer->budget->used[LXA_BUDGET_INPUT]) {
        input_left -= tokenizer->budget->used[LXA_BUDGET_INPUT];
    }
    else {
        input_left = 0;
    }
    if (credit > tokenizer->budget->limits.max_work) {
        credit = tokenizer->budget->limits.max_work;
    }
    if (used <= credit && bytes <= credit - used && bytes <= input_left
        && (tokenizer->budget->limits.work_per_input_byte == 0
            || bytes <= (UINT64_MAX - tokenizer->budget->work_credit)
                        / tokenizer->budget->limits.work_per_input_byte)) {
        result = accept_input(tokenizer, bytes);
        return result == LXA_HTML_OK ? charge_work(tokenizer, bytes) : result;
    }
    while (bytes-- != 0) {
        result = accept_input(tokenizer, 1);
        if (result != LXA_HTML_OK) {
            return result;
        }
        result = charge_work(tokenizer, 1);
        if (result != LXA_HTML_OK) {
            return result;
        }
    }
    return LXA_HTML_OK;
}

static lxa_html_result_t
emit(lxa_html_tokenizer_t *tokenizer, lxa_html_token_t *token)
{
    lxa_status_t status;
    tokenizer->in_callback = true;
    status = tokenizer->on_token(tokenizer->context, tokenizer, token);
    tokenizer->in_callback = false;
    if (status != LXA_OK) {
        tokenizer->callback_status = status;
        return LXA_HTML_CALLBACK_FAILURE;
    }
    return LXA_HTML_OK;
}

static lxa_html_result_t
emit_error(lxa_html_tokenizer_t *tokenizer, const char *code, size_t offset)
{
    lxa_status_t status;
    if (tokenizer->control_reported
        && tokenizer->control_offset == offset
        && strcmp(code, "control-character-in-input-stream") == 0) {
        return LXA_HTML_OK;
    }
    if (tokenizer->on_error == NULL) {
        return LXA_HTML_OK;
    }
    tokenizer->in_callback = tokenizer->in_error = true;
    status = tokenizer->on_error(tokenizer->context, code, offset);
    tokenizer->in_error = tokenizer->in_callback = false;
    if (status != LXA_OK) {
        tokenizer->callback_status = status;
        return LXA_HTML_CALLBACK_FAILURE;
    }
    return LXA_HTML_OK;
}

static lxa_html_result_t
append_attribute(lxa_html_tokenizer_t *tokenizer, lxa_span_t bytes, bool name);

static lxa_html_result_t
append_doctype_id(lxa_html_tokenizer_t *tokenizer, lxa_span_t bytes,
                  bool public_id)
{
    lxa_buffer_t *target = public_id ? &tokenizer->public_id
                                     : &tokenizer->system_id;
    size_t total;
    lxa_limit_reason_t reason = LXA_BUDGET_COUNT;
    lxa_status_t status = lxa_size_add(tokenizer->name.length,
                                       tokenizer->public_id.length, &total);
    if (status == LXA_OK) {
        status = lxa_size_add(total, tokenizer->system_id.length, &total);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(total, bytes.length, &total);
    }
    if (status != LXA_OK) {
        return LXA_HTML_OVERFLOW;
    }
    status = lxa_budget_check_item(tokenizer->budget, LXA_BUDGET_TOKEN_BYTES,
                                   total, &reason);
    return status == LXA_OK
           ? buffer_append(tokenizer, target, bytes,
                           LXA_BUDGET_TOKEN_BYTES)
           : budget_result(tokenizer, status, reason);
}

static lxa_html_result_t
append_decoded_scalar(lxa_html_tokenizer_t *tokenizer, uint32_t cp,
                      bool in_name)
{
    uint8_t bytes[4];
    lxa_mut_span_t out = {bytes, sizeof(bytes)};
    lxa_encoding_progress_t progress;
    lxa_encoding_result_t result = lxa_encoder_feed(tokenizer->encoder, &cp, 1,
                                                     out, &progress);
    if (result != LXA_ENCODING_OK || progress.consumed != 1) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    {
        lxa_span_t decoded = {bytes, progress.produced};
        if (tokenizer->state == STATE_ATTRIBUTE_NAME
            || tokenizer->state == STATE_ATTRIBUTE_VALUE_DOUBLE
            || tokenizer->state == STATE_ATTRIBUTE_VALUE_SINGLE
            || tokenizer->state == STATE_ATTRIBUTE_VALUE_UNQUOTED)
        {
            return append_attribute(tokenizer, decoded,
                                    tokenizer->state == STATE_ATTRIBUTE_NAME);
        }
        if (tokenizer->state == STATE_SYSTEM_ID_DOUBLE
            || tokenizer->state == STATE_SYSTEM_ID_SINGLE) {
            return append_doctype_id(tokenizer, decoded, false);
        }
        if (tokenizer->state == STATE_PUBLIC_ID_DOUBLE
            || tokenizer->state == STATE_PUBLIC_ID_SINGLE) {
            return append_doctype_id(tokenizer, decoded, true);
        }
        return buffer_append(tokenizer,
                             in_name ? &tokenizer->name : &tokenizer->text,
                             decoded, in_name ? LXA_BUDGET_NAME_BYTES
                                              : LXA_BUDGET_TOKEN_BYTES);
    }
}

static lxa_html_result_t
append_scalar(lxa_html_tokenizer_t *tokenizer, uint32_t cp)
{
    return append_decoded_scalar(tokenizer, cp, false);
}

static bool
reference_in_attribute(const lxa_html_tokenizer_t *tokenizer)
{
    return tokenizer->reference_return_state == STATE_ATTRIBUTE_VALUE_DOUBLE
        || tokenizer->reference_return_state == STATE_ATTRIBUTE_VALUE_SINGLE
        || tokenizer->reference_return_state == STATE_ATTRIBUTE_VALUE_UNQUOTED;
}

static lxa_html_result_t
append_reference_bytes(lxa_html_tokenizer_t *tokenizer, lxa_span_t bytes)
{
    return reference_in_attribute(tokenizer)
           ? append_attribute(tokenizer, bytes, false)
           : buffer_append(tokenizer, &tokenizer->text, bytes,
                           LXA_BUDGET_TOKEN_BYTES);
}

static lxa_html_result_t
append_reference_scalar(lxa_html_tokenizer_t *tokenizer, uint32_t cp)
{
    uint8_t bytes[4];
    lxa_mut_span_t out = {bytes, sizeof(bytes)};
    lxa_encoding_progress_t progress;
    lxa_encoding_result_t status;
    if (!reference_in_attribute(tokenizer)) {
        return append_scalar(tokenizer, cp);
    }
    status = lxa_encoder_feed(tokenizer->encoder, &cp, 1, out, &progress);
    if (status != LXA_ENCODING_OK || progress.consumed != 1) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    {
        lxa_span_t decoded = {bytes, progress.produced};
        return append_reference_bytes(tokenizer, decoded);
    }
}

static bool
supported_scalar(uint32_t cp)
{
    return cp != 0 && cp <= 0x10ffff
           && !(cp >= 0xd800 && cp <= 0xdfff);
}

static const char *
input_scalar_error(uint32_t cp)
{
    if (cp >= 0x80 && cp <= 0x9f) {
        return "control-character-in-input-stream";
    }
    if ((cp >= 0xfdd0 && cp <= 0xfdef)
        || (cp & 0xfffe) == 0xfffe) {
        return "noncharacter-in-input-stream";
    }
    return NULL;
}

static lxa_html_result_t
find_entity(lxa_html_tokenizer_t *tokenizer, const char *name,
            const liblexa_html_entity_entry **entry)
{
    size_t lo = 0, hi = LIBLEXA_HTML_ENTITY_COUNT;
    *entry = NULL;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int order;
        lxa_html_result_t result = charge_work(tokenizer, 1);
        if (result != LXA_HTML_OK) {
            return result;
        }
        order = strcmp(name, liblexa_html_entities[mid].name);
        if (order < 0) {
            hi = mid;
        }
        else if (order > 0) {
            lo = mid + 1;
        }
        else {
            *entry = &liblexa_html_entities[mid];
            break;
        }
    }
    return LXA_HTML_OK;
}

static lxa_html_result_t
resolve_name(lxa_html_tokenizer_t *tokenizer, bool semicolon,
             size_t delimiter_offset, uint8_t delimiter)
{
    const liblexa_html_entity_entry *match = NULL;
    lxa_html_result_t result;
    unsigned matched_length = 0, j;
    bool ambiguous_legacy = false;
    if (semicolon) {
        tokenizer->reference_name[tokenizer->reference_length] = ';';
        tokenizer->reference_name[tokenizer->reference_length + 1] = '\0';
        result = find_entity(tokenizer, tokenizer->reference_name, &match);
        if (result != LXA_HTML_OK) {
            return result;
        }
        if (match != NULL) {
            matched_length = tokenizer->reference_length + 1;
        }
    }
    if (match == NULL) {
        match = tokenizer->reference_match;
        matched_length = tokenizer->reference_match_length;
        if (match != NULL && reference_in_attribute(tokenizer)) {
            size_t end = tokenizer->reference_length + (semicolon ? 1 : 0);
            uint8_t next = matched_length < end
                           ? (uint8_t) tokenizer->reference_name[matched_length]
                           : delimiter;
            /* A legacy semicolonless name followed by an attribute-name
             * character remains literal, including when it spans feeds. */
            if (lxa_html_byte_alnum(next) || next == '=') {
                match = NULL;
                matched_length = 0;
                ambiguous_legacy = true;
            }
        }
        if (match != NULL) {
            /* The error belongs to the matched name, before any literal
             * suffix reconsumed after a semicolonless legacy reference. */
            result = emit_error(tokenizer,
                                "missing-semicolon-after-character-reference",
                                delimiter_offset
                                - (tokenizer->reference_length - matched_length));
            if (result != LXA_HTML_OK) {
                return result;
            }
        }
        else if (semicolon && !ambiguous_legacy) {
            result = emit_error(tokenizer, "unknown-named-character-reference",
                                delimiter_offset);
            if (result != LXA_HTML_OK) {
                return result;
            }
        }
    }
    if (match != NULL) {
        for (j = 0; j < match->codepoint_count; j++) {
            result = append_reference_scalar(tokenizer, match->codepoints[j]);
            if (result != LXA_HTML_OK) {
                return result;
            }
        }
    }
    {
        size_t end = tokenizer->reference_length + (semicolon ? 1 : 0);
        lxa_span_t rest = {
            (const uint8_t *) tokenizer->reference_name + matched_length,
            end - matched_length
        };
        result = append_reference_bytes(tokenizer, rest);
        if (result != LXA_HTML_OK) {
            return result;
        }
    }
    tokenizer->reference_length = tokenizer->reference_match_length = 0;
    tokenizer->reference_match = NULL;
    tokenizer->state = tokenizer->reference_return_state;
    return LXA_HTML_OK;
}

static lxa_html_result_t
resolve_numeric(lxa_html_tokenizer_t *tokenizer, bool semicolon,
                size_t delimiter_offset)
{
    uint32_t cp = tokenizer->reference;
    const char *error = NULL;
    lxa_html_result_t result;
    bool replace = false;
    if (!semicolon) {
        result = emit_error(tokenizer,
                            "missing-semicolon-after-character-reference",
                            delimiter_offset);
        if (result != LXA_HTML_OK) {
            return result;
        }
    }
    if (cp == 0) {
        error = "null-character-reference";
        replace = true;
    }
    else if (cp > 0x10ffff) {
        error = "character-reference-outside-unicode-range";
        replace = true;
    }
    else if (cp >= 0xd800 && cp <= 0xdfff) {
        error = "surrogate-character-reference";
        replace = true;
    }
    else if ((cp >= 0xfdd0 && cp <= 0xfdef)
             || (cp & 0xfffe) == 0xfffe) {
        error = "noncharacter-character-reference";
    }
    else if (cp == '\r'
             || (cp < 0x20 && cp != '\t'
                 && cp != '\n' && cp != '\f')
             || (cp >= 0x7f && cp <= 0x9f)) {
        size_t j;
        error = "control-character-reference";
        for (j = 0; j < LIBLEXA_HTML_CHARREF_OVERRIDE_COUNT; j++) {
            if (liblexa_html_charref_overrides[j].input == cp) {
                cp = liblexa_html_charref_overrides[j].output;
                break;
            }
        }
    }
    if (error != NULL) {
        /* End-state errors follow the semicolon, or point at the delimiter
         * that is about to be reconsumed. */
        result = emit_error(tokenizer, error,
                            delimiter_offset + (semicolon ? 1 : 0));
        if (result != LXA_HTML_OK) {
            return result;
        }
        if (replace) {
            cp = 0xfffd;
        }
    }
    result = append_reference_scalar(tokenizer, cp);
    if (result == LXA_HTML_OK) {
        tokenizer->reference = 0;
        tokenizer->reference_digits = false;
        tokenizer->state = tokenizer->reference_return_state;
    }
    return result;
}

static lxa_html_result_t
missing_numeric_digits(lxa_html_tokenizer_t *tokenizer, size_t offset)
{
    uint8_t prefix[] = {'&', '#', tokenizer->reference_hex_marker};
    bool hex = tokenizer->state == STATE_REFERENCE_HEX_START
               || tokenizer->state == STATE_REFERENCE_HEX;
    lxa_span_t literal = {prefix, hex ? 3 : 2};
    lxa_html_result_t result = emit_error(
        tokenizer, "absence-of-digits-in-numeric-character-reference",
        offset);
    if (result == LXA_HTML_OK) {
        result = append_reference_bytes(tokenizer, literal);
    }
    if (result == LXA_HTML_OK) {
        tokenizer->state = tokenizer->reference_return_state;
    }
    return result;
}

static lxa_html_result_t
decode_byte(lxa_html_tokenizer_t *tokenizer, uint8_t c, bool *consumed,
            bool in_name)
{
    lxa_span_t byte = {&c, 1};
    lxa_encoding_progress_t progress;
    lxa_encoding_result_t status;
    lxa_html_result_t result;
    uint32_t cp;
    size_t errors = lxa_decoder_error_count(tokenizer->decoder);
    status = lxa_decoder_feed(tokenizer->decoder, byte, &cp, 1, &progress);
    if (status != LXA_ENCODING_OK
        && !(status == LXA_ENCODING_OUTPUT_FULL && progress.produced == 1)) {
        return status == LXA_ENCODING_NO_MEMORY ? LXA_HTML_NO_MEMORY
             : status == LXA_ENCODING_OVERFLOW ? LXA_HTML_OVERFLOW
             : LXA_HTML_BAD_ARGUMENT;
    }
    *consumed = progress.consumed != 0;
    if (progress.produced == 0) {
        tokenizer->utf8_pending = true;
        return LXA_HTML_OK;
    }
    tokenizer->utf8_pending = false;
    if (lxa_decoder_error_count(tokenizer->decoder) != errors) {
        result = emit_error(tokenizer, "invalid-utf-8",
                            tokenizer->utf8_start);
        if (result != LXA_HTML_OK) {
            return result;
        }
    }
    if (!supported_scalar(cp)) {
        return LXA_HTML_UNSUPPORTED;
    }
    {
        const char *code = input_scalar_error(cp);
        if (code != NULL) {
            result = emit_error(tokenizer, code, tokenizer->utf8_start);
            if (result != LXA_HTML_OK) {
                return result;
            }
        }
    }
    return tokenizer->state == STATE_BOGUS_DOCTYPE
           ? LXA_HTML_OK : append_decoded_scalar(tokenizer, cp, in_name);
}

static lxa_html_result_t
finish_decoder(lxa_html_tokenizer_t *tokenizer, bool in_name)
{
    lxa_encoding_progress_t progress;
    lxa_encoding_result_t status;
    lxa_html_result_t result;
    uint32_t cp;
    size_t errors = lxa_decoder_error_count(tokenizer->decoder);
    status = lxa_decoder_finish(tokenizer->decoder, &cp, 1, &progress);
    if (status != LXA_ENCODING_OK) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    if (lxa_decoder_error_count(tokenizer->decoder) != errors) {
        result = emit_error(tokenizer, "invalid-utf-8",
                            tokenizer->utf8_start);
        if (result != LXA_HTML_OK) {
            return result;
        }
    }
    if (progress.produced != 0) {
        return tokenizer->state == STATE_BOGUS_DOCTYPE
               ? LXA_HTML_OK : append_decoded_scalar(tokenizer, cp, in_name);
    }
    return LXA_HTML_OK;
}

static lxa_html_result_t
emit_text(lxa_html_tokenizer_t *tokenizer)
{
    lxa_html_token_t token = {0};
    lxa_html_result_t result;
    if (tokenizer->text.length == 0) {
        return LXA_HTML_OK;
    }
    token.kind = LXA_HTML_TEXT;
    token.data.data = tokenizer->text.data;
    token.data.length = tokenizer->text.length;
    result = emit(tokenizer, &token);
    if (result == LXA_HTML_OK) {
        lxa_buffer_clear(&tokenizer->text);
    }
    return result;
}

static lxa_html_result_t
emit_comment(lxa_html_tokenizer_t *tokenizer)
{
    lxa_html_token_t token = {0};
    lxa_html_result_t result;
    token.kind = LXA_HTML_COMMENT;
    token.data.data = tokenizer->text.data;
    token.data.length = tokenizer->text.length;
    result = emit(tokenizer, &token);
    if (result == LXA_HTML_OK) {
        lxa_buffer_clear(&tokenizer->text);
        tokenizer->state = STATE_DATA;
    }
    return result;
}

static lxa_html_result_t
pi_to_comment(lxa_html_tokenizer_t *tokenizer)
{
    lxa_span_t question = {(const uint8_t *) "?", 1};
    lxa_span_t target = {tokenizer->name.data, tokenizer->name.length};
    lxa_html_result_t result = buffer_append(tokenizer, &tokenizer->text,
                                             question, LXA_BUDGET_TOKEN_BYTES);
    if (result == LXA_HTML_OK) {
        result = buffer_append(tokenizer, &tokenizer->text,
                               target, LXA_BUDGET_TOKEN_BYTES);
    }
    if (result == LXA_HTML_OK) {
        lxa_buffer_clear(&tokenizer->name);
        tokenizer->state = STATE_BOGUS_COMMENT;
    }
    return result;
}

static lxa_html_result_t
emit_pi(lxa_html_tokenizer_t *tokenizer)
{
    lxa_html_token_t token = {0};
    lxa_html_result_t result;
    token.kind = LXA_HTML_PROCESSING_INSTRUCTION;
    token.name.data = tokenizer->name.data;
    token.name.length = tokenizer->name.length;
    token.data.data = tokenizer->text.data;
    token.data.length = tokenizer->text.length;
    tokenizer->state = STATE_DATA;
    result = emit(tokenizer, &token);
    if (result == LXA_HTML_OK) {
        lxa_buffer_clear(&tokenizer->name);
        lxa_buffer_clear(&tokenizer->text);
    }
    return result;
}

static lxa_html_result_t
emit_doctype(lxa_html_tokenizer_t *tokenizer)
{
    lxa_html_token_t token = {0};
    lxa_html_result_t result;
    token.kind = LXA_HTML_DOCTYPE;
    token.name.data = tokenizer->name.data;
    token.name.length = tokenizer->name.length;
    token.has_public_id = tokenizer->has_public_id;
    if (token.has_public_id) {
        token.public_id.data = tokenizer->public_id.data;
        token.public_id.length = tokenizer->public_id.length;
    }
    token.has_system_id = tokenizer->has_system_id;
    if (token.has_system_id) {
        token.system_id.data = tokenizer->system_id.data;
        token.system_id.length = tokenizer->system_id.length;
    }
    token.force_quirks = tokenizer->doctype_quirks;
    tokenizer->state = STATE_DATA;
    result = emit(tokenizer, &token);
    if (result == LXA_HTML_OK) {
        lxa_buffer_clear(&tokenizer->name);
        lxa_buffer_clear(&tokenizer->public_id);
        lxa_buffer_clear(&tokenizer->system_id);
        tokenizer->has_public_id = false;
        tokenizer->has_system_id = false;
        tokenizer->doctype_quirks = false;
    }
    return result;
}

static lxa_html_result_t
attribute_table_result(lxa_html_tokenizer_t *tokenizer, lxa_status_t status)
{
    switch (status) {
    case LXA_OK: return LXA_HTML_OK;
    case LXA_ERROR_NO_MEMORY: return LXA_HTML_NO_MEMORY;
    case LXA_ERROR_ENTROPY: return LXA_HTML_ENTROPY;
    case LXA_ERROR_OVERFLOW: return LXA_HTML_OVERFLOW;
    case LXA_ERROR_LIMIT:
        return budget_result(tokenizer, status,
            tokenizer->attribute_names == NULL ? LXA_BUDGET_MEMORY
            : lxa_table_limit_reason(tokenizer->attribute_names));
    default:
        assert(false);
        return LXA_HTML_INTERNAL;
    }
}

static lxa_html_result_t
finish_attribute(lxa_html_tokenizer_t *tokenizer)
{
    lxa_status_t status;
    if (tokenizer->attribute_state == ATTRIBUTE_NONE) {
        return LXA_HTML_OK;
    }
    if (tokenizer->attribute_state == ATTRIBUTE_DISCARD) {
        status = lxa_buffer_truncate(&tokenizer->attribute_bytes,
                                      tokenizer->attribute.name_start);
    }
    else if (tokenizer->attribute_state == ATTRIBUTE_KEEP) {
        status = lxa_vector_push(&tokenizer->attribute_slots,
                                  &tokenizer->attribute);
    }
    else {
        assert(false);
        return LXA_HTML_INTERNAL;
    }
    if (status == LXA_ERROR_ARGUMENT || status == LXA_ERROR_INTERNAL) {
        assert(false);
        return LXA_HTML_INTERNAL;
    }
    if (status != LXA_OK) {
        return status == LXA_ERROR_NO_MEMORY ? LXA_HTML_NO_MEMORY
             : budget_result(tokenizer, status, LXA_BUDGET_MEMORY);
    }
    tokenizer->attribute_state = ATTRIBUTE_NONE;
    return LXA_HTML_OK;
}

static lxa_html_result_t
emit_tag(lxa_html_tokenizer_t *tokenizer, bool self_closing, size_t offset)
{
    lxa_html_token_t token = {0};
    lxa_html_result_t result;
    size_t i, count;
    result = finish_attribute(tokenizer);
    if (result != LXA_HTML_OK) {
        return result;
    }
    count = lxa_vector_length(&tokenizer->attribute_slots);
    if (tokenizer->attribute_names != NULL) {
        size_t work;
        lxa_status_t status = lxa_table_clear_cost(tokenizer->attribute_names,
                                                   &work);
        result = attribute_table_result(tokenizer, status);
        if (result == LXA_HTML_OK) {
            result = charge_work(tokenizer, work);
        }
        if (result != LXA_HTML_OK) {
            return result;
        }
    }
    token.kind = tokenizer->end_tag ? LXA_HTML_END_TAG : LXA_HTML_START_TAG;
    token.name.data = tokenizer->name.data;
    token.name.length = tokenizer->name.length;
    token.self_closing = self_closing;
    if (tokenizer->end_tag && count != 0) {
        result = emit_error(tokenizer, "end-tag-with-attributes", offset);
        if (result != LXA_HTML_OK) {
            return result;
        }
    }
    if (!tokenizer->end_tag) {
        lxa_vector_clear(&tokenizer->attribute_views);
        for (i = 0; i < count; i++) {
            attribute_slot_t *slot;
            lxa_html_attribute_t view;
            lxa_status_t status = lxa_vector_at(&tokenizer->attribute_slots,
                                                  i, (void **) &slot);
            if (status != LXA_OK) {
                return budget_result(tokenizer, status, LXA_BUDGET_COUNT);
            }
            view.name.data = tokenizer->attribute_bytes.data + slot->name_start;
            view.name.length = slot->name_length;
            view.value.data = tokenizer->attribute_bytes.data + slot->value_start;
            view.value.length = slot->value_length;
            status = lxa_vector_push(&tokenizer->attribute_views, &view);
            if (status != LXA_OK) {
                return status == LXA_ERROR_NO_MEMORY ? LXA_HTML_NO_MEMORY
                     : budget_result(tokenizer, status, LXA_BUDGET_MEMORY);
            }
        }
        token.attribute_count = count;
        if (count != 0) {
            token.attributes = (const lxa_html_attribute_t *)
                               tokenizer->attribute_views.bytes.data;
        }
    }
    tokenizer->state = STATE_DATA;
    if (tokenizer->end_tag && tokenizer->raw_mode != STATE_DATA) {
        lxa_buffer_clear(&tokenizer->appropriate_end_tag);
        lxa_buffer_clear(&tokenizer->raw_candidate);
        tokenizer->raw_mode = STATE_DATA;
    }
    result = emit(tokenizer, &token);
    if (result == LXA_HTML_OK) {
        lxa_table_destroy(tokenizer->attribute_names);
        tokenizer->attribute_names = NULL;
        lxa_buffer_clear(&tokenizer->name);
        lxa_buffer_clear(&tokenizer->attribute_bytes);
        lxa_vector_clear(&tokenizer->attribute_slots);
        lxa_vector_clear(&tokenizer->attribute_views);
        tokenizer->source_attribute_count = 0;
    }
    return result;
}

static lxa_html_result_t
begin_attribute(lxa_html_tokenizer_t *tokenizer)
{
    lxa_limit_reason_t reason = LXA_BUDGET_COUNT;
    lxa_status_t status;
    size_t count;
    lxa_html_result_t result = finish_attribute(tokenizer);
    if (result != LXA_HTML_OK) {
        return result;
    }
    status = lxa_size_add(tokenizer->source_attribute_count, 1, &count);
    if (status != LXA_OK) {
        return LXA_HTML_OVERFLOW;
    }
    status = lxa_budget_check_item(tokenizer->budget,
                                   LXA_BUDGET_ATTRS_PER_ELEMENT,
                                   count, &reason);
    if (status != LXA_OK) {
        return budget_result(tokenizer, status, reason);
    }
    tokenizer->source_attribute_count = count;
    tokenizer->attribute = (attribute_slot_t) {0};
    tokenizer->attribute.name_start = tokenizer->attribute.value_start
                                    = tokenizer->attribute_bytes.length;
    tokenizer->attribute_state = ATTRIBUTE_KEEP;
    tokenizer->state = STATE_ATTRIBUTE_NAME;
    return LXA_HTML_OK;
}

static attribute_slot_t *
current_attribute(lxa_html_tokenizer_t *tokenizer)
{
    bool valid = tokenizer->attribute_state == ATTRIBUTE_KEEP
                 || tokenizer->attribute_state == ATTRIBUTE_DISCARD;
    assert(valid);
    return valid ? &tokenizer->attribute : NULL;
}

static lxa_html_result_t
append_attribute(lxa_html_tokenizer_t *tokenizer, lxa_span_t bytes, bool name)
{
    attribute_slot_t *slot = current_attribute(tokenizer);
    lxa_limit_reason_t reason = LXA_BUDGET_COUNT;
    lxa_status_t status;
    size_t length, total, component_length;
    if (slot == NULL) {
        return LXA_HTML_INTERNAL;
    }
    status = lxa_size_add(name ? slot->name_length : slot->value_length,
                          bytes.length, &component_length);
    if (status != LXA_OK) {
        return LXA_HTML_OVERFLOW;
    }
    status = lxa_size_add(tokenizer->attribute_bytes.length, bytes.length,
                          &length);
    if (status != LXA_OK) {
        return LXA_HTML_OVERFLOW;
    }
    status = lxa_size_add(length, tokenizer->name.length, &total);
    if (status != LXA_OK) {
        return LXA_HTML_OVERFLOW;
    }
    status = lxa_budget_check_item(tokenizer->budget, LXA_BUDGET_TOKEN_BYTES,
                                   total, &reason);
    if (status == LXA_OK && name) {
        status = lxa_budget_check_item(tokenizer->budget, LXA_BUDGET_NAME_BYTES,
                                       component_length, &reason);
    }
    if (status != LXA_OK) {
        return budget_result(tokenizer, status, reason);
    }
    {
        lxa_html_result_t result = buffer_append(tokenizer,
                                      &tokenizer->attribute_bytes, bytes,
                                      LXA_BUDGET_TOKEN_BYTES);
        if (result == LXA_HTML_OK) {
            if (name) {
                slot->name_length = component_length;
            }
            else {
                slot->value_length = component_length;
            }
        }
        return result;
    }
}

static lxa_html_result_t
end_attribute_name(lxa_html_tokenizer_t *tokenizer, size_t offset)
{
    attribute_slot_t *slot = current_attribute(tokenizer);
    size_t i, count = lxa_vector_length(&tokenizer->attribute_slots);
    if (slot == NULL) {
        return LXA_HTML_INTERNAL;
    }
    slot->value_start = tokenizer->attribute_bytes.length;
    /* Bound flat lookup; copied index keys survive attribute-buffer growth. */
    if (tokenizer->attribute_names != NULL || count >= 32) {
        bool duplicate;
        lxa_status_t status;
        lxa_html_result_t result;
        if (tokenizer->attribute_names == NULL) {
            status = lxa_table_create_with_budget(&tokenizer->allocator,
                tokenizer->budget, NULL, NULL, &tokenizer->attribute_names);
            result = attribute_table_result(tokenizer, status);
            if (result != LXA_HTML_OK) {
                return result;
            }
            for (i = 0; i < count; i++) {
                attribute_slot_t *prior;
                status = lxa_vector_at(&tokenizer->attribute_slots, i,
                                       (void **) &prior);
                if (status != LXA_OK) {
                    assert(false);
                    return LXA_HTML_INTERNAL;
                }
                status = lxa_table_set(tokenizer->attribute_names,
                    (lxa_span_t) {
                        tokenizer->attribute_bytes.data + prior->name_start,
                        prior->name_length
                    }, NULL, &duplicate, NULL);
                result = attribute_table_result(tokenizer, status);
                if (result != LXA_HTML_OK) {
                    return result;
                }
                if (duplicate) {
                    assert(false);
                    return LXA_HTML_INTERNAL;
                }
            }
        }
        status = lxa_table_set(tokenizer->attribute_names,
            (lxa_span_t) {
                tokenizer->attribute_bytes.data + slot->name_start,
                slot->name_length
            }, NULL, &duplicate, NULL);
        result = attribute_table_result(tokenizer, status);
        if (result != LXA_HTML_OK) {
            return result;
        }
        if (duplicate) {
            tokenizer->attribute_state = ATTRIBUTE_DISCARD;
            return emit_error(tokenizer, "duplicate-attribute", offset);
        }
        return LXA_HTML_OK;
    }
    for (i = 0; i < count; i++) {
        attribute_slot_t *prior;
        lxa_status_t status;
        lxa_html_result_t result;
        size_t work = 1;
        status = lxa_vector_at(&tokenizer->attribute_slots, i,
                               (void **) &prior);
        if (status != LXA_OK) {
            assert(false);
            return LXA_HTML_INTERNAL;
        }
        if (prior->name_length == slot->name_length
            && lxa_size_add(work, slot->name_length, &work) != LXA_OK) {
            return LXA_HTML_OVERFLOW;
        }
        result = charge_work(tokenizer, work);
        if (result != LXA_HTML_OK) {
            return result;
        }
        if (prior->name_length == slot->name_length
            && memcmp(tokenizer->attribute_bytes.data + prior->name_start,
                      tokenizer->attribute_bytes.data + slot->name_start,
                      slot->name_length) == 0) {
            tokenizer->attribute_state = ATTRIBUTE_DISCARD;
            return emit_error(tokenizer, "duplicate-attribute", offset);
        }
    }
    return LXA_HTML_OK;
}

static bool
ascii_name_is(lxa_span_t name, const char *lowercase, size_t length)
{
    size_t i;
    if (name.length != length) {
        return false;
    }
    for (i = 0; i < length; i++) {
        uint8_t c = name.data[i];
        if (lxa_html_byte_upper(c)) {
            c += 'a' - 'A';
        }
        if (c != (uint8_t) lowercase[i]) {
            return false;
        }
    }
    return true;
}

static lxa_html_result_t
append_name(lxa_html_tokenizer_t *tokenizer, uint8_t c)
{
    lxa_span_t character;
    if (lxa_html_byte_upper(c)) {
        c += 'a' - 'A';
    }
    character.data = &c;
    character.length = 1;
    return buffer_append(tokenizer, &tokenizer->name, character,
                         LXA_BUDGET_NAME_BYTES);
}

static lxa_html_result_t
flush_raw_candidate(lxa_html_tokenizer_t *tokenizer, bool slash)
{
    lxa_span_t prefix = {
        (const uint8_t *) (slash ? "</" : "<"), slash ? 2 : 1
    };
    lxa_span_t candidate = {tokenizer->raw_candidate.data,
                            tokenizer->raw_candidate.length};
    lxa_html_result_t result = buffer_append(tokenizer, &tokenizer->text,
                                             prefix, LXA_BUDGET_TOKEN_BYTES);
    if (result == LXA_HTML_OK) {
        result = buffer_append(tokenizer, &tokenizer->text, candidate,
                               LXA_BUDGET_TOKEN_BYTES);
    }
    if (result == LXA_HTML_OK) {
        lxa_buffer_clear(&tokenizer->raw_candidate);
        tokenizer->state = tokenizer->raw_return_mode;
    }
    return result;
}

lxa_status_t
lxa_html_tokenizer_create(const lxa_allocator_t *allocator,
                          lxa_html_token_callback_t on_token,
                          lxa_html_error_callback_t on_error, void *context,
                          lxa_html_tokenizer_t **out)
{
    return lxa_html_tokenizer_create_with_budget(
        allocator, on_token, on_error, context, NULL, out);
}

lxa_status_t
lxa_html_tokenizer_create_with_budget(
    const lxa_allocator_t *allocator, lxa_html_token_callback_t on_token,
    lxa_html_error_callback_t on_error, void *context,
    lxa_budget_t *budget, lxa_html_tokenizer_t **out)
{
    lxa_html_tokenizer_t *tokenizer;
    lxa_budget_t owned;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (on_token == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    if (allocator == NULL || allocator->allocate == NULL
        || allocator->resize == NULL || allocator->release == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    if (budget == NULL) {
        status = lxa_budget_init(&owned, NULL);
        if (status != LXA_OK) {
            return status;
        }
        budget = &owned;
    }
    status = lxa_budget_charge(budget, LXA_BUDGET_MEMORY,
                               sizeof(*tokenizer), NULL);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_alloc(allocator, sizeof(*tokenizer), (void **) &tokenizer);
    if (status != LXA_OK) {
        lxa_status_t released = lxa_budget_release(
            budget, LXA_BUDGET_MEMORY, sizeof(*tokenizer));
        assert(released == LXA_OK);
        (void) released;
        return status;
    }
    memset(tokenizer, 0, sizeof(*tokenizer));
    tokenizer->allocator = *allocator;
    tokenizer->on_token = on_token;
    tokenizer->on_error = on_error;
    tokenizer->context = context;
    if (budget == &owned) {
        tokenizer->owned_budget = owned;
        budget = &tokenizer->owned_budget;
    }
    tokenizer->budget = budget;
    tokenizer->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_buffer_init(&tokenizer->text, allocator);
    if (status == LXA_OK) {
        status = lxa_buffer_init(&tokenizer->name, allocator);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_init(&tokenizer->attribute_bytes, allocator);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_init(&tokenizer->appropriate_end_tag, allocator);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_init(&tokenizer->raw_candidate, allocator);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_init(&tokenizer->system_id, allocator);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_init(&tokenizer->public_id, allocator);
    }
    if (status == LXA_OK) {
        status = lxa_vector_init(&tokenizer->attribute_slots, allocator,
                                 sizeof(attribute_slot_t));
    }
    if (status == LXA_OK) {
        status = lxa_vector_init(&tokenizer->attribute_views, allocator,
                                 sizeof(lxa_html_attribute_t));
    }
    if (status == LXA_OK) {
        status = lxa_buffer_bind_budget(&tokenizer->text, budget);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_bind_budget(&tokenizer->name, budget);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_bind_budget(&tokenizer->attribute_bytes, budget);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_bind_budget(&tokenizer->appropriate_end_tag, budget);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_bind_budget(&tokenizer->raw_candidate, budget);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_bind_budget(&tokenizer->system_id, budget);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_bind_budget(&tokenizer->public_id, budget);
    }
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&tokenizer->attribute_slots, budget);
    }
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&tokenizer->attribute_views, budget);
    }
    if (status == LXA_OK) {
        lxa_span_t label = {(const uint8_t *) "utf-8", 5};
        const lxa_encoding_t *utf8 = lxa_encoding_lookup(label);
        status = lxa_decoder_create_with_budget(
            utf8, LXA_ENCODING_REPLACE, allocator, budget, &tokenizer->decoder);
        if (status == LXA_OK) {
            status = lxa_encoder_create_with_budget(
                utf8, LXA_ENCODING_FATAL, allocator, budget,
                &tokenizer->encoder);
        }
    }
    if (status != LXA_OK) {
        lxa_html_tokenizer_destroy(tokenizer);
        return status;
    }
    *out = tokenizer;
    return LXA_OK;
}

void
lxa_html_tokenizer_destroy(lxa_html_tokenizer_t *tokenizer)
{
    lxa_allocator_t allocator;
    lxa_budget_t *external;
    if (tokenizer == NULL) {
        return;
    }
    allocator = tokenizer->allocator;
    external = tokenizer->budget == &tokenizer->owned_budget
               ? NULL : tokenizer->budget;
    lxa_table_destroy(tokenizer->attribute_names);
    lxa_encoder_destroy(tokenizer->encoder);
    lxa_decoder_destroy(tokenizer->decoder);
    lxa_vector_destroy(&tokenizer->attribute_views);
    lxa_vector_destroy(&tokenizer->attribute_slots);
    lxa_buffer_destroy(&tokenizer->raw_candidate);
    lxa_buffer_destroy(&tokenizer->appropriate_end_tag);
    lxa_buffer_destroy(&tokenizer->system_id);
    lxa_buffer_destroy(&tokenizer->public_id);
    lxa_buffer_destroy(&tokenizer->attribute_bytes);
    lxa_buffer_destroy(&tokenizer->name);
    lxa_buffer_destroy(&tokenizer->text);
    lxa_free(&allocator, tokenizer);
    if (external != NULL) {
        lxa_status_t released = lxa_budget_release(
            external, LXA_BUDGET_MEMORY, sizeof(*tokenizer));
        assert(released == LXA_OK);
        (void) released;
    }
}

void
lxa_html_tokenizer_reset(lxa_html_tokenizer_t *tokenizer)
{
    if (tokenizer != NULL) {
        lxa_table_destroy(tokenizer->attribute_names);
        tokenizer->attribute_names = NULL;
        lxa_buffer_clear(&tokenizer->text);
        lxa_buffer_clear(&tokenizer->name);
        lxa_buffer_clear(&tokenizer->attribute_bytes);
        lxa_buffer_clear(&tokenizer->appropriate_end_tag);
        lxa_buffer_clear(&tokenizer->raw_candidate);
        lxa_buffer_clear(&tokenizer->system_id);
        lxa_buffer_clear(&tokenizer->public_id);
        lxa_vector_clear(&tokenizer->attribute_slots);
        lxa_vector_clear(&tokenizer->attribute_views);
        tokenizer->attribute_state = ATTRIBUTE_NONE;
        tokenizer->source_attribute_count = 0;
        lxa_decoder_reset(tokenizer->decoder);
        lxa_encoder_reset(tokenizer->encoder);
        tokenizer->state = STATE_DATA;
        tokenizer->raw_mode = STATE_DATA;
        tokenizer->raw_return_mode = STATE_DATA;
        tokenizer->script_word_length = 0;
        tokenizer->script_word_matches = false;
        tokenizer->allow_cdata = false;
        tokenizer->stopped = LXA_HTML_OK;
        tokenizer->declaration_length = 0;
        tokenizer->doctype_keyword_length = 0;
        tokenizer->doctype_keyword_offset = 0;
        tokenizer->doctype_quirks = false;
        tokenizer->has_system_id = false;
        tokenizer->has_public_id = false;
        tokenizer->callback_status = LXA_OK;
        tokenizer->limit_reason = LXA_BUDGET_COUNT;
        if (tokenizer->budget == &tokenizer->owned_budget) {
            lxa_budget_reset_session(tokenizer->budget);
        }
        tokenizer->end_tag = tokenizer->closed = false;
        tokenizer->in_callback = false;
        tokenizer->in_error = tokenizer->utf8_pending = false;
        tokenizer->previous_cr = false;
        tokenizer->feed_started = tokenizer->prepaid_input = false;
        tokenizer->offset = tokenizer->utf8_start = 0;
        tokenizer->reference = 0;
        tokenizer->reference_hex_marker = 0;
        tokenizer->reference_length = 0;
        tokenizer->reference_match_length = 0;
        tokenizer->reference_match = NULL;
        tokenizer->reference_digits = false;
        tokenizer->control_reported = false;
        tokenizer->reference_return_state = STATE_DATA;
    }
}

lxa_status_t
lxa_html_tokenizer_callback_status(const lxa_html_tokenizer_t *tokenizer)
{
    return tokenizer == NULL ? LXA_ERROR_ARGUMENT : tokenizer->callback_status;
}

lxa_limit_reason_t
lxa_html_tokenizer_limit_reason(const lxa_html_tokenizer_t *tokenizer)
{
    return tokenizer == NULL ? LXA_BUDGET_COUNT : tokenizer->limit_reason;
}

lxa_html_result_t
lxa_html_tokenizer_set_text_mode(lxa_html_tokenizer_t *tokenizer,
                                 lxa_html_text_mode_t mode,
                                 lxa_span_t appropriate_end_tag)
{
    if (tokenizer == NULL || tokenizer->in_error || tokenizer->closed
        || tokenizer->stopped != LXA_HTML_OK
        || (appropriate_end_tag.length != 0 && appropriate_end_tag.data == NULL))
    {
        return LXA_HTML_BAD_ARGUMENT;
    }
    if (mode < LXA_HTML_DATA || mode > LXA_HTML_PLAINTEXT
        || (mode == LXA_HTML_DATA && appropriate_end_tag.length != 0))
    {
        return LXA_HTML_BAD_ARGUMENT;
    }
    if (mode == LXA_HTML_DATA && appropriate_end_tag.length == 0) {
        if (tokenizer->state != STATE_DATA
            && tokenizer->state != STATE_RCDATA
            && tokenizer->state != STATE_RAWTEXT
            && tokenizer->state != STATE_PLAINTEXT
            && tokenizer->state != STATE_SCRIPT_DATA) {
            return LXA_HTML_BAD_ARGUMENT;
        }
        tokenizer->state = STATE_DATA;
        tokenizer->raw_mode = STATE_DATA;
        tokenizer->raw_return_mode = STATE_DATA;
        lxa_buffer_clear(&tokenizer->appropriate_end_tag);
        lxa_buffer_clear(&tokenizer->raw_candidate);
        return LXA_HTML_OK;
    }
    if (mode == LXA_HTML_PLAINTEXT) {
        if (tokenizer->state != STATE_DATA
            || appropriate_end_tag.length != 0) {
            return LXA_HTML_BAD_ARGUMENT;
        }
        tokenizer->state = STATE_PLAINTEXT;
        return LXA_HTML_OK;
    }
    if (mode == LXA_HTML_RCDATA || mode == LXA_HTML_RAWTEXT
        || mode == LXA_HTML_SCRIPT_DATA) {
        lxa_html_result_t result;
        lxa_limit_reason_t reason = LXA_BUDGET_COUNT;
        lxa_status_t status;
        size_t i;
        if (tokenizer->state != STATE_DATA || appropriate_end_tag.length == 0) {
            return LXA_HTML_BAD_ARGUMENT;
        }
        status = lxa_budget_check_item(tokenizer->budget, LXA_BUDGET_NAME_BYTES,
                                       appropriate_end_tag.length, &reason);
        if (status != LXA_OK) {
            return budget_result(tokenizer, status, reason);
        }
        for (i = 0; i < appropriate_end_tag.length; i++) {
            uint8_t c = appropriate_end_tag.data[i];
            if (!lxa_html_byte_alpha(c)) {
                return LXA_HTML_BAD_ARGUMENT;
            }
        }
        lxa_buffer_clear(&tokenizer->appropriate_end_tag);
        result = buffer_append(tokenizer, &tokenizer->appropriate_end_tag,
                               appropriate_end_tag, LXA_BUDGET_NAME_BYTES);
        if (result != LXA_HTML_OK) {
            return result;
        }
        for (i = 0; i < tokenizer->appropriate_end_tag.length; i++) {
            uint8_t *c = &tokenizer->appropriate_end_tag.data[i];
            if (lxa_html_byte_upper(*c)) {
                *c += 'a' - 'A';
            }
        }
        tokenizer->raw_mode = mode == LXA_HTML_RCDATA ? STATE_RCDATA
                            : mode == LXA_HTML_SCRIPT_DATA ? STATE_SCRIPT_DATA
                                                            : STATE_RAWTEXT;
        tokenizer->raw_return_mode = tokenizer->raw_mode;
        tokenizer->state = tokenizer->raw_mode;
        return LXA_HTML_OK;
    }
    return LXA_HTML_UNSUPPORTED;
}

lxa_html_result_t
lxa_html_tokenizer_allow_cdata(lxa_html_tokenizer_t *tokenizer, bool allow)
{
    if (tokenizer == NULL || tokenizer->in_error || tokenizer->closed
        || tokenizer->stopped != LXA_HTML_OK) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    tokenizer->allow_cdata = allow;
    return LXA_HTML_OK;
}

lxa_html_result_t
tokenizer_feed(lxa_html_tokenizer_t *tokenizer, lxa_span_t input, bool prepaid)
{
    size_t i = 0, accepted = 0;
    lxa_html_result_t result;
    if (tokenizer == NULL || tokenizer->in_callback
        || (input.data == NULL && input.length != 0)) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    if (tokenizer->stopped != LXA_HTML_OK) {
        return tokenizer->stopped;
    }
    if (tokenizer->closed) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    if ((tokenizer->feed_started && tokenizer->prepaid_input != prepaid)
        || (prepaid && (tokenizer->budget == &tokenizer->owned_budget
            || (input.length != 0
                && tokenizer->budget->used[LXA_BUDGET_INPUT] == 0)))) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    if (input.length > SIZE_MAX - tokenizer->offset) {
        tokenizer->stopped = LXA_HTML_OVERFLOW;
        return tokenizer->stopped;
    }
    if (input.length != 0) {
        tokenizer->feed_started = true;
        tokenizer->prepaid_input = prepaid;
    }
    while (i < input.length) {
        uint8_t c;
        /* Reconsuming a byte costs work, but never accepts that byte twice. */
        if (accepted == i) {
            if (!prepaid) {
                result = accept_input(tokenizer, 1);
                if (result != LXA_HTML_OK) {
                    goto fail;
                }
            }
            accepted++;
        }
        result = charge_work(tokenizer, 1);
        if (result != LXA_HTML_OK) {
            goto fail;
        }
        c = input.data[i];
        if (tokenizer->previous_cr) {
            tokenizer->previous_cr = false;
            if (c == '\n') {
                i++;
                continue;
            }
        }
        /* Text-run states normalize CR when they consume the source byte. */
        if (c == '\r' && !tokenizer->utf8_pending
            && (tokenizer->state == STATE_NAME
                || tokenizer->state == STATE_AFTER_NAME
                || tokenizer->state == STATE_ATTRIBUTE_NAME
                || tokenizer->state == STATE_AFTER_ATTRIBUTE_NAME
                || tokenizer->state == STATE_BEFORE_ATTRIBUTE_VALUE
                || tokenizer->state == STATE_ATTRIBUTE_VALUE_DOUBLE
                || tokenizer->state == STATE_ATTRIBUTE_VALUE_SINGLE
                || tokenizer->state == STATE_ATTRIBUTE_VALUE_UNQUOTED
                || tokenizer->state == STATE_AFTER_ATTRIBUTE_VALUE
                || tokenizer->state == STATE_RAW_END_TAG_NAME
                || tokenizer->state == STATE_DOCTYPE
                || tokenizer->state == STATE_BEFORE_DOCTYPE_NAME
                || tokenizer->state == STATE_DOCTYPE_NAME
                || tokenizer->state == STATE_AFTER_DOCTYPE_NAME
                || tokenizer->state == STATE_DOCTYPE_KEYWORD
                || tokenizer->state == STATE_AFTER_PUBLIC_KEYWORD
                || tokenizer->state == STATE_BEFORE_PUBLIC_ID
                || tokenizer->state == STATE_PUBLIC_ID_DOUBLE
                || tokenizer->state == STATE_PUBLIC_ID_SINGLE
                || tokenizer->state == STATE_AFTER_PUBLIC_ID
                || tokenizer->state == STATE_BETWEEN_PUBLIC_SYSTEM_IDS
                || tokenizer->state == STATE_AFTER_SYSTEM_KEYWORD
                || tokenizer->state == STATE_BEFORE_SYSTEM_ID
                || tokenizer->state == STATE_SYSTEM_ID_DOUBLE
                || tokenizer->state == STATE_SYSTEM_ID_SINGLE
                || tokenizer->state == STATE_AFTER_SYSTEM_ID
                || tokenizer->state == STATE_SCRIPT_ESCAPE_START
                || tokenizer->state == STATE_SCRIPT_ESCAPE_START_DASH
                || tokenizer->state == STATE_SCRIPT_DOUBLE_ESCAPE_START
                || tokenizer->state == STATE_SCRIPT_DOUBLE_LESS_THAN
                || tokenizer->state == STATE_SCRIPT_DOUBLE_ESCAPE_END
                || tokenizer->state == STATE_DECLARATION_CDATA
                || tokenizer->state == STATE_CDATA
                || tokenizer->state == STATE_CDATA_BRACKET
                || tokenizer->state == STATE_CDATA_END
                || tokenizer->state == STATE_COMMENT
                || tokenizer->state == STATE_BOGUS_COMMENT
                || tokenizer->state == STATE_PI_OPEN
                || tokenizer->state == STATE_PI_TARGET
                || tokenizer->state == STATE_PI_AFTER_TARGET
                || tokenizer->state == STATE_PI_DATA))
        {
            c = '\n';
            tokenizer->previous_cr = true;
        }
        /* A failed declaration lookahead reports an error at the start of
         * the lookahead. Defer a control error on its mismatching byte until
         * bogus-comment reconsumption so callbacks retain input order.
         * Otherwise report controls before state errors, once per byte. */
        if (!tokenizer->utf8_pending
            && tokenizer->state != STATE_COMMENT_OPEN
            && tokenizer->state != STATE_DECLARATION_DOCTYPE
            && tokenizer->state != STATE_DECLARATION_CDATA
            && ((c < ' ' && !lxa_html_byte_space(c) && c != 0 && c != '\r')
                || c == 0x7f)
            && (!tokenizer->control_reported
                || tokenizer->control_offset != tokenizer->offset + i)) {
            result = emit_error(tokenizer, "control-character-in-input-stream",
                                tokenizer->offset + i);
            if (result != LXA_HTML_OK) {
                goto fail;
            }
            tokenizer->control_offset = tokenizer->offset + i;
            tokenizer->control_reported = true;
        }
        switch (tokenizer->state) {
#define LXA_HTML_STATE(name, id, action, ...) case STATE_##name: __VA_ARGS__
#include "html/tokenizer_states.def"
#undef LXA_HTML_STATE
        }
    }
    tokenizer->offset += input.length;
    return LXA_HTML_OK;

fail:
    tokenizer->stopped = result;
    return result;
}

lxa_html_result_t
lxa_html_tokenizer_feed(lxa_html_tokenizer_t *tokenizer, lxa_span_t input)
{
    return tokenizer_feed(tokenizer, input, false);
}

lxa_html_result_t
lxa_html_tokenizer_feed_prepaid(lxa_html_tokenizer_t *tokenizer,
                                lxa_span_t decoded_utf8)
{
    return tokenizer_feed(tokenizer, decoded_utf8, true);
}

lxa_html_result_t
lxa_html_tokenizer_finish(lxa_html_tokenizer_t *tokenizer)
{
    lxa_html_token_t eof = {0};
    lxa_html_result_t result;
    bool decoder_finished = false;
    if (tokenizer == NULL || tokenizer->in_callback) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    if (tokenizer->stopped != LXA_HTML_OK) {
        return tokenizer->stopped;
    }
    if (tokenizer->closed) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    if (eof_action(tokenizer->state) == EOF_TAG_PREFIX
        && (tokenizer->state == STATE_TAG_OPEN || tokenizer->end_tag)) {
        bool end_tag = tokenizer->state == STATE_NAME_BEGIN;
        lxa_span_t prefix = {
            (const uint8_t *) (end_tag ? "</" : "<"),
            end_tag ? 2 : 1
        };
        result = emit_error(tokenizer, "eof-before-tag-name", tokenizer->offset);
        if (result == LXA_HTML_OK) {
            result = buffer_append(tokenizer, &tokenizer->text, prefix,
                                   LXA_BUDGET_TOKEN_BYTES);
        }
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
        tokenizer->state = STATE_DATA;
        tokenizer->end_tag = false;
    }
    else if (eof_action(tokenizer->state) == EOF_DECLARATION_DASH
             || eof_action(tokenizer->state) == EOF_COMMENT_OPEN) {
        bool dash = tokenizer->state == STATE_COMMENT_OPEN;
        result = emit_error(tokenizer, "incorrectly-opened-comment",
                            tokenizer->offset - (dash ? 1 : 0));
        if (result == LXA_HTML_OK && dash) {
            lxa_span_t character = {(const uint8_t *) "-", 1};
            result = buffer_append(tokenizer, &tokenizer->text,
                                   character, LXA_BUDGET_TOKEN_BYTES);
        }
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
        tokenizer->state = STATE_BOGUS_COMMENT;
    }
    else if (eof_action(tokenizer->state) == EOF_DECLARATION_PREFIX) {
        lxa_span_t prefix = {
            (const uint8_t *) tokenizer->declaration,
            tokenizer->declaration_length
        };
        result = emit_error(tokenizer, "incorrectly-opened-comment",
                            tokenizer->offset - tokenizer->declaration_length);
        if (result == LXA_HTML_OK) {
            result = buffer_append(tokenizer, &tokenizer->text, prefix,
                                   LXA_BUDGET_TOKEN_BYTES);
        }
        if (result == LXA_HTML_OK) {
            result = emit_comment(tokenizer);
        }
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    else if (eof_action(tokenizer->state) == EOF_CDATA) {
        lxa_span_t brackets = {
            (const uint8_t *) "]]",
            tokenizer->state == STATE_CDATA ? 0
            : tokenizer->state == STATE_CDATA_BRACKET ? 1 : 2
        };
        if (tokenizer->utf8_pending) {
            result = finish_decoder(tokenizer, false);
            if (result != LXA_HTML_OK) {
                tokenizer->stopped = result;
                return result;
            }
            decoder_finished = true;
            tokenizer->utf8_pending = false;
        }
        result = buffer_append(tokenizer, &tokenizer->text, brackets,
                               LXA_BUDGET_TOKEN_BYTES);
        if (result == LXA_HTML_OK) {
            result = emit_error(tokenizer, "eof-in-cdata", tokenizer->offset);
        }
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
        tokenizer->state = STATE_DATA;
    }
    else if (eof_action(tokenizer->state) == EOF_BOGUS_DOCTYPE) {
        if (tokenizer->utf8_pending) {
            result = finish_decoder(tokenizer, false);
            if (result != LXA_HTML_OK) {
                tokenizer->stopped = result;
                return result;
            }
            decoder_finished = true;
            tokenizer->utf8_pending = false;
        }
        result = emit_doctype(tokenizer);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    else if (eof_action(tokenizer->state) == EOF_DOCTYPE_KEYWORD) {
        result = emit_error(tokenizer,
                            "invalid-character-sequence-after-doctype-name",
                            tokenizer->doctype_keyword_offset);
        if (result == LXA_HTML_OK) {
            tokenizer->doctype_quirks = true;
            result = emit_doctype(tokenizer);
        }
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    else if (eof_action(tokenizer->state) == EOF_DOCTYPE) {
        if (tokenizer->utf8_pending) {
            result = finish_decoder(tokenizer,
                        tokenizer->state == STATE_DOCTYPE_NAME);
            if (result != LXA_HTML_OK) {
                tokenizer->stopped = result;
                return result;
            }
            decoder_finished = true;
            tokenizer->utf8_pending = false;
        }
        result = emit_error(tokenizer, "eof-in-doctype", tokenizer->offset);
        if (result == LXA_HTML_OK) {
            tokenizer->doctype_quirks = true;
            result = emit_doctype(tokenizer);
        }
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    else if (eof_action(tokenizer->state) == EOF_PI) {
        if (tokenizer->utf8_pending) {
            result = finish_decoder(tokenizer, false);
            if (result != LXA_HTML_OK) {
                tokenizer->stopped = result;
                return result;
            }
            decoder_finished = true;
            tokenizer->utf8_pending = false;
        }
        result = emit_error(tokenizer, "eof-in-processing-instruction",
                            tokenizer->offset);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
        lxa_buffer_clear(&tokenizer->name);
        lxa_buffer_clear(&tokenizer->text);
        tokenizer->state = STATE_DATA;
    }
    if (eof_action(tokenizer->state) == EOF_REFERENCE_MARK) {
        lxa_span_t ampersand = {(const uint8_t *) "&", 1};
        result = append_reference_bytes(tokenizer, ampersand);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
        tokenizer->state = tokenizer->reference_return_state;
    }
    else if (eof_action(tokenizer->state) == EOF_REFERENCE_NAME) {
        result = resolve_name(tokenizer, false, tokenizer->offset, 0);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    else if (eof_action(tokenizer->state) == EOF_REFERENCE_LONG_NAME) {
        tokenizer->state = tokenizer->reference_return_state;
    }
    else if (eof_action(tokenizer->state) == EOF_REFERENCE_NO_DIGITS
             || (eof_action(tokenizer->state) == EOF_REFERENCE_NUMBER
                 && !tokenizer->reference_digits)) {
        result = missing_numeric_digits(tokenizer, tokenizer->offset);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    else if (eof_action(tokenizer->state) == EOF_REFERENCE_NUMBER
             && tokenizer->reference_digits) {
        result = resolve_numeric(tokenizer, false, tokenizer->offset);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    else if (eof_action(tokenizer->state) == EOF_BOGUS_COMMENT) {
        if (tokenizer->utf8_pending) {
            result = finish_decoder(tokenizer, false);
            if (result != LXA_HTML_OK) {
                tokenizer->stopped = result;
                return result;
            }
            decoder_finished = true;
            tokenizer->utf8_pending = false;
        }
        result = emit_comment(tokenizer);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    else if (eof_action(tokenizer->state) == EOF_COMMENT) {
        if (tokenizer->utf8_pending) {
            result = finish_decoder(tokenizer, false);
            if (result != LXA_HTML_OK) {
                tokenizer->stopped = result;
                return result;
            }
            decoder_finished = true;
            tokenizer->utf8_pending = false;
        }
        result = emit_error(tokenizer, "eof-in-comment", tokenizer->offset);
        if (result == LXA_HTML_OK) {
            result = emit_comment(tokenizer);
        }
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    if (eof_action(tokenizer->state) == EOF_TAG) {
        if (tokenizer->utf8_pending) {
            result = finish_decoder(tokenizer, true);
            if (result != LXA_HTML_OK) {
                tokenizer->stopped = result;
                return result;
            }
            decoder_finished = true;
            tokenizer->utf8_pending = false;
        }
        result = tokenizer->state == STATE_ATTRIBUTE_NAME
                 ? end_attribute_name(tokenizer, tokenizer->offset)
                 : LXA_HTML_OK;
        if (result == LXA_HTML_OK) {
            result = emit_error(tokenizer, "eof-in-tag", tokenizer->offset);
        }
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
        lxa_buffer_clear(&tokenizer->name);
        lxa_buffer_clear(&tokenizer->attribute_bytes);
        lxa_vector_clear(&tokenizer->attribute_slots);
        lxa_vector_clear(&tokenizer->attribute_views);
        tokenizer->attribute_state = ATTRIBUTE_NONE;
        tokenizer->source_attribute_count = 0;
        tokenizer->state = STATE_DATA;
        tokenizer->end_tag = false;
    }
    if (eof_action(tokenizer->state) == EOF_RAW_CANDIDATE) {
        result = flush_raw_candidate(tokenizer,
                    tokenizer->state != STATE_RAW_LESS_THAN);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    if (eof_action(tokenizer->state) == EOF_SCRIPT_ESCAPED) {
        if (tokenizer->utf8_pending) {
            result = finish_decoder(tokenizer, false);
            if (result != LXA_HTML_OK) {
                tokenizer->stopped = result;
                return result;
            }
            decoder_finished = true;
            tokenizer->utf8_pending = false;
        }
        result = emit_error(tokenizer, "eof-in-script-html-comment-like-text",
                            tokenizer->offset);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
        tokenizer->state = STATE_SCRIPT_DATA;
    }
    if (eof_action(tokenizer->state) == EOF_SCRIPT_ESCAPE_START) {
        tokenizer->state = STATE_SCRIPT_DATA;
    }
    if (eof_action(tokenizer->state) != EOF_TEXT) {
        assert(!"unhandled tokenizer EOF state");
        tokenizer->stopped = LXA_HTML_INTERNAL;
        return tokenizer->stopped;
    }
    if (!decoder_finished) {
        result = finish_decoder(tokenizer, false);
        if (result != LXA_HTML_OK) {
            tokenizer->stopped = result;
            return result;
        }
    }
    result = emit_text(tokenizer);
    if (result == LXA_HTML_OK) {
        eof.kind = LXA_HTML_EOF;
        result = emit(tokenizer, &eof);
    }
    if (result != LXA_HTML_OK) {
        tokenizer->stopped = result;
    }
    else {
        tokenizer->closed = true;
    }
    return result;
}
