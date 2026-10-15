#ifndef LIBLEXA_HTML_TOKENIZER_INTERNAL_H
#define LIBLEXA_HTML_TOKENIZER_INTERNAL_H

#include <assert.h>
#include <string.h>
#include "base/budget_internal.h"
#include "html/tokenizer.h"
#include "html/tokenizer_classes.h"
#include "liblexa/encoding.h"
#include "html-entities.h"

typedef enum {
#define LXA_HTML_STATE(name, id, action, scan, ...) STATE_##name = id,
#include "html/tokenizer_states.def"
#undef LXA_HTML_STATE
} tokenizer_state_t;

typedef struct {
    size_t name_start, name_length, value_start, value_length;
    /* SIZE_MAX value_start counts normalized bytes without retaining a value. */
} attribute_slot_t;

/* A discarded duplicate remains the current parsing candidate for its value. */
typedef enum {
    ATTRIBUTE_NONE, ATTRIBUTE_KEEP, ATTRIBUTE_DISCARD
} attribute_state_t;

struct lxa_html_tokenizer {
    lxa_allocator_t allocator;
    lxa_buffer_t text, name, attribute_bytes, appropriate_end_tag, raw_candidate;
    lxa_buffer_t public_id, system_id;
    lxa_vector_t attribute_slots, attribute_views, attribute_sources;
    lxa_table_t *attribute_names; /* Large tags only; owns normalized key copies. */
    attribute_slot_t attribute;
    lxa_html_source_range_t attribute_source;
    attribute_state_t attribute_state;
    size_t source_attribute_count; /* Includes duplicates for the source limit. */
    size_t omitted_value_bytes; /* Normalized bytes excluded from attribute storage. */
    lxa_budget_t owned_budget, *budget;
    lxa_decoder_t *decoder; /* Created on first decode; retained across resets. */
    lxa_html_token_callback_t on_token;
    lxa_html_error_callback_t on_error;
    lxa_html_attribute_value_filter_f value_filter;
    void *value_filter_context;
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
    bool source_ranges;
    bool allow_cdata;
    bool end_tag, closed, in_callback, in_error, utf8_pending, previous_cr;
    bool reference_digits;
    bool control_reported;
    bool doctype_quirks, has_public_id, has_system_id;
};

lxa_html_result_t lxa_html_tokenizer_find_entity(
    lxa_html_tokenizer_t *tokenizer, const char *name,
    const liblexa_html_entity_entry **entry);

static inline lxa_html_result_t
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

/* Append after the caller has checked token/name sizes. */
static inline lxa_html_result_t
buffer_append_storage(lxa_html_tokenizer_t *tokenizer, lxa_buffer_t *buffer,
                      lxa_span_t bytes)
{
    lxa_status_t status = lxa_buffer_append(buffer, bytes);
    if (status == LXA_ERROR_NO_MEMORY) {
        return LXA_HTML_NO_MEMORY;
    }
    if (status == LXA_ERROR_LIMIT) {
        return budget_result(tokenizer, status, LXA_BUDGET_MEMORY);
    }
    return budget_result(tokenizer, status, LXA_BUDGET_COUNT);
}

static inline lxa_html_result_t
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
    status = lxa_budget_check_item_impl(tokenizer->budget, kind, length, &reason);
    if (status == LXA_OK && buffer == &tokenizer->name) {
        status = lxa_budget_check_item_impl(tokenizer->budget,
                                       LXA_BUDGET_TOKEN_BYTES, length, &reason);
    }
    else if (status == LXA_OK && buffer == &tokenizer->text
             && (tokenizer->state == STATE_PI_DATA
                 || tokenizer->state == STATE_PI_QUESTIONABLE)) {
        size_t total;
        status = lxa_size_add(tokenizer->name.length, length, &total);
        if (status == LXA_OK) {
            status = lxa_budget_check_item_impl(tokenizer->budget,
                                           LXA_BUDGET_TOKEN_BYTES, total,
                                           &reason);
        }
    }
    if (status != LXA_OK) {
        return budget_result(tokenizer, status, reason);
    }
    return buffer_append_storage(tokenizer, buffer, bytes);
}

#endif
