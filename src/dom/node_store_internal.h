#ifndef LIBLEXA_DOM_NODE_STORE_INTERNAL_H
#define LIBLEXA_DOM_NODE_STORE_INTERNAL_H

#include "liblexa/dom.h"
#include "liblexa/base.h"

/* The engine record contains IDs only. Drafts have no published links or
 * payload; typed payload construction and tree publication are separate.
 * No caller may retain an address into the growing node array.
 */
typedef struct {
    lxa_id_t parent, first_child, last_child, prev, next;
    lxa_id_t payload;
    uint16_t kind, flags;
    uint32_t generation;
} lxa_dom_record_t;

/* HTML option state starts clear; the selected attribute supplies its default. */
enum {
    LXA_DOM_OPTION_SELECTED = 1u << 0,
    LXA_DOM_OPTION_DIRTY = 1u << 1
};

enum { LXA_DOM_SELECTEDCONTENT_DISABLED = 1u << 2 };

/* Preflight connected selectedcontent under the proposed parent. Reject
 * enabled single-select contexts until their clone effects are implemented. */
lxa_status_t lxa_dom_nodes_prepare_selectedcontent(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, bool *disabled);
/* Apply the supported post-connection state after all fallible checks.
 * Inert template contents do not run these steps. */
lxa_status_t lxa_dom_nodes_connect_selectedcontent(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element);
/* Parser-pop effect. Non-HTML options and unselected/unowned options do nothing.
 * Clone into the first enabled selectedcontent, including in inert templates.
 * Failure preserves published children; unpublished clones remain store-owned. */
lxa_status_t lxa_dom_nodes_maybe_clone_selected_option(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element);
/* Update the enabled selectedcontent from the first selected option in the
 * canonical option list, or clear it when no option is selected. */
lxa_status_t lxa_dom_nodes_update_selectedcontent(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t select);
/* Preserve the first HTML selectedcontent descendant and clear all later ones.
 * Each replacement is staged; earlier successful clears survive a later error. */
lxa_status_t lxa_dom_nodes_clear_nonprimary_selectedcontent(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t select);

/* Apply the nearest select's selectedness rules. Preflight all traversal and
 * allocation before changing option flags; no links or attributes change. */
lxa_status_t lxa_dom_nodes_reset_option_select(lxa_dom_nodes_t *nodes,
                                               lxa_dom_ref_t option);

typedef char lxa_dom_record_size_must_be_32[
    sizeof(lxa_dom_record_t) == 32 ? 1 : -1];

/* Copies the record, avoiding a pointer into a relocatable array. */
lxa_status_t lxa_dom_nodes_read(const lxa_dom_nodes_t *nodes,
                                lxa_dom_ref_t handle,
                                lxa_dom_record_t *out);
/* Parser-only draft construction. A draft has no typed payload or links.
 * Failure leaves length and earlier references valid. */
lxa_status_t lxa_dom_nodes_append_draft(lxa_dom_nodes_t *nodes,
                                        lxa_dom_kind_t kind,
                                        lxa_dom_ref_t *out);

/* Text-only construction boundary used by DOM model tests.
 * Accept only owned TEXT/COMMENT children of a document fragment. Validate
 * both handles and existing endpoints before an allocation-free link commit.
 * On failure no link changes. No public pointers or callbacks are involved.
 */
lxa_status_t lxa_dom_nodes_append_text_child(lxa_dom_nodes_t *nodes,
                                              lxa_dom_ref_t parent,
                                              lxa_dom_ref_t child);

typedef struct {
    lxa_id_t element, fragment;
} lxa_dom_template_t;

/* Private store layout, shared with DOM code and invariant tests.
 * [0, retired) contains slots retired after a generation wrap. Active
 * records occupy [retired, records.length); no retired ID is reused.
 */
struct lxa_dom_nodes {
    lxa_allocator_t allocator;
    lxa_vector_t records;
    lxa_vector_t characters;
    lxa_vector_t pis; /* target and data spans, stored separately from text */
    lxa_vector_t elements;
    lxa_vector_t templates; /* sparse HTML template -> detached fragment */
    lxa_vector_t attributes;
    lxa_vector_t doctypes;
    lxa_vector_t atoms; /* lxa_dom_atom_t *; atoms stay at stable addresses */
    lxa_table_t *atom_lookup;
    lxa_buffer_t strings;
    size_t retired;
    uint64_t epoch;
    uint32_t generation;
    lxa_budget_t own_budget;
    lxa_budget_t *budget;
    size_t charged_slots;
    size_t charged_string_bytes;
    size_t charged_attrs;
    lxa_budget_kind_t limit_reason;
};

/* The parser's element namespace is stored with the typed element payload,
 * not inferred from its spelling or ancestor links. */
typedef enum {
    LXA_DOM_NS_HTML = 1,
    LXA_DOM_NS_MATHML = 2,
    LXA_DOM_NS_SVG = 3
} lxa_dom_namespace_t;

typedef enum {
    LXA_DOM_ATTR_NS_NONE,
    LXA_DOM_ATTR_NS_XML,
    LXA_DOM_ATTR_NS_XLINK,
    LXA_DOM_ATTR_NS_XMLNS
} lxa_dom_attr_namespace_t;

lxa_status_t lxa_dom_nodes_append_namespaced_element(lxa_dom_nodes_t *nodes,
    lxa_span_t name, lxa_dom_namespace_t ns, lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_element_namespace(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_namespace_t *out);
/* Parser-only constructor for adjusted foreign attribute spellings and
 * qualified-name namespace. Same ownership and limits as HTML attributes. */
lxa_status_t lxa_dom_nodes_append_foreign_attribute(lxa_dom_nodes_t *nodes,
    lxa_span_t name, lxa_span_t value, lxa_dom_attr_namespace_t ns,
    lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_attribute_namespace(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t attribute, lxa_dom_attr_namespace_t *ns,
    lxa_span_t *local_name);
/* Detached shallow copy, preserving element and attribute namespaces.
 * Failed construction may retain document-owned unpublished nodes. */
lxa_status_t lxa_dom_nodes_clone_element(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t source, lxa_dom_ref_t *out);
/* Copy source children and their template contents before replacing an element
 * or fragment's children. Associated templates use their content fragment as
 * target. A zero or empty source clears the list. Failure leaves target links
 * intact; unpublished candidates remain document-owned. Old children remain live as
 * detached subtrees after success. Nonempty copying uses a store-owned staging
 * fragment. No lifecycle callbacks run during copying. */
lxa_status_t lxa_dom_nodes_replace_children_with_clones(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t target, lxa_dom_ref_t source);
/* Parser-only association. The fragment remains detached from the tree and
 * is owned by the same document as its HTML template element. Bind once,
 * before inserting content; zero on lookup means no association. */
lxa_status_t lxa_dom_nodes_bind_template(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t fragment);
lxa_status_t lxa_dom_nodes_template_contents(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_template_owner(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t fragment, lxa_dom_ref_t *out);

/* Parser-only construction step: link a fresh detached leaf to a document
 * (comments/PIs, at most one doctype before at most one element root),
 * fragment or element.
 * The child stays detached on depth/work/hierarchy failure; no public tree
 * mutation API is exposed. Reparenting requires a later operation.
 */
lxa_status_t lxa_dom_nodes_append_constructed_child(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child);
/* Tokenizer-only PI construction; copies target and data into document
 * storage. Returned spans expire on string growth/reset/destroy. */
lxa_status_t lxa_dom_nodes_append_pi(lxa_dom_nodes_t *nodes,
    lxa_span_t target, lxa_span_t data, lxa_dom_ref_t *out);
lxa_status_t lxa_dom_nodes_pi_read(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t pi, lxa_span_t *target, lxa_span_t *data);
/* Insert a detached parser-owned leaf before an existing child, preserving
 * document doctype/root ordering when the parent is a document. */
lxa_status_t lxa_dom_nodes_insert_constructed_before(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child, lxa_dom_ref_t before);
/* Unlink an attached parser-owned element after preflight. Removing owned HTML
 * options, including descendants, resets their old select at the link commit.
 * Keep descendants/handles live in the store. Failure changes neither links
 * nor option state. */
lxa_status_t lxa_dom_nodes_detach_constructed_element(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element);
/* Move an attached parser-owned subtree to the end of an element or fragment,
 * or immediately before a child of that parent. Validate link symmetry,
 * cycles, subtree depth and budget before commit. HTML options changing owners,
 * including descendants, reset affected selects at the link commit. Failure
 * preserves links/state. */
lxa_status_t lxa_dom_nodes_reparent_constructed_child(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child);
lxa_status_t lxa_dom_nodes_reparent_constructed_before(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child, lxa_dom_ref_t before);

#endif
