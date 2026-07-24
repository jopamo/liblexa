/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_DOM_H
#define LIBLEXA_DOM_H

#include <stdbool.h>
#include <stdint.h>
#include "liblexa/base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LXA_DOM_KIND_INVALID = 0,
    LXA_DOM_KIND_DOCUMENT,
    LXA_DOM_KIND_ELEMENT,
    LXA_DOM_KIND_ATTRIBUTE,
    LXA_DOM_KIND_TEXT,
    LXA_DOM_KIND_COMMENT,
    LXA_DOM_KIND_PROCESSING_INSTRUCTION,
    LXA_DOM_KIND_DOCUMENT_TYPE,
    LXA_DOM_KIND_DOCUMENT_FRAGMENT,
    LXA_DOM_KIND_COUNT
} lxa_dom_kind_t;

typedef struct lxa_dom_nodes lxa_dom_nodes_t;

/* Packed handles are document-local. A reference supplies document identity
 * and reset epoch outside the 64-bit slot/generation value. References are
 * valid only while the document is alive. Construction is append-only;
 * reset may reuse slots only with a new nonzero generation. On wrap all
 * previous slots are retired, retaining their storage and budget charge.
 */
typedef struct {
    const lxa_dom_nodes_t *document;
    uint64_t epoch;
    lxa_handle_t handle;
} lxa_dom_ref_t;

lxa_status_t lxa_dom_nodes_create(const lxa_allocator_t *allocator,
                                   lxa_dom_nodes_t **out);
/* Optional shared parser budget must outlive this store. A NULL budget uses
 * the default bounded profile. Limits are never reset while retained
 * allocations remain live. */
lxa_status_t lxa_dom_nodes_create_with_budget(
    const lxa_allocator_t *allocator, lxa_budget_t *shared,
    lxa_budget_kind_t *reason, lxa_dom_nodes_t **out);
/* NULL entropy uses OS randomness; an application callback supplies the
 * per-document atom-map key. No deterministic production fallback exists. */
lxa_status_t lxa_dom_nodes_create_with_entropy(
    const lxa_allocator_t *allocator, lxa_budget_t *shared,
    lxa_entropy_callback_t entropy, void *entropy_context,
    lxa_budget_kind_t *reason, lxa_dom_nodes_t **out);
/* Last failed resource check, COUNT for success, invalid input or OOM. */
lxa_budget_kind_t lxa_dom_nodes_limit_reason(const lxa_dom_nodes_t *nodes);
void lxa_dom_nodes_destroy(lxa_dom_nodes_t *nodes);
/* Reset preflights atom-map clear and atom-release WORK before invalidating
 * references; limit failure leaves records, names and attributes intact.
 * Epoch UINT64_MAX also fails without changing nodes. */
lxa_status_t lxa_dom_nodes_reset(lxa_dom_nodes_t *nodes);
size_t lxa_dom_nodes_length(const lxa_dom_nodes_t *nodes);

/* Return a live node's kind without exposing storage IDs or payload layout.
 * out is required and is set to INVALID on failure. */
lxa_status_t lxa_dom_nodes_kind(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t node, lxa_dom_kind_t *out);
/* Checked traversal; a zero handle means the relation is absent. A returned
 * handle is document-scoped and expires on reset or destruction. */
lxa_status_t lxa_dom_nodes_first_child(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_next_sibling(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t sibling, lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_last_child(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_prev_sibling(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t sibling, lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_parent(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t child, lxa_dom_ref_t *out);

/* TEXT/COMMENT construction copies bytes into persistent document storage
 * and appends a separate typed payload. Input must not alias this document.
 * Failure does not append any node, payload or string bytes.
 */
lxa_status_t lxa_dom_nodes_append_character(lxa_dom_nodes_t *nodes,
                                             lxa_dom_kind_t kind,
                                             lxa_span_t text,
                                             lxa_dom_ref_t *out);
/* Borrowed bytes expire at the next string append/reset/destroy. */
lxa_status_t lxa_dom_nodes_character_read(const lxa_dom_nodes_t *nodes,
                                           lxa_dom_ref_t handle,
                                           lxa_span_t *out);
/* HTML tag names start with a lowercase ASCII letter and then contain
 * tokenizer-normalized non-delimiter bytes; the internal namespaced
 * constructor also accepts canonical mixed-case foreign names.
 * Names are copied into document storage; borrowed names from that storage
 * survive its growth.
 * No record/string length changes on failure. */
lxa_status_t lxa_dom_nodes_append_html_element(lxa_dom_nodes_t *nodes,
                                                lxa_span_t name,
                                                lxa_dom_ref_t *out);
/* Borrowed name bytes expire at the next string append/reset/destroy. */
lxa_status_t lxa_dom_nodes_element_name(const lxa_dom_nodes_t *nodes,
                                         lxa_dom_ref_t element,
                                         lxa_span_t *out);
/* Detached HTML attribute: normalized nonempty name excludes markup
 * delimiters and ASCII uppercase; values are binary. Both are copied into
 * document storage; input spans may alias that storage. */
lxa_status_t lxa_dom_nodes_append_html_attribute(lxa_dom_nodes_t *nodes,
    lxa_span_t name, lxa_span_t value, lxa_dom_ref_t *out);
/* Distinct, non-NULL outputs; both borrow document string storage and expire
 * at the next string append/reset/destroy. Empty values are still present. */
lxa_status_t lxa_dom_nodes_attribute_read(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t attribute, lxa_span_t *name, lxa_span_t *value);
/* Replace the value of a live attached or detached attribute without
 * changing its identity, name, owner or position. Copies caller bytes into
 * retained document storage; a value borrowed from this store is accepted.
 * Failure leaves the previous value readable. Borrowed views expire on the
 * next string append/reset/destroy. */
lxa_status_t lxa_dom_nodes_attribute_set_value(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t attribute, lxa_span_t value);
/* Borrowed document-type input. On success the store copies
 * every present byte span; a missing public/system identifier is distinct
 * from an explicitly empty identifier. Read views expire on string growth,
 * reset or destruction. */
typedef struct {
    lxa_span_t name, public_id, system_id;
    bool has_public_id, has_system_id, force_quirks;
} lxa_dom_doctype_data_t;
lxa_status_t lxa_dom_nodes_append_doctype(lxa_dom_nodes_t *nodes,
    lxa_dom_doctype_data_t data, lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_doctype_read(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t doctype, lxa_dom_doctype_data_t *out);
/* Same-document element attachment. Attributes remain outside the child
 * list. A duplicate name or already owned attribute is rejected without
 * changing either owner. Lookup returns a zero handle when no match exists.
 */
lxa_status_t lxa_dom_nodes_attach_attribute(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t attribute);
/* Detach a known attribute from its owner after a bounded list scan. The
 * attribute remains live and may be attached again. A rejected operation
 * leaves the ordered attribute list unchanged. */
lxa_status_t lxa_dom_nodes_detach_attribute(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t attribute);
/* Replace an attached attribute with a detached one at the same position.
 * The old attribute remains live and detached; a name already owned by
 * another attribute of the element is rejected without changing either.
 * No derived indexes are maintained. */
lxa_status_t lxa_dom_nodes_replace_attribute(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t old_attribute,
    lxa_dom_ref_t new_attribute);
lxa_status_t lxa_dom_nodes_find_attribute(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_span_t name, lxa_dom_ref_t *out);
/* Iterate attached attributes in insertion order. A zero previous handle
 * starts at the first attribute; a zero result ends the list. Replacements
 * preserve the position, while detachment invalidates that cursor.
 */
lxa_status_t lxa_dom_nodes_attribute_next(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t previous, lxa_dom_ref_t *out);

/* Append a node's descendant TEXT bytes in tree order to caller-owned output.
 * A TEXT or COMMENT root yields its own data; descendant comments are skipped.
 * max_output_bytes bounds the whole result, including preexisting output.
 * On failure output length is restored; its grown capacity may be retained.
 * The walk charges WORK per visited node and ascent and uses no recursion.
 */
lxa_status_t lxa_dom_nodes_text_content(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t root, lxa_buffer_t *output, size_t max_output_bytes);
/* Debug/model validation of the supported subset. Detached draft
 * records may have no payload; only owned TEXT/COMMENT may be linked beneath
 * a fragment. Allocates budgeted temporary bookkeeping and charges visits
 * against WORK; never mutates links.
 */
lxa_status_t lxa_dom_nodes_validate(const lxa_dom_nodes_t *nodes);

#ifdef __cplusplus
}
#endif
#endif
