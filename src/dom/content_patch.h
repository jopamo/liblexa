#ifndef LIBLEXA_DOM_CONTENT_PATCH_H
#define LIBLEXA_DOM_CONTENT_PATCH_H

#include "liblexa/dom.h"

typedef struct {
    lxa_dom_ref_t target, start, end;
} lxa_dom_content_patch_t;

/* Find the first named marker/start PI among actual scope descendants in tree
 * order. A marker returns itself twice; a start pairs with the next unnested
 * end among its siblings, regardless of their names. An unpaired start has
 * no end. Inert template contents are not actual children of their host.
 * Scope must be an element or fragment. Failure leaves both outputs empty. */
lxa_status_t lxa_dom_nodes_find_markers(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t scope, lxa_span_t name, lxa_dom_ref_t *start,
    lxa_dom_ref_t *end);
/* Prepare a named patch within an element/fragment scope. Missing or empty
 * names leave target empty. Snapshot range roots before removing them through
 * canonical lifecycle operations; remove each still-attached root regardless
 * of its current parent. Handles remain store-owned. Failure clears out but
 * retains earlier successful removals. Markers are not removed here. */
lxa_status_t lxa_dom_nodes_prepare_content_patch(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t scope, lxa_span_t name, lxa_dom_content_patch_t *out);

#endif
