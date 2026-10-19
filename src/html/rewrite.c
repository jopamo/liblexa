#include "base/budget_internal.h"
#include "base/bytes_internal.h"
#include "encoding/stream_internal.h"
#include "liblexa/html_rewrite.h"
#include "html/source_consumer.h"
#include "html-charref-overrides.h"
#include <stdio.h>

typedef struct {
    const lxa_allocator_t *allocator;
    lxa_budget_t budget;
    lxa_buffer_t syntax;
    lxa_decoder_t *decoder;
    lxa_encoder_t *encoder;
    lxa_span_t input;
    size_t cursor;
    lxa_html_source_rewrite_callback_t callback;
    void *context;
    lxa_write_f write;
    void *write_context;
    lxa_limit_reason_t reason;
    bool utf8, unicode, callback_failed, operation_failed;
} rewrite_t;

static lxa_status_t
append(rewrite_t *owner, lxa_span_t bytes)
{
    size_t length;
    lxa_status_t status = lxa_budget_charge_impl(&owner->budget, LXA_BUDGET_STRINGS,
                                            bytes.length, &owner->reason);
    if (status == LXA_OK && bytes.length != 0) {
        status = lxa_size_add(owner->syntax.length, bytes.length, &length);
        if (status == LXA_OK && length > owner->syntax.capacity) {
            status = lxa_buffer_reserve_impl(&owner->syntax, length);
            if (status == LXA_OK)
                status = lxa_budget_check_impl(&owner->budget, LXA_BUDGET_WORK,
                                               0, &owner->reason);
            if (status == LXA_OK)
                status = lxa_budget_check_impl(&owner->budget, LXA_BUDGET_STRINGS,
                                               0, &owner->reason);
        }
    }
    if (status == LXA_OK)
        status = lxa_buffer_append_impl(&owner->syntax, bytes);
    if (status == LXA_ERROR_LIMIT && owner->reason == LXA_BUDGET_COUNT)
        owner->reason = LXA_BUDGET_MEMORY;
    return status;
}

static lxa_status_t
encode(rewrite_t *owner, uint32_t scalar)
{
    uint8_t bytes[64];
    lxa_encoding_progress_t progress;
    lxa_status_t status =
        lxa_budget_charge_work(&owner->budget, 1, &owner->reason);
    if (status != LXA_OK)
        return status;
    if (owner->utf8) {
        if (!lxa_encoding_scalar_valid(scalar))
            return LXA_ERROR_UNSUPPORTED;
        size_t width = lxa_encoding_utf8_scalar_length(scalar);
        lxa_encoding_utf8_write(bytes, scalar, width);
        return append(owner, (lxa_span_t){bytes, width});
    }
    lxa_encoding_result_t result = lxa_encoder_feed(
        owner->encoder, &scalar, 1, (lxa_mut_span_t){bytes, sizeof(bytes)}, &progress);
    if (result == LXA_ENCODING_MALFORMED)
        return LXA_ERROR_UNSUPPORTED;
    if (result != LXA_ENCODING_OK || progress.consumed != 1)
        return LXA_ERROR_INTERNAL;
    return append(owner, (lxa_span_t){bytes, progress.produced});
}

static lxa_status_t
ascii(rewrite_t *owner, const char *text)
{
    for (; *text != 0; ++text) {
        lxa_status_t status = encode(owner, (uint8_t)*text);
        if (status != LXA_OK)
            return status;
    }
    return LXA_OK;
}

static lxa_status_t
scalar(rewrite_t *owner, uint32_t cp)
{
    if (cp == 0)
        return LXA_ERROR_ARGUMENT;
    if (cp == '&')
        return ascii(owner, "&amp;");
    if (cp == '"')
        return ascii(owner, "&quot;");
    if (cp == '<')
        return ascii(owner, "&lt;");
    if (cp >= 0x80 && cp <= 0x9f) {
        for (size_t i = 0; i < LIBLEXA_HTML_CHARREF_OVERRIDE_COUNT; ++i) {
            if (liblexa_html_charref_overrides[i].input == cp &&
                liblexa_html_charref_overrides[i].output != cp)
                return encode(owner, cp);
        }
    }
    if (cp >= 0x20 && (owner->unicode || cp < 0x80))
        return encode(owner, cp);
    char reference[16];
    int length = snprintf(reference, sizeof(reference), "&#x%lx;", (unsigned long)cp);
    if (length <= 0 || (size_t)length >= sizeof(reference))
        return LXA_ERROR_INTERNAL;
    return ascii(owner, reference);
}

enum { DECODE_BATCH = 32 };

static size_t
ordinary_ascii_prefix(const uint32_t *scalars, size_t count, bool value,
                      uint8_t *bytes)
{
    if (count < 2) return 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t cp = scalars[i];
        if (cp < 0x20 || cp >= 0x80
            || (value && (cp == '&' || cp == '"' || cp == '<')))
            return i;
        bytes[i] = (uint8_t) cp;
    }
    return count;
}

static lxa_status_t
encode_ascii_batch(rewrite_t *owner, lxa_span_t bytes)
{
    for (size_t i = 0; i < bytes.length;) {
        size_t length = 0;
        if (owner->syntax.data != NULL
            && owner->syntax.length <= owner->syntax.capacity
            && lxa_buffer_allocator_valid(&owner->syntax.allocator)) {
            length = owner->syntax.capacity - owner->syntax.length;
            if (length > bytes.length - i) length = bytes.length - i;
        }
        lxa_status_t status;
        if (length > 1
            && lxa_budget_check_impl(&owner->budget, LXA_BUDGET_WORK,
                                      length, NULL) == LXA_OK
            && lxa_budget_check_impl(&owner->budget, LXA_BUDGET_STRINGS,
                                      length, NULL) == LXA_OK) {
            /* Capacity and both quotas fit; no callback can observe
             * intermediate charges. Retain scalar failure boundaries below. */
            status = lxa_budget_charge_work(&owner->budget, length, &owner->reason);
            if (status == LXA_OK)
                status = append(owner, (lxa_span_t){bytes.data + i, length});
        } else {
            length = 1;
            status = encode(owner, bytes.data[i]);
        }
        if (status != LXA_OK) return status;
        i += length;
    }
    return LXA_OK;
}

static lxa_status_t
decode(rewrite_t *owner, lxa_span_t input, bool value)
{
    if (input.data == NULL && input.length != 0)
        return LXA_ERROR_ARGUMENT;
    lxa_status_t status =
        lxa_budget_charge_work(&owner->budget, input.length, &owner->reason);
    if (status != LXA_OK)
        return status;
    lxa_decoder_reset(owner->decoder);
    while (input.length != 0) {
        uint32_t scalars[DECODE_BATCH];
        lxa_encoding_progress_t progress;
        lxa_encoding_result_t result =
            lxa_decoder_feed(owner->decoder, input, scalars,
                             sizeof(scalars) / sizeof(scalars[0]), &progress);
        if (result == LXA_ENCODING_MALFORMED)
            return LXA_ERROR_ARGUMENT;
        if ((result != LXA_ENCODING_OK && result != LXA_ENCODING_OUTPUT_FULL) ||
            (progress.consumed == 0 && progress.produced == 0))
            return LXA_ERROR_INTERNAL;
        uint8_t bytes[DECODE_BATCH];
        size_t prefix = owner->utf8
            ? ordinary_ascii_prefix(scalars, progress.produced, value, bytes) : 0;
        size_t i = 0;
        if (prefix > 1) {
            status = encode_ascii_batch(owner, (lxa_span_t){bytes, prefix});
            if (status != LXA_OK) return status;
            i = prefix;
        }
        for (; i < progress.produced; ++i) {
            status = value ? scalar(owner, scalars[i]) : encode(owner, scalars[i]);
            if (status != LXA_OK)
                return status;
        }
        input.data += progress.consumed;
        input.length -= progress.consumed;
    }
    uint32_t cp;
    lxa_encoding_progress_t progress;
    lxa_encoding_result_t result = lxa_decoder_finish(owner->decoder, &cp, 1, &progress);
    return result == LXA_ENCODING_OK          ? LXA_OK
           : result == LXA_ENCODING_MALFORMED ? LXA_ERROR_ARGUMENT
                                              : LXA_ERROR_INTERNAL;
}

static lxa_status_t
build(rewrite_t *owner, const lxa_html_source_attribute_t *event, lxa_span_t value)
{
    lxa_status_t status;
    if (owner->decoder == NULL) {
        const lxa_encoding_t *utf8 =
            lxa_encoding_lookup((lxa_span_t){(const uint8_t *)"utf-8", 5});
        owner->utf8 = event->encoding == utf8;
        owner->unicode =
            owner->utf8 ||
            event->encoding ==
                lxa_encoding_lookup((lxa_span_t){(const uint8_t *)"utf-16le", 8}) ||
            event->encoding ==
                lxa_encoding_lookup((lxa_span_t){(const uint8_t *)"utf-16be", 8});
        status = lxa_decoder_create_with_budget(
            utf8, LXA_ENCODING_FATAL, owner->allocator, &owner->budget, &owner->decoder);
        if (status == LXA_OK && !owner->utf8)
            status = lxa_encoder_create_with_budget(event->encoding, LXA_ENCODING_FATAL,
                                                    owner->allocator, &owner->budget,
                                                    &owner->encoder);
        if (status != LXA_OK) {
            if (status == LXA_ERROR_LIMIT)
                owner->reason = LXA_BUDGET_MEMORY;
            return status;
        }
    }
    lxa_buffer_clear(&owner->syntax);
    if (!owner->utf8)
        lxa_encoder_reset(owner->encoder);
    status = decode(owner, event->name, false);
    if (status == LXA_ERROR_UNSUPPORTED) {
        lxa_buffer_clear(&owner->syntax);
        lxa_encoder_reset(owner->encoder);
        status =
            append(owner, (lxa_span_t){owner->input.data + event->source_start,
                                       event->source_name_end - event->source_start});
    }
    if (status == LXA_OK)
        status = ascii(owner, "=\"");
    if (status == LXA_OK)
        status = decode(owner, value, true);
    if (status == LXA_OK)
        status = ascii(owner, "\"");
    if (status == LXA_OK) {
        if (owner->utf8)
            return append(owner, (lxa_span_t){0});
        uint8_t bytes[64];
        lxa_encoding_progress_t progress;
        lxa_encoding_result_t result = lxa_encoder_finish(
            owner->encoder, (lxa_mut_span_t){bytes, sizeof(bytes)}, &progress);
        if (result != LXA_ENCODING_OK)
            return LXA_ERROR_INTERNAL;
        status = append(owner, (lxa_span_t){bytes, progress.produced});
    }
    return status;
}

static lxa_status_t
publish(rewrite_t *owner, lxa_span_t bytes)
{
    lxa_status_t status =
        lxa_budget_charge_work(&owner->budget, bytes.length, &owner->reason);
    if (status != LXA_OK) {
        owner->operation_failed = true;
        return status;
    }
    status = lxa_write_all(bytes, owner->write, owner->write_context);
    if (status != LXA_OK)
        owner->callback_failed = true;
    return status;
}

static lxa_status_t
rewrite(void *context, const lxa_html_source_attribute_t *event)
{
    rewrite_t *owner = context;
    lxa_html_source_replacement_t replacement = {0};
    lxa_status_t status = owner->callback(owner->context, event, &replacement);
    if (status != LXA_OK) {
        owner->callback_failed = true;
        return status;
    }
    if (!replacement.replace)
        return LXA_OK;
    if (event->source_start < owner->cursor ||
        event->source_start >= event->source_name_end ||
        event->source_name_end > event->source_end ||
        event->source_end > owner->input.length) {
        owner->operation_failed = true;
        return LXA_ERROR_INTERNAL;
    }
    status = build(owner, event, replacement.value);
    if (status != LXA_OK) {
        owner->operation_failed = true;
        return status;
    }
    status = publish(owner, (lxa_span_t){owner->input.data + owner->cursor,
                                         event->source_start - owner->cursor});
    if (status == LXA_OK)
        status = publish(owner, (lxa_span_t){owner->syntax.data, owner->syntax.length});
    if (status == LXA_OK)
        owner->cursor = event->source_end;
    return status;
}

lxa_status_t
lxa_html_rewrite_attributes(const lxa_allocator_t *allocator, const lxa_limits_t *limits,
                            lxa_span_t input, const lxa_html_extract_options_t *options,
                            const lxa_html_source_target_t *targets, size_t count,
                            lxa_html_source_rewrite_callback_t callback, void *context,
                            lxa_write_f write, void *write_context,
                            lxa_html_result_t *parser_result, lxa_limit_reason_t *reason)
{
    rewrite_t owner = {.allocator = allocator,
                       .input = input,
                       .callback = callback,
                       .context = context,
                       .write = write,
                       .write_context = write_context,
                       .reason = LXA_BUDGET_COUNT};
    lxa_html_result_t result = LXA_HTML_BAD_ARGUMENT;
    lxa_limit_reason_t source_reason = LXA_BUDGET_COUNT;
    lxa_status_t status = LXA_ERROR_ARGUMENT;
    if (parser_result != NULL)
        *parser_result = result;
    if (reason != NULL)
        *reason = LXA_BUDGET_COUNT;
    if (allocator == NULL || allocator->allocate == NULL || allocator->resize == NULL ||
        allocator->release == NULL || write == NULL || (count != 0 && callback == NULL))
        return status;
    status = lxa_budget_init(&owner.budget, limits);
    if (status == LXA_OK)
        status = lxa_buffer_init(&owner.syntax, allocator);
    if (status == LXA_OK)
        status = lxa_buffer_bind_budget(&owner.syntax, &owner.budget);
    if (status == LXA_OK)
        status = lxa_html_extract_attributes_with_budget(
            allocator, &owner.budget, input, options, targets, count, rewrite, &owner,
            &result, &source_reason);
    if (status == LXA_OK)
        status = publish(
            &owner, (lxa_span_t){input.data == NULL ? NULL : input.data + owner.cursor,
                                 input.length - owner.cursor});
    if (owner.callback_failed) {
        result = LXA_HTML_CALLBACK_FAILURE;
        source_reason = LXA_BUDGET_COUNT;
    } else if (owner.operation_failed || status != LXA_OK) {
        result = lxa_html_source_result(status);
        if (owner.operation_failed)
            source_reason = owner.reason;
    }
    lxa_encoder_destroy(owner.encoder);
    lxa_decoder_destroy(owner.decoder);
    lxa_buffer_destroy(&owner.syntax);
    if (parser_result != NULL)
        *parser_result = result;
    if (reason != NULL)
        *reason = source_reason;
    return status;
}
