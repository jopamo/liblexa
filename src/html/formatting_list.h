#ifndef LIBLEXA_HTML_FORMATTING_LIST_H
#define LIBLEXA_HTML_FORMATTING_LIST_H

#include "html/formatting_store.h"

typedef struct {
    size_t snapshot; /* zero only for a marker */
    uint64_t identity; /* current open identity, or marker owner */
    size_t previous_marker; /* read only through the marker chain */
} lxa_html_formatting_item_t;

typedef struct {
    lxa_html_formatting_store_t *store;
    lxa_vector_t entries;
    size_t marker;
} lxa_html_formatting_list_t;

/* Fresh single-thread-confined list; the initialized store, allocator context
 * and operation budget outlive it. Destroy the list before the store.
 * Membership owns live ACTIVE_FORMATTING charges, not snapshot bytes.
 */
lxa_status_t lxa_html_formatting_list_init(lxa_html_formatting_list_t *list,
    lxa_html_formatting_store_t *store);
void lxa_html_formatting_list_destroy(lxa_html_formatting_list_t *list);

/* These are membership primitives, not complete admission/reconstruction.
 * Append an accepted snapshot/current identity, or a marker's owner identity.
 * Failure preserves membership and marker indices but may retain capacity.
 */
lxa_status_t lxa_html_formatting_list_append(lxa_html_formatting_list_t *list,
    size_t snapshot, uint64_t identity);
lxa_status_t lxa_html_formatting_list_push_marker(lxa_html_formatting_list_t *list,
    uint64_t identity);
/* Remove only after the last marker. Charge one WORK unit per removed/shifted
 * slot before mutation. Clear removes the last marker and its suffix (or all
 * entries when no marker exists), without scanning or copying retained slots.
 */
lxa_status_t lxa_html_formatting_list_remove(lxa_html_formatting_list_t *list,
    size_t index);
lxa_status_t lxa_html_formatting_list_clear(lxa_html_formatting_list_t *list);
/* Copy one item for one WORK unit; clear output on failure. */
lxa_status_t lxa_html_formatting_list_get(lxa_html_formatting_list_t *list,
    size_t index, lxa_html_formatting_item_t *out);
/* Shared scans use the list's last-marker boundary. Publish only on success.
 * Noah planning does not reserve storage, admit a token or change membership.
 */
lxa_status_t lxa_html_formatting_list_find(lxa_html_formatting_list_t *list,
    lxa_span_t target, size_t *index);
lxa_status_t lxa_html_formatting_list_noah_plan(lxa_html_formatting_list_t *list,
    const lxa_html_token_t *token, size_t *drop);

#endif
