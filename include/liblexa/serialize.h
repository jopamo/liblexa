/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_SERIALIZE_H
#define LIBLEXA_SERIALIZE_H

#include "liblexa/base.h"
#include "liblexa/dom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A write may consume a prefix before reporting failure. Short successful
 * writes are retried; a successful write of zero bytes is an error. */
typedef struct {
    size_t consumed;
    lxa_status_t status;
} lxa_write_result_t;

typedef lxa_write_result_t (*lxa_write_f)(void *context, lxa_span_t bytes);

typedef enum {
    LXA_ESCAPE_TEXT,
    LXA_ESCAPE_ATTRIBUTE
} lxa_escape_context_t;

/* HTML fragment serialization escaping (WHATWG HTML, serializing fragments).
 * Input bytes are already encoded; malformed UTF-8 is passed through. In
 * raw-text elements the caller must write directly to the sink instead.
 * Previously emitted bytes cannot be rolled back on a sink failure.
 * Sink/context and input storage must remain live and unmodified throughout.
 */
/* Retry short successful writes. Return the first sink error, or ARGUMENT
 * when a writer reports zero progress or more than the requested length.
 */
lxa_status_t lxa_write_all(lxa_span_t bytes, lxa_write_f write, void *context);

lxa_status_t lxa_html_escape(lxa_span_t input, lxa_escape_context_t context,
                             lxa_write_f write, void *write_context);

/* Serializes the currently supported HTML document subset.
 * The caller owns the sink and bounds its output. No mutation of the store
 * is permitted during serialization, including from the write callback.
 * Sink failure may leave a prefix written. Foreign nodes and template
 * contents require later typed payload support.
 */
lxa_status_t lxa_html_serialize_document(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t document, lxa_write_f write, void *context,
    lxa_budget_kind_t *reason);

#ifdef __cplusplus
}
#endif
#endif
