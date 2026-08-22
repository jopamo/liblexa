#ifndef LIBLEXA_ENCODING_STREAM_INTERNAL_H
#define LIBLEXA_ENCODING_STREAM_INTERNAL_H

#include "liblexa/encoding.h"

/* Buffered source bytes awaiting scalar emission. Query after OK or
 * OUTPUT_FULL in replacement mode, including UTF-16 replay units. */
size_t lxa_decoder_pending_bytes(const lxa_decoder_t *decoder);

#endif
