#ifndef LIBLEXA_HTML_FORMATTING_STORE_H
#define LIBLEXA_HTML_FORMATTING_STORE_H

#include "html/formatting_policy.h"

typedef struct {
    size_t name_offset, name_length, value_offset, value_length;
} lxa_html_formatting_attribute_t;

typedef struct {
    size_t name_offset, name_length, attribute_start, attribute_count;
} lxa_html_formatting_snapshot_t;

typedef struct {
    lxa_vector_t snapshots, attributes;
    lxa_buffer_t bytes;
    lxa_budget_t *budget;
    lxa_limit_reason_t limit_reason;
} lxa_html_formatting_store_t;

/* Initialize fresh caller-owned storage. Allocator context and operation
 * budget outlive destroy; no reset while snapshots/readers remain live.
 * Single-thread confined, including serialized access to a shared budget.
 */
lxa_status_t lxa_html_formatting_store_init(lxa_html_formatting_store_t *store,
    const lxa_allocator_t *allocator, lxa_budget_t *budget);
void lxa_html_formatting_store_destroy(lxa_html_formatting_store_t *store);

/* Capture an accepted HTML formatting start token's effective attributes.
 * Token storage, nonempty source spans and descriptor arrays must not overlap
 * this store.
 * Failure preserves existing snapshots but may retain capacity. Nonzero
 * references are stable until destroy and scoped to this initialized store.
 * Snapshots own copied bytes, not input views, DOM records or list membership.
 */
lxa_status_t lxa_html_formatting_store_capture(lxa_html_formatting_store_t *store,
    const lxa_html_token_t *token, size_t *reference);

/* Returned spans expire on any capture attempt or destroy. Getters clear
 * their outputs on failure; equality publishes only on success. Reconstruction
 * reuses a snapshot reference rather than recapturing its borrowed spans.
 */
lxa_status_t lxa_html_formatting_store_get(lxa_html_formatting_store_t *store,
    size_t reference, lxa_span_t *name, size_t *attribute_count);
lxa_status_t lxa_html_formatting_store_attribute(lxa_html_formatting_store_t *store,
    size_t reference, size_t index, lxa_html_attribute_t *out);
lxa_status_t lxa_html_formatting_store_equal(lxa_html_formatting_store_t *store,
    size_t reference, const lxa_html_token_t *token, bool *out);

#endif
