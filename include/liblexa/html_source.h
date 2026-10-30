#ifndef LIBLEXA_HTML_SOURCE_H
#define LIBLEXA_HTML_SOURCE_H

#include "liblexa/html.h"
#include "liblexa/encoding.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lxa_span_t tag; /* Empty matches every tokenizer tag name. */
    lxa_span_t attribute; /* Nonempty normalized tokenizer attribute name. */
} lxa_html_source_target_t;

typedef struct {
    /* NULL selects automatically; fixed decoding ignores declarations.
     * A fixed prefix skips caller-identified BOM bytes. Automatic prefix is 0. */
    const lxa_encoding_t *encoding;
    size_t prefix_length;
    bool include_templates;
} lxa_html_extract_options_t;

typedef struct {
    lxa_span_t tag, name, value;
    size_t source_start, source_end; /* Original-byte, half-open attribute syntax. */
    size_t target_index; /* First matching descriptor, not event order. */
    const lxa_encoding_t *encoding; /* Final immutable decoder identity. */
    size_t source_name_end; /* Original-byte end of the raw attribute name. */
} lxa_html_source_attribute_t;

typedef lxa_status_t (*lxa_html_source_attribute_callback_t)(
    void *context, const lxa_html_source_attribute_t *attribute);

/* Synchronous complete-input extraction using control feedback, without a DOM.
 * NULL options uses automatic decoding and excludes template contents.
 * Names in targets are valid UTF-8, ASCII-lowercased, without NUL, ASCII
 * whitespace or control bytes. Names and input are caller-owned, immutable
 * and live throughout the call. Options must also remain immutable.
 * targets may be NULL only when count is zero;
 * callback is required for nonzero count. Duplicate/overlapping descriptors
 * never duplicate an event. Callback tag/name/value views and the event expire
 * on return; original source bytes and encoding identity remain caller-usable.
 *
 * Emit one event per requested effective tokenizer attribute in surviving
 * source order, before recovery discards or reconstructs elements. Duplicate
 * source attributes are suppressed. No URL/base/loading policy is performed.
 * Settle decoding before any caller callback. A non-OK callback return stops
 * delivery and is returned unchanged with CALLBACK_FAILURE, including LIMIT
 * and ARGUMENT. Earlier callback effects are not rolled back or replayed.
 * Other failures report their typed parser result and limit reason (COUNT
 * when unavailable). Optional result/reason outputs must not alias borrowed
 * input or descriptors. Allocator/callbacks must not mutate those views or
 * reenter this invocation. All owned resources are destroyed before return.
 */
lxa_status_t
lxa_html_extract_attributes(const lxa_allocator_t *allocator, const lxa_limits_t *limits,
    lxa_span_t input, const lxa_html_extract_options_t *options,
    const lxa_html_source_target_t *targets, size_t count,
    lxa_html_source_attribute_callback_t callback, void *context,
    lxa_html_result_t *parser_result, lxa_limit_reason_t *reason);

#ifdef __cplusplus
}
#endif
#endif
