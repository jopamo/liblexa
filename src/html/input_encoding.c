#include "html/input_encoding.h"

static lxa_html_input_result_t
input_status(lxa_status_t status)
{
    switch (status) {
    case LXA_OK: return LXA_HTML_INPUT_OK;
    case LXA_ERROR_LIMIT: return LXA_HTML_INPUT_LIMIT;
    case LXA_ERROR_NO_MEMORY: return LXA_HTML_INPUT_NO_MEMORY;
    case LXA_ERROR_OVERFLOW: return LXA_HTML_INPUT_OVERFLOW;
    default: return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
}

static lxa_html_input_result_t
append_scalars(lxa_buffer_t *output, const uint32_t *scalars, size_t count,
               lxa_budget_t *budget, lxa_limit_reason_t *reason)
{
    uint8_t bytes[256 * 4];
    size_t i, length = 0;
    if (count > sizeof(bytes) / 4) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    lxa_status_t status = lxa_budget_charge(budget, LXA_BUDGET_WORK,
                                            count, reason);
    if (status != LXA_OK) {
        return input_status(status);
    }
    for (i = 0; i < count; i++) {
        uint32_t cp = scalars[i];
        if (cp < 0x80) {
            bytes[length++] = (uint8_t) cp;
        }
        else if (cp < 0x800) {
            bytes[length++] = (uint8_t) (0xc0 | (cp >> 6));
            bytes[length++] = (uint8_t) (0x80 | (cp & 0x3f));
        }
        else if (cp < 0x10000) {
            bytes[length++] = (uint8_t) (0xe0 | (cp >> 12));
            bytes[length++] = (uint8_t) (0x80 | ((cp >> 6) & 0x3f));
            bytes[length++] = (uint8_t) (0x80 | (cp & 0x3f));
        }
        else {
            bytes[length++] = (uint8_t) (0xf0 | (cp >> 18));
            bytes[length++] = (uint8_t) (0x80 | ((cp >> 12) & 0x3f));
            bytes[length++] = (uint8_t) (0x80 | ((cp >> 6) & 0x3f));
            bytes[length++] = (uint8_t) (0x80 | (cp & 0x3f));
        }
    }
    if (length != 0) {
        status = lxa_buffer_append(output, (lxa_span_t) {bytes, length});
        if (status != LXA_OK) {
            if (status == LXA_ERROR_LIMIT && reason != NULL) {
                *reason = LXA_BUDGET_MEMORY;
            }
            return input_status(status);
        }
    }
    return LXA_HTML_INPUT_OK;
}

static lxa_html_input_result_t
report_malformed(lxa_span_t bytes, const lxa_encoding_t *encoding,
                 size_t prefix_length, lxa_budget_t *budget,
                 const lxa_allocator_t *allocator,
                 lxa_html_input_error_callback_t on_error, void *context,
                 lxa_status_t *callback_status, lxa_limit_reason_t *reason,
                 size_t expected)
{
    lxa_decoder_t *decoder = NULL;
    lxa_status_t status;
    lxa_html_input_result_t result = LXA_HTML_INPUT_OK;
    const lxa_encoding_t *utf8 = lxa_encoding_lookup((lxa_span_t) {
        (const uint8_t *) "utf-8", 5
    });
    const lxa_encoding_t *utf16le = lxa_encoding_lookup((lxa_span_t) {
        (const uint8_t *) "utf-16le", 8
    });
    const lxa_encoding_t *utf16be = lxa_encoding_lookup((lxa_span_t) {
        (const uint8_t *) "utf-16be", 8
    });
    bool utf16 = encoding == utf16le || encoding == utf16be;
    size_t i, pending_start = prefix_length, reported = 0;
    bool pending = false;
    uint32_t scalars[4];
    lxa_encoding_progress_t progress;
    lxa_encoding_result_t decode_result;
    /* A work-limit failure must not publish only a prefix of the errors. */
    status = lxa_budget_check(budget, LXA_BUDGET_WORK,
                              bytes.length - prefix_length, reason);
    if (status != LXA_OK) {
        return input_status(status);
    }
    status = lxa_decoder_create_with_budget(encoding, LXA_ENCODING_REPLACE,
                                             allocator, budget, &decoder);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT && reason != NULL) {
            *reason = LXA_BUDGET_MEMORY;
        }
        return input_status(status);
    }
    for (i = prefix_length; i < bytes.length; i++) {
        size_t before = lxa_decoder_error_count(decoder);
        size_t errors, j;
        lxa_span_t byte = {bytes.data + i, 1};
        status = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, reason);
        if (status != LXA_OK) {
            result = input_status(status);
            goto done;
        }
        decode_result = lxa_decoder_feed(decoder, byte, scalars, 4, &progress);
        if (decode_result != LXA_ENCODING_OK || progress.consumed != 1) {
            result = LXA_HTML_INPUT_BAD_ARGUMENT;
            goto done;
        }
        errors = lxa_decoder_error_count(decoder) - before;
        for (j = 0; j < errors; j++) {
            size_t at = j == 0 && pending ? pending_start
                      : utf16 && i != prefix_length ? i - 1 : i;
            status = on_error(context, at);
            if (status != LXA_OK) {
                if (callback_status != NULL) {
                    *callback_status = status;
                }
                result = LXA_HTML_INPUT_CALLBACK_FAILURE;
                goto done;
            }
            reported++;
        }
        if (progress.produced != 0) {
            pending = false;
        }
        else if (!pending) {
            pending_start = i;
            pending = true;
        }
        /* An invalid UTF-8 continuation may also begin a new prefix. */
        if (errors != 0 && encoding == utf8
            && bytes.data[i] >= 0xc2 && bytes.data[i] <= 0xf4) {
            pending_start = i;
            pending = true;
        }
        if (errors != 0 && utf16 && progress.produced != 0
            && i > prefix_length && ((i - prefix_length) & 1) != 0) {
            uint16_t unit = encoding == utf16be
                ? ((uint16_t) bytes.data[i - 1] << 8) | bytes.data[i]
                : ((uint16_t) bytes.data[i] << 8) | bytes.data[i - 1];
            if (unit >= 0xd800 && unit <= 0xdbff) {
                pending_start = i - 1;
                pending = true;
            }
        }
    }
    {
        size_t before = lxa_decoder_error_count(decoder);
        decode_result = lxa_decoder_finish(decoder, scalars, 4, &progress);
        if (decode_result != LXA_ENCODING_OK) {
            result = LXA_HTML_INPUT_BAD_ARGUMENT;
            goto done;
        }
        for (i = before; i < lxa_decoder_error_count(decoder); i++) {
            status = on_error(context, pending ? pending_start : bytes.length);
            if (status != LXA_OK) {
                if (callback_status != NULL) {
                    *callback_status = status;
                }
                result = LXA_HTML_INPUT_CALLBACK_FAILURE;
                goto done;
            }
            reported++;
        }
    }
    if (reported != expected) {
        result = LXA_HTML_INPUT_BAD_ARGUMENT;
    }
done:
    lxa_decoder_destroy(decoder);
    return result;
}

static lxa_html_input_result_t
decode_input(lxa_span_t bytes, const lxa_encoding_t *encoding,
             size_t prefix_length, lxa_budget_t *budget,
             lxa_buffer_t *output, size_t *malformed,
             lxa_limit_reason_t *reason,
             lxa_html_input_error_callback_t on_error,
             void *context, lxa_status_t *callback_status, bool prepaid)
{
    lxa_decoder_t *decoder = NULL;
    lxa_buffer_t temporary;
    lxa_status_t status;
    lxa_html_input_result_t result;
    size_t offset;
    uint32_t scalars[256];
    lxa_encoding_progress_t progress;
    lxa_encoding_result_t decode_result;
    if (malformed != NULL) {
        *malformed = 0;
    }
    if (reason != NULL) {
        *reason = LXA_BUDGET_COUNT;
    }
    if (callback_status != NULL) {
        *callback_status = LXA_OK;
    }
    if (bytes.length != 0 && bytes.data == NULL) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    if (encoding == NULL || budget == NULL
        || output == NULL || malformed == NULL || prefix_length > bytes.length
        || output->budget != budget || output->length != 0
        || output->capacity != 0 || output->data != NULL) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    if (prepaid) {
        /* The retry must not mint fresh work credit or hide uncharged input. */
        if (budget->used[LXA_BUDGET_INPUT] != bytes.length) {
            return LXA_HTML_INPUT_BAD_ARGUMENT;
        }
    }
    else {
        status = lxa_budget_accept_input(budget, bytes.length, reason);
        if (status != LXA_OK) {
            return input_status(status);
        }
    }
    status = lxa_buffer_init(&temporary, &output->allocator);
    if (status != LXA_OK) {
        return input_status(status);
    }
    status = lxa_buffer_bind_budget(&temporary, budget);
    if (status != LXA_OK) {
        lxa_buffer_destroy(&temporary);
        return input_status(status);
    }
    status = lxa_decoder_create_with_budget(encoding, LXA_ENCODING_REPLACE,
                                             &output->allocator, budget,
                                             &decoder);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT && reason != NULL) {
            *reason = LXA_BUDGET_MEMORY;
        }
        result = input_status(status);
        goto done;
    }
    offset = prefix_length;
    do {
        lxa_span_t remaining = {
            bytes.data == NULL ? NULL : bytes.data + offset,
            bytes.length - offset
        };
        decode_result = lxa_decoder_feed(decoder, remaining, scalars, 256,
                                          &progress);
        offset += progress.consumed;
        status = lxa_budget_charge(budget, LXA_BUDGET_WORK,
                                   progress.consumed, reason);
        if (status != LXA_OK) {
            result = input_status(status);
            goto done;
        }
        result = append_scalars(&temporary, scalars, progress.produced, budget,
                                reason);
        if (result != LXA_HTML_INPUT_OK) {
            goto done;
        }
        if (decode_result != LXA_ENCODING_OK
            && decode_result != LXA_ENCODING_OUTPUT_FULL) {
            result = LXA_HTML_INPUT_BAD_ARGUMENT;
            goto done;
        }
    } while (offset != bytes.length || decode_result == LXA_ENCODING_OUTPUT_FULL);
    do {
        decode_result = lxa_decoder_finish(decoder, scalars, 256, &progress);
        result = append_scalars(&temporary, scalars, progress.produced, budget,
                                reason);
        if (result != LXA_HTML_INPUT_OK) {
            goto done;
        }
        if (decode_result != LXA_ENCODING_OK
            && decode_result != LXA_ENCODING_OUTPUT_FULL) {
            result = LXA_HTML_INPUT_BAD_ARGUMENT;
            goto done;
        }
    } while (decode_result == LXA_ENCODING_OUTPUT_FULL);
    if (on_error != NULL && lxa_decoder_error_count(decoder) != 0) {
        result = report_malformed(bytes, encoding, prefix_length, budget,
                                  &output->allocator, on_error, context,
                                  callback_status, reason,
                                  lxa_decoder_error_count(decoder));
        if (result != LXA_HTML_INPUT_OK) {
            goto done;
        }
    }
    *malformed = lxa_decoder_error_count(decoder);
    *output = temporary;
    temporary.data = NULL;
    temporary.length = temporary.capacity = 0;
    result = LXA_HTML_INPUT_OK;
done:
    lxa_decoder_destroy(decoder);
    lxa_buffer_destroy(&temporary);
    return result;
}

lxa_html_input_result_t
lxa_html_input_decode_report(lxa_span_t bytes, const lxa_encoding_t *encoding,
                             size_t prefix_length, lxa_budget_t *budget,
                             lxa_buffer_t *output, size_t *malformed,
                             lxa_limit_reason_t *reason,
                             lxa_html_input_error_callback_t on_error,
                             void *context, lxa_status_t *callback_status)
{
    return decode_input(bytes, encoding, prefix_length, budget, output,
                        malformed, reason, on_error, context, callback_status,
                        false);
}

lxa_html_input_result_t
lxa_html_input_decode_prepaid_report(
    lxa_span_t bytes, const lxa_encoding_t *encoding, size_t prefix_length,
    lxa_budget_t *budget, lxa_buffer_t *output, size_t *malformed,
    lxa_limit_reason_t *reason, lxa_html_input_error_callback_t on_error,
    void *context, lxa_status_t *callback_status)
{
    return decode_input(bytes, encoding, prefix_length, budget, output,
                        malformed, reason, on_error, context, callback_status,
                        true);
}

lxa_html_input_result_t
lxa_html_input_report_malformed(
    lxa_span_t bytes, const lxa_encoding_t *encoding, size_t prefix_length,
    size_t malformed, lxa_budget_t *budget, const lxa_allocator_t *allocator,
    lxa_html_input_error_callback_t on_error, void *context,
    lxa_status_t *callback_status, lxa_limit_reason_t *reason)
{
    if (reason != NULL) {
        *reason = LXA_BUDGET_COUNT;
    }
    if (callback_status != NULL) {
        *callback_status = LXA_OK;
    }
    if ((bytes.length != 0 && bytes.data == NULL) || encoding == NULL
        || prefix_length > bytes.length || budget == NULL
        || budget->used[LXA_BUDGET_INPUT] != bytes.length
        || allocator == NULL || allocator->allocate == NULL
        || allocator->resize == NULL || allocator->release == NULL
        || (malformed != 0 && on_error == NULL)) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    if (malformed == 0) {
        return LXA_HTML_INPUT_OK;
    }
    return report_malformed(bytes, encoding, prefix_length, budget, allocator,
                            on_error, context, callback_status, reason,
                            malformed);
}

lxa_html_input_result_t
lxa_html_input_decode(lxa_span_t bytes, const lxa_encoding_t *encoding,
                      size_t prefix_length, lxa_budget_t *budget,
                      lxa_buffer_t *output, size_t *malformed,
                      lxa_limit_reason_t *reason)
{
    return lxa_html_input_decode_report(bytes, encoding, prefix_length, budget,
                                         output, malformed, reason, NULL,
                                         NULL, NULL);
}

static lxa_html_input_result_t
resolve_label(lxa_span_t label, const lxa_encoding_t **encoding)
{
    const lxa_encoding_t *selected;
    if (encoding == NULL) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    *encoding = NULL;
    if (label.length != 0 && label.data == NULL) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    selected = lxa_encoding_lookup(label);
    if (selected == NULL) {
        return LXA_HTML_INPUT_UNKNOWN_ENCODING;
    }
    if (selected == lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-16le", 8
        }) || selected == lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-16be", 8
        })) {
        selected = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-8", 5
        });
    }
    *encoding = selected;
    return LXA_HTML_INPUT_OK;
}

lxa_html_input_result_t
lxa_html_input_resolve_label(lxa_span_t label, const lxa_encoding_t **encoding)
{
    lxa_html_input_result_t result = resolve_label(label, encoding);
    if (result == LXA_HTML_INPUT_OK
        && *encoding == lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "x-user-defined", 14
        })) {
        *encoding = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "windows-1252", 12
        });
    }
    return result;
}

static bool
space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static uint8_t
lower(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? (uint8_t) (c + 'a' - 'A') : c;
}

static bool
equals(const uint8_t *s, size_t length, const char *literal, size_t expected)
{
    size_t i;
    if (length != expected) {
        return false;
    }
    for (i = 0; i < length; i++) {
        if (lower(s[i]) != (uint8_t) literal[i]) {
            return false;
        }
    }
    return true;
}

lxa_status_t
lxa_html_input_content_charset(lxa_span_t content, bool *found,
                               lxa_span_t *label)
{
    size_t i;
    if (found == NULL || label == NULL
        || (content.length != 0 && content.data == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    *found = false;
    *label = (lxa_span_t) {NULL, 0};
    if (content.length < 7) {
        return LXA_OK;
    }
    for (i = 0; i <= content.length - 7; i++) {
        size_t pos, start;
        if (!equals(content.data + i, 7, "charset", 7)) {
            continue;
        }
        pos = i + 7;
        while (pos < content.length && space(content.data[pos])) {
            pos++;
        }
        if (pos == content.length || content.data[pos] != '=') {
            continue;
        }
        pos++;
        while (pos < content.length && space(content.data[pos])) {
            pos++;
        }
        if (pos == content.length) {
            return LXA_OK;
        }
        if (content.data[pos] == '"' || content.data[pos] == '\'') {
            uint8_t quote = content.data[pos++];
            start = pos;
            while (pos < content.length && content.data[pos] != quote) {
                pos++;
            }
            if (pos == content.length) {
                return LXA_OK;
            }
        }
        else {
            start = pos;
            while (pos < content.length && !space(content.data[pos])
                   && content.data[pos] != ';') {
                pos++;
            }
        }
        *label = (lxa_span_t) {content.data + start, pos - start};
        *found = true;
        return LXA_OK;
    }
    return LXA_OK;
}

lxa_status_t
lxa_html_input_bom(lxa_span_t bytes, const lxa_encoding_t **encoding,
                   size_t *prefix_length)
{
    if (encoding == NULL || prefix_length == NULL
        || (bytes.length != 0 && bytes.data == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    *encoding = NULL;
    *prefix_length = 0;
    if (bytes.length >= 3 && bytes.data[0] == 0xef
        && bytes.data[1] == 0xbb && bytes.data[2] == 0xbf) {
        *encoding = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-8", 5
        });
        *prefix_length = 3;
    }
    else if (bytes.length >= 2 && bytes.data[0] == 0xfe
             && bytes.data[1] == 0xff) {
        *encoding = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-16be", 8
        });
        *prefix_length = 2;
    }
    else if (bytes.length >= 2 && bytes.data[0] == 0xff
             && bytes.data[1] == 0xfe) {
        *encoding = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-16le", 8
        });
        *prefix_length = 2;
    }
    return LXA_OK;
}

lxa_status_t
lxa_html_input_meta_charset(lxa_span_t bytes, bool *found, lxa_span_t *label)
{
    size_t end, i;
    if (found == NULL || label == NULL
        || (bytes.length != 0 && bytes.data == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    *found = false;
    *label = (lxa_span_t) {NULL, 0};
    end = bytes.length < 1024 ? bytes.length : 1024;
    for (i = 0; i < end; i++) {
        size_t pos, name_start;
        uint8_t quote = 0;
        if (bytes.data[i] != '<' || i + 1 == end) {
            continue;
        }
        if (end - i >= 4 && bytes.data[i + 1] == '!'
            && bytes.data[i + 2] == '-' && bytes.data[i + 3] == '-') {
            /* The terminator can reuse the opening hyphens. */
            for (i += 2; i + 2 < end; i++) {
                if (bytes.data[i] == '-' && bytes.data[i + 1] == '-'
                    && bytes.data[i + 2] == '>') {
                    i += 2;
                    break;
                }
            }
            if (i + 2 >= end) {
                break;
            }
            continue;
        }
        pos = i + 1;
        if (end - pos >= 4
            && equals(bytes.data + pos, 4, "meta", 4)
            && pos + 4 < end
            && (space(bytes.data[pos + 4]) || bytes.data[pos + 4] == '/')) {
            bool got_pragma = false, seen_pragma = false;
            bool seen_content = false;
            bool content_found = false, charset_found = false;
            lxa_span_t content_label = {NULL, 0}, charset_label = {NULL, 0};
            pos += 4;
            while (pos < end) {
                size_t value_start = pos, value_length = 0;
                while (pos < end
                       && (space(bytes.data[pos]) || bytes.data[pos] == '/')) {
                    pos++;
                }
                if (pos == end || bytes.data[pos] == '>') {
                    break;
                }
                name_start = pos;
                /* A leading '=' belongs to the attribute name. */
                pos++;
                while (pos < end && !space(bytes.data[pos])
                       && bytes.data[pos] != '=' && bytes.data[pos] != '/'
                       && bytes.data[pos] != '>') {
                    pos++;
                }
                {
                    size_t name_length = pos - name_start;
                    while (pos < end && space(bytes.data[pos])) {
                        pos++;
                    }
                    if (pos < end && bytes.data[pos] == '=') {
                        pos++;
                        while (pos < end && space(bytes.data[pos])) {
                            pos++;
                        }
                        if (pos == end) {
                            break;
                        }
                        if (bytes.data[pos] == '"' || bytes.data[pos] == '\'') {
                            quote = bytes.data[pos++];
                            value_start = pos;
                            while (pos < end && bytes.data[pos] != quote) {
                                pos++;
                            }
                            if (pos == end) {
                                break;
                            }
                            value_length = pos++ - value_start;
                        }
                        else {
                            value_start = pos;
                            while (pos < end && !space(bytes.data[pos])
                                   && bytes.data[pos] != '>') {
                                pos++;
                            }
                            value_length = pos - value_start;
                        }
                    }
                    if (equals(bytes.data + name_start, name_length,
                               "charset", 7) && !charset_found) {
                        charset_found = true;
                        charset_label = (lxa_span_t) {
                            bytes.data + value_start, value_length
                        };
                    }
                    else if (equals(bytes.data + name_start, name_length,
                                    "http-equiv", 10)) {
                        if (!seen_pragma) {
                            got_pragma = equals(bytes.data + value_start,
                                                value_length,
                                                "content-type", 12);
                            seen_pragma = true;
                        }
                    }
                    else if (equals(bytes.data + name_start, name_length,
                                    "content", 7) && !seen_content) {
                        lxa_span_t value = {bytes.data + value_start,
                                            value_length};
                        lxa_status_t status = lxa_html_input_content_charset(
                            value, &content_found, &content_label);
                        if (status != LXA_OK) {
                            return status;
                        }
                        seen_content = true;
                    }
                }
            }
            if (pos < end && bytes.data[pos] == '>'
                && (charset_found || (got_pragma && content_found))) {
                *found = true;
                *label = charset_found ? charset_label : content_label;
                return LXA_OK;
            }
            i = pos;
            if (i >= end) {
                break;
            }
            continue;
        }
        /* A quoted '>' in another tag is not a new declaration. */
        if (bytes.data[pos] == '/' || bytes.data[pos] == '!'
            || bytes.data[pos] == '?'
            || (lower(bytes.data[pos]) >= 'a'
                && lower(bytes.data[pos]) <= 'z')) {
            for (; pos < end; pos++) {
                uint8_t c = bytes.data[pos];
                if (quote != 0) {
                    if (c == quote) {
                        quote = 0;
                    }
                }
                else if (c == '"' || c == '\'') {
                    quote = c;
                }
                else if (c == '>') {
                    break;
                }
            }
            i = pos;
            if (i >= end) {
                break;
            }
        }
    }
    return LXA_OK;
}

lxa_status_t
lxa_html_input_xml_encoding(lxa_span_t bytes, bool *found, lxa_span_t *label)
{
    static const char xml[] = "<?xml";
    static const char keyword[] = "encoding";
    size_t end, i, pos, start;
    uint8_t quote;
    if (found == NULL || label == NULL
        || (bytes.length != 0 && bytes.data == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    *found = false;
    *label = (lxa_span_t) {NULL, 0};
    if (bytes.length < sizeof(xml) - 1) {
        return LXA_OK;
    }
    for (i = 0; i < sizeof(xml) - 1; i++) {
        if (bytes.data[i] != (uint8_t) xml[i]) {
            return LXA_OK;
        }
    }
    for (end = sizeof(xml) - 1; end < bytes.length; end++) {
        if (bytes.data[end] == '>') {
            break;
        }
    }
    if (end == bytes.length) {
        return LXA_OK;
    }
    for (i = 0; i < end && end - i >= sizeof(keyword) - 1; i++) {
        size_t j;
        for (j = 0; j < sizeof(keyword) - 1; j++) {
            if (bytes.data[i + j] != (uint8_t) keyword[j]) {
                break;
            }
        }
        if (j != sizeof(keyword) - 1) {
            continue;
        }
        pos = i + j;
        while (pos < end && bytes.data[pos] <= 0x20) {
            pos++;
        }
        if (pos == end || bytes.data[pos++] != '=') {
            return LXA_OK;
        }
        while (pos < end && bytes.data[pos] <= 0x20) {
            pos++;
        }
        if (pos == end
            || (bytes.data[pos] != '"' && bytes.data[pos] != '\'')) {
            return LXA_OK;
        }
        quote = bytes.data[pos++];
        start = pos;
        /* Only the keyword search is bounded by the first '>'. */
        while (pos < bytes.length && bytes.data[pos] != quote) {
            if (bytes.data[pos] <= 0x20) {
                return LXA_OK;
            }
            pos++;
        }
        if (pos == bytes.length) {
            return LXA_OK;
        }
        *label = (lxa_span_t) {bytes.data + start, pos - start};
        *found = true;
        return LXA_OK;
    }
    return LXA_OK;
}

lxa_html_input_result_t
lxa_html_input_select(lxa_span_t bytes, size_t max_input_bytes,
                      const lxa_encoding_t **encoding, size_t *prefix_length)
{
    bool found;
    lxa_span_t label;
    if (encoding == NULL || prefix_length == NULL) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    *encoding = NULL;
    *prefix_length = 0;
    if (bytes.length != 0 && bytes.data == NULL) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    if (bytes.length > max_input_bytes) {
        return LXA_HTML_INPUT_LIMIT;
    }
    if (lxa_html_input_bom(bytes, encoding, prefix_length) != LXA_OK) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    if (*encoding != NULL) {
        return LXA_HTML_INPUT_OK;
    }
    /* HTML's prescan recognizes initial UTF-16 XML byte patterns without
     * a BOM, before looking for an ASCII meta declaration. */
    if (bytes.length >= 6
        && bytes.data[0] == '<' && bytes.data[1] == 0
        && bytes.data[2] == '?' && bytes.data[3] == 0
        && bytes.data[4] == 'x' && bytes.data[5] == 0) {
        *encoding = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-16le", 8
        });
        return LXA_HTML_INPUT_OK;
    }
    if (bytes.length >= 6
        && bytes.data[0] == 0 && bytes.data[1] == '<'
        && bytes.data[2] == 0 && bytes.data[3] == '?'
        && bytes.data[4] == 0 && bytes.data[5] == 'x') {
        *encoding = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-16be", 8
        });
        return LXA_HTML_INPUT_OK;
    }
    if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    if (found) {
        return lxa_html_input_resolve_label(label, encoding);
    }
    if (lxa_html_input_xml_encoding(bytes, &found, &label) != LXA_OK) {
        return LXA_HTML_INPUT_BAD_ARGUMENT;
    }
    if (!found) {
        *encoding = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-8", 5
        });
        return LXA_HTML_INPUT_OK;
    }
    return resolve_label(label, encoding);
}
