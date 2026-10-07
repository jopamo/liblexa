#ifndef LIBLEXA_ENCODING_STREAM_INTERNAL_H
#define LIBLEXA_ENCODING_STREAM_INTERNAL_H

#include "liblexa/encoding.h"

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
