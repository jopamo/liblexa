#ifndef LIBLEXA_HTML_TREE_H
#define LIBLEXA_HTML_TREE_H

#include "html/tokenizer.h"
#include "liblexa/dom.h"
#include "liblexa/base.h"

/* Internal tree-construction candidate. This slice constructs the document,
 * implicit/explicit html/head/body roots, doctype, comments, processing
 * instructions and simple
 * properly nested common sectioning blocks/div/span/p/pre/headings/lists/dl,
 * plus body forms with parser-local form pointer and nested-start rejection,
 * and a/b/em/strong/code and ordinary unknown/custom elements
 * (including adjacent p, heading
 * and item/list closure through open descendants and block starts after p),
 * common head/body void elements, table columns/groups/sections/rows/cells
 * (including implicit colgroup,
 * implicit tbody/tr, adjacent cell/row/section recovery, bounded chunk-invariant text
 * fostering and simple element fostering outside cells, with hidden table
 * inputs retained in table context and other inputs fostered),
 * explicit table captions and title/style/textarea/script/plaintext modes
 * with tokenizer feedback. An active anchor is reconstructed across implied
 * list-item endings; simple select/option/optgroup text and implied option
 * endings are supported, but other select-mode tags remain unqualified.
 * Head/body templates, including templates under open body tables, retain
 * text, comments, head tokens, text-mode elements,
 * void body elements, p/div/span/a, properly nested b/em/strong/i/code,
 * simple sectioning, pre/listing, noscript (scripting disabled), scoped
 * ul/ol/li, headings, simple select/option/optgroup, picture and unknown
 * elements, and caption/cell-descendant recovery with simple
 * table columns/groups/sections/rows/cells, including bounded fostered
 * table text in template fragments
 * with implied tbody/tr;
 * table fostering/recovery and overlapping formatting remain open.
 * Crossed inline formatting, direct blocks and selected list-item chains
 * are handled; general adoption remains open.
 * Frameset roots accept nested framesets and frame children; other frameset
 * insertion-mode recovery remains open.
 * All other element/insertion-mode behavior is rejected until its recovery
 * rules exist. */
typedef enum {
    LXA_TREE_INITIAL, LXA_TREE_BEFORE_HTML, LXA_TREE_BEFORE_HEAD,
    LXA_TREE_IN_HEAD, LXA_TREE_HEAD_NOSCRIPT, LXA_TREE_TEMPLATE_TEXT,
    LXA_TREE_AFTER_HEAD, LXA_TREE_IN_BODY,
    LXA_TREE_TEXT, LXA_TREE_AFTER_BODY, LXA_TREE_AFTER_HTML, LXA_TREE_DONE,
    LXA_TREE_IN_FRAMESET, LXA_TREE_AFTER_FRAMESET
} lxa_html_root_mode_t;

typedef enum {
    LXA_TEMPLATE_INITIAL, LXA_TEMPLATE_TABLE, LXA_TEMPLATE_TABLE_BODY,
    LXA_TEMPLATE_ROW, LXA_TEMPLATE_COLUMN_GROUP, LXA_TEMPLATE_BODY
} lxa_html_template_mode_t;

typedef struct {
    lxa_dom_ref_t fragment;
    lxa_dom_ref_t pending_formatting;
    size_t pending_extra_count;
    size_t element_count;
    lxa_html_template_mode_t insertion_mode;
} lxa_html_template_frame_t;

typedef struct {
    lxa_dom_nodes_t *nodes;
    lxa_dom_ref_t document, html, head, head_noscript, body, form;
    lxa_dom_ref_t template_fragment; /* active in-head template content */
    lxa_dom_ref_t text_element;
    lxa_dom_ref_t active_anchor; /* formatting entry, may leave open stack */
    lxa_dom_ref_t pending_formatting; /* one crossed inline formatting entry */
    lxa_dom_ref_t pending_template_formatting; /* active fragment's last entry */
    lxa_vector_t pending_template_extras; /* earlier entries, scoped by frame */
    lxa_vector_t open_elements; /* parser scratch; owns only references */
    lxa_vector_t template_stack; /* enclosing fragment, boundary and mode */
    lxa_vector_t template_elements; /* open HTML elements in template content */
    lxa_buffer_t table_text; /* held until the next non-text token */
    lxa_html_root_mode_t mode;
    lxa_html_root_mode_t text_return_mode;
    lxa_html_root_mode_t template_return_mode;
    lxa_html_template_mode_t template_insertion_mode;
    bool skip_pre_newline;
    bool doctype_seen;
    lxa_status_t failure;
} lxa_html_tree_t;

lxa_status_t lxa_html_tree_init(
    lxa_html_tree_t *tree, lxa_dom_nodes_t *nodes);
void lxa_html_tree_destroy(
    lxa_html_tree_t *tree);
lxa_status_t lxa_html_tree_on_token(void *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token);

#endif
