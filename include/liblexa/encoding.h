#ifndef LIBLEXA_ENCODING_H
#define LIBLEXA_ENCODING_H

#include <stddef.h>
#include <stdint.h>
#include "liblexa/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical internal encoding identity. Look up WHATWG labels, including
 * aliases, once.
 * Unknown labels and the replacement-only encoding cannot create an encoder.
 */
typedef struct lxa_encoding lxa_encoding_t;
const lxa_encoding_t *lxa_encoding_lookup(lxa_span_t label);

typedef enum {
    LXA_ENCODING_REPLACE, /* decode with U+FFFD; encode with '?' */
    LXA_ENCODING_FATAL
} lxa_encoding_error_mode_t;

typedef struct lxa_decoder lxa_decoder_t;
typedef struct lxa_encoder lxa_encoder_t;

typedef struct {
    size_t consumed; /* source units accepted into the stream */
    size_t produced; /* complete destination units written */
} lxa_encoding_progress_t;

typedef enum {
    LXA_ENCODING_OK,
    LXA_ENCODING_OUTPUT_FULL,
    LXA_ENCODING_MALFORMED,
    LXA_ENCODING_INVALID_ARGUMENT,
    LXA_ENCODING_NO_MEMORY,
    LXA_ENCODING_OVERFLOW
} lxa_encoding_result_t;

/* Decoder input is bytes, output is Unicode scalar values. Encoder input
 * is scalars, output is bytes.
 *
 * Each call writes progress, including on OUTPUT_FULL and MALFORMED.
 * consumed counts input irrevocably accepted, including pending prefixes
 * and an offending sequence in FATAL mode; produced counts only complete
 * scalars or complete encoded byte sequences. There is no implicit retry
 * of consumed input. On OUTPUT_FULL call again with input advanced by
 * consumed and a fresh output buffer; output may be full before all input
 * is consumed. Zero-capacity output is valid and can report OUTPUT_FULL.
 *
 * Partial multibyte input is held in stream state between calls. Without
 * finish it produces no replacement. Malformed input in REPLACE mode emits
 * U+FFFD; an invalid continuation is reprocessed as the next input byte
 * per WHATWG Encoding (so no byte disappears). FATAL mode returns
 * MALFORMED, latches the failure, and requires reset before reuse.
 * Invalid Unicode scalar input to the encoder follows the same error mode.
 * NO_MEMORY/OVERFLOW are fatal and also require reset. An error return
 * never leaves half a scalar or half an encoded sequence in the output.
 * The WHATWG replacement decoder emits one U+FFFD on its first nonempty
 * input, then discards subsequent bytes. In FATAL mode its first byte
 * instead returns MALFORMED.
 * UTF-16 streams expose U+FEFF as a scalar and do not insert a BOM on
 * encode; BOM sniffing/skipping belongs to the caller's input policy.
 *
 * finish flushes incomplete sequences (replacement or MALFORMED) and
 * stateful encoder terminators. OUTPUT_FULL from finish is retryable with
 * a new buffer; it must not emit a terminator twice. A successful finish
 * closes the stream; feed/finish then require reset. reset restores the
 * selected encoding and error mode, discards pending input and failure,
 * and may retain backing capacity. Destroy releases it. These operations
 * are confined to one thread; the immutable encoding identity is sharable.
 */
lxa_status_t lxa_decoder_create(const lxa_encoding_t *encoding,
    lxa_encoding_error_mode_t mode,
    const lxa_allocator_t *allocator,
    lxa_decoder_t **out);
/* budget is borrowed until destroy. NULL leaves allocation uncharged for
 * callers that do not yet use a parse budget. The fixed object size is
 * reserved before allocating and released on allocation failure/destroy.
 */
lxa_status_t lxa_decoder_create_with_budget(
    const lxa_encoding_t *encoding, lxa_encoding_error_mode_t mode,
    const lxa_allocator_t *allocator, lxa_budget_t *budget,
    lxa_decoder_t **out);
lxa_status_t lxa_encoder_create(const lxa_encoding_t *encoding,
    lxa_encoding_error_mode_t mode,
    const lxa_allocator_t *allocator,
    lxa_encoder_t **out);
lxa_status_t lxa_encoder_create_with_budget(
    const lxa_encoding_t *encoding, lxa_encoding_error_mode_t mode,
    const lxa_allocator_t *allocator, lxa_budget_t *budget,
    lxa_encoder_t **out);
void lxa_decoder_reset(lxa_decoder_t *decoder);
void lxa_encoder_reset(lxa_encoder_t *encoder);
void lxa_decoder_destroy(lxa_decoder_t *decoder);
void lxa_encoder_destroy(lxa_encoder_t *encoder);
/* Number of malformed decode events committed since creation/reset.
 * OUTPUT_FULL does not advance it; NULL returns zero.
 */
size_t lxa_decoder_error_count(const lxa_decoder_t *decoder);
lxa_encoding_result_t lxa_decoder_feed(lxa_decoder_t *decoder,
    lxa_span_t input, uint32_t *output,
    size_t capacity,
    lxa_encoding_progress_t *progress);
lxa_encoding_result_t lxa_encoder_feed(lxa_encoder_t *encoder,
    const uint32_t *input, size_t length,
    lxa_mut_span_t output,
    lxa_encoding_progress_t *progress);
lxa_encoding_result_t lxa_decoder_finish(
    lxa_decoder_t *decoder, uint32_t *output, size_t capacity,
    lxa_encoding_progress_t *progress);
lxa_encoding_result_t lxa_encoder_finish(
    lxa_encoder_t *encoder, lxa_mut_span_t output,
    lxa_encoding_progress_t *progress);

#ifdef __cplusplus
}
#endif

#endif /* LIBLEXA_ENCODING_H */
