#ifndef LIBLEXA_ENCODING_STREAM_INTERNAL_H
#define LIBLEXA_ENCODING_STREAM_INTERNAL_H

#include "liblexa/encoding.h"

static inline size_t
lxa_encoding_utf8_lead(uint8_t byte, uint8_t *lower, uint8_t *upper)
{
    *lower = 0x80;
    *upper = 0xbf;
    if (byte >= 0xc2 && byte <= 0xdf) return 2;
    if (byte >= 0xe0 && byte <= 0xef) {
        if (byte == 0xe0) *lower = 0xa0;
        if (byte == 0xed) *upper = 0x9f;
        return 3;
    }
    if (byte >= 0xf0 && byte <= 0xf4) {
        if (byte == 0xf0) *lower = 0x90;
        if (byte == 0xf4) *upper = 0x8f;
        return 4;
    }
    return 0;
}

/* Read one complete multibyte scalar; leave malformed/incomplete recovery
 * to the streaming decoder. Zero consumes nothing and leaves scalar unset. */
static inline size_t
lxa_encoding_utf8_complete(lxa_span_t input, uint32_t *scalar)
{
    if (input.length == 0) return 0;
    uint8_t lower, upper;
    size_t width = lxa_encoding_utf8_lead(input.data[0], &lower, &upper);
    if (width == 0 || width > input.length) return 0;
    uint32_t value = input.data[0] & (0x7fu >> width);
    for (size_t i = 1; i < width; ++i) {
        uint8_t byte = input.data[i];
        if (byte < lower || byte > upper) return 0;
        value = (value << 6) | (byte & 0x3f);
        lower = 0x80;
        upper = 0xbf;
    }
    *scalar = value;
    return width;
}

/* The caller supplies a Unicode scalar, after malformed-input handling. */
static inline size_t
lxa_encoding_utf8_scalar_length(uint32_t scalar)
{
    if (scalar < 0x80) return 1;
    if (scalar < 0x800) return 2;
    if (scalar < 0x10000) return 3;
    return 4;
}

/* Buffered source bytes awaiting scalar emission. Query after OK or
 * OUTPUT_FULL in replacement mode, including UTF-16 replay units. */
size_t lxa_decoder_pending_bytes(const lxa_decoder_t *decoder);

#endif
