/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_DOM_NODE_STORE_INTERNAL_H
#define LIBLEXA_DOM_NODE_STORE_INTERNAL_H

#include "liblexa/dom.h"
#include "liblexa/base.h"

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
/* Move an attached parser-owned subtree to the end of an element or fragment,
 * or immediately before a child of that parent. Validate link symmetry,
 * cycles, subtree depth and budget before commit. */
lxa_status_t lxa_dom_nodes_reparent_constructed_child(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child);
lxa_status_t lxa_dom_nodes_reparent_constructed_before(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child, lxa_dom_ref_t before);

#endif
