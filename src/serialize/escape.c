/* Copyright (C) 2026 Paul Moses */
#include "liblexa/serialize.h"

lxa_status_t
lxa_write_all(lxa_span_t bytes, lxa_write_f write, void *context)
{
    if (write == NULL || (bytes.data == NULL && bytes.length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    while (bytes.length != 0) {
        lxa_write_result_t result = write(context, bytes);
        if (result.consumed > bytes.length) {
            return LXA_ERROR_ARGUMENT;
        }
        if (result.status != LXA_OK) {
            return result.status;
        }
        if (result.consumed == 0) {
            return LXA_ERROR_ARGUMENT;
        }
        bytes.data += result.consumed;
        bytes.length -= result.consumed;
    }
    return LXA_OK;
}

lxa_status_t
lxa_html_escape(lxa_span_t input, lxa_escape_context_t mode,
                lxa_write_f write, void *context)
{
    size_t i, start, width, replacement_length;
    const char *replacement;
    lxa_status_t status;
    lxa_span_t part;

    if (write == NULL || (input.data == NULL && input.length != 0)
        || (mode != LXA_ESCAPE_TEXT && mode != LXA_ESCAPE_ATTRIBUTE)) {
        return LXA_ERROR_ARGUMENT;
    }
    start = 0;
    for (i = 0; i < input.length; i += width) {
        width = 1;
        replacement = NULL;
        replacement_length = 0;
        switch (input.data[i]) {
            case '&':
                replacement = "&amp;";
                replacement_length = 5;
                break;
            case '<':
                replacement = "&lt;";
                replacement_length = 4;
                break;
            case '>':
                replacement = "&gt;";
                replacement_length = 4;
                break;
            case '"':
                if (mode == LXA_ESCAPE_ATTRIBUTE) {
                    replacement = "&quot;";
                    replacement_length = 6;
                }
                break;
            case 0xc2:
                if (input.length - i >= 2 && input.data[i + 1] == 0xa0) {
                    replacement = "&nbsp;";
                    replacement_length = 6;
                    width = 2;
                }
                break;
        }
        if (replacement == NULL) {
            continue;
        }
        part.data = input.data + start;
        part.length = i - start;
        status = lxa_write_all(part, write, context);
        if (status != LXA_OK) {
            return status;
        }
        part.data = (const uint8_t *) replacement;
        part.length = replacement_length;
        status = lxa_write_all(part, write, context);
        if (status != LXA_OK) {
            return status;
        }
        start = i + width;
    }
    part.data = input.data == NULL ? NULL : input.data + start;
    part.length = input.length - start;
    return lxa_write_all(part, write, context);
}
