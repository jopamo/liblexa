#ifndef LIBLEXA_HTML_TREE_H
#define LIBLEXA_HTML_TREE_H

#include "html/tokenizer.h"
#include "html/tree_policy.h"
#include "dom/content_patch.h"
#include "liblexa/dom.h"
#include "liblexa/base.h"
#include "dom/node_store_internal.h"

/* Private complete-document tree-construction candidate over a borrowed store.
 * Root/template contexts share scope and active-formatting algorithms. Template
 * frames save fragment, patch references, open-element boundary and insertion
 * mode; nodes and attributes belong to the store. Token callbacks update
 * text/CDATA feedback.
 * The store must outlive the tree. Record growth preserves borrowed pointers;
 * reset invalidates the tree. Check the captured epoch before record access
 * and after token effects, including allocator and metadata callbacks.
 * The first failure is latched. Successful EOF completes construction and
 * subsequent tokens are rejected. No fragment-context or script-execution API.
 */
typedef struct {
    lxa_dom_ref_t fragment;
    lxa_dom_content_patch_t patch;
    size_t element_count;
    lxa_html_template_mode_t insertion_mode;
} lxa_html_template_frame_t;

typedef struct {
    lxa_dom_record_t *element; /* marker entries identify their open owner */
    size_t previous_marker; /* read only through the marker index chain */
} lxa_html_formatting_entry_t;

/* Open stacks borrow stable records from one parser document/epoch.
 * Reset/reuse invalidates every entry, even when its address is retained.
 * Zero depth is unknown; a stale revision requires checked ancestry repair. */
typedef struct {
    lxa_dom_record_t *node;
    uint32_t actual_depth, depth_revision;
    uint16_t tag_id; /* immutable element metadata; zero requires checked lookup */
    uint16_t button_flags;
    uint32_t button_stop; /* one past nearest p/boundary in this stack prefix */
} lxa_html_open_entry_t;

enum {
    LXA_HTML_DEPTH_DOCUMENT,
    LXA_HTML_DEPTH_HTML,
    LXA_HTML_DEPTH_HEAD,
    LXA_HTML_DEPTH_HEAD_NOSCRIPT,
    LXA_HTML_DEPTH_BODY,
    LXA_HTML_DEPTH_TEMPLATE,
    LXA_HTML_DEPTH_FIXED_COUNT
};

typedef struct {
    lxa_dom_nodes_t *nodes;
    lxa_dom_ref_t document, html, head, head_noscript, body, form;
    lxa_dom_ref_t template_fragment; /* active virtual-template content */
    lxa_dom_content_patch_t template_patch;
    lxa_dom_ref_t text_element;
    lxa_dom_insertion_depth_t insertion_depth;
    lxa_html_open_entry_t fixed_depth[LXA_HTML_DEPTH_FIXED_COUNT];
    lxa_vector_t formatting; /* active entries and scope markers */
    size_t formatting_marker; /* SIZE_MAX when no marker is active */
    lxa_vector_t open_elements; /* lxa_html_open_entry_t; no node ownership */
    lxa_vector_t template_stack; /* enclosing fragment, patch, boundary and mode */
    lxa_vector_t template_elements; /* open HTML elements in template content */
    lxa_buffer_t table_text; /* held until the next non-text token */
    lxa_html_meta_sink_t meta; /* observer context outlives tree use */
    lxa_html_root_mode_t mode;
    lxa_html_text_context_t text_context;
    lxa_html_root_mode_t template_return_mode;
    lxa_html_template_mode_t template_insertion_mode;
    lxa_html_document_mode_t document_mode;
    bool skip_pre_newline;
    bool frameset_ok;
    lxa_status_t failure;
} lxa_html_tree_t;

static inline bool
lxa_html_tree_epoch_valid(const lxa_html_tree_t *tree)
{
    return tree->nodes != NULL && tree->document.document == tree->nodes
        && tree->document.epoch == tree->nodes->epoch;
}

/* Only records acquired by this tree may enter. Do not dereference a retained
 * record after a callback or external reset invalidates the captured epoch. */
static inline lxa_dom_ref_t
lxa_html_tree_ref(const lxa_html_tree_t *tree, const lxa_dom_record_t *node)
{
    lxa_dom_ref_t ref = {tree->nodes, tree->document.epoch, LXA_HANDLE_NONE};
    if (node != NULL && lxa_html_tree_epoch_valid(tree))
        (void) lxa_handle_pack(node->id, node->generation, &ref.handle);
    return ref;
}

static inline lxa_dom_ref_t
lxa_html_open_ref(const lxa_html_tree_t *tree, const lxa_html_open_entry_t *entry)
{
    return lxa_html_tree_ref(tree, entry->node);
}

static inline lxa_status_t
lxa_html_tree_read_element(const lxa_html_tree_t *tree,
    const lxa_dom_record_t *node, lxa_span_t *name, lxa_dom_namespace_t *ns)
{
    if (!lxa_html_tree_epoch_valid(tree)) {
        if (name != NULL) *name = (lxa_span_t) {0};
        if (ns != NULL) *ns = 0;
        return LXA_ERROR_ARGUMENT;
    }
    return lxa_dom_element_read_owned(tree->nodes, node, name, ns);
}

lxa_status_t lxa_html_tree_init(
    lxa_html_tree_t *tree, lxa_dom_nodes_t *nodes);
void lxa_html_tree_destroy(
    lxa_html_tree_t *tree);
/* Active tokens require nonempty tag/attribute names and valid borrowed spans. */
lxa_status_t lxa_html_tree_on_token(void *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token);

#endif
