#ifndef LIBLEXA_CSS_FILTER_H
#define LIBLEXA_CSS_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* CSS Syntax 3 input preprocessing, downstream of the shared byte decoder.
 * The caller decodes bytes to Unicode scalars once (e.g. via lxa_decoder_feed)
 * and owns both input and output buffers. DOMString callers may also supply
 * surrogates directly; they are replaced with U+FFFD.
 *
 * CR is held across calls to fold CRLF into one LF. A full output does not
 * consume the code point requiring that output; progress always describes
 * irrevocably accepted input and complete output. No pending input pointer is
 * saved. finish emits a pending CR as LF, then closes the filter; reset drops
 * any pending CR and reopens it. One filter is confined to one thread.
 */
typedef struct {
    bool pending_cr;
    bool finished;
} lxa_css_filter_t;

typedef enum {
    LXA_CSS_FILTER_OK,
    LXA_CSS_FILTER_OUTPUT_FULL,
    LXA_CSS_FILTER_INVALID_ARGUMENT
} lxa_css_filter_result_t;

typedef struct {
    size_t consumed;
    size_t produced;
} lxa_css_filter_progress_t;

void lxa_css_filter_reset(lxa_css_filter_t *filter);
lxa_css_filter_result_t lxa_css_filter_feed(lxa_css_filter_t *filter,
    const uint32_t *input, size_t length, uint32_t *output, size_t capacity,
    lxa_css_filter_progress_t *progress);
lxa_css_filter_result_t lxa_css_filter_finish(lxa_css_filter_t *filter,
    uint32_t *output, size_t capacity, lxa_css_filter_progress_t *progress);

#endif /* LIBLEXA_CSS_FILTER_H */
