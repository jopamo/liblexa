/* Copyright (C) 2026 Paul Moses */
#include <assert.h>
#include "css/scan.h"
#include "css/filter.h"
#include "liblexa/encoding.h"

enum { CSS_MAX_BYTES = 1024 * 1024 };

struct lxa_css_byte_stream {
    lxa_allocator_t allocator;
    lxa_decoder_t *decoder;
    lxa_css_filter_t filter;
    lxa_vector_t units;
    size_t position;
    size_t byte_start;
    size_t cr_start;
    size_t cr_end;
    lxa_css_scan_cursor_t cursor;
    lxa_budget_t local_budget;
    lxa_budget_t *budget;
    lxa_budget_kind_t limit_reason;
    bool sealed;
    bool closed;
};

static lxa_css_scan_result_t
append(lxa_css_byte_stream_t *stream, uint32_t cp, size_t start, size_t end)
{
    lxa_css_unit_t unit = {cp, start, end - start};
    lxa_status_t status = lxa_vector_push(&stream->units, &unit);
    if (status == LXA_ERROR_LIMIT) {
        stream->limit_reason = LXA_BUDGET_MEMORY;
    }
    return status == LXA_OK ? LXA_CSS_SCAN_OK
           : status == LXA_ERROR_LIMIT ? LXA_CSS_SCAN_LIMIT
           : status == LXA_ERROR_OVERFLOW ? LXA_CSS_SCAN_OVERFLOW
           : LXA_CSS_SCAN_NO_MEMORY;
}

static lxa_css_scan_result_t
accept(lxa_css_byte_stream_t *stream, uint32_t cp, size_t start, size_t end)
{
    uint32_t output[2];
    lxa_css_filter_progress_t progress;
    bool had_cr = stream->filter.pending_cr;
    lxa_css_scan_result_t result;
    lxa_css_filter_result_t r = lxa_css_filter_feed(&stream->filter, &cp, 1,
        output, 2, &progress);
    size_t j = 0;
    if (r != LXA_CSS_FILTER_OK || progress.consumed != 1) {
        return LXA_CSS_SCAN_INVALID;
    }
    if (had_cr) {
        size_t cr_end = cp == '\n' ? end : stream->cr_end;
        result = append(stream, '\n', stream->cr_start, cr_end);
        if (result != LXA_CSS_SCAN_OK) {
            return result;
        }
        j++;
    }
    if (cp == '\r') {
        stream->cr_start = start;
        stream->cr_end = end;
    }
    else if (!(had_cr && cp == '\n')) {
        result = append(stream, output[j], start, end);
        if (result != LXA_CSS_SCAN_OK) {
            return result;
        }
        j++;
    }
    return j == progress.produced ? LXA_CSS_SCAN_OK : LXA_CSS_SCAN_INVALID;
}

lxa_css_scan_result_t
lxa_css_byte_stream_create_budget(const lxa_allocator_t *allocator,
                                  lxa_budget_t *budget,
                                  lxa_css_byte_stream_t **out)
{
    lxa_css_byte_stream_t *stream;
    lxa_budget_t local_budget;
    lxa_budget_t *active = budget;
    lxa_status_t status;
    const lxa_encoding_t *utf8;
    if (out == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    *out = NULL;
    if (allocator == NULL || allocator->allocate == NULL
        || allocator->resize == NULL || allocator->release == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    if (active == NULL) {
        lxa_budget_init(&local_budget, NULL);
        active = &local_budget;
    }
    status = lxa_budget_charge(active, LXA_BUDGET_MEMORY, sizeof(*stream), NULL);
    if (status != LXA_OK) {
        return status == LXA_ERROR_LIMIT ? LXA_CSS_SCAN_LIMIT
               : status == LXA_ERROR_OVERFLOW ? LXA_CSS_SCAN_OVERFLOW
               : LXA_CSS_SCAN_INVALID;
    }
    status = lxa_alloc(allocator, sizeof(*stream), (void **) &stream);
    if (status != LXA_OK) {
        lxa_budget_release(active, LXA_BUDGET_MEMORY, sizeof(*stream));
        return status == LXA_ERROR_NO_MEMORY ? LXA_CSS_SCAN_NO_MEMORY
               : LXA_CSS_SCAN_INVALID;
    }
    *stream = (lxa_css_byte_stream_t) {0};
    stream->allocator = *allocator;
    if (budget == NULL) {
        stream->local_budget = local_budget;
    }
    stream->budget = budget == NULL ? &stream->local_budget : budget;
    stream->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_vector_init(&stream->units, allocator, sizeof(lxa_css_unit_t));
    if (status != LXA_OK) {
        lxa_budget_release(stream->budget, LXA_BUDGET_MEMORY, sizeof(*stream));
        lxa_free(allocator, stream);
        return LXA_CSS_SCAN_INVALID;
    }
    status = lxa_vector_bind_budget(&stream->units, stream->budget);
    if (status != LXA_OK) {
        lxa_vector_destroy(&stream->units);
        lxa_budget_release(stream->budget, LXA_BUDGET_MEMORY, sizeof(*stream));
        lxa_free(allocator, stream);
        return LXA_CSS_SCAN_INVALID;
    }
    lxa_css_filter_reset(&stream->filter);
    utf8 = lxa_encoding_lookup((lxa_span_t) {(const uint8_t *) "utf-8", 5});
    status = lxa_decoder_create_with_budget(utf8, LXA_ENCODING_REPLACE,
                                            allocator, stream->budget,
                                            &stream->decoder);
    if (status != LXA_OK) {
        lxa_vector_destroy(&stream->units);
        lxa_budget_release(stream->budget, LXA_BUDGET_MEMORY, sizeof(*stream));
        lxa_free(allocator, stream);
        return status == LXA_ERROR_LIMIT ? LXA_CSS_SCAN_LIMIT
               : status == LXA_ERROR_OVERFLOW ? LXA_CSS_SCAN_OVERFLOW
               : status == LXA_ERROR_NO_MEMORY ? LXA_CSS_SCAN_NO_MEMORY
               : LXA_CSS_SCAN_INVALID;
    }
    *out = stream;
    return LXA_CSS_SCAN_OK;
}

lxa_css_scan_result_t
lxa_css_byte_stream_create(const lxa_allocator_t *allocator,
                           lxa_css_byte_stream_t **out)
{
    return lxa_css_byte_stream_create_budget(allocator, NULL, out);
}

lxa_budget_kind_t
lxa_css_byte_stream_limit_reason(const lxa_css_byte_stream_t *stream)
{
    return stream == NULL ? LXA_BUDGET_COUNT : stream->limit_reason;
}

lxa_css_scan_result_t
lxa_css_byte_stream_feed(lxa_css_byte_stream_t *stream,
                         lxa_span_t bytes, size_t *consumed)
{
    lxa_encoding_progress_t progress;
    lxa_css_scan_result_t result;
    lxa_encoding_result_t dec_result;
    uint32_t cp;
    if (consumed == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    *consumed = 0;
    if (stream == NULL || stream->closed || stream->sealed
        || (bytes.data == NULL && bytes.length != 0)) {
        return LXA_CSS_SCAN_INVALID;
    }
    stream->limit_reason = LXA_BUDGET_COUNT;
    if (bytes.length > CSS_MAX_BYTES - stream->position) {
        stream->limit_reason = LXA_BUDGET_INPUT;
        return LXA_CSS_SCAN_LIMIT;
    }
    /* Preflight the entire chunk (including work-credit arithmetic) before
     * advancing the decoder. Accepted bytes are charged individually below. */
    {
        lxa_budget_t prospective = *stream->budget;
        lxa_status_t check = lxa_budget_accept_input(&prospective,
            bytes.length, &stream->limit_reason);
        if (check != LXA_OK) {
            return check == LXA_ERROR_LIMIT ? LXA_CSS_SCAN_LIMIT
                   : check == LXA_ERROR_OVERFLOW ? LXA_CSS_SCAN_OVERFLOW
                   : LXA_CSS_SCAN_INVALID;
        }
    }
    while (*consumed < bytes.length) {
        lxa_status_t charge = lxa_budget_charge(stream->budget,
            LXA_BUDGET_WORK, 1, &stream->limit_reason);
        if (charge != LXA_OK) {
            stream->closed = true;
            return charge == LXA_ERROR_LIMIT ? LXA_CSS_SCAN_LIMIT
                   : charge == LXA_ERROR_OVERFLOW ? LXA_CSS_SCAN_OVERFLOW
                   : LXA_CSS_SCAN_INVALID;
        }
        dec_result = lxa_decoder_feed(stream->decoder,
            (lxa_span_t) {bytes.data + *consumed, 1}, &cp, 1, &progress);
        if ((dec_result != LXA_ENCODING_OK
             && dec_result != LXA_ENCODING_OUTPUT_FULL) || progress.consumed > 1
            || progress.produced > 1 || (!progress.consumed && !progress.produced)) {
            stream->closed = true;
            return LXA_CSS_SCAN_INVALID;
        }
        if (progress.produced) {
            size_t end = stream->position + progress.consumed;
            uint8_t current = bytes.data[*consumed];
            /* The shared decoder may emit a replacement for an incomplete
             * prefix and accept a new leading byte in the same call. That
             * byte begins the next scalar, rather than ending the old one.
             * Only the source boundary is decided here; scalar validity and
             * replacement remain entirely the decoder's responsibility. */
            if (cp == 0xfffd && progress.consumed == 1
                && stream->byte_start < stream->position
                && current >= 0xc2 && current <= 0xf4) {
                end = stream->position;
            }
            /* Initial BOM is a byte-order hint, not CSS input. */
            if (!(stream->byte_start == 0 && cp == 0xfeff)) {
                result = accept(stream, cp, stream->byte_start, end);
                if (result != LXA_CSS_SCAN_OK) {
                    /* The decoder has already accepted this input even if
                     * retaining the resulting unit failed. Work and accepted
                     * input are not refunded when the stream closes. */
                    if (progress.consumed != 0) {
                        lxa_budget_kind_t failure = stream->limit_reason;
                        lxa_budget_accept_input(stream->budget,
                            progress.consumed, NULL);
                        stream->limit_reason = failure;
                    }
                    stream->closed = true;
                    *consumed += progress.consumed;
                    stream->position += progress.consumed;
                    return result;
                }
            }
            stream->byte_start = end;
        }
        if (progress.consumed != 0) {
            lxa_status_t charged = lxa_budget_accept_input(stream->budget,
                progress.consumed, &stream->limit_reason);
            if (charged != LXA_OK) {
                stream->closed = true;
                *consumed += progress.consumed;
                stream->position += progress.consumed;
                return charged == LXA_ERROR_LIMIT ? LXA_CSS_SCAN_LIMIT
                       : charged == LXA_ERROR_OVERFLOW ? LXA_CSS_SCAN_OVERFLOW
                       : LXA_CSS_SCAN_INVALID;
            }
        }
        *consumed += progress.consumed;
        stream->position += progress.consumed;
    }
    return LXA_CSS_SCAN_OK;
}

lxa_css_scan_result_t
lxa_css_byte_stream_seal(lxa_css_byte_stream_t *stream)
{
    lxa_encoding_progress_t progress;
    lxa_css_scan_result_t result;
    uint32_t cp, out;
    lxa_css_filter_progress_t filtered;
    if (stream == NULL || stream->closed || stream->sealed) {
        return LXA_CSS_SCAN_INVALID;
    }
    stream->sealed = true;
    if (lxa_decoder_finish(stream->decoder, &cp, 1, &progress) != LXA_ENCODING_OK
        || progress.produced > 1) {
        stream->closed = true;
        return LXA_CSS_SCAN_INVALID;
    }
    if (progress.produced) {
        result = accept(stream, cp, stream->byte_start, stream->position);
        if (result != LXA_CSS_SCAN_OK) {
            stream->closed = true;
            return result;
        }
    }
    if (lxa_css_filter_finish(&stream->filter, &out, 1, &filtered)
        != LXA_CSS_FILTER_OK) {
        stream->closed = true;
        return LXA_CSS_SCAN_INVALID;
    }
    if (filtered.produced) {
        result = append(stream, out, stream->cr_start, stream->cr_end);
        if (result != LXA_CSS_SCAN_OK) {
            stream->closed = true;
            return result;
        }
    }
    result = lxa_css_scan_cursor_init_budget(&stream->cursor,
        (const lxa_css_unit_t *) stream->units.bytes.data,
        lxa_vector_length(&stream->units), stream->position, stream->budget);
    if (result != LXA_CSS_SCAN_OK) {
        if (result == LXA_CSS_SCAN_LIMIT) {
            stream->limit_reason = stream->cursor.limit_reason;
        }
        stream->closed = true;
    }
    return result;
}

/* External limits apply to both stream state and caller-retained output. */
static bool
tokens_share_budget(const lxa_css_byte_stream_t *stream,
                    const lxa_css_tokens_t *tokens)
{
    return stream->budget == &stream->local_budget
           || (tokens->text.budget == stream->budget
               && tokens->entries.bytes.budget == stream->budget);
}

lxa_css_scan_result_t
lxa_css_byte_stream_drain(lxa_css_byte_stream_t *stream,
                          lxa_css_tokens_t *tokens, size_t capacity,
                          size_t *produced)
{
    lxa_css_scan_result_t result;
    if (produced == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    *produced = 0;
    if (stream == NULL || !stream->sealed || stream->closed || tokens == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    if (!tokens_share_budget(stream, tokens)) {
        return LXA_CSS_SCAN_INVALID;
    }
    result = lxa_css_scan_cursor_batch(&stream->cursor,
        (const lxa_css_unit_t *) stream->units.bytes.data, tokens,
        capacity, produced);
    if (result != LXA_CSS_SCAN_OUTPUT_FULL) {
        stream->closed = true;
    }
    if (result == LXA_CSS_SCAN_LIMIT) {
        stream->limit_reason = stream->cursor.limit_reason;
    }
    return result;
}

lxa_css_scan_result_t
lxa_css_byte_stream_finish(lxa_css_byte_stream_t *stream,
                           lxa_css_tokens_t *tokens)
{
    lxa_css_scan_result_t result;
    size_t produced;
    if (tokens == NULL || (stream != NULL
        && !tokens_share_budget(stream, tokens))) {
        return LXA_CSS_SCAN_INVALID;
    }
    lxa_css_tokens_clear(tokens);
    result = lxa_css_byte_stream_seal(stream);
    if (result == LXA_CSS_SCAN_OK) {
        result = lxa_css_byte_stream_drain(stream, tokens,
            lxa_vector_length(&stream->units) + 1, &produced);
    }
    if (result != LXA_CSS_SCAN_OK) {
        lxa_css_tokens_clear(tokens);
    }
    return result;
}

void
lxa_css_byte_stream_destroy(lxa_css_byte_stream_t *stream)
{
    if (stream != NULL) {
        lxa_allocator_t allocator = stream->allocator;
        lxa_decoder_destroy(stream->decoder);
        lxa_vector_destroy(&stream->units);
        lxa_status_t status = lxa_budget_release(stream->budget,
            LXA_BUDGET_MEMORY, sizeof(*stream));
        assert(status == LXA_OK);
        (void) status;
        lxa_free(&allocator, stream);
    }
}

lxa_css_scan_result_t
lxa_css_scan_bytes(lxa_span_t bytes, lxa_css_tokens_t *tokens)
{
    lxa_css_byte_stream_t *stream;
    lxa_css_scan_result_t result;
    size_t consumed;
    if (tokens == NULL) {
        return LXA_CSS_SCAN_INVALID;
    }
    lxa_css_tokens_clear(tokens);
    if (bytes.data == NULL && bytes.length != 0) {
        return LXA_CSS_SCAN_INVALID;
    }
    if (bytes.length > CSS_MAX_BYTES) {
        return LXA_CSS_SCAN_LIMIT;
    }
    result = lxa_css_byte_stream_create(&tokens->text.allocator, &stream);
    if (result != LXA_CSS_SCAN_OK) {
        return result;
    }
    result = lxa_css_byte_stream_feed(stream, bytes, &consumed);
    if (result == LXA_CSS_SCAN_OK) {
        result = lxa_css_byte_stream_finish(stream, tokens);
    }
    lxa_css_byte_stream_destroy(stream);
    return result;
}
