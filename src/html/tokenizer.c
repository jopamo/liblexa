#include "html/tokenizer_internal.h"

#define LXA_HTML_ENGINE_NAME(name) lxa_html_tokenizer_##name
#define LXA_HTML_ENGINE_ACCEPTS(tokenizer) true
#define LXA_HTML_EMIT_TOKEN(tokenizer, token) \
    (tokenizer)->on_token((tokenizer)->context, tokenizer, token)
#define LXA_HTML_HAS_DIAGNOSTICS(tokenizer) ((tokenizer)->on_error != NULL)
#define LXA_HTML_EMIT_DIAGNOSTIC(tokenizer, code, offset) \
    (tokenizer)->on_error((tokenizer)->context, code, offset)
#include "html/tokenizer_engine.inc"
#undef LXA_HTML_EMIT_DIAGNOSTIC
#undef LXA_HTML_HAS_DIAGNOSTICS
#undef LXA_HTML_EMIT_TOKEN
#undef LXA_HTML_ENGINE_ACCEPTS
#undef LXA_HTML_ENGINE_NAME

lxa_html_result_t
lxa_html_tokenizer_find_entity(lxa_html_tokenizer_t *tokenizer, const char *name,
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
        status = lxa_vector_init(&tokenizer->attribute_sources, allocator,
                                 sizeof(lxa_html_source_range_t));
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
        status = lxa_vector_bind_budget(&tokenizer->attribute_sources, budget);
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
    lxa_vector_destroy(&tokenizer->attribute_sources);
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
        lxa_vector_clear(&tokenizer->attribute_sources);
        tokenizer->attribute_source = (lxa_html_source_range_t) {0};
        tokenizer->attribute_state = ATTRIBUTE_NONE;
        tokenizer->source_attribute_count = 0;
        tokenizer->omitted_value_bytes = 0;
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

lxa_html_result_t
lxa_html_tokenizer_enable_source_ranges(lxa_html_tokenizer_t *tokenizer)
{
    if (tokenizer == NULL || tokenizer->feed_started || tokenizer->closed
        || tokenizer->in_callback || tokenizer->stopped != LXA_HTML_OK) {
        return LXA_HTML_BAD_ARGUMENT;
    }
    tokenizer->source_ranges = true;
    return LXA_HTML_OK;
}

lxa_status_t
lxa_html_tokenizer_callback_status(const lxa_html_tokenizer_t *tokenizer)
{
    return tokenizer == NULL ? LXA_ERROR_ARGUMENT : tokenizer->callback_status;
}

lxa_html_result_t
lxa_html_tokenizer_filter_attribute_values(lxa_html_tokenizer_t *tokenizer,
    lxa_html_attribute_value_filter_f filter, void *context)
{
    if (tokenizer == NULL || tokenizer->feed_started || tokenizer->closed
        || tokenizer->in_callback || tokenizer->stopped != LXA_HTML_OK)
        return LXA_HTML_BAD_ARGUMENT;
    tokenizer->value_filter = filter;
    tokenizer->value_filter_context = context;
    return LXA_HTML_OK;
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
