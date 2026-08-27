#ifndef LIBLEXA_HTML_FORMATTING_LIST_H
#define LIBLEXA_HTML_FORMATTING_LIST_H

#include "html/formatting_store.h"
#include "html/control_stack.h"

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
/* Find the newest identity after the last marker, or list length if absent.
 * Charge one WORK unit before each identity read; do not read snapshots.
 */
lxa_status_t lxa_html_formatting_list_find_reference(lxa_html_formatting_list_t *list,
    uint64_t identity, size_t *index);
lxa_status_t lxa_html_formatting_list_noah_plan(lxa_html_formatting_list_t *list,
    const lxa_html_token_t *token, size_t *drop);

/* Borrow the initialized stack that assigned the list's identities, sharing
 * the operation budget. Identities are not interchangeable across stacks or
 * reinitializations. Plan the closed suffix after the last marker, using open
 * identities at/after boundary.
 * List length means no reconstruction; publish only on success.
 */
lxa_status_t lxa_html_formatting_list_reconstruct_plan(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary, size_t *start);
/* Reopen the planned suffix as HTML metadata, reusing immutable snapshots.
 * Update each identity only after its push succeeds. Failure retains any
 * completed prefix and leaves remaining entries closed; a retry replans.
 * No token callbacks, attribute copying or new live formatting charges.
 * The engine still owns virtual-context depth admission and DOM effects.
 */
lxa_status_t lxa_html_formatting_list_reconstruct(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary);

/* Select one canonical adoption outer round in the same identity domain.
 * Exclude entries before the last marker and open identities before boundary.
 * Publish indices/action only on success; no allocation or mutation.
 * The engine owns the initial current-element shortcut, ordinary-end fallback,
 * pop/removal, repair cloning/bookmarks and the bounded outer loop.
 */
lxa_status_t lxa_html_formatting_list_adopt_plan(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary, lxa_span_t subject,
    lxa_html_formatting_adopt_plan_t *out);

/* Replace an open formatting identity for an adoption clone in the same stack
 * domain. The selected entry must follow the last marker and match the open
 * slot. Retain its snapshot and open metadata. Charge one WORK per owner read,
 * then publish both identities without allocation or live-quota changes.
 * Ordinary failure preserves both owners and key assignment; clear output.
 */
lxa_status_t lxa_html_formatting_list_replace_open(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t formatting_index, size_t open_index,
    uint64_t *identity);

/* Admit an already prepared HTML formatting start in the same stack domain:
 * enforce Noah removal,
 * capture its effective attributes, push an HTML open identity and append
 * membership. The engine owns anchor/nobr preprocessing, reconstruction
 * scheduling, insertion effects and virtual depth admission.
 * Clear identity on failure. Ordinary failure preserves prior snapshots,
 * membership and open identities, but reserves may retain capacity and
 * cumulative WORK/STRINGS charges are not refunded. Source views must obey
 * capture's non-alias contract; no token views survive this call.
 */
lxa_status_t lxa_html_formatting_list_admit(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, const lxa_html_token_t *token, uint64_t *identity);

#endif
