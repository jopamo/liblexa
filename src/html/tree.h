#ifndef LIBLEXA_HTML_TREE_H
#define LIBLEXA_HTML_TREE_H

#include "html/tokenizer.h"
#include "html/tree_policy.h"
#include "dom/content_patch.h"
#include "liblexa/dom.h"
#include "liblexa/base.h"

/* Private complete-document tree-construction candidate over a borrowed store.
 * Root/template contexts share scope and active-formatting algorithms. Template
 * frames save fragment, patch references, open-element boundary and insertion
 * mode; nodes and attributes belong to the store. Token callbacks update
 * text/CDATA feedback.
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
    lxa_dom_ref_t element; /* marker entries identify their open owner */
    size_t previous_marker; /* read only through the marker index chain */
} lxa_html_formatting_entry_t;

typedef struct {
    lxa_dom_nodes_t *nodes;
    lxa_dom_ref_t document, html, head, head_noscript, body, form;
    lxa_dom_ref_t template_fragment; /* active virtual-template content */
    lxa_dom_content_patch_t template_patch;
    lxa_dom_ref_t text_element;
    lxa_vector_t formatting; /* active entries and scope markers */
    size_t formatting_marker; /* SIZE_MAX when no marker is active */
    lxa_vector_t open_elements; /* parser scratch; owns only references */
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

lxa_status_t lxa_html_tree_init(
    lxa_html_tree_t *tree, lxa_dom_nodes_t *nodes);
void lxa_html_tree_destroy(
    lxa_html_tree_t *tree);
/* Active tokens require nonempty tag/attribute names and valid borrowed spans. */
lxa_status_t lxa_html_tree_on_token(void *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token);

#endif
