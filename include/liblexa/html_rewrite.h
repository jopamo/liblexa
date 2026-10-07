#ifndef LIBLEXA_HTML_REWRITE_H
#define LIBLEXA_HTML_REWRITE_H

#include "liblexa/html_source.h"
#include "liblexa/serialize.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool replace;
    lxa_span_t value;
} lxa_html_source_replacement_t;

typedef lxa_status_t (*lxa_html_source_rewrite_callback_t)(
    void *context, const lxa_html_source_attribute_t *attribute,
    lxa_html_source_replacement_t *replacement);

/* Apply keep/replace decisions to effective source attributes, without a DOM.
 * Input, targets, options and event views follow the extraction contract.
 * Each decision starts with replace=false. A replacement borrows valid UTF-8
 * without NUL until it has been consumed, before the next decision callback.
 * It may alias the event or input. Its complete syntax is built and validated
 * before publishing it. Bytes outside replaced attribute spans stay exact.
 *
 * Preserve source encoding, quote values and escape references/CR. Legacy
 * encodings use numeric references for non-ASCII values. C1 controls whose
 * references change value require literal encoding or return UNSUPPORTED.
 * A no-replacement pass reproduces input bytes, including malformed input.
 * Callback/sink errors propagate unchanged with CALLBACK_FAILURE. Invalid
 * replacement values return ARGUMENT. Short successful writes are retried;
 * zero progress or over-consumption returns ARGUMENT.
 * Failure can leave an accepted output prefix. No effect is replayed or rolled
 * back; atomic publication belongs to the caller. All owned resources are
 * released. The parser and replacement storage share one resource budget.
 * Allocator, decision and write callbacks must not mutate borrowed input,
 * targets/options or reenter this invocation.
 */
lxa_status_t lxa_html_rewrite_attributes(
    const lxa_allocator_t *allocator, const lxa_limits_t *limits, lxa_span_t input,
    const lxa_html_extract_options_t *options, const lxa_html_source_target_t *targets,
    size_t count, lxa_html_source_rewrite_callback_t callback, void *context,
    lxa_write_f write, void *write_context, lxa_html_result_t *parser_result,
    lxa_limit_reason_t *reason);

#ifdef __cplusplus
}
#endif
#endif
