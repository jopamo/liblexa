#include <string.h>
#include "dom/node_store_internal.h"
#include "html/foreign_names.h"
#include "html-categories.h"
#include "html-tags.h"
#include "html/tree.h"

static lxa_span_t
literal(const char *text)
{
    lxa_span_t result = {(const uint8_t *) text, strlen(text)};
    return result;
}

static bool
name_equals_literal(lxa_span_t name, const char *text, size_t length)
{
    return name.length == length && name.data != NULL
           && memcmp(name.data, text, length) == 0;
}

/* All tree-builder names here are literals; carry their lengths at compile
 * time instead of rediscovering them for every token and open-stack probe. */
#define named(name, text) name_equals_literal((name), (text), sizeof(text) - 1)

static bool
special_element(lxa_span_t name, lxa_dom_namespace_t ns)
{
    const char *namespace_name =
        ns == LXA_DOM_NS_HTML ? "html"
        : ns == LXA_DOM_NS_SVG ? "svg"
        : ns == LXA_DOM_NS_MATHML ? "mathml" : NULL;
    size_t i;
    if (namespace_name == NULL) return false;
    for (i = 0; i < LIBLEXA_HTML_SPECIAL_COUNT; ++i) {
        const liblexa_html_special_tag *tag = &liblexa_html_special_tags[i];
        if (strcmp(tag->namespace_name, namespace_name) == 0
            && name_equals_literal(name, tag->tag, strlen(tag->tag)))
            return true;
    }
    return false;
}

static bool
ascii_case_equals(lxa_span_t value, const char *expected, size_t length)
{
    size_t i;
    if (value.length != length || value.data == NULL) return false;
    for (i = 0; i < length; ++i) {
        uint8_t c = value.data[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (uint8_t) expected[i]) return false;
    }
    return true;
}

static bool
heading(lxa_span_t name)
{
    return name.length == 2 && name.data != NULL && name.data[0] == 'h'
           && name.data[1] >= '1' && name.data[1] <= '6';
}

static bool
block_container(lxa_span_t name)
{
    return named(name, "div") || named(name, "section")
           || named(name, "address") || named(name, "dialog")
           || named(name, "dir") || named(name, "hgroup")
           || named(name, "search") || named(name, "summary")
           || named(name, "article") || named(name, "main")
           || named(name, "center") || named(name, "menu")
           || named(name, "aside")
           || named(name, "header") || named(name, "footer")
           || named(name, "nav") || named(name, "blockquote")
           || named(name, "pre") || named(name, "listing")
           || named(name, "figure") || named(name, "figcaption")
           || named(name, "fieldset")
           || named(name, "details") || named(name, "button")
           || named(name, "ul") || named(name, "ol")
           || named(name, "dl");
}

static bool
inline_container(lxa_span_t name)
{
    return named(name, "span") || named(name, "a")
           || named(name, "ins") || named(name, "slot")
           || named(name, "label")
           || named(name, "abbr") || named(name, "cite")
           || named(name, "data") || named(name, "dfn")
           || named(name, "font")
           || named(name, "mark") || named(name, "q")
           || named(name, "time")
           || named(name, "small") || named(name, "big")
           || named(name, "acronym") || named(name, "var")
           || named(name, "sub") || named(name, "sup")
           || named(name, "em") || named(name, "b")
           || named(name, "strong") || named(name, "code")
           || named(name, "i") || named(name, "del")
           || named(name, "s") || named(name, "strike")
           || named(name, "samp") || named(name, "kbd");
}

static bool
mathml_text_integration(lxa_span_t name)
{
    return named(name, "mi") || named(name, "mo") || named(name, "mn")
           || named(name, "ms") || named(name, "mtext");
}

static lxa_status_t
annotation_html_integration(lxa_html_tree_t *tree,
                            lxa_dom_ref_t element, bool *html)
{
    lxa_dom_ref_t attribute;
    lxa_span_t name, value;
    lxa_status_t status;
    *html = false;
    status = lxa_dom_nodes_find_attribute(tree->nodes, element,
                                           literal("encoding"), &attribute);
    if (status != LXA_OK || attribute.handle == LXA_HANDLE_NONE) return status;
    status = lxa_dom_nodes_attribute_read(tree->nodes, attribute, &name, &value);
    if (status != LXA_OK) return status;
    *html = ascii_case_equals(value, "text/html", sizeof("text/html") - 1)
            || ascii_case_equals(value, "application/xhtml+xml",
                                 sizeof("application/xhtml+xml") - 1);
    return LXA_OK;
}

static bool
simple_container(lxa_span_t name)
{
    return block_container(name) || inline_container(name)
           || named(name, "p") || named(name, "li")
           || named(name, "dt") || named(name, "dd")
           || named(name, "noscript")
           || named(name, "select")
           || named(name, "option") || named(name, "optgroup")
           || heading(name);
}

static bool
table_section(lxa_span_t name)
{
    return named(name, "tbody") || named(name, "thead")
           || named(name, "tfoot");
}

static bool
table_part(lxa_span_t name)
{
    return named(name, "table") || named(name, "caption")
           || named(name, "colgroup") || named(name, "col")
           || table_section(name)
           || named(name, "tr") || named(name, "td") || named(name, "th");
}

static bool
table_context(lxa_span_t name)
{
    return named(name, "table") || named(name, "colgroup")
           || table_section(name)
           || named(name, "tr");
}

static bool
body_void(lxa_span_t name)
{
    return named(name, "br") || named(name, "img")
           || named(name, "basefont") || named(name, "bgsound")
           || named(name, "input") || named(name, "keygen")
           || named(name, "hr")
           || named(name, "wbr") || named(name, "area")
           || named(name, "embed") || named(name, "source")
           || named(name, "track") || named(name, "param");
}

static lxa_status_t
hidden_input(lxa_html_tree_t *tree,
             const lxa_html_token_t *token, bool *result)
{
    size_t i;
    *result = false;
    for (i = 0; i < token->attribute_count; ++i) {
        lxa_span_t key = token->attributes[i].name;
        lxa_span_t value = token->attributes[i].value;
        static const char hidden[] = "hidden";
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if ((key.data == NULL && key.length != 0)
            || (value.data == NULL && value.length != 0))
            return LXA_ERROR_ARGUMENT;
        if (!named(key, "type")) continue;
        *result = ascii_case_equals(value, hidden, sizeof(hidden) - 1);
        return LXA_OK;
    }
    return LXA_OK;
}

static lxa_status_t
font_breakout(lxa_html_tree_t *tree,
              const lxa_html_token_t *token, bool *result)
{
    size_t i;
    *result = false;
    for (i = 0; i < token->attribute_count; ++i) {
        lxa_span_t name = token->attributes[i].name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (name.data == NULL && name.length != 0)
            return LXA_ERROR_ARGUMENT;
        if (named(name, "color") || named(name, "face")
            || named(name, "size")) {
            *result = true;
            return LXA_OK;
        }
    }
    return LXA_OK;
}

static bool
foreign_html_start_breakout(lxa_span_t name)
{
    return named(name, "p") || named(name, "div")
        || named(name, "body") || named(name, "span")
        || named(name, "br") || named(name, "img")
        || named(name, "embed") || named(name, "meta")
        || named(name, "hr") || named(name, "ul")
        || named(name, "ol") || named(name, "menu")
        || named(name, "li") || named(name, "ruby")
        || named(name, "pre") || named(name, "listing")
        || named(name, "blockquote") || named(name, "center")
        || named(name, "dl") || named(name, "dt")
        || named(name, "dd") || named(name, "table")
        || heading(name) || named(name, "b")
        || named(name, "em") || named(name, "strong")
        || named(name, "i") || named(name, "sub")
        || named(name, "sup") || named(name, "big")
        || named(name, "small") || named(name, "s")
        || named(name, "strike") || named(name, "tt")
        || named(name, "u") || named(name, "var")
        || named(name, "nobr") || named(name, "code");
}

static lxa_status_t
in_tag_table(lxa_html_tree_t *tree, lxa_span_t name,
             const liblexa_html_element *table, size_t count, bool *found)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        size_t shared = name.length < table[mid].length
                        ? name.length : table[mid].length;
        int order;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        order = memcmp(name.data, table[mid].name, shared);
        if (order == 0)
            order = (name.length > table[mid].length)
                    - (name.length < table[mid].length);
        if (order == 0) {
            *found = true;
            return LXA_OK;
        }
        if (order < 0) hi = mid;
        else lo = mid + 1;
    }
    *found = false;
    return LXA_OK;
}

static lxa_status_t
supported_container(lxa_html_tree_t *tree,
                    lxa_span_t name, bool *supported)
{
    bool known;
    lxa_status_t status;
    if (simple_container(name) || named(name, "selectedcontent")
        || named(name, "picture")
        || named(name, "video") || named(name, "audio")
        || named(name, "map") || named(name, "canvas")
        || named(name, "legend") || named(name, "isindex")
        || named(name, "menuitem") || named(name, "multicol")
        || named(name, "nextid") || named(name, "spacer")) {
        *supported = true;
        return LXA_OK;
    }
    /* <image> is a parser alias of <img>, not a generic unknown element. */
    if (name.length == 0 || named(name, "image")) {
        *supported = false;
        return LXA_OK;
    }
    status = in_tag_table(tree, name, liblexa_html_elements,
                          LIBLEXA_HTML_ELEMENT_COUNT, &known);
    if (status != LXA_OK) return status;
    if (!known)
        status = in_tag_table(tree, name, liblexa_html_obsolete_elements,
                              LIBLEXA_HTML_OBSOLETE_COUNT, &known);
    if (status == LXA_OK) *supported = !known;
    return status;
}

static bool
space_only(lxa_span_t data)
{
    size_t i;
    for (i = 0; i < data.length; ++i) {
        uint8_t c = data.data[i];
        if (c != ' ' && c != '\n' && c != '\t' && c != '\f' && c != '\r'
            && c != 0)
            return false;
    }
    return true;
}

static lxa_status_t
ensure_element(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
               const char *name, lxa_dom_ref_t *slot)
{
    lxa_dom_ref_t node;
    lxa_status_t status;
    if (slot->handle != LXA_HANDLE_NONE) return LXA_OK;
    status = lxa_dom_nodes_append_html_element(tree->nodes, literal(name), &node);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes, parent,
                                                          node);
    if (status == LXA_OK) *slot = node;
    return status;
}

static lxa_status_t
ensure_html(lxa_html_tree_t *tree)
{
    return ensure_element(tree, tree->document, "html", &tree->html);
}

static lxa_status_t
ensure_head(lxa_html_tree_t *tree)
{
    lxa_status_t status = ensure_html(tree);
    return status == LXA_OK
           ? ensure_element(tree, tree->html, "head", &tree->head) : status;
}

static lxa_status_t
ensure_body(lxa_html_tree_t *tree)
{
    lxa_status_t status = ensure_head(tree);
    if (status == LXA_OK)
        status = ensure_element(tree, tree->html, "body", &tree->body);
    if (status != LXA_OK || lxa_vector_length(&tree->open_elements) != 0)
        return status;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                               1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_vector_push(&tree->open_elements, &tree->body);
    if (status != LXA_OK)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static lxa_dom_ref_t
current_element(const lxa_html_tree_t *tree)
{
    size_t count = lxa_vector_length(&tree->open_elements);
    return ((const lxa_dom_ref_t *) tree->open_elements.bytes.data)[count - 1];
}

static lxa_status_t
push_element(lxa_html_tree_t *tree, lxa_dom_ref_t element)
{
    lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_vector_push(&tree->open_elements, &element);
    if (status != LXA_OK)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static lxa_status_t
remove_open_element(lxa_html_tree_t *tree, lxa_vector_t *elements, size_t index)
{
    lxa_dom_record_t record;
    lxa_dom_ref_t ref;
    lxa_status_t status;
    if (index >= lxa_vector_length(elements)) return LXA_ERROR_INTERNAL;
    ref = ((const lxa_dom_ref_t *) elements->bytes.data)[index];
    status = lxa_dom_nodes_read(tree->nodes, ref, &record);
    if (status == LXA_OK && (record.flags & LXA_DOM_OPTION_SELECTED) != 0)
        status = lxa_dom_nodes_maybe_clone_selected_option(tree->nodes, ref);
    if (status != LXA_OK) return status;
    lxa_vector_erase(elements, index, 1);
    lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
    return LXA_OK;
}

static lxa_status_t
pop_elements(lxa_html_tree_t *tree, lxa_vector_t *elements, size_t keep)
{
    size_t length = lxa_vector_length(elements);
    if (keep > length) return LXA_ERROR_INTERNAL;
    while (length > keep) {
        lxa_status_t status = remove_open_element(tree, elements, length - 1);
        if (status != LXA_OK) return status;
        --length;
    }
    return LXA_OK;
}

static lxa_status_t
pop_element(lxa_html_tree_t *tree)
{
    size_t length = lxa_vector_length(&tree->open_elements);
    return length == 0 ? LXA_ERROR_INTERNAL
        : remove_open_element(tree, &tree->open_elements, length - 1);
}

static void
forget_anchor(lxa_html_tree_t *tree)
{
    if (tree->active_anchor.handle != LXA_HANDLE_NONE) {
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                           1);
        tree->active_anchor = (lxa_dom_ref_t){0};
    }
}

static void
forget_pending_formatting(lxa_html_tree_t *tree)
{
    if (tree->pending_formatting.handle != LXA_HANDLE_NONE) {
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                           1);
        tree->pending_formatting = (lxa_dom_ref_t){0};
    }
}

static void
forget_pending_template_formatting(lxa_html_tree_t *tree)
{
    if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE) {
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                           1);
        tree->pending_template_formatting = (lxa_dom_ref_t){0};
    }
}

static lxa_status_t
reference_is_open(lxa_html_tree_t *tree, lxa_dom_ref_t ref,
                  bool *found)
{
    size_t i;
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    *found = false;
    for (i = 0; i < lxa_vector_length(&tree->open_elements); ++i) {
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (open[i].handle == ref.handle) {
            *found = true;
            break;
        }
    }
    return LXA_OK;
}

static lxa_status_t reserve_open_elements(lxa_html_tree_t *,
                                           size_t);

static lxa_status_t
clone_formatting_element(lxa_html_tree_t *tree,
                         lxa_dom_ref_t old, lxa_dom_ref_t *out)
{
    lxa_dom_ref_t clone, attr = {0}, next, copied;
    lxa_span_t name, key, value;
    lxa_status_t status;
    status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                              1, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, old, &name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_html_element(tree->nodes, name, &clone);
    while (status == LXA_OK) {
        status = lxa_dom_nodes_attribute_next(tree->nodes, old, attr, &next);
        if (status != LXA_OK || next.handle == LXA_HANDLE_NONE) break;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                   &tree->nodes->limit_reason);
        if (status != LXA_OK) break;
        status = lxa_dom_nodes_attribute_read(tree->nodes, next, &key, &value);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_html_attribute(tree->nodes, key,
                                                           value, &copied);
        if (status == LXA_OK)
            status = lxa_dom_nodes_attach_attribute(tree->nodes, clone, copied);
        attr = next;
    }
    if (status == LXA_OK) *out = clone;
    return status;
}

static lxa_status_t
reconstruct_element(lxa_html_tree_t *tree,
                    lxa_dom_ref_t old, lxa_dom_ref_t *out)
{
    lxa_dom_ref_t clone;
    lxa_status_t status = reserve_open_elements(tree, 1);
    if (status == LXA_OK)
        status = clone_formatting_element(tree, old, &clone);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(
            tree->nodes, current_element(tree), clone);
    if (status == LXA_OK) status = push_element(tree, clone);
    if (status == LXA_OK) *out = clone;
    return status;
}

/* The active <a> entry survives implied </li>. Recreate its tag and parsed
 * attributes under the next item before inserting content there. Both the
 * original and reconstructed nodes stay in the canonical ID store. */
static lxa_status_t
reconstruct_anchor(lxa_html_tree_t *tree)
{
    lxa_status_t status;
    bool found;
    if (tree->active_anchor.handle == LXA_HANDLE_NONE) return LXA_OK;
    status = reference_is_open(tree, tree->active_anchor, &found);
    if (status != LXA_OK || found) return status;
    return reconstruct_element(tree, tree->active_anchor,
                               &tree->active_anchor);
}

/* Recreate the innermost formatting element after its enclosing formatting
 * end tag was seen out of order. The previous node remains in the tree. */
static lxa_status_t
reconstruct_pending_formatting(lxa_html_tree_t *tree)
{
    lxa_dom_ref_t old = tree->pending_formatting;
    lxa_dom_ref_t clone;
    bool found;
    lxa_status_t status;
    if (old.handle == LXA_HANDLE_NONE) return LXA_OK;
    status = reference_is_open(tree, old, &found);
    if (status != LXA_OK || found) return status;
    status = reconstruct_element(tree, old, &clone);
    if (status == LXA_OK) tree->pending_formatting = clone;
    return status;
}

static lxa_status_t
place_adoption_node(lxa_html_tree_t *tree, lxa_dom_ref_t node,
                    lxa_dom_ref_t parent, lxa_dom_ref_t before)
{
    lxa_dom_ref_t current;
    lxa_dom_record_t host;
    bool ancestor;
    lxa_status_t status = lxa_dom_nodes_parent(tree->nodes, node, &current);
    if (status == LXA_OK && current.handle != LXA_HANDLE_NONE)
        status = lxa_dom_nodes_detach_constructed_element(tree->nodes, node);
    if (status == LXA_OK)
        status = lxa_dom_nodes_parent(tree->nodes, node, &current);
    if (status != LXA_OK || current.handle != LXA_HANDLE_NONE) return status;
    status = lxa_dom_nodes_host_including_ancestor(tree->nodes, node, parent,
                                                   &ancestor);
    if (status != LXA_OK || ancestor) return status;
    status = lxa_dom_nodes_read(tree->nodes, parent, &host);
    if (status != LXA_OK) return status;
    if (host.kind == LXA_DOM_KIND_DOCUMENT) {
        size_t visits = 0;
        status = lxa_dom_nodes_first_child(tree->nodes, parent, &current);
        while (status == LXA_OK && current.handle != LXA_HANDLE_NONE) {
            lxa_dom_record_t child;
            if (++visits > lxa_dom_nodes_length(tree->nodes))
                return LXA_ERROR_INTERNAL;
            status = lxa_dom_nodes_read(tree->nodes, current, &child);
            if (status != LXA_OK || child.kind == LXA_DOM_KIND_ELEMENT)
                return status;
            status = lxa_dom_nodes_next_sibling(tree->nodes, current, &current);
        }
        if (status != LXA_OK) return status;
    }
    if (before.handle != LXA_HANDLE_NONE) {
        status = lxa_dom_nodes_parent(tree->nodes, before, &current);
        if (status != LXA_OK || current.handle != parent.handle) return status;
    }
    return lxa_dom_nodes_insert_constructed_subtree(tree->nodes, parent, node,
                                                    before);
}

static lxa_status_t
adopt_anchor_blocks(lxa_html_tree_t *tree)
{
    size_t length = lxa_vector_length(&tree->open_elements), i, anchor = length;
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    lxa_status_t status;
    if (tree->pending_formatting.handle != LXA_HANDLE_NONE)
        return LXA_OK; /* A second formatting entry needs the full list. */
    for (i = length; i > 1; ) {
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (open[--i].handle == tree->active_anchor.handle) {
            anchor = i;
            break;
        }
    }
    if (anchor == length || anchor + 1 == length)
        return LXA_OK;
    /* This is the direct block chain of the adoption algorithm. Do not
     * rearrange an intervening formatting entry without its own list. */
    {
        lxa_span_t common_name;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                 open[anchor - 1],
                                                 &common_name);
        if (status != LXA_OK) return status;
        if (!named(common_name, "body") && !named(common_name, "div")
            && !named(common_name, "address")
            && !(named(common_name, "b") && length == anchor + 2))
            return LXA_OK;
    }
    for (i = anchor + 1; i < length; ++i) {
        lxa_span_t name;
        lxa_dom_ref_t parent;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status == LXA_OK)
            status = lxa_dom_nodes_parent(tree->nodes, open[i], &parent);
        if (status != LXA_OK) return status;
        if ((!named(name, "div") && !named(name, "address")
             && !((named(name, "p") || named(name, "button"))
                  && i == anchor + 1
                  && length == anchor + 2))
            || parent.handle != open[i - 1].handle)
            return LXA_OK;
    }
    status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                               length - anchor - 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    while (lxa_vector_length(&tree->open_elements) > anchor + 1) {
        lxa_dom_ref_t common, block, clone, first;
        size_t visits = 0;
        open = (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
        common = open[anchor - 1];
        block = open[anchor + 1];
        status = clone_formatting_element(tree, tree->active_anchor, &clone);
        if (status == LXA_OK)
            status = place_adoption_node(tree, block, common, (lxa_dom_ref_t){0});
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                              block, clone);
        while (status == LXA_OK) {
            status = lxa_dom_nodes_first_child(tree->nodes, block, &first);
            if (status != LXA_OK || first.handle == clone.handle) break;
            if (++visits > lxa_dom_nodes_length(tree->nodes))
                return LXA_ERROR_INTERNAL;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                    clone, first);
        }
        if (status != LXA_OK) return status;
        status = lxa_vector_erase(&tree->open_elements, anchor, 1);
        if (status != LXA_OK) return status;
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
        status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_vector_insert(&tree->open_elements, anchor + 1, &clone);
        if (status != LXA_OK) {
            lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
            return status;
        }
        tree->active_anchor = clone;
        ++anchor;
    }
    return LXA_OK;
}

static lxa_status_t
close_active_anchor(lxa_html_tree_t *tree)
{
    bool found = false;
    lxa_status_t status;
    if (tree->active_anchor.handle != LXA_HANDLE_NONE) {
        status = reference_is_open(tree, tree->active_anchor, &found);
        if (status != LXA_OK) return status;
    }
    if (found) {
        const lxa_dom_ref_t *open;
        status = adopt_anchor_blocks(tree);
        if (status != LXA_OK) return status;
        open = (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
        while (lxa_vector_length(&tree->open_elements) != 0) {
            bool is_anchor = open[lxa_vector_length(&tree->open_elements) - 1]
                             .handle == tree->active_anchor.handle;
            status = pop_element(tree);
            if (status != LXA_OK) return status;
            if (is_anchor) break;
        }
    }
    forget_anchor(tree);
    return LXA_OK;
}

static bool
basic_formatting(lxa_span_t name)
{
    return named(name, "b") || named(name, "i")
           || named(name, "em") || named(name, "strong")
           || named(name, "code");
}

/* With no furthest block, closing an anchor under one formatting element
 * leaves that element in the formatting list for the next text token. */
static lxa_status_t
close_crossed_anchor_formatting(lxa_html_tree_t *tree,
                                bool *closed)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->open_elements);
    lxa_dom_ref_t pending, parent;
    lxa_span_t name;
    lxa_status_t status;
    *closed = false;
    if (tree->active_anchor.handle == LXA_HANDLE_NONE
        || tree->pending_formatting.handle != LXA_HANDLE_NONE
        || length < 3
        || open[length - 2].handle != tree->active_anchor.handle)
        return LXA_OK;
    pending = open[length - 1];
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, pending, &name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_parent(tree->nodes, pending, &parent);
    if (status != LXA_OK) return status;
    if (!basic_formatting(name)
        || parent.handle != tree->active_anchor.handle)
        return LXA_OK;
    status = pop_element(tree);
    if (status != LXA_OK) return status;
    status = pop_element(tree);
    if (status != LXA_OK) return status;
    /* The inactive formatting entry takes over the anchor's existing slot. */
    tree->active_anchor = (lxa_dom_ref_t){0};
    tree->pending_formatting = pending;
    *closed = true;
    return LXA_OK;
}

/* The first furthest-block case: a paragraph directly inside two open
 * formatting elements. The original branch keeps its prior children; the
 * paragraph moves under a replacement inner element and its contents move
 * under a clone of the closed outer element. Other adoption shapes still
 * require the general active-formatting algorithm. */
static lxa_status_t
close_crossed_paragraph(lxa_html_tree_t *tree,
                        lxa_span_t target, bool *closed)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->open_elements);
    lxa_dom_ref_t outer, inner, paragraph, parent, actual;
    lxa_dom_ref_t replacement, clone;
    lxa_dom_ref_t first;
    lxa_span_t outer_name, inner_name, top_name;
    lxa_status_t status;
    size_t visits = 0;
    *closed = false;
    if (!basic_formatting(target) || length < 4)
        return LXA_OK;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                3, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    outer = open[length - 3];
    inner = open[length - 2];
    paragraph = open[length - 1];
    parent = open[length - 4];
    status = lxa_dom_nodes_element_name(tree->nodes, outer, &outer_name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, inner, &inner_name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, paragraph, &top_name);
    if (status != LXA_OK) return status;
    if (!named(top_name, "p") || !basic_formatting(inner_name)
        || outer_name.length != target.length
        || memcmp(outer_name.data, target.data, target.length) != 0)
        return LXA_OK;
    status = lxa_dom_nodes_parent(tree->nodes, outer, &actual);
    if (status != LXA_OK || actual.handle != parent.handle)
        return status == LXA_OK ? LXA_ERROR_UNSUPPORTED : status;
    status = lxa_dom_nodes_parent(tree->nodes, inner, &actual);
    if (status != LXA_OK || actual.handle != outer.handle)
        return status == LXA_OK ? LXA_ERROR_UNSUPPORTED : status;
    status = lxa_dom_nodes_parent(tree->nodes, paragraph, &actual);
    if (status != LXA_OK || actual.handle != inner.handle)
        return status == LXA_OK ? LXA_ERROR_UNSUPPORTED : status;
    if (tree->pending_formatting.handle != LXA_HANDLE_NONE
        || tree->active_anchor.handle != LXA_HANDLE_NONE)
        return LXA_ERROR_UNSUPPORTED;
    status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                               2, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget,
            LXA_BUDGET_ACTIVE_FORMATTING, 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = clone_formatting_element(tree, inner, &replacement);
    if (status == LXA_OK)
        status = clone_formatting_element(tree, outer, &clone);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                         parent, replacement);
    if (status == LXA_OK)
        status = lxa_dom_nodes_reparent_constructed_child(
            tree->nodes, replacement, paragraph);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                         paragraph, clone);
    while (status == LXA_OK) {
        status = lxa_dom_nodes_first_child(tree->nodes, paragraph, &first);
        if (status != LXA_OK || first.handle == clone.handle) break;
        if (++visits > lxa_dom_nodes_length(tree->nodes))
            return LXA_ERROR_INTERNAL;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                clone, first);
    }
    if (status != LXA_OK) return status;
    status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_ACTIVE_FORMATTING, 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = pop_element(tree);
    if (status != LXA_OK) return status;
    status = pop_element(tree);
    if (status != LXA_OK) return status;
    status = pop_element(tree);
    if (status != LXA_OK) return status;
    status = push_element(tree, replacement);
    if (status == LXA_OK) status = push_element(tree, paragraph);
    if (status == LXA_OK) tree->pending_formatting = replacement;
    else lxa_budget_release(tree->nodes->budget,
                             LXA_BUDGET_ACTIVE_FORMATTING, 1);
    *closed = status == LXA_OK;
    return status;
}

/* One crossed pair with no intervening block has no furthest block to move.
 * Keep the inner element as an inactive formatting entry for reconstruction. */
static lxa_status_t
close_crossed_formatting(lxa_html_tree_t *tree,
                         lxa_span_t target, bool *closed)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->open_elements);
    lxa_span_t inner, outer;
    lxa_dom_ref_t pending;
    lxa_status_t status;
    *closed = false;
    if (!basic_formatting(target) || length < 3)
        return LXA_OK;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                2, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, open[length - 1],
                                             &inner);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, open[length - 2],
                                             &outer);
    if (status != LXA_OK) return status;
    if (!basic_formatting(inner)
        || outer.length != target.length
        || memcmp(outer.data, target.data, outer.length) != 0)
        return LXA_OK;
    if (tree->pending_formatting.handle != LXA_HANDLE_NONE)
        return LXA_ERROR_UNSUPPORTED; /* A second entry needs a formatting list. */
    status = lxa_budget_check(tree->nodes->budget,
        LXA_BUDGET_ACTIVE_FORMATTING, 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    pending = open[length - 1];
    status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_ACTIVE_FORMATTING, 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = pop_element(tree);
    if (status != LXA_OK) return status;
    status = pop_element(tree);
    if (status != LXA_OK) return status;
    tree->pending_formatting = pending;
    *closed = true;
    return LXA_OK;
}

static lxa_status_t
reserve_open_elements(lxa_html_tree_t *tree, size_t count)
{
    size_t required;
    lxa_status_t status = lxa_budget_check(tree->nodes->budget,
        LXA_BUDGET_OPEN_ELEMENTS, count, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&tree->open_elements),
                              count, &required);
    if (status == LXA_OK)
        status = lxa_vector_reserve(&tree->open_elements, required);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

/* A new item closes the preceding item in this list, including any
 * still-open descendants. An inner list stops the search at its own root. */
static lxa_status_t
close_list_item(lxa_html_tree_t *tree, bool definition)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    while (i > 1) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML
            && (named(name, "ul") || named(name, "ol") || named(name, "dl")
                || named(name, "menu")))
            break;
        if (ns == LXA_DOM_NS_HTML
            && ((!definition && named(name, "li"))
                || (definition && (named(name, "dt")
                                   || named(name, "dd"))))) {
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            break;
        }
        if (special_element(name, ns)
            && !(ns == LXA_DOM_NS_HTML
                 && (named(name, "address") || named(name, "div")
                     || named(name, "p"))))
            break;
    }
    return LXA_OK;
}

/* A block or <hr> closes a paragraph even when inline descendants are open.
 * The search stops at the nearest scope boundary; failed work/node preflight
 * leaves the open stack untouched. */
static lxa_status_t
close_paragraph(lxa_html_tree_t *tree, bool *closed)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    if (closed != NULL) *closed = false;
    while (i > 0) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) break;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "p")) {
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            if (closed != NULL) *closed = true;
            return LXA_OK;
        }
        if (named(name, "body") || named(name, "li")
            || named(name, "td") || named(name, "th")
            || named(name, "caption") || named(name, "button"))
            break;
    }
    return LXA_OK;
}

static lxa_status_t
close_list(lxa_html_tree_t *tree, lxa_span_t target)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    while (i > 1) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) break;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (name.length == target.length
            && memcmp(name.data, target.data, name.length) == 0) {
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        if (named(name, "body") || named(name, "table")
            || named(name, "caption") || named(name, "td")
            || named(name, "th"))
            break;
    }
    return LXA_OK;
}

static lxa_status_t
end_list_item(lxa_html_tree_t *tree, lxa_span_t target)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    bool definition = named(target, "dt") || named(target, "dd");
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    while (i > 1) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "ul") || named(name, "ol") || named(name, "dl")
            || named(name, "menu"))
            break;
        if ((definition && (named(name, "dt") || named(name, "dd")))
            || (!definition && named(name, "li"))) {
            if (name.length != target.length
                || memcmp(name.data, target.data, name.length) != 0)
                return LXA_OK;
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
    }
    /* A list-item end without a matching item in this list's scope is a
     * parse error, but does not abort construction or close another list. */
    return LXA_OK;
}

/* The any-other-end-tag search for ordinary HTML elements stops at an
 * HTML special element; a missing match is an ignored parse error. */
static lxa_status_t
end_ordinary_element(lxa_html_tree_t *tree, lxa_span_t target)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    size_t i = lxa_vector_length(&tree->open_elements);
    while (i > 0) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        bool ordinary;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) break;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (name.length == target.length
            && memcmp(name.data, target.data, name.length) == 0) {
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        if (inline_container(name)) continue;
        status = supported_container(tree, name, &ordinary);
        if (status != LXA_OK) return status;
        if (!ordinary || simple_container(name)) break;
    }
    return LXA_OK;
}

/* Close a block or heading through open inline descendants, but do not
 * cross a different special element or a table/list scope boundary. */
static lxa_status_t
end_scoped_container(lxa_html_tree_t *tree, lxa_span_t target)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    while (i > 0) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (name.length == target.length
            && memcmp(name.data, target.data, name.length) == 0) {
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        if (!inline_container(name))
            break;
    }
    /* A block end with no matching element in the supported scope is
     * ignored; it must not terminate an unrelated parent. */
    return LXA_OK;
}

static lxa_status_t
close_open_heading(lxa_html_tree_t *tree)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    while (i > 0) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (heading(name)) {
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        if (!inline_container(name))
            break;
    }
    return LXA_OK;
}

static lxa_status_t
close_open_button(lxa_html_tree_t *tree)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    while (i > 0) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "button")) {
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        if (named(name, "body") || named(name, "li")
            || named(name, "td") || named(name, "th")
            || named(name, "caption"))
            break;
    }
    return LXA_OK;
}

static lxa_status_t
append_character(lxa_html_tree_t *tree,
                 lxa_dom_ref_t parent, lxa_dom_kind_t kind, lxa_span_t bytes)
{
    lxa_dom_ref_t child;
    lxa_status_t status = lxa_dom_nodes_append_character(
        tree->nodes, kind, bytes, &child);
    return status == LXA_OK
           ? lxa_dom_nodes_append_constructed_child(tree->nodes, parent, child)
           : status;
}

static lxa_status_t
foster_location_in(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                    size_t boundary, lxa_dom_ref_t fallback,
                    lxa_dom_ref_t *parent, lxa_dom_ref_t *before)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              elements->bytes.data;
    size_t i = lxa_vector_length(elements);
    lxa_status_t status;
    if (boundary > i || fallback.handle == LXA_HANDLE_NONE)
        return LXA_ERROR_INTERNAL;
    *parent = fallback;
    *before = (lxa_dom_ref_t){0};
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_dom_ref_t table = open[--i];
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes, table, &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, table, &name);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML || !named(name, "table")) continue;
        status = lxa_dom_nodes_parent(tree->nodes, table, parent);
        if (status != LXA_OK) return status;
        if (parent->handle != LXA_HANDLE_NONE) *before = table;
        else *parent = i > boundary ? open[i - 1] : fallback;
        return LXA_OK;
    }
    return LXA_OK;
}

static lxa_status_t
foster_location(lxa_html_tree_t *tree,
                lxa_dom_ref_t *parent, lxa_dom_ref_t *table)
{
    return foster_location_in(tree, &tree->open_elements, 0, tree->html,
                               parent, table);
}

static lxa_status_t
flush_table_text(lxa_html_tree_t *tree)
{
    lxa_dom_ref_t table, parent, child;
    lxa_span_t bytes, top;
    size_t prefix = 0;
    lxa_status_t status;
    if (tree->table_text.length == 0) return LXA_OK;
    bytes.data = tree->table_text.data;
    bytes.length = tree->table_text.length;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                               bytes.length, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (space_only(bytes)) {
        status = append_character(tree, current_element(tree),
                                  LXA_DOM_KIND_TEXT, bytes);
    }
    else {
        status = lxa_dom_nodes_element_name(tree->nodes,
                                            current_element(tree), &top);
        if (status != LXA_OK) return status;
        if (named(top, "colgroup")) {
            /* Whitespace before the first nonspace stays in colgroup.
             * Anything else closes it and is reprocessed in table mode. */
            while (prefix < bytes.length
                   && (bytes.data[prefix] == ' '
                       || bytes.data[prefix] == '\n'
                       || bytes.data[prefix] == '\r'
                       || bytes.data[prefix] == '\t'
                       || bytes.data[prefix] == '\f'))
                ++prefix;
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       prefix != 0 ? 2 : 1,
                                       &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (prefix != 0) {
                lxa_span_t spaces = {bytes.data, prefix};
                status = append_character(tree, current_element(tree),
                                           LXA_DOM_KIND_TEXT, spaces);
                if (status != LXA_OK) return status;
            }
            status = pop_element(tree);
            if (status != LXA_OK) return status;
            bytes.data += prefix;
            bytes.length -= prefix;
        }
        status = foster_location(tree, &parent, &table);
        if (status != LXA_OK) return status;
        status = lxa_dom_nodes_append_character(tree->nodes,
                                                LXA_DOM_KIND_TEXT, bytes, &child);
        if (status == LXA_OK)
            status = table.handle == LXA_HANDLE_NONE
                ? lxa_dom_nodes_append_constructed_child(
                    tree->nodes, parent, child)
                : lxa_dom_nodes_insert_constructed_before(
                    tree->nodes, parent, child, table);
    }
    if (status == LXA_OK) lxa_buffer_clear(&tree->table_text);
    return status;
}

static lxa_status_t
queue_table_text(lxa_html_tree_t *tree, lxa_span_t bytes)
{
    size_t combined;
    lxa_status_t status = lxa_size_add(tree->table_text.length,
                                       bytes.length, &combined);
    if (status == LXA_OK)
        status = lxa_budget_check_item(tree->nodes->budget,
            LXA_BUDGET_TOKEN_BYTES, combined, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    bytes.length, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_buffer_append(&tree->table_text, bytes);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

/* DATA nulls have tree-context semantics. Scan bounded token spans so text
 * across nulls is retained without allocating or treating NUL as a terminator. */
static lxa_status_t
insert_data_text(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
                 lxa_span_t bytes, bool foreign, bool table)
{
    static const uint8_t replacement[] = {0xef, 0xbf, 0xbd};
    size_t offset = 0;
    lxa_status_t status;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                               bytes.length, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    while (offset < bytes.length) {
        const uint8_t *nul = memchr(bytes.data + offset, 0,
                                    bytes.length - offset);
        size_t run = nul == NULL ? bytes.length - offset
                                : (size_t) (nul - (bytes.data + offset));
        if (run != 0) {
            lxa_span_t segment = {bytes.data + offset, run};
            status = table ? queue_table_text(tree, segment)
                           : append_character(tree, parent, LXA_DOM_KIND_TEXT,
                                              segment);
            if (status != LXA_OK) return status;
        }
        if (nul == NULL) break;
        if (foreign) {
            lxa_span_t segment = {replacement, sizeof(replacement)};
            status = append_character(tree, parent, LXA_DOM_KIND_TEXT, segment);
            if (status != LXA_OK) return status;
        }
        offset += run + 1;
    }
    return LXA_OK;
}

static lxa_status_t
apply_attributes(lxa_html_tree_t *tree,
                 lxa_dom_ref_t element, const lxa_html_token_t *token)
{
    size_t i;
    lxa_dom_namespace_t ns;
    lxa_status_t status;
    if (token->attribute_count != 0 && token->attributes == NULL)
        return LXA_ERROR_ARGUMENT;
    if (token->attribute_count == 0) return LXA_OK;
    status = lxa_dom_nodes_element_namespace(tree->nodes, element, &ns);
    if (status != LXA_OK) return status;
    for (i = 0; i < token->attribute_count; ++i) {
        lxa_dom_ref_t existing, attribute;
        lxa_html_attribute_t entry = token->attributes[i];
        lxa_dom_attr_namespace_t attr_ns = LXA_DOM_ATTR_NS_NONE;
        if ((entry.name.data == NULL && entry.name.length != 0)
            || (entry.value.data == NULL && entry.value.length != 0))
            return LXA_ERROR_ARGUMENT;
        if (ns != LXA_DOM_NS_HTML) {
            if (named(entry.name, "xml:base") || named(entry.name, "xml:lang")
                || named(entry.name, "xml:space"))
                attr_ns = LXA_DOM_ATTR_NS_XML;
            else if (named(entry.name, "xlink:actuate")
                     || named(entry.name, "xlink:arcrole")
                     || named(entry.name, "xlink:href")
                     || named(entry.name, "xlink:role")
                     || named(entry.name, "xlink:show")
                     || named(entry.name, "xlink:title")
                     || named(entry.name, "xlink:type"))
                attr_ns = LXA_DOM_ATTR_NS_XLINK;
            else if (named(entry.name, "xmlns")
                     || named(entry.name, "xmlns:xlink"))
                attr_ns = LXA_DOM_ATTR_NS_XMLNS;
        }
        if (ns == LXA_DOM_NS_SVG)
            status = lxa_html_svg_attribute_name(entry.name, &entry.name);
        else if (ns == LXA_DOM_NS_MATHML)
            status = lxa_html_mathml_attribute_name(entry.name, &entry.name);
        if (status != LXA_OK) return status;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_find_attribute(tree->nodes, element,
                                                   entry.name, &existing);
        if (status != LXA_OK) return status;
        if (existing.handle != LXA_HANDLE_NONE) continue;
        status = ns == LXA_DOM_NS_HTML
            ? lxa_dom_nodes_append_html_attribute(tree->nodes, entry.name,
                                                   entry.value, &attribute)
            : lxa_dom_nodes_append_foreign_attribute(tree->nodes, entry.name,
                                                      entry.value, attr_ns,
                                                      &attribute);
        if (status == LXA_OK)
            status = lxa_dom_nodes_attach_attribute(tree->nodes, element,
                                                     attribute);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

static lxa_status_t
open_container_ns(lxa_html_tree_t *tree,
                  const lxa_html_token_t *token, lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t child;
    lxa_span_t name = token->name;
    lxa_status_t status;
    if (ns == LXA_DOM_NS_SVG) {
        status = lxa_html_svg_tag_name(name, &name);
        if (status != LXA_OK) return status;
    }
    status = lxa_dom_nodes_append_namespaced_element(tree->nodes,
                                                      name, ns, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                        current_element(tree), child);
    if (status == LXA_OK && ns == LXA_DOM_NS_HTML && named(name, "option"))
        status = lxa_dom_nodes_reset_option_select(tree->nodes, child);
    if (status == LXA_OK && ns == LXA_DOM_NS_HTML && named(name, "selectedcontent")
        && tree->mode != LXA_TREE_TEMPLATE_TEXT)
        status = lxa_dom_nodes_connect_selectedcontent(tree->nodes, child);
    if (status == LXA_OK) status = push_element(tree, child);
    return status;
}

static lxa_status_t
open_container(lxa_html_tree_t *tree,
               const lxa_html_token_t *token)
{
    return open_container_ns(tree, token, LXA_DOM_NS_HTML);
}

static lxa_status_t
append_comment_or_pi(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
                     const lxa_html_token_t *token)
{
    lxa_dom_ref_t child;
    lxa_status_t status;
    if (token->kind == LXA_HTML_COMMENT)
        return append_character(tree, parent, LXA_DOM_KIND_COMMENT,
                                token->data);
    status = lxa_dom_nodes_append_pi(tree->nodes, token->name, token->data,
                                     &child);
    return status == LXA_OK
           ? lxa_dom_nodes_append_constructed_child(tree->nodes, parent, child)
           : status;
}


/* Foreign descendants retain their parent's namespace unless an integration
 * point dispatches the token through HTML construction instead. */
static lxa_status_t
consume_foreign(lxa_html_tree_t *tree,
                const lxa_html_token_t *token, lxa_dom_namespace_t ns,
                bool *reprocess)
{
    lxa_span_t top;
    lxa_status_t status;
    bool known;
    *reprocess = false;
    if (token->kind == LXA_HTML_COMMENT
        || token->kind == LXA_HTML_PROCESSING_INSTRUCTION)
        return append_comment_or_pi(tree, current_element(tree), token);
    if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
    if (token->kind == LXA_HTML_TEXT)
        return insert_data_text(tree, current_element(tree), token->data,
                                true, false);
    if (token->kind == LXA_HTML_END_TAG) {
        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                                  tree->open_elements.bytes.data;
        size_t i = lxa_vector_length(&tree->open_elements);
        while (i > 0) {
            lxa_dom_namespace_t ancestor_ns;
            lxa_span_t target = token->name;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            --i;
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[i],
                                                      &ancestor_ns);
            if (status != LXA_OK) return status;
            if (ancestor_ns == LXA_DOM_NS_HTML) {
                if (named(token->name, "body")
                    || named(token->name, "html")) {
                    *reprocess = true;
                    return LXA_OK;
                }
                /* A matching ordinary HTML container closes through its
                 * foreign descendants. Do not let an unrelated HTML end
                 * tear down this subtree before its mode is supported. */
                if (named(token->name, "span")
                    || named(token->name, "div")
                    || named(token->name, "section")
                    || named(token->name, "article")
                    || named(token->name, "main")
                    || named(token->name, "aside")
                    || named(token->name, "header")
                    || named(token->name, "footer")
                    || named(token->name, "nav")) {
                    status = lxa_dom_nodes_element_name(tree->nodes, open[i],
                                                        &top);
                    if (status != LXA_OK) return status;
                    if (top.length == token->name.length
                        && memcmp(top.data, token->name.data, top.length) == 0) {
                        while (lxa_vector_length(&tree->open_elements) > i) {
                            status = pop_element(tree);
                            if (status != LXA_OK) return status;
                        }
                        return LXA_OK;
                    }
                }
                break;
            }
            status = lxa_dom_nodes_element_name(tree->nodes, open[i], &top);
            if (status != LXA_OK) return status;
            if (ancestor_ns == LXA_DOM_NS_SVG) {
                status = lxa_html_svg_tag_name(target, &target);
                if (status != LXA_OK) return status;
            }
            if (top.length == target.length
                && memcmp(top.data, target.data, top.length) == 0) {
                while (lxa_vector_length(&tree->open_elements) > i) {
                    status = pop_element(tree);
                    if (status != LXA_OK) return status;
                }
                return LXA_OK;
            }
        }
        /* Once the search reaches an HTML element, an unknown end tag is
         * ignored by the in-body fallback. An unmatched foreign root end
         * is ignored there too. Other known HTML ends still require their
         * insertion-mode rules; do not close the foreign stack. */
        status = in_tag_table(tree, token->name, liblexa_html_elements,
                              LIBLEXA_HTML_ELEMENT_COUNT, &known);
        if (status != LXA_OK) return status;
        if (!known)
            status = in_tag_table(tree, token->name,
                liblexa_html_obsolete_elements, LIBLEXA_HTML_OBSOLETE_COUNT,
                &known);
        return status != LXA_OK ? status
               : known && !named(token->name, "svg")
                       && !named(token->name, "math")
                       && !named(token->name, "span")
                 ? LXA_ERROR_UNSUPPORTED : LXA_OK;
    }
    if (token->kind != LXA_HTML_START_TAG)
        return LXA_ERROR_UNSUPPORTED;
    /* HTML breakout starts must be reprocessed outside the foreign stack;
     * until that algorithm exists, reject rather than construct false SVG/
     * MathML descendants. A foreign <a> is allowed too. */
    status = in_tag_table(tree, token->name, liblexa_html_elements,
                          LIBLEXA_HTML_ELEMENT_COUNT, &known);
    if (status != LXA_OK) return status;
    if (!known)
        status = in_tag_table(tree, token->name, liblexa_html_obsolete_elements,
                              LIBLEXA_HTML_OBSOLETE_COUNT, &known);
    if (status != LXA_OK) return status;
    if (known && !named(token->name, "a")
        && !named(token->name, "font")
        && !named(token->name, "svg")
        && !named(token->name, "math")
        && !(ns == LXA_DOM_NS_SVG
             && (named(token->name, "desc")
                 || named(token->name, "title")
                 || named(token->name, "template"))))
        return LXA_ERROR_UNSUPPORTED;
    status = reserve_open_elements(tree, 1);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   1, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = open_container_ns(tree, token,
            named(token->name, "svg") ? LXA_DOM_NS_SVG
            : named(token->name, "math") ? LXA_DOM_NS_MATHML : ns);
    if (status == LXA_OK && token->self_closing) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    return status;
}

static lxa_status_t
append_void(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
            const lxa_html_token_t *token);

static lxa_status_t
close_select(lxa_html_tree_t *tree)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    while (i > 1) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "select")) {
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
    }
    return LXA_ERROR_INTERNAL;
}

static lxa_status_t
select_open(lxa_html_tree_t *tree, bool *found)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    size_t i = lxa_vector_length(&tree->open_elements);
    *found = false;
    while (i > 1) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[--i],
                                                &name);
        if (status != LXA_OK) return status;
        if (named(name, "select")) {
            *found = true;
            break;
        }
        if (!named(name, "option") && !named(name, "optgroup")) break;
    }
    return LXA_OK;
}

typedef enum {
    SELECT_PARENT_OTHER, SELECT_PARENT_TABLE, SELECT_PARENT_SECTION,
    SELECT_PARENT_ROW, SELECT_PARENT_COLGROUP
} select_parent_t;

static lxa_status_t
select_table_scope(lxa_html_tree_t *tree, bool *found,
                   bool *cell, bool *caption, select_parent_t *parent)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    bool below_select = false;
    *found = *cell = *caption = false;
    *parent = SELECT_PARENT_OTHER;
    while (i > 1) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) {
            below_select = false;
            continue;
        }
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (below_select) {
            if (named(name, "table")) *parent = SELECT_PARENT_TABLE;
            else if (table_section(name)) *parent = SELECT_PARENT_SECTION;
            else if (named(name, "tr")) *parent = SELECT_PARENT_ROW;
            else if (named(name, "colgroup"))
                *parent = SELECT_PARENT_COLGROUP;
            below_select = false;
        }
        if (named(name, "select")) below_select = true;
        if (named(name, "td") || named(name, "th")) *cell = true;
        if (named(name, "caption")) *caption = true;
        if (named(name, "table")) {
            *found = true;
            return LXA_OK;
        }
        if (named(name, "html")) break;
    }
    return LXA_OK;
}

/* A select admits options and groups rather than ordinary body containers.
 * Keep unsupported select-mode tags explicit until their reprocessing rules
 * are implemented. */
static lxa_status_t
consume_select(lxa_html_tree_t *tree,
               const lxa_html_token_t *token, lxa_span_t top)
{
    lxa_status_t status;
    if (token->kind == LXA_HTML_START_TAG) {
        if (named(token->name, "select")) return close_select(tree);
        if (named(token->name, "hr")) {
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (named(top, "option")) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
                status = lxa_dom_nodes_element_name(tree->nodes,
                                                    current_element(tree),
                                                    &top);
                if (status != LXA_OK) return status;
            }
            if (named(top, "optgroup")) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return append_void(tree, current_element(tree), token);
        }
        if (!named(token->name, "option")
            && !named(token->name, "optgroup")) {
            /* Ordinary start tags inside a select are ignored. Keep tokens
             * requiring a different insertion mode or reprocessing explicit
             * until that mode is implemented. */
            return named(token->name, "input")
                   || named(token->name, "textarea")
                   || named(token->name, "keygen")
                   || named(token->name, "script")
                   || named(token->name, "template")
                   ? LXA_ERROR_UNSUPPORTED : LXA_OK;
        }
        status = reserve_open_elements(tree, 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (named(top, "option")) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                current_element(tree), &top);
            if (status != LXA_OK) return status;
        }
        if (named(token->name, "optgroup") && named(top, "optgroup")) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        return open_container(tree, token);
    }
    if (token->kind == LXA_HTML_END_TAG) {
        if (named(token->name, "select")) return close_select(tree);
        if (named(token->name, "option")) {
            if (named(top, "option")) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        if (named(token->name, "optgroup")) {
            if (named(top, "option")) {
                const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                                          tree->open_elements.bytes.data;
                size_t i = lxa_vector_length(&tree->open_elements);
                if (i > 1) {
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                    status = lxa_dom_nodes_element_name(tree->nodes,
                                                        open[i - 2], &top);
                    if (status != LXA_OK) return status;
                    if (named(top, "optgroup")) {
                        status = pop_element(tree);
                        if (status != LXA_OK) return status;
                    }
                }
                else return LXA_OK;
            }
            if (named(top, "optgroup")) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        /* Other end tags are parse errors in select mode. Table and
         * template endings still require their own insertion-mode rules. */
        return table_part(token->name) || named(token->name, "template")
               ? LXA_ERROR_UNSUPPORTED : LXA_OK;
    }
    return LXA_OK;
}

static lxa_status_t
foster_element(lxa_html_tree_t *tree,
               const lxa_html_token_t *token, bool is_void)
{
    lxa_dom_ref_t table, parent, child;
    lxa_status_t status = foster_location(tree, &parent, &table);
    if (status != LXA_OK) return status;
    if (!is_void) {
        status = reserve_open_elements(tree, 1);
        if (status != LXA_OK) return status;
    }
    status = lxa_dom_nodes_append_html_element(tree->nodes,
                                                token->name, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = table.handle == LXA_HANDLE_NONE
            ? lxa_dom_nodes_append_constructed_child(
                tree->nodes, parent, child)
            : lxa_dom_nodes_insert_constructed_before(
                tree->nodes, parent, child, table);
    if (status == LXA_OK && named(token->name, "selectedcontent"))
        status = lxa_dom_nodes_connect_selectedcontent(tree->nodes, child);
    if (status == LXA_OK && !is_void) status = push_element(tree, child);
    return status;
}

static lxa_status_t
foster_foreign(lxa_html_tree_t *tree,
               const lxa_html_token_t *token, lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t table, parent, child;
    lxa_span_t name = token->name;
    lxa_status_t status = reserve_open_elements(tree, 1);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   1, &tree->nodes->limit_reason);
    if (status == LXA_OK) status = foster_location(tree, &parent, &table);
    if (status == LXA_OK && ns == LXA_DOM_NS_SVG)
        status = lxa_html_svg_tag_name(name, &name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_namespaced_element(tree->nodes,
                                                          name, ns, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = table.handle == LXA_HANDLE_NONE
            ? lxa_dom_nodes_append_constructed_child(
                tree->nodes, parent, child)
            : lxa_dom_nodes_insert_constructed_before(
                tree->nodes, parent, child, table);
    if (status == LXA_OK) status = push_element(tree, child);
    if (status == LXA_OK && token->self_closing) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    return status;
}

typedef enum { TABLE_CELL, TABLE_ROW, TABLE_SECTION } table_group_t;

static lxa_status_t
close_open_table_part(lxa_html_tree_t *tree,
                      table_group_t group)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    size_t i = lxa_vector_length(&tree->open_elements);
    while (i > 1) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if ((group == TABLE_CELL
             && (named(name, "td") || named(name, "th")))
            || (group == TABLE_ROW && named(name, "tr"))
            || (group == TABLE_SECTION && table_section(name))) {
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        if (named(name, "table")
            || (group == TABLE_CELL && table_context(name))
            || (group == TABLE_ROW && table_section(name)))
            break;
    }
    return LXA_ERROR_UNSUPPORTED;
}

static lxa_status_t
end_table_part(lxa_html_tree_t *tree, lxa_span_t target);

static lxa_status_t
close_caption_for_table_start(lxa_html_tree_t *tree,
                              lxa_span_t incoming)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    size_t i = lxa_vector_length(&tree->open_elements);
    size_t needed = named(incoming, "td") || named(incoming, "th") ? 3
                    : named(incoming, "tr") || named(incoming, "col") ? 2 : 1;
    while (i > 1) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "caption")) {
            /* Reprocessing may allocate implicit tbody/tr nodes. Preflight
             * before discarding the open caption and its descendants. */
            status = reserve_open_elements(tree, needed);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, needed, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            return end_table_part(tree, literal("caption"));
        }
        if (named(name, "table")) break;
    }
    return LXA_OK;
}

static lxa_status_t
open_table_part(lxa_html_tree_t *tree,
                const lxa_html_token_t *token)
{
    lxa_html_token_t implied = {0};
    lxa_span_t top;
    bool section, row, cell;
    size_t extra = 1;
    lxa_status_t status = ensure_body(tree);
    if (status != LXA_OK) return status;
    tree->mode = LXA_TREE_IN_BODY;
    status = lxa_dom_nodes_element_name(tree->nodes,
                                        current_element(tree), &top);
    if (status != LXA_OK) return status;
    if ((block_container(top) || named(top, "p")
         || named(top, "span"))
        && (named(token->name, "tr")
            || named(token->name, "td") || named(token->name, "th"))) {
        const lxa_dom_ref_t *open =
            (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
        size_t length = lxa_vector_length(&tree->open_elements);
        lxa_span_t below;
        if (length > 2) {
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[length - 2], &below);
            if (status != LXA_OK) return status;
            if (named(below, "table")) {
                size_t needed = named(token->name, "tr") ? 2 : 3;
                status = reserve_open_elements(tree, needed);
                if (status == LXA_OK)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, needed,
                        &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = pop_element(tree);
                if (status != LXA_OK) return status;
                top = below;
            }
        }
    }
    if (named(token->name, "table")) {
        bool close_existing = table_context(top);
        if (!close_existing) {
            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                                      tree->open_elements.bytes.data;
            size_t i = lxa_vector_length(&tree->open_elements);
            while (i > 1) {
                lxa_span_t name;
                lxa_dom_namespace_t ns;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_namespace(tree->nodes,
                                                               open[--i], &ns);
                if (status != LXA_OK) return status;
                if (ns != LXA_DOM_NS_HTML) continue;
                status = lxa_dom_nodes_element_name(tree->nodes, open[i],
                                                     &name);
                if (status != LXA_OK) return status;
                if (named(name, "table")) {
                    close_existing = true;
                    break;
                }
                if (named(name, "td") || named(name, "th")
                    || named(name, "caption"))
                    break;
            }
        }
        status = reserve_open_elements(tree, close_existing ? 0 : 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (close_existing) {
            status = end_table_part(tree, literal("table"));
            if (status != LXA_OK) return status;
        }
        status = close_paragraph(tree, NULL);
        if (status != LXA_OK) return status;
        return open_container(tree, token);
    }
    status = close_caption_for_table_start(tree, token->name);
    if (status != LXA_OK) return status;
    status = lxa_dom_nodes_element_name(tree->nodes,
                                        current_element(tree), &top);
    if (status != LXA_OK) return status;
    if (named(token->name, "caption")
        && !named(top, "table") && !named(top, "colgroup")) {
        status = close_open_table_part(tree, TABLE_SECTION);
        if (status != LXA_OK) return status;
        top = literal("table");
    }
    if (named(top, "colgroup") && !named(token->name, "col")) {
        size_t needed = named(token->name, "td")
                        || named(token->name, "th") ? 3
                        : named(token->name, "tr") ? 2 : 1;
        status = reserve_open_elements(tree, needed);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       needed, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = pop_element(tree);
        if (status != LXA_OK) return status;
        top = literal("table");
    }
    if (named(token->name, "colgroup")) {
        if (!named(top, "table")) return LXA_ERROR_UNSUPPORTED;
        status = reserve_open_elements(tree, 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
        return status == LXA_OK ? open_container(tree, token) : status;
    }
    if (named(token->name, "col")) {
        if (!named(top, "table") && !named(top, "colgroup"))
            return LXA_ERROR_UNSUPPORTED;
        if (named(top, "table")) {
            status = reserve_open_elements(tree, 1);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 2, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            implied.kind = LXA_HTML_START_TAG;
            implied.name = literal("colgroup");
            status = open_container(tree, &implied);
            if (status != LXA_OK) return status;
        }
        return append_void(tree, current_element(tree), token);
    }
    if (named(token->name, "caption")) {
        if (!named(top, "table")) return LXA_ERROR_UNSUPPORTED;
        status = reserve_open_elements(tree, 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
        return status == LXA_OK ? open_container(tree, token) : status;
    }
    section = table_section(token->name);
    row = named(token->name, "tr");
    cell = named(token->name, "td") || named(token->name, "th");
    if ((cell && !table_context(top))
        || (row && !named(top, "table") && !table_section(top))
        || (section && !named(top, "table"))) {
        status = close_open_table_part(tree, cell ? TABLE_CELL
                                              : row ? TABLE_ROW : TABLE_SECTION);
        if (status != LXA_OK) return status;
        status = lxa_dom_nodes_element_name(tree->nodes,
                                            current_element(tree), &top);
        if (status != LXA_OK) return status;
    }
    if (!((named(top, "table") && (section || row || cell))
          || (table_section(top) && (row || cell))
          || (named(top, "tr") && cell)))
        return LXA_ERROR_UNSUPPORTED;
    if (named(top, "table")) {
        if (row) extra = 2;
        if (cell) extra = 3;
    }
    else if (table_section(top) && cell)
        extra = 2;
    status = reserve_open_elements(tree, extra);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                  extra, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (named(top, "table") && (row || cell)) {
        implied.kind = LXA_HTML_START_TAG;
        implied.name = literal("tbody");
        status = open_container(tree, &implied);
        if (status != LXA_OK) return status;
        top = implied.name;
    }
    if (table_section(top) && cell) {
        implied.kind = LXA_HTML_START_TAG;
        implied.name = literal("tr");
        status = open_container(tree, &implied);
        if (status != LXA_OK) return status;
        top = implied.name;
    }
    return open_container(tree, token);
}

static lxa_status_t
end_table_part(lxa_html_tree_t *tree, lxa_span_t target)
{
    size_t i = lxa_vector_length(&tree->open_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                              tree->open_elements.bytes.data;
    while (i > 1) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (table_part(name) && name.length == target.length
            && memcmp(name.data, target.data, name.length) == 0) {
            while (lxa_vector_length(&tree->open_elements) > i) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return LXA_OK;
        }
        /* A mismatched cell or a different table ends this scope. */
        if (named(name, "table")
            || ((named(target, "td") || named(target, "th"))
                && (named(name, "td") || named(name, "th"))))
            break;
    }
    /* A table-part end without a match in this table's scope is ignored.
     * A mismatched cell must not close the currently open cell. */
    return LXA_OK;
}

static lxa_status_t
append_void(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
            const lxa_html_token_t *token)
{
    lxa_dom_ref_t element;
    lxa_status_t status = lxa_dom_nodes_append_html_element(
        tree->nodes, token->name, &element);
    if (status == LXA_OK)
        status = apply_attributes(tree, element, token);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                         parent, element);
    return status;
}

static lxa_status_t
enter_text_mode(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_text_mode_t text_mode, lxa_dom_ref_t element)
{
    lxa_html_result_t result;
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    result = lxa_html_tokenizer_set_text_mode(tokenizer, text_mode,
        text_mode == LXA_HTML_PLAINTEXT ? literal("") : token->name);
    if (result != LXA_HTML_OK)
        return result == LXA_HTML_LIMIT ? LXA_ERROR_LIMIT
               : result == LXA_HTML_NO_MEMORY ? LXA_ERROR_NO_MEMORY
               : result == LXA_HTML_OVERFLOW ? LXA_ERROR_OVERFLOW
               : LXA_ERROR_ARGUMENT;
    tree->text_element = element;
    tree->text_return_mode = tree->mode;
    tree->mode = LXA_TREE_TEXT;
    return LXA_OK;
}

static lxa_status_t
begin_text_element(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, lxa_dom_ref_t parent,
    const lxa_html_token_t *token, lxa_html_text_mode_t text_mode)
{
    lxa_dom_ref_t element;
    lxa_status_t status;
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    status = lxa_dom_nodes_append_html_element(tree->nodes, token->name,
                                                &element);
    if (status == LXA_OK)
        status = apply_attributes(tree, element, token);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                         parent, element);
    return status == LXA_OK
           ? enter_text_mode(tree, tokenizer, token, text_mode, element)
           : status;
}

lxa_status_t
lxa_html_tree_init(lxa_html_tree_t *tree,
                                    lxa_dom_nodes_t *nodes)
{
    lxa_status_t status;
    if (tree == NULL || nodes == NULL)
        return LXA_ERROR_ARGUMENT;
    memset(tree, 0, sizeof(*tree));
    status = lxa_vector_init(&tree->open_elements, &nodes->allocator,
                             sizeof(lxa_dom_ref_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&tree->open_elements, nodes->budget);
    if (status == LXA_OK)
        status = lxa_vector_init(&tree->template_stack, &nodes->allocator,
                                 sizeof(lxa_html_template_frame_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&tree->template_stack, nodes->budget);
    if (status == LXA_OK)
        status = lxa_vector_init(&tree->template_elements, &nodes->allocator,
                                 sizeof(lxa_dom_ref_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&tree->template_elements, nodes->budget);
    if (status == LXA_OK)
        status = lxa_vector_init(&tree->pending_template_extras,
                                 &nodes->allocator, sizeof(lxa_dom_ref_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&tree->pending_template_extras,
                                        nodes->budget);
    if (status == LXA_OK)
        status = lxa_buffer_init(&tree->table_text, &nodes->allocator);
    if (status == LXA_OK)
        status = lxa_buffer_bind_budget(&tree->table_text, nodes->budget);
    if (status != LXA_OK) {
        lxa_buffer_destroy(&tree->table_text);
        lxa_vector_destroy(&tree->pending_template_extras);
        lxa_vector_destroy(&tree->template_elements);
        lxa_vector_destroy(&tree->template_stack);
        lxa_vector_destroy(&tree->open_elements);
        return status;
    }
    status = lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT,
                                        &tree->document);
    if (status != LXA_OK) {
        lxa_buffer_destroy(&tree->table_text);
        lxa_vector_destroy(&tree->pending_template_extras);
        lxa_vector_destroy(&tree->template_elements);
        lxa_vector_destroy(&tree->template_stack);
        lxa_vector_destroy(&tree->open_elements);
        return status;
    }
    tree->nodes = nodes;
    tree->mode = LXA_TREE_INITIAL;
    return LXA_OK;
}

void
lxa_html_tree_destroy(lxa_html_tree_t *tree)
{
    if (tree == NULL) return;
    if (tree->nodes != NULL) forget_anchor(tree);
    if (tree->nodes != NULL) forget_pending_formatting(tree);
    if (tree->nodes != NULL) {
        size_t i;
        const lxa_html_template_frame_t *frames =
            (const lxa_html_template_frame_t *) tree->template_stack.bytes.data;
        forget_pending_template_formatting(tree);
        lxa_budget_release(tree->nodes->budget,
            LXA_BUDGET_ACTIVE_FORMATTING,
            lxa_vector_length(&tree->pending_template_extras));
        for (i = 0; i < lxa_vector_length(&tree->template_stack); ++i)
            if (frames[i].pending_formatting.handle != LXA_HANDLE_NONE)
                lxa_budget_release(tree->nodes->budget,
                                   LXA_BUDGET_ACTIVE_FORMATTING, 1);
    }
    if (tree->nodes != NULL)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                            lxa_vector_length(&tree->open_elements));
    if (tree->nodes != NULL)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
            lxa_vector_length(&tree->template_stack)
            + lxa_vector_length(&tree->template_elements)
            + (tree->template_fragment.handle != LXA_HANDLE_NONE ? 1 : 0));
    lxa_buffer_destroy(&tree->table_text);
    lxa_vector_destroy(&tree->pending_template_extras);
    lxa_vector_destroy(&tree->template_elements);
    lxa_vector_destroy(&tree->template_stack);
    lxa_vector_destroy(&tree->open_elements);
    memset(tree, 0, sizeof(*tree));
}

static lxa_status_t
template_pop_elements(lxa_html_tree_t *tree, size_t keep)
{
    return pop_elements(tree, &tree->template_elements, keep);
}


static lxa_status_t
close_template(lxa_html_tree_t *tree)
{
    size_t length = lxa_vector_length(&tree->template_stack);
    size_t extra_count;
    lxa_html_template_frame_t frame = {0};
    size_t boundary = 0;
    lxa_status_t status;
    if (length != 0) {
        frame = ((const lxa_html_template_frame_t *)
                  tree->template_stack.bytes.data)[length - 1];
        boundary = frame.element_count;
    }
    status = template_pop_elements(tree, boundary);
    if (status != LXA_OK) return status;
    forget_pending_template_formatting(tree);
    extra_count = lxa_vector_length(&tree->pending_template_extras)
                  - frame.pending_extra_count;
    lxa_vector_erase(&tree->pending_template_extras,
        frame.pending_extra_count, extra_count);
    lxa_budget_release(tree->nodes->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                       extra_count);
    lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
    if (length != 0) {
        tree->template_fragment = frame.fragment;
        tree->pending_template_formatting = frame.pending_formatting;
        tree->template_insertion_mode = frame.insertion_mode;
        lxa_vector_erase(&tree->template_stack, length - 1, 1);
    }
    else {
        tree->template_fragment = (lxa_dom_ref_t){0};
        tree->template_insertion_mode = LXA_TEMPLATE_INITIAL;
        tree->mode = tree->template_return_mode;
    }
    return LXA_OK;
}

static size_t
template_boundary(const lxa_html_tree_t *tree)
{
    size_t length = lxa_vector_length(&tree->template_stack);
    return length == 0 ? 0
        : ((const lxa_html_template_frame_t *)
           tree->template_stack.bytes.data)[length - 1].element_count;
}

static size_t
template_pending_extra_start(const lxa_html_tree_t *tree)
{
    size_t length = lxa_vector_length(&tree->template_stack);
    return length == 0 ? 0
        : ((const lxa_html_template_frame_t *)
           tree->template_stack.bytes.data)[length - 1].pending_extra_count;
}

static lxa_dom_ref_t
template_parent(const lxa_html_tree_t *tree)
{
    size_t length = lxa_vector_length(&tree->template_elements);
    return length == template_boundary(tree) ? tree->template_fragment
        : ((const lxa_dom_ref_t *) tree->template_elements.bytes.data)[length - 1];
}

static lxa_status_t
template_foster_element(lxa_html_tree_t *tree,
                        const lxa_html_token_t *token, bool keep_open,
                        lxa_dom_ref_t *created, lxa_dom_namespace_t ns)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    lxa_dom_ref_t table = {0}, parent, child;
    size_t i = lxa_vector_length(&tree->template_elements), depth, after;
    lxa_status_t status;
    bool before_table = false;
    while (i > template_boundary(tree)) {
        lxa_span_t name;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        table = open[--i];
        status = lxa_dom_nodes_element_name(tree->nodes, table, &name);
        if (status != LXA_OK) return status;
        if (named(name, "table")) {
            status = lxa_dom_nodes_parent(tree->nodes, table, &parent);
            if (status != LXA_OK) return status;
            if (parent.handle == LXA_HANDLE_NONE) return LXA_ERROR_INTERNAL;
            before_table = true;
            break;
        }
    }
    if (!before_table) {
        lxa_span_t name;
        size_t length = lxa_vector_length(&tree->template_elements);
        if (length != template_boundary(tree) + 1
            && !(length == template_boundary(tree) + 2
                 && tree->template_insertion_mode == LXA_TEMPLATE_TABLE))
            return LXA_ERROR_UNSUPPORTED;
        status = lxa_dom_nodes_element_name(tree->nodes,
            open[template_boundary(tree)], &name);
        if (status != LXA_OK) return status;
        if (length == template_boundary(tree) + 2) {
            lxa_span_t row;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[template_boundary(tree) + 1], &row);
            if (status != LXA_OK) return status;
            if (!table_section(name) || !named(row, "tr"))
                return LXA_ERROR_UNSUPPORTED;
        }
        else if (!((tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
               && named(name, "tr"))
              || (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                  && table_section(name))))
            return LXA_ERROR_UNSUPPORTED;
        parent = tree->template_fragment;
    }
    if (keep_open) {
        status = lxa_size_add(
            lxa_vector_length(&tree->template_elements), 1, &after);
        if (status == LXA_OK)
            status = lxa_size_add(
                lxa_vector_length(&tree->template_stack), 1, &depth);
        if (status == LXA_OK) status = lxa_size_mul(depth, 2, &depth);
        if (status == LXA_OK) status = lxa_size_add(depth, 2, &depth);
        if (status == LXA_OK) status = lxa_size_add(depth, after, &depth);
        if (status == LXA_OK
            && tree->template_return_mode == LXA_TREE_IN_BODY) {
            size_t body = lxa_vector_length(&tree->open_elements);
            if (body == 0) return LXA_ERROR_INTERNAL;
            status = lxa_size_add(depth, body - 1, &depth);
        }
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget,
                LXA_BUDGET_TREE_DEPTH, depth, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget,
                LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_vector_reserve(&tree->template_elements, after);
        if (status == LXA_ERROR_LIMIT
            && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
            tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
        if (status != LXA_OK) return status;
    }
    status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                               1, &tree->nodes->limit_reason);
    if (status == LXA_OK && ns == LXA_DOM_NS_SVG) {
        lxa_span_t name;
        status = lxa_html_svg_tag_name(token->name, &name);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_namespaced_element(tree->nodes,
                name, ns, &child);
    }
    else if (status == LXA_OK)
        status = ns == LXA_DOM_NS_HTML
            ? lxa_dom_nodes_append_html_element(tree->nodes,
                token->name, &child)
            : lxa_dom_nodes_append_namespaced_element(tree->nodes,
                token->name, ns, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = before_table
            ? lxa_dom_nodes_insert_constructed_before(tree->nodes,
                                                         parent, child, table)
            : lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                       parent, child);
    if (status == LXA_OK && keep_open)
        status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK && keep_open) {
        status = lxa_vector_push(&tree->template_elements, &child);
        if (status != LXA_OK)
            lxa_budget_release(tree->nodes->budget,
                                LXA_BUDGET_OPEN_ELEMENTS, 1);
    }
    if (status == LXA_OK && created != NULL) *created = child;
    return status;
}

static lxa_status_t
flush_template_table_text(lxa_html_tree_t *tree)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    lxa_dom_ref_t table = {0}, parent, child;
    lxa_span_t bytes = {tree->table_text.data, tree->table_text.length};
    size_t i = lxa_vector_length(&tree->template_elements), prefix = 0;
    bool colgroup = false;
    lxa_status_t status;
    if (bytes.length == 0) return LXA_OK;
    if (i <= template_boundary(tree)) return LXA_ERROR_INTERNAL;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                bytes.length, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (space_only(bytes)) {
        status = append_character(tree, template_parent(tree),
                                   LXA_DOM_KIND_TEXT, bytes);
    }
    else {
        lxa_span_t top;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i - 1], &top);
        if (status != LXA_OK) return status;
        colgroup = named(top, "colgroup");
        while (i > template_boundary(tree)) {
            lxa_span_t name;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes, open[--i],
                                                     &name);
            if (status != LXA_OK) return status;
            if (named(name, "table")) {
                table = open[i];
                break;
            }
        }
        if (table.handle == LXA_HANDLE_NONE)
            parent = tree->template_fragment;
        else {
            status = lxa_dom_nodes_parent(tree->nodes, table, &parent);
            if (status != LXA_OK || parent.handle == LXA_HANDLE_NONE)
                return status == LXA_OK ? LXA_ERROR_INTERNAL : status;
        }
        if (colgroup) {
            while (prefix < bytes.length
                   && (bytes.data[prefix] == ' '
                       || bytes.data[prefix] == '\n'
                       || bytes.data[prefix] == '\r'
                       || bytes.data[prefix] == '\t'
                       || bytes.data[prefix] == '\f'))
                ++prefix;
        }
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   prefix != 0 ? 2 : 1,
                                   &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (prefix != 0) {
            lxa_span_t spaces = {bytes.data, prefix};
            status = append_character(tree, template_parent(tree),
                                       LXA_DOM_KIND_TEXT, spaces);
            if (status != LXA_OK) return status;
        }
        if (colgroup) {
            status = template_pop_elements(tree,
            lxa_vector_length(&tree->template_elements) - 1);
            if (status != LXA_OK) return status;
        }
        bytes.data += prefix;
        bytes.length -= prefix;
        status = lxa_dom_nodes_append_character(tree->nodes,
                                                 LXA_DOM_KIND_TEXT,
                                                 bytes, &child);
        if (status == LXA_OK)
            status = table.handle == LXA_HANDLE_NONE
                ? lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                           parent, child)
                : lxa_dom_nodes_insert_constructed_before(
                    tree->nodes, parent, child, table);
    }
    if (status == LXA_OK) lxa_buffer_clear(&tree->table_text);
    return status;
}

static bool
template_formatting(lxa_span_t name)
{
    return named(name, "a") || named(name, "b") || named(name, "em")
        || named(name, "strong") || named(name, "i")
        || named(name, "code");
}

static bool
template_sectioning(lxa_span_t name)
{
    return named(name, "section") || named(name, "article")
        || named(name, "main") || named(name, "aside")
        || named(name, "nav") || named(name, "header")
        || named(name, "footer");
}

static bool
template_simple_inline(lxa_span_t name)
{
    return named(name, "label") || named(name, "q")
           || named(name, "cite") || named(name, "abbr");
}

static bool
template_html_text_element(lxa_span_t name)
{
    return named(name, "title") || named(name, "style")
        || named(name, "script") || named(name, "textarea")
        || named(name, "iframe") || named(name, "noembed")
        || named(name, "noframes") || named(name, "xmp")
        || named(name, "plaintext");
}

static bool
template_crossed_inline(lxa_span_t name)
{
    return named(name, "span") || named(name, "ruby")
        || template_simple_inline(name);
}

static bool
template_adoption_block(lxa_span_t name)
{
    /* These ordinary containers are not furthest blocks in adoption. */
    return (named(name, "p") || heading(name) || block_container(name))
        && !named(name, "dialog") && !named(name, "search");
}

static lxa_status_t
template_reparent_after(lxa_html_tree_t *tree,
                        lxa_dom_ref_t parent, lxa_dom_ref_t child,
                        lxa_dom_ref_t preceding)
{
    lxa_dom_ref_t following;
    lxa_status_t status = lxa_dom_nodes_next_sibling(tree->nodes, preceding,
                                                      &following);
    if (status != LXA_OK) return status;
    return following.handle == LXA_HANDLE_NONE
        ? lxa_dom_nodes_reparent_constructed_child(tree->nodes, parent, child)
        : lxa_dom_nodes_reparent_constructed_before(tree->nodes, parent,
                                                     child, following);
}

/* Direct furthest-block case under an open formatting element. The block
 * moves beside the original; its children move under a formatting clone. */
static lxa_status_t
template_close_crossed_block(lxa_html_tree_t *tree,
                             size_t outer_index, const size_t formatting_offsets[5],
                             size_t reconstruct_count)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    lxa_dom_ref_t outer = open[outer_index], block = open[outer_index + 1];
    lxa_dom_ref_t parent, actual, clone, first, continuation;
    lxa_span_t name;
    size_t visits = 0, j;
    lxa_status_t status = lxa_dom_nodes_element_name(tree->nodes, block, &name);
    if (status != LXA_OK) return status;
    if (!template_adoption_block(name))
        return LXA_ERROR_UNSUPPORTED;
    status = lxa_dom_nodes_parent(tree->nodes, outer, &parent);
    if (status == LXA_OK)
        status = lxa_dom_nodes_parent(tree->nodes, block, &actual);
    if (status != LXA_OK) return status;
    if (actual.handle != outer.handle) return LXA_ERROR_UNSUPPORTED;
    status = clone_formatting_element(tree, outer, &clone);
    if (status == LXA_OK)
        status = template_reparent_after(tree, parent, block, outer);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                         block, clone);
    while (status == LXA_OK) {
        status = lxa_dom_nodes_first_child(tree->nodes, block, &first);
        if (status != LXA_OK || first.handle == clone.handle) break;
        if (++visits > lxa_dom_nodes_length(tree->nodes))
            return LXA_ERROR_INTERNAL;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                clone, first);
    }
    for (j = 0; status == LXA_OK && j < reconstruct_count; ++j) {
        lxa_dom_ref_t reconstructed;
        status = clone_formatting_element(tree,
            open[outer_index + formatting_offsets[j]], &reconstructed);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                j == 0 ? block : continuation, reconstructed);
        if (status == LXA_OK) {
            lxa_dom_ref_t *entries =
                (lxa_dom_ref_t *) tree->template_elements.bytes.data;
            entries[outer_index + 1 + j] = reconstructed;
            continuation = reconstructed;
        }
    }
    if (status == LXA_OK) {
        lxa_dom_ref_t *entries =
            (lxa_dom_ref_t *) tree->template_elements.bytes.data;
        entries[outer_index] = block;
        status = template_pop_elements(tree, outer_index + 1 + reconstruct_count);
        if (status != LXA_OK) return status;
    }
    return status;
}

static bool
template_crossed_list_furthest_block(lxa_span_t name)
{
    return named(name, "div") || named(name, "section")
        || named(name, "p") || named(name, "main")
        || named(name, "article") || named(name, "aside")
        || named(name, "nav") || named(name, "header")
        || named(name, "footer") || named(name, "figure")
        || named(name, "blockquote") || named(name, "address")
        || named(name, "center") || named(name, "pre")
        || named(name, "details") || named(name, "fieldset")
        || named(name, "summary") || named(name, "dl")
        || named(name, "hgroup") || named(name, "ul")
        || named(name, "ol") || named(name, "menu")
        || heading(name);
}

/* A still-open li is the next block in the crossed-list adoption chain.
 * Preceding text, comments and closed items move under the list clone;
 * the open item retains a second clone. Formatting descendants are
 * reconstructed for text after the crossed end tag. */
static lxa_status_t
template_close_crossed_list_item(lxa_html_tree_t *tree,
                                 size_t outer_index, size_t span_count,
                                 size_t formatting_count, bool direct_block,
                                 size_t block_span_count, size_t nested_blocks,
                                 const size_t inline_gaps[4],
                                 size_t preblock_length)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    lxa_dom_ref_t outer = open[outer_index], list = open[outer_index + 1];
    lxa_dom_ref_t item = open[outer_index + 2], parent, actual, first, cursor;
    lxa_dom_ref_t list_clone, item_clone, continuation, block = {0};
    lxa_dom_ref_t last_formatting_clone = {0};
    lxa_dom_ref_t nested[4] = {0};
    lxa_span_t name;
    size_t visits = 0, j;
    size_t count = lxa_vector_length(&tree->template_elements) - outer_index;
    size_t next_formatting = outer_index + 3 + span_count;
    size_t formatting_start = preblock_length > 3 ? preblock_length - 3 : 0;
    size_t kept_formatting = 0;
    size_t cloned_formatting = 0;
    lxa_status_t status = lxa_dom_nodes_element_name(tree->nodes, list,
                                                      &name);
    if (status != LXA_OK) return status;
    if (!named(name, "menu") && !named(name, "ul") && !named(name, "ol"))
        return LXA_ERROR_UNSUPPORTED;
    status = lxa_dom_nodes_element_name(tree->nodes, item, &name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_parent(tree->nodes, outer, &parent);
    if (status == LXA_OK)
        status = lxa_dom_nodes_parent(tree->nodes, list, &actual);
    if (status != LXA_OK) return status;
    if (!named(name, "li") || actual.handle != outer.handle)
        return LXA_ERROR_UNSUPPORTED;
    status = lxa_dom_nodes_parent(tree->nodes, item, &actual);
    if (status == LXA_OK)
        status = lxa_dom_nodes_first_child(tree->nodes, list, &first);
    if (status != LXA_OK) return status;
    if (actual.handle != list.handle) return LXA_ERROR_UNSUPPORTED;
    if (direct_block) {
        if (preblock_length != 0) {
            if (count < 4 || preblock_length > count - 4
                || count - 4 - preblock_length != block_span_count
                || span_count != 0 || formatting_count != 0
                || nested_blocks != 0)
                return LXA_ERROR_UNSUPPORTED;
            for (j = 0; j < preblock_length; ++j) {
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[outer_index + 3 + j], &name);
                if (status != LXA_OK) return status;
                if ((j == 0 && !template_formatting(name))
                    || (!template_formatting(name)
                        && !template_crossed_inline(name)))
                    return LXA_ERROR_UNSUPPORTED;
                if (j >= formatting_start && template_formatting(name))
                    ++kept_formatting;
                status = lxa_dom_nodes_parent(tree->nodes,
                    open[outer_index + 3 + j], &actual);
                if (status != LXA_OK) return status;
                if (actual.handle
                    != open[outer_index + 2 + j].handle)
                    return LXA_ERROR_UNSUPPORTED;
            }
        }
        block = open[outer_index + 3 + span_count + preblock_length];
        status = lxa_dom_nodes_element_name(tree->nodes,
                                             block, &name);
        if (status == LXA_OK
            && !template_crossed_list_furthest_block(name))
            return LXA_ERROR_UNSUPPORTED;
        if (status == LXA_OK)
            status = lxa_dom_nodes_parent(tree->nodes,
                                           block, &actual);
        if (status != LXA_OK) return status;
        if (actual.handle != (preblock_length != 0
                                ? open[outer_index + 2 + preblock_length].handle
                                : span_count == 0 ? item.handle
                                : open[outer_index + 2 + span_count].handle))
            return LXA_ERROR_UNSUPPORTED;
        if (nested_blocks != 0
            && !named(name, "div") && !named(name, "section"))
            return LXA_ERROR_UNSUPPORTED;
        if (nested_blocks > 4) return LXA_ERROR_UNSUPPORTED;
        continuation = block;
        {
            size_t k = 4 + span_count + preblock_length;
            size_t gap_total = 0, n;
            for (n = 0; n < nested_blocks; ++n) {
                if (inline_gaps[n] > block_span_count - gap_total)
                    return LXA_ERROR_INTERNAL;
                gap_total += inline_gaps[n];
                for (j = 0; j < inline_gaps[n]; ++j, ++k) {
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[outer_index + k], &name);
                    if (status == LXA_OK && !template_crossed_inline(name))
                        return LXA_ERROR_UNSUPPORTED;
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_parent(tree->nodes,
                            open[outer_index + k], &actual);
                    if (status != LXA_OK) return status;
                    if (actual.handle != continuation.handle)
                        return LXA_ERROR_UNSUPPORTED;
                    continuation = open[outer_index + k];
                }
                nested[n] = open[outer_index + k++];
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        nested[n], &name);
                if (status != LXA_OK) return status;
                if (!named(name, "section") && !named(name, "div")
                    && !named(name, "p"))
                    return LXA_ERROR_UNSUPPORTED;
                if (n + 1 < nested_blocks && named(name, "p"))
                    return LXA_ERROR_UNSUPPORTED;
                status = lxa_dom_nodes_parent(tree->nodes, nested[n], &actual);
                if (status != LXA_OK) return status;
                if (actual.handle != continuation.handle)
                    return LXA_ERROR_UNSUPPORTED;
                continuation = nested[n];
            }
            j = k;
        }
        for (; j < count; ++j) {
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                                                     open[outer_index + j],
                                                     &name);
            if (status == LXA_OK
                && (j < 4 + span_count + nested_blocks + block_span_count
                          + preblock_length
                    ? !template_crossed_inline(name)
                    : !template_formatting(name)))
                return LXA_ERROR_UNSUPPORTED;
            if (status == LXA_OK)
                status = lxa_dom_nodes_parent(tree->nodes,
                                               open[outer_index + j], &actual);
            if (status != LXA_OK) return status;
            if (actual.handle != continuation.handle) return LXA_ERROR_UNSUPPORTED;
            continuation = open[outer_index + j];
        }
    }
    /* Inline containers stay with the old content; only active formatting
     * descendants are reconstructed after the crossed end tag. */
    continuation = item;
    for (j = 0; j < span_count; ++j) {
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                 open[outer_index + 3 + j],
                                                 &name);
        if (status == LXA_OK && !template_crossed_inline(name))
            return LXA_ERROR_UNSUPPORTED;
        if (status == LXA_OK)
            status = lxa_dom_nodes_parent(tree->nodes,
                                           open[outer_index + 3 + j], &actual);
        if (status != LXA_OK) return status;
        if (actual.handle != continuation.handle) return LXA_ERROR_UNSUPPORTED;
        continuation = open[outer_index + 3 + j];
    }
    for (j = 0; !direct_block && j < count - 3 - span_count; ++j) {
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                                    open[outer_index + 3 + span_count + j],
                                    &name);
        if (status == LXA_OK && !template_formatting(name)
            && !template_crossed_inline(name))
            return LXA_ERROR_UNSUPPORTED;
        if (status == LXA_OK)
            status = lxa_dom_nodes_parent(tree->nodes,
                                    open[outer_index + 3 + span_count + j],
                                    &actual);
        if (status != LXA_OK) return status;
        if (actual.handle != continuation.handle) return LXA_ERROR_UNSUPPORTED;
        continuation = open[outer_index + 3 + span_count + j];
    }
    cursor = first;
    while (cursor.handle != item.handle) {
        lxa_dom_ref_t next;
        lxa_dom_record_t record;
        if (cursor.handle == LXA_HANDLE_NONE
            || ++visits > lxa_dom_nodes_length(tree->nodes))
            return LXA_ERROR_INTERNAL;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_read(tree->nodes, cursor, &record);
        if (status != LXA_OK) return status;
        if (record.kind == LXA_DOM_KIND_ELEMENT) {
            status = lxa_dom_nodes_element_name(tree->nodes, cursor, &name);
            if (status != LXA_OK) return status;
        }
        if (record.kind != LXA_DOM_KIND_TEXT
            && record.kind != LXA_DOM_KIND_COMMENT
            && (record.kind != LXA_DOM_KIND_ELEMENT || !named(name, "li")))
            return LXA_ERROR_UNSUPPORTED; /* Other prior content needs more recovery. */
        status = lxa_dom_nodes_next_sibling(tree->nodes, cursor, &next);
        if (status != LXA_OK) return status;
        cursor = next;
    }
    status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                               2 + formatting_count + (direct_block ? 1 : 0)
                               + nested_blocks + kept_formatting,
                               &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = clone_formatting_element(tree, outer, &list_clone);
    if (status == LXA_OK)
        status = clone_formatting_element(tree, outer, &item_clone);
    if (status == LXA_OK)
        status = template_reparent_after(tree, parent, list, outer);
    if (status == LXA_OK)
        status = lxa_dom_nodes_insert_constructed_before(tree->nodes,
                                                          list, list_clone, item);
    visits = 0;
    while (status == LXA_OK) {
        status = lxa_dom_nodes_first_child(tree->nodes, list, &first);
        if (status != LXA_OK || first.handle == list_clone.handle) break;
        if (++visits > lxa_dom_nodes_length(tree->nodes))
            return LXA_ERROR_INTERNAL;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                list_clone, first);
    }
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                         item, item_clone);
    visits = 0;
    while (status == LXA_OK) {
        status = lxa_dom_nodes_first_child(tree->nodes, item, &first);
        if (status != LXA_OK || first.handle == item_clone.handle) break;
        if (++visits > lxa_dom_nodes_length(tree->nodes))
            return LXA_ERROR_UNSUPPORTED;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                item_clone, first);
    }
    if (status == LXA_OK && direct_block) {
        lxa_dom_ref_t clone;
        if (status == LXA_OK)
            status = clone_formatting_element(tree, outer, &clone);
        if (status == LXA_OK)
            status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                item, block);
        /* The adoption inner loop visits at most three ancestors. Earlier
         * originals and inline gaps stay inside the displaced <b>. */
        for (j = formatting_start;
             status == LXA_OK && j < preblock_length; ++j) {
            lxa_dom_ref_t formatting_clone;
            status = lxa_dom_nodes_element_name(tree->nodes,
                open[outer_index + 3 + j], &name);
            if (status != LXA_OK) return status;
            if (!template_formatting(name)) continue;
            status = clone_formatting_element(tree,
                open[outer_index + 3 + j], &formatting_clone);
            if (status == LXA_OK)
                status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                    last_formatting_clone.handle == LXA_HANDLE_NONE
                    ? item : last_formatting_clone,
                    formatting_clone);
            if (status == LXA_OK) {
                lxa_dom_ref_t *entries =
                    (lxa_dom_ref_t *) tree->template_elements.bytes.data;
                entries[outer_index + 2 + cloned_formatting]
                    = formatting_clone;
                ++cloned_formatting;
                last_formatting_clone = formatting_clone;
            }
        }
        if (status == LXA_OK && kept_formatting != 0)
            status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                last_formatting_clone, block);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                             block, clone);
        visits = 0;
        while (status == LXA_OK) {
            status = lxa_dom_nodes_first_child(tree->nodes, block, &first);
            if (status != LXA_OK || first.handle == clone.handle) break;
            if (++visits > lxa_dom_nodes_length(tree->nodes))
                return LXA_ERROR_INTERNAL;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                    clone, first);
        }
    }
    for (j = 0; status == LXA_OK && j < nested_blocks; ++j) {
        lxa_dom_ref_t clone, inner = nested[j];
        lxa_dom_ref_t container = j == 0 ? block
                                         : nested[j - 1];
        status = clone_formatting_element(tree, outer, &clone);
        if (status == LXA_OK)
            status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                container, inner);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                             inner, clone);
        visits = 0;
        while (status == LXA_OK) {
            status = lxa_dom_nodes_first_child(tree->nodes, inner, &first);
            if (status != LXA_OK || first.handle == clone.handle) break;
            if (++visits > lxa_dom_nodes_length(tree->nodes))
                return LXA_ERROR_INTERNAL;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                    clone, first);
        }
    }
    continuation = nested_blocks != 0 ? nested[nested_blocks - 1]
                   : direct_block ? block : item;
    for (j = 0; status == LXA_OK && j < formatting_count; ++j) {
        lxa_dom_ref_t clone;
        if (!direct_block) {
            while (next_formatting < outer_index + count) {
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[next_formatting], &name);
                if (status != LXA_OK) return status;
                if (template_formatting(name)) break;
                ++next_formatting;
            }
            if (next_formatting == outer_index + count)
                return LXA_ERROR_INTERNAL;
        }
        status = clone_formatting_element(tree,
                                          open[direct_block
                                               ? outer_index + 4 + span_count
                                                 + block_span_count
                                                 + nested_blocks + j
                                               : next_formatting++],
                                          &clone);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                             continuation, clone);
        if (status == LXA_OK) {
            lxa_dom_ref_t *entries =
                (lxa_dom_ref_t *) tree->template_elements.bytes.data;
            entries[outer_index + 2 + (direct_block ? 1 : 0)
                    + nested_blocks + j] = clone;
            continuation = clone;
        }
    }
    if (status == LXA_OK) {
        lxa_dom_ref_t *entries =
            (lxa_dom_ref_t *) tree->template_elements.bytes.data;
        entries[outer_index] = list;
        entries[outer_index + 1] = item;
        if (direct_block)
            entries[outer_index + 2 + kept_formatting] = block;
        for (j = 0; j < nested_blocks; ++j)
            entries[outer_index + 3 + j] = nested[j];
        status = template_pop_elements(tree, outer_index + 2 + formatting_count
                               + (direct_block ? 1 : 0)
                               + nested_blocks + kept_formatting);
        if (status != LXA_OK) return status;
    }
    return status;
}

/* Locate the active paragraph in this template's content, stopping at its
 * fragment boundary. Reconstructing formatting left open inside that
 * paragraph needs the adoption algorithm; reject rather than misplace it. */
static lxa_status_t
template_open_paragraph(lxa_html_tree_t *tree, size_t *index)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->template_elements), i;
    bool formatting_after = false;
    *index = length;
    for (i = template_boundary(tree); i < length; ++i) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "p")) {
            *index = i;
            formatting_after = false;
        }
        else if (*index != length && template_formatting(name))
            formatting_after = true;
    }
    return formatting_after ? LXA_ERROR_UNSUPPORTED : LXA_OK;
}

static lxa_status_t
template_open_list_item(lxa_html_tree_t *tree, size_t *index)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t i = lxa_vector_length(&tree->template_elements);
    bool formatting_above = false;
    *index = i;
    while (i > template_boundary(tree)) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_dom_nodes_element_name(tree->nodes, open[--i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "li")) {
            if (formatting_above) return LXA_ERROR_UNSUPPORTED;
            *index = i;
            return LXA_OK;
        }
        if (named(name, "ul") || named(name, "ol")) break;
        if (template_formatting(name)) formatting_above = true;
        /* A special block stops the search before the earlier li. Formatting
         * above that block stays open; a later crossed end still needs
         * adoption recovery. */
        if (block_container(name) && !named(name, "address")
            && !named(name, "div") && !named(name, "p"))
            if (special_element(name, LXA_DOM_NS_HTML))
                return LXA_OK;
    }
    return LXA_OK;
}

static lxa_status_t
template_select_scope(lxa_html_tree_t *tree, size_t *index,
                      bool *option, bool *group)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t i = lxa_vector_length(&tree->template_elements);
    lxa_span_t name;
    lxa_status_t status;
    *index = i;
    *option = *group = false;
    if (i == template_boundary(tree)) return LXA_OK;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                               &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, open[i - 1], &name);
    if (status != LXA_OK) return status;
    *option = named(name, "option");
    *group = named(name, "optgroup");
    if (!*option && !*group && !named(name, "select")) return LXA_OK;
    while (i > template_boundary(tree)) {
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[--i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "select")) {
            *index = i;
            return LXA_OK;
        }
        if (!named(name, "option") && !named(name, "optgroup"))
            return LXA_OK; /* Orphan options use ordinary body handling. */
    }
    return LXA_OK;
}

static lxa_status_t
begin_template(lxa_html_tree_t *tree,
               const lxa_html_token_t *token)
{
    lxa_dom_ref_t child, fragment;
    lxa_html_template_frame_t frame;
    bool nested = tree->mode == LXA_TREE_TEMPLATE_TEXT;
    size_t depth, slots;
    lxa_status_t status;
    status = lxa_size_add(lxa_vector_length(&tree->template_stack),
                          nested ? 2 : 1, &depth);
    if (status == LXA_OK) status = lxa_size_mul(depth, 2, &slots);
    if (status == LXA_OK) status = lxa_size_add(slots, 2, &slots);
    if (status == LXA_OK)
        status = lxa_size_add(slots,
                    lxa_vector_length(&tree->template_elements), &slots);
    if (status == LXA_OK
        && (tree->mode == LXA_TREE_IN_BODY
            || (nested && tree->template_return_mode == LXA_TREE_IN_BODY))) {
        size_t open = lxa_vector_length(&tree->open_elements);
        if (open == 0) return LXA_ERROR_INTERNAL;
        status = lxa_size_add(slots, open - 1, &slots);
    }
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_TREE_DEPTH,
                                   slots, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES, 2,
                                   &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget,
            LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK && nested)
        status = lxa_vector_reserve(&tree->template_stack, depth - 1);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status != LXA_OK) return status;
    status = lxa_dom_nodes_append_html_element(tree->nodes, token->name, &child);
    if (status == LXA_OK)
        status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_draft(tree->nodes,
            LXA_DOM_KIND_DOCUMENT_FRAGMENT, &fragment);
    if (status == LXA_OK)
        status = lxa_dom_nodes_bind_template(tree->nodes, child, fragment);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
            nested ? template_parent(tree)
                   : tree->mode == LXA_TREE_IN_HEAD
                     || tree->mode == LXA_TREE_AFTER_HEAD
                     ? tree->head : current_element(tree), child);
    if (status != LXA_OK) return status;
    if (!nested) tree->template_return_mode = tree->mode;
    if (nested) {
        frame.fragment = tree->template_fragment;
        frame.pending_formatting = tree->pending_template_formatting;
        frame.pending_extra_count =
            lxa_vector_length(&tree->pending_template_extras);
        frame.element_count = lxa_vector_length(&tree->template_elements);
        frame.insertion_mode = tree->template_insertion_mode;
        status = lxa_vector_push(&tree->template_stack, &frame);
        if (status != LXA_OK) return status;
    }
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                                1, &tree->nodes->limit_reason);
    if (status != LXA_OK) {
        if (nested)
            lxa_vector_erase(&tree->template_stack,
                lxa_vector_length(&tree->template_stack) - 1, 1);
        return status;
    }
    tree->template_fragment = fragment;
    tree->pending_template_formatting = (lxa_dom_ref_t){0};
    tree->template_insertion_mode = LXA_TEMPLATE_INITIAL;
    tree->mode = LXA_TREE_TEMPLATE_TEXT;
    return LXA_OK;
}

static lxa_status_t
template_push_table_part(lxa_html_tree_t *tree,
                         const lxa_html_token_t *token)
{
    lxa_dom_ref_t child;
    lxa_status_t status = lxa_dom_nodes_append_html_element(
        tree->nodes, token->name, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
            template_parent(tree), child);
    if (status != LXA_OK) return status;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                                1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_vector_push(&tree->template_elements, &child);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status != LXA_OK)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
    return status;
}

static lxa_status_t
template_open_foreign(lxa_html_tree_t *tree,
                      const lxa_html_token_t *token, lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t child;
    lxa_span_t name = token->name;
    size_t count, depth;
    bool first = lxa_vector_length(&tree->template_elements)
                 == template_boundary(tree);
    lxa_status_t status = lxa_size_add(
        lxa_vector_length(&tree->template_elements), 1, &count);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&tree->template_stack),
                              1, &depth);
    if (status == LXA_OK) status = lxa_size_mul(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, count, &depth);
    if (status == LXA_OK
        && tree->template_return_mode == LXA_TREE_IN_BODY) {
        size_t body = lxa_vector_length(&tree->open_elements);
        if (body == 0) return LXA_ERROR_INTERNAL;
        status = lxa_size_add(depth, body - 1, &depth);
    }
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_TREE_DEPTH,
                                   depth, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   1, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget,
            LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_vector_reserve(&tree->template_elements, count);
    if (status == LXA_OK && ns == LXA_DOM_NS_SVG)
        status = lxa_html_svg_tag_name(name, &name);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status != LXA_OK) return status;
    status = lxa_dom_nodes_append_namespaced_element(tree->nodes,
        name, ns, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_constructed_child(tree->nodes,
            template_parent(tree), child);
    if (status == LXA_OK)
        status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_vector_push(&tree->template_elements, &child);
    if (status != LXA_OK) {
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
        return status;
    }
    if (token->self_closing) {
        status = template_pop_elements(tree, count - 1);
        if (status != LXA_OK) return status;
    }
    if (first && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
        tree->template_insertion_mode = LXA_TEMPLATE_BODY;
    return LXA_OK;
}

static lxa_status_t
template_fostered_descendant(lxa_span_t name,
                             lxa_dom_namespace_t ns, bool *result)
{
    if (ns != LXA_DOM_NS_HTML) {
        *result = true;
        return LXA_OK;
    }
    *result = block_container(name) || heading(name)
        || named(name, "p") || named(name, "li")
        || named(name, "form")
        || named(name, "dt") || named(name, "dd")
        || template_crossed_inline(name)
        || named(name, "rt") || named(name, "rp")
        || named(name, "datalist") || named(name, "output")
        || named(name, "progress") || named(name, "meter")
        || named(name, "map") || named(name, "picture")
        || named(name, "canvas") || named(name, "video")
        || named(name, "audio") || named(name, "object")
        || named(name, "legend")
        || named(name, "marquee") || named(name, "applet")
        || named(name, "noscript") || named(name, "option")
        || named(name, "optgroup")
        || named(name, "ins") || named(name, "slot")
        || named(name, "data") || named(name, "dfn")
        || named(name, "mark") || named(name, "time")
        || named(name, "del") || named(name, "samp")
        || named(name, "kbd") || named(name, "sub")
        || named(name, "sup") || named(name, "var")
        || template_formatting(name);
    return LXA_OK;
}

static lxa_status_t
template_retain_formatting_chain(lxa_html_tree_t *tree,
                                 size_t first, size_t count, size_t keep);

static lxa_status_t
template_open_table_part(lxa_html_tree_t *tree,
                         const lxa_html_token_t *token)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    lxa_html_token_t implied = {0};
    size_t length = lxa_vector_length(&tree->template_elements);
    size_t i = length, table = SIZE_MAX, context = SIZE_MAX;
    size_t caption_index = SIZE_MAX;
    size_t cell_index = SIZE_MAX;
    size_t keep, replace_at = SIZE_MAX, extra = 1, depth, after;
    bool section = table_section(token->name);
    bool row = named(token->name, "tr");
    bool cell = named(token->name, "td") || named(token->name, "th");
    bool caption = named(token->name, "caption");
    bool column = named(token->name, "col");
    bool group = column || named(token->name, "colgroup");
    bool caption_open = false, ordinary_above = false, clear_ordinary;
    bool group_open = false;
    bool implied_section = false, implied_row = false;
    bool replace_section = false, clear_fostered = false;
    bool fostered_section = false;
    lxa_html_template_mode_t next_mode = tree->template_insertion_mode;
    lxa_span_t top;
    lxa_status_t status;
    if (!section && !row && !cell && !caption && !group)
        return LXA_ERROR_UNSUPPORTED;
    while (i > template_boundary(tree)) {
        lxa_span_t name;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[--i], &name);
        if (status != LXA_OK) return status;
        if (context == SIZE_MAX && table_context(name)) context = i;
        if (named(name, "table")) {
            table = i;
            break;
        }
        if (named(name, "caption")) {
            if (caption_open) return LXA_ERROR_UNSUPPORTED;
            caption_open = true;
            caption_index = i;
        }
        else if (caption_open)
            return LXA_ERROR_UNSUPPORTED; /* Caption must be inside table. */
        else if (named(name, "colgroup"))
            group_open = true;
        else if (named(name, "td") || named(name, "th"))
            cell_index = i;
        else if (!table_section(name) && !named(name, "tr"))
            ordinary_above = true;
    }
    if (table == SIZE_MAX) {
        if (context == SIZE_MAX && cell_index == SIZE_MAX && !caption_open
            && (tree->template_insertion_mode == LXA_TEMPLATE_ROW
                || tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY)) {
            if (!cell && !(row && tree->template_insertion_mode
                                 == LXA_TEMPLATE_TABLE_BODY))
                return LXA_OK;
            keep = template_boundary(tree);
            implied_row = cell && tree->template_insertion_mode
                                  == LXA_TEMPLATE_TABLE_BODY;
            extra = implied_row ? 2 : 1;
            goto reserve_part;
        }
        if (length > template_boundary(tree)
            && tree->template_insertion_mode == LXA_TEMPLATE_TABLE
            && (ordinary_above || caption_open)) {
            lxa_span_t root;
            size_t j;
            bool clearable = true;
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[template_boundary(tree)], &root);
            if (status != LXA_OK) return status;
            if (named(root, "caption") || !table_context(root)) {
                for (j = template_boundary(tree); j < length; ++j) {
                    lxa_span_t name;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[j], &name);
                    if (status != LXA_OK) return status;
                    if (template_formatting(name)) clearable = false;
                }
                if (!clearable) return LXA_ERROR_UNSUPPORTED;
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, cell ? 3 : row || column ? 2 : 1,
                    &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = template_pop_elements(tree, template_boundary(tree));
                if (status != LXA_OK) return status;
                return template_open_table_part(tree, token);
            }
        }
        /* A direct row leaves the template in table-body mode even after
         * the row closes; the next cell then needs another implied row. */
        if (length != template_boundary(tree)) {
            if (tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
                && (section || caption || group)
                && length > template_boundary(tree) + 1) {
                size_t j = template_boundary(tree) + 1;
                lxa_span_t root;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[template_boundary(tree)], &root);
                if (status != LXA_OK) return status;
                if (!named(root, "tr")) return LXA_ERROR_UNSUPPORTED;
                for (; j < length; ++j) {
                    lxa_span_t name;
                    lxa_dom_namespace_t ns;
                    bool clearable;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[j], &name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_namespace(tree->nodes,
                            open[j], &ns);
                    if (status != LXA_OK) return status;
                    if (j == template_boundary(tree) + 1
                        && (named(name, "td") || named(name, "th")))
                        continue;
                    status = template_fostered_descendant(name, ns, &clearable);
                    if (status != LXA_OK) return status;
                    if (!clearable) return LXA_ERROR_UNSUPPORTED;
                }
                status = template_pop_elements(tree, template_boundary(tree));
                if (status != LXA_OK) return status;
                return LXA_OK; /* No table remains to receive this part. */
            }
            if (length > template_boundary(tree) + 2
                && !((cell || row)
                     && (tree->template_insertion_mode
                         == LXA_TEMPLATE_TABLE_BODY
                         || tree->template_insertion_mode
                            == LXA_TEMPLATE_TABLE))
                && !(tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                     && (section || caption || group)))
                return LXA_ERROR_UNSUPPORTED;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                open[length - 1], &top);
            if (status != LXA_OK) return status;
            if (length == template_boundary(tree) + 1
                && tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
                && named(top, "tr")
                && (section || caption || group)) {
                status = template_pop_elements(tree, template_boundary(tree));
                if (status != LXA_OK) return status;
                return LXA_OK; /* No table remains to receive this part. */
            }
            if (length == template_boundary(tree) + 1
                && tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                && named(top, "colgroup")) {
                /* Column-group mode closes before reprocessing a table part,
                 * even when the fragment has no table element. */
                status = template_pop_elements(tree, template_boundary(tree));
                if (status != LXA_OK) return status;
                return template_open_table_part(tree, token);
            }
            if (length == template_boundary(tree) + 1
                && tree->template_insertion_mode == LXA_TEMPLATE_ROW
                && (named(top, "td") || named(top, "th"))) {
                /* Close the direct cell before reprocessing in row mode.
                 * With no tr in scope, only another cell can be inserted. */
                if (!cell) {
                    status = template_pop_elements(tree, length - 1);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                --length;
            }
            else if (length == template_boundary(tree) + 1
                     && tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                     && table_section(top)
                     && (row || cell || section || caption || group)) {
                /* A direct section already supplies the table-body context. */
                if (cell) {
                    implied_row = true;
                    extra = 2;
                }
                replace_section = section || caption || group;
            }
            else if (length == template_boundary(tree) + 2
                     && tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                     && named(top, "tr") && (row || cell)) {
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[template_boundary(tree)], &top);
                if (status != LXA_OK) return status;
                if (!table_section(top)) return LXA_ERROR_UNSUPPORTED;
                if (row) replace_at = length - 1;
            }
            else if (length == template_boundary(tree) + 2
                     && tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
                     && (named(top, "td") || named(top, "th"))
                     && (row || cell)) {
                lxa_span_t parent;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[template_boundary(tree)], &parent);
                if (status != LXA_OK) return status;
                if (!named(parent, "tr")) return LXA_ERROR_UNSUPPORTED;
                replace_at = template_boundary(tree) + (cell ? 1 : 0);
            }
            else if (length == template_boundary(tree) + 3
                     && tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                     && (named(top, "td") || named(top, "th"))
                     && (row || cell)) {
                lxa_span_t parent;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[length - 2], &parent);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[template_boundary(tree)], &top);
                if (status != LXA_OK) return status;
                if (!named(parent, "tr") || !table_section(top))
                    return LXA_ERROR_UNSUPPORTED;
                replace_at = length - (row ? 2 : 1);
            }
            else if (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                     && (section || caption || group)
                     && length >= template_boundary(tree) + 2) {
                lxa_span_t row_name;
                size_t j;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[template_boundary(tree)], &top);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[template_boundary(tree) + 1], &row_name);
                if (status != LXA_OK) return status;
                if (!table_section(top)) return LXA_ERROR_UNSUPPORTED;
                j = template_boundary(tree) + 1;
                if (named(row_name, "tr")) {
                    ++j;
                }
                if (j == template_boundary(tree) + 2 && j < length) {
                    lxa_span_t cell_name;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[j], &cell_name);
                    if (status != LXA_OK) return status;
                    if (named(cell_name, "td") || named(cell_name, "th"))
                        ++j;
                }
                for (; j < length; ++j) {
                    lxa_span_t name;
                    lxa_dom_namespace_t ns;
                    bool clearable;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[j], &name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_namespace(tree->nodes,
                            open[j], &ns);
                    if (status == LXA_OK)
                        status = template_fostered_descendant(name, ns, &clearable);
                    if (status != LXA_OK) return status;
                    if (!clearable)
                        return LXA_ERROR_UNSUPPORTED;
                }
                replace_section = true;
            }
            else if (length >= template_boundary(tree) + 2
                     && (cell || row)
                     && (tree->template_insertion_mode
                         == LXA_TEMPLATE_TABLE_BODY
                         || tree->template_insertion_mode
                            == LXA_TEMPLATE_TABLE)) {
                size_t j = template_boundary(tree) + 1;
                bool fostered_cell = false, fostered_row = false;
                bool direct_cell = false;
                if (tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
                    && length >= template_boundary(tree) + 3) {
                    lxa_span_t row_name, cell_name;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[template_boundary(tree)], &row_name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[j], &cell_name);
                    if (status != LXA_OK) return status;
                    direct_cell = named(row_name, "tr")
                        && (named(cell_name, "td") || named(cell_name, "th"));
                    if (direct_cell) ++j;
                }
                if (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                    && length >= template_boundary(tree) + 3) {
                    lxa_span_t section_name, row_name;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[template_boundary(tree)], &section_name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[j], &row_name);
                    if (status != LXA_OK) return status;
                    fostered_row = table_section(section_name)
                        && named(row_name, "tr");
                    if (fostered_row) ++j;
                }
                if (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                    && length >= template_boundary(tree) + 4) {
                    lxa_span_t section_name, row_name, cell_name;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 3, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[template_boundary(tree)], &section_name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[template_boundary(tree) + 1], &row_name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[template_boundary(tree) + 2], &cell_name);
                    if (status != LXA_OK) return status;
                    fostered_cell = table_section(section_name)
                        && named(row_name, "tr")
                        && (named(cell_name, "td")
                            || named(cell_name, "th"));
                    if (fostered_cell) j = template_boundary(tree) + 3;
                }
                for (; j < length; ++j) {
                    lxa_span_t name;
                    lxa_dom_namespace_t ns;
                    bool clearable;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                                                             open[j], &name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_namespace(tree->nodes,
                            open[j], &ns);
                    if (status == LXA_OK)
                        status = template_fostered_descendant(name, ns, &clearable);
                    if (status != LXA_OK) return status;
                    if (!clearable)
                        return LXA_ERROR_UNSUPPORTED;
                }
                if (direct_cell)
                    replace_at = template_boundary(tree) + (cell ? 1 : 0);
                else if (fostered_cell || fostered_row)
                    replace_at = template_boundary(tree) + (cell ? 2 : 1);
                else {
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[template_boundary(tree)], &top);
                    if (status != LXA_OK) return status;
                    fostered_section = tree->template_insertion_mode
                                           == LXA_TEMPLATE_TABLE
                                       && table_section(top);
                    if (!fostered_section
                        && !(tree->template_insertion_mode
                             == LXA_TEMPLATE_TABLE_BODY && named(top, "tr")))
                        return LXA_ERROR_UNSUPPORTED;
                    if (fostered_section && cell) {
                        implied_row = true;
                        extra = 2;
                    }
                }
                clear_fostered = true;
            }
            else if (row && length == template_boundary(tree) + 1
                     && tree->template_insertion_mode
                        == LXA_TEMPLATE_TABLE_BODY && named(top, "tr"))
                clear_fostered = true;
            else if (!named(top, "tr")
                     || (length == template_boundary(tree) + 1 && !cell)
                     || (length == template_boundary(tree) + 2
                         && (tree->template_insertion_mode
                             != LXA_TEMPLATE_TABLE
                             || (!cell && !section && !caption && !group))))
                return LXA_ERROR_UNSUPPORTED;
            else if (length == template_boundary(tree) + 2) {
                status = lxa_dom_nodes_element_name(tree->nodes,
                                                    open[length - 2], &top);
                if (status != LXA_OK) return status;
                if (!table_section(top)) return LXA_ERROR_UNSUPPORTED;
                replace_section = section || caption || group;
            }
        }
        keep = replace_section ? template_boundary(tree)
             : replace_at != SIZE_MAX ? replace_at
             : clear_fostered ? template_boundary(tree)
                                + (cell || fostered_section ? 1 : 0)
                              : length;
        if (length == template_boundary(tree)) {
            implied_section = (row || cell) && tree->template_insertion_mode
                                               == LXA_TEMPLATE_TABLE;
            if (implied_section) {
                implied_row = cell;
                extra = cell ? 3 : 2;
            }
            if (tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
                next_mode = row ? LXA_TEMPLATE_TABLE_BODY
                          : cell ? LXA_TEMPLATE_ROW
                          : section || caption || group ? LXA_TEMPLATE_TABLE
                          : LXA_TEMPLATE_INITIAL;
        }
        goto reserve_part;
    }
    clear_ordinary = ordinary_above && !caption_open
                     && cell_index == SIZE_MAX;
    if (caption_open && caption_index != table + 1)
        return LXA_ERROR_UNSUPPORTED;
    /* Table-part starts clear fostered open descendants back to the nearest
     * table context. Size preflight uses the resulting stack before popping. */
    status = lxa_dom_nodes_element_name(tree->nodes,
        open[clear_ordinary ? context : length - 1], &top);
    if (status != LXA_OK) return status;
    implied_section = (named(top, "table") || caption_open || group_open)
                      && (row || cell);
    implied_row = cell && (named(top, "table") || caption_open || group_open
                           || table_section(top));
    keep = clear_ordinary ? context + 1 : length;
    if (group) {
        keep = table + 1;
    }
    else if (caption) {
        if (caption_open) keep = table + 1;
        else if (cell_index != SIZE_MAX) keep = table + 1;
        else if (group_open || table_section(top) || named(top, "tr"))
            keep = table + 1;
        else if (!named(top, "table")) return LXA_ERROR_UNSUPPORTED;
    }
    else if (section)
        keep = table + 1;
    else if (row) {
        if (named(top, "table") || caption_open || group_open) {
            extra = 2;
            if (caption_open || group_open) keep = table + 1;
        }
        else if (named(top, "tr")) keep = clear_ordinary
                                            ? context : length - 1;
        else if (cell_index != SIZE_MAX) keep = cell_index - 1;
        else if (!table_section(top)) return LXA_ERROR_UNSUPPORTED;
    }
    else {
        if (named(top, "table") || caption_open || group_open) {
            extra = 3;
            if (caption_open || group_open) keep = table + 1;
        }
        else if (table_section(top)) extra = 2;
        else if (cell_index != SIZE_MAX) keep = cell_index;
        else if (!named(top, "tr")) return LXA_ERROR_UNSUPPORTED;
    }
    if (keep <= table) return LXA_ERROR_INTERNAL;
reserve_part:
    status = lxa_size_add(keep, extra, &after);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&tree->template_stack), 1,
                              &depth);
    if (status == LXA_OK) status = lxa_size_mul(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, after, &depth);
    if (status == LXA_OK
        && tree->template_return_mode == LXA_TREE_IN_BODY) {
        size_t body = lxa_vector_length(&tree->open_elements);
        if (body == 0) return LXA_ERROR_INTERNAL;
        status = lxa_size_add(depth, body - 1, &depth);
    }
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_TREE_DEPTH,
                                   depth, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   extra + column, &tree->nodes->limit_reason);
    if (status == LXA_OK && extra > length - keep)
        status = lxa_budget_check(tree->nodes->budget,
            LXA_BUDGET_OPEN_ELEMENTS, extra - (length - keep),
            &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_vector_reserve(&tree->template_elements, after);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status != LXA_OK) return status;
    if (((group && table != SIZE_MAX && keep == table + 1)
         || (row && table != SIZE_MAX && context != SIZE_MAX
             && context > table && keep == context + 1))
        && length > keep
        && tree->pending_template_formatting.handle == LXA_HANDLE_NONE) {
        size_t j, first = length;
        bool retain = true;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    length - keep,
                                    &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        for (j = keep; j < length; ++j) {
            lxa_span_t name;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                 open[j], &name);
            if (status != LXA_OK) return status;
            if (template_formatting(name)) {
                if (first == length) first = j;
            }
            else if (first != length
                     || (!named(name, "span") && !named(name, "div"))) {
                retain = false;
                break;
            }
        }
        if (retain && first < length)
            status = template_retain_formatting_chain(tree, first,
                                                       length - first, keep);
        else {
            status = template_pop_elements(tree, keep);
            if (status != LXA_OK) return status;
        }
        if (status != LXA_OK) return status;
    }
    else {
        status = template_pop_elements(tree, keep);
        if (status != LXA_OK) return status;
    }
    implied.kind = LXA_HTML_START_TAG;
    if (implied_section) {
        implied.name = literal("tbody");
        status = template_push_table_part(tree, &implied);
        if (status != LXA_OK) return status;
    }
    if (implied_row) {
        implied.name = literal("tr");
        status = template_push_table_part(tree, &implied);
        if (status != LXA_OK) return status;
    }
    if (column) {
        implied.name = literal("colgroup");
        status = template_push_table_part(tree, &implied);
        if (status == LXA_OK)
            status = append_void(tree, template_parent(tree), token);
    }
    else
        status = template_push_table_part(tree, token);
    if (status == LXA_OK) tree->template_insertion_mode = next_mode;
    return status;
}

static lxa_status_t
template_open_column(lxa_html_tree_t *tree,
                     const lxa_html_token_t *token)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    lxa_html_token_t implied = {0};
    lxa_span_t top;
    size_t length = lxa_vector_length(&tree->template_elements);
    size_t depth, count, after;
    bool add_group;
    lxa_status_t status;
    if (length == template_boundary(tree)) {
        if (tree->template_insertion_mode == LXA_TEMPLATE_ROW
            || tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY)
            return LXA_OK; /* No table can receive this column. */
        if (tree->template_insertion_mode != LXA_TEMPLATE_TABLE) {
            /* The first column in initial/column-group mode is direct. */
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = append_void(tree, template_parent(tree), token);
            if (status == LXA_OK
                && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
                tree->template_insertion_mode = LXA_TEMPLATE_COLUMN_GROUP;
            return status;
        }
    }
    else {
        status = lxa_dom_nodes_element_name(tree->nodes, open[length - 1],
                                            &top);
        if (status != LXA_OK) return status;
        if (length == template_boundary(tree) + 1
            && tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
            && named(top, "tr")) {
            status = template_pop_elements(tree, template_boundary(tree));
            if (status != LXA_OK) return status;
            return LXA_OK;
        }
        if (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
            && !named(top, "table") && !named(top, "colgroup")) {
            size_t i = length;
            bool table = false, formatting = false;
            while (i > template_boundary(tree)) {
                lxa_span_t name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[--i], &name);
                if (status != LXA_OK) return status;
                if (named(name, "table")) {
                    table = true;
                    break;
                }
                if (template_formatting(name)) formatting = true;
            }
            if (!table && !formatting) {
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 2, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = template_pop_elements(tree, template_boundary(tree));
                if (status != LXA_OK) return status;
                return template_open_column(tree, token);
            }
        }
    }
    if (length == template_boundary(tree)
        || (!named(top, "table") && !named(top, "colgroup"))) {
        /* The in-table col rule clears an open row/cell (or fostered
         * descendant) before inserting its implicit column group. */
        return template_open_table_part(tree, token);
    }
    add_group = named(top, "table");
    count = add_group ? 2 : 1;
    status = lxa_size_add(length, count, &after);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   count, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&tree->template_stack),
                              1, &depth);
    if (status == LXA_OK) status = lxa_size_mul(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, 2, &depth);
    if (status == LXA_OK)
        status = lxa_size_add(depth, after, &depth);
    if (status == LXA_OK
        && tree->template_return_mode == LXA_TREE_IN_BODY) {
        size_t body = lxa_vector_length(&tree->open_elements);
        if (body == 0) return LXA_ERROR_INTERNAL;
        status = lxa_size_add(depth, body - 1, &depth);
    }
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_TREE_DEPTH,
                                   depth, &tree->nodes->limit_reason);
    if (status == LXA_OK && add_group)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                                   1, &tree->nodes->limit_reason);
    if (status == LXA_OK && add_group)
        status = lxa_vector_reserve(&tree->template_elements, after - 1);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status != LXA_OK) return status;
    if (add_group) {
        implied.kind = LXA_HTML_START_TAG;
        implied.name = literal("colgroup");
        status = template_open_table_part(tree, &implied);
        if (status != LXA_OK) return status;
    }
    return append_void(tree, template_parent(tree), token);
}

static lxa_status_t
template_column_text(lxa_html_tree_t *tree, lxa_span_t bytes)
{
    lxa_buffer_t spaces;
    size_t i = 0, start;
    lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_WORK, bytes.length, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_buffer_init(&spaces, &tree->nodes->allocator);
    if (status != LXA_OK) return status;
    status = lxa_buffer_bind_budget(&spaces, tree->nodes->budget);
    while (i < bytes.length && status == LXA_OK) {
        start = i;
        while (i < bytes.length
               && (bytes.data[i] == ' ' || bytes.data[i] == '\t'
                   || bytes.data[i] == '\n' || bytes.data[i] == '\f'
                   || bytes.data[i] == '\r'))
            ++i;
        if (i != start)
            status = lxa_buffer_append(&spaces,
                (lxa_span_t){bytes.data + start, i - start});
        if (i < bytes.length) ++i;
    }
    if (status == LXA_OK && spaces.length != 0)
        status = append_character(tree, template_parent(tree),
            LXA_DOM_KIND_TEXT, (lxa_span_t){spaces.data, spaces.length});
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    lxa_buffer_destroy(&spaces);
    return status;
}

static bool
template_pending_at_top(const lxa_html_tree_t *tree)
{
    size_t count = lxa_vector_length(&tree->template_elements);
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    return count > template_boundary(tree)
        && open[count - 1].handle == tree->pending_template_formatting.handle;
}

static lxa_status_t
template_pending_paragraph_chain(lxa_html_tree_t *tree,
                                 size_t paragraph, bool *match)
{
    size_t start = template_pending_extra_start(tree);
    size_t extras = lxa_vector_length(&tree->pending_template_extras) - start;
    size_t boundary = template_boundary(tree), j;
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    const lxa_dom_ref_t *entries =
        (const lxa_dom_ref_t *) tree->pending_template_extras.bytes.data;
    lxa_status_t status;
    *match = false;
    if (extras == 0 || paragraph != boundary
        || !template_pending_at_top(tree)
        || lxa_vector_length(&tree->template_elements) - boundary
           != extras + 2)
        return LXA_OK;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                               extras + 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    for (j = 0; j < extras; ++j)
        if (open[boundary + 1 + j].handle != entries[start + j].handle)
            return LXA_OK;
    *match = true;
    return LXA_OK;
}

static bool
template_pending_parent_of_top(const lxa_html_tree_t *tree)
{
    size_t count = lxa_vector_length(&tree->template_elements);
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    return count > template_boundary(tree) + 1
        && open[count - 2].handle
           == tree->pending_template_formatting.handle;
}

static lxa_status_t
template_pending_span_chain(lxa_html_tree_t *tree,
                            size_t length, size_t *index)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t i = length;
    *index = SIZE_MAX;
    while (i > template_boundary(tree)) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        if (open[i].handle == tree->pending_template_formatting.handle) {
            *index = i;
            return LXA_OK;
        }
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (!named(name, "span")) break;
    }
    return LXA_OK;
}

static lxa_status_t
template_pending_on_stack(lxa_html_tree_t *tree, bool *found)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t i = lxa_vector_length(&tree->template_elements);
    *found = false;
    while (i > template_boundary(tree)) {
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (open[--i].handle == tree->pending_template_formatting.handle) {
            *found = true;
            break;
        }
    }
    return LXA_OK;
}

static bool
template_pending_chain_block(lxa_span_t name)
{
    return named(name, "div") || named(name, "section")
        || named(name, "blockquote");
}

static bool
template_pending_direct_block(lxa_span_t name)
{
    return named(name, "article") || named(name, "address")
        || named(name, "figure") || named(name, "pre")
        || named(name, "listing") || heading(name);
}

static bool
template_pending_marker(lxa_span_t name)
{
    return named(name, "object") || named(name, "marquee")
        || named(name, "applet");
}

static bool
template_pending_foreign_name(lxa_span_t name)
{
    return named(name, "svg") || named(name, "math")
        || named(name, "g") || named(name, "path")
        || named(name, "circle") || named(name, "mi")
        || named(name, "mo") || named(name, "mn");
}

static lxa_status_t
template_pending_block_chain(lxa_html_tree_t *tree,
                             size_t length, bool *found)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t i = template_boundary(tree);
    *found = length > i;
    while (i < length) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                open[i], &name);
        if (status != LXA_OK) return status;
        if (!template_pending_chain_block(name)) {
            *found = false;
            break;
        }
        ++i;
    }
    return LXA_OK;
}

static lxa_status_t
template_pending_list_index(lxa_html_tree_t *tree,
                            size_t *index)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t count = lxa_vector_length(&tree->template_elements);
    size_t i = template_boundary(tree);
    lxa_span_t name;
    lxa_status_t status;
    *index = SIZE_MAX;
    while (i < count) {
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                   &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                open[i], &name);
        if (status != LXA_OK) return status;
        if (named(name, "ul") || named(name, "ol")) {
            *index = i;
            if (++i >= count) break;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                       &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[i], &name);
            if (status != LXA_OK) return status;
            if (!named(name, "li")) break;
            ++i;
            if (i < count
                && open[i].handle
                   == tree->pending_template_formatting.handle)
                ++i;
            continue;
        }
        if (!template_pending_chain_block(name)) break;
        ++i;
    }
    return LXA_OK;
}

static lxa_status_t
template_pending_list_block_tail(lxa_html_tree_t *tree,
                                 size_t length, bool *found)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t list, i;
    lxa_span_t name;
    lxa_status_t status = template_pending_list_index(tree, &list);
    *found = false;
    if (status != LXA_OK || list == SIZE_MAX || length < list + 2)
        return status;
    for (i = list + 1; i < length; ++i) {
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                   &tree->nodes->limit_reason);
        if (status == LXA_OK && i == list + 2
            && open[i].handle
               == tree->pending_template_formatting.handle)
            continue;
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                open[i], &name);
        if (status != LXA_OK) return status;
        if (!(i == list + 1 ? named(name, "li")
                            : template_pending_chain_block(name)))
            return LXA_OK;
    }
    *found = true;
    return LXA_OK;
}

static lxa_status_t
reconstruct_template_formatting(lxa_html_tree_t *tree)
{
    size_t count, depth, start, extra_count, kept = 0, needed, j;
    bool pending_open;
    lxa_dom_ref_t clone;
    lxa_status_t status;
    if (tree->pending_template_formatting.handle == LXA_HANDLE_NONE)
        return LXA_OK;
    count = lxa_vector_length(&tree->template_elements);
    if (template_pending_at_top(tree)
        || template_pending_parent_of_top(tree))
        return LXA_OK;
    status = template_pending_on_stack(tree, &pending_open);
    if (status != LXA_OK || pending_open) return status;
    start = template_pending_extra_start(tree);
    extra_count = lxa_vector_length(&tree->pending_template_extras) - start;
    if (extra_count != 0 && count > template_boundary(tree)
        && count - template_boundary(tree) <= extra_count) {
        const lxa_dom_ref_t *open =
            (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
        const lxa_dom_ref_t *extras =
            (const lxa_dom_ref_t *) tree->pending_template_extras.bytes.data;
        size_t length = count - template_boundary(tree);
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                   length, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        for (j = 0; j < length; ++j)
            if (open[template_boundary(tree) + j].handle
                != extras[start + j].handle)
                break;
        if (j == length) kept = length;
    }
    status = lxa_size_add(extra_count - kept, 1, &needed);
    if (status == LXA_OK) status = lxa_size_add(count, needed, &count);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&tree->template_stack), 1,
                              &depth);
    if (status == LXA_OK) status = lxa_size_mul(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, count, &depth);
    if (status == LXA_OK
        && tree->template_return_mode == LXA_TREE_IN_BODY) {
        size_t body = lxa_vector_length(&tree->open_elements);
        if (body == 0) return LXA_ERROR_INTERNAL;
        status = lxa_size_add(depth, body - 1, &depth);
    }
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_TREE_DEPTH,
                                   depth, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES, needed,
                                   &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget,
            LXA_BUDGET_OPEN_ELEMENTS, needed, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_vector_reserve(&tree->template_elements, count);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status != LXA_OK) return status;
    for (j = 0; j < needed; ++j) {
        lxa_dom_ref_t *extras = (lxa_dom_ref_t *)
            tree->pending_template_extras.bytes.data;
        lxa_dom_ref_t source = j < extra_count - kept
                                               ? extras[start + kept + j]
                                               : tree->pending_template_formatting;
        status = clone_formatting_element(tree, source, &clone);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                template_parent(tree), clone);
        if (status == LXA_OK)
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_vector_push(&tree->template_elements, &clone);
        if (status != LXA_OK) {
            lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                               1);
            if (status == LXA_ERROR_LIMIT
                && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
                tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
            return status;
        }
        if (j < extra_count - kept) extras[start + kept + j] = clone;
        else tree->pending_template_formatting = clone;
    }
    return LXA_OK;
}

static lxa_status_t
template_retain_formatting_chain(lxa_html_tree_t *tree,
                                 size_t first, size_t count, size_t keep)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->pending_template_extras);
    size_t after, j, format_count = 0, last = SIZE_MAX;
    lxa_status_t status;
    if (count == 0 || tree->pending_template_formatting.handle
                     != LXA_HANDLE_NONE)
        return LXA_ERROR_UNSUPPORTED;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, count,
                               &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    for (j = 0; j < count; ++j) {
        lxa_span_t name;
        status = lxa_dom_nodes_element_name(tree->nodes, open[first + j],
                                            &name);
        if (status != LXA_OK) return status;
        if (template_formatting(name)) {
            ++format_count;
            last = j;
        }
        else if (format_count == 0 || !template_crossed_inline(name))
            return LXA_ERROR_UNSUPPORTED;
    }
    status = lxa_size_add(length, format_count - 1, &after);
    if (status == LXA_OK)
        status = lxa_vector_reserve(&tree->pending_template_extras, after);
    if (status == LXA_OK)
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                   count, &tree->nodes->limit_reason);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status == LXA_OK)
        status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_ACTIVE_FORMATTING, format_count,
            &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    for (j = 0; j < last; ++j) {
        lxa_span_t name;
        status = lxa_dom_nodes_element_name(tree->nodes,
                                             open[first + j], &name);
        if (status != LXA_OK) {
            lxa_vector_erase(&tree->pending_template_extras, length,
                lxa_vector_length(&tree->pending_template_extras) - length);
            lxa_budget_release(tree->nodes->budget,
                                LXA_BUDGET_ACTIVE_FORMATTING, format_count);
            return status;
        }
        if (!template_formatting(name)) continue;
        status = lxa_vector_push(&tree->pending_template_extras,
                                 &open[first + j]);
        if (status != LXA_OK) {
            lxa_vector_erase(&tree->pending_template_extras, length,
                lxa_vector_length(&tree->pending_template_extras) - length);
            lxa_budget_release(tree->nodes->budget,
                                LXA_BUDGET_ACTIVE_FORMATTING, format_count);
            return status;
        }
    }
    tree->pending_template_formatting = open[first + last];
    status = template_pop_elements(tree, keep);
    if (status != LXA_OK) return status;
    return LXA_OK;
}

static lxa_status_t
current_html_column_group(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                          size_t boundary, bool *column_group)
{
    size_t length = lxa_vector_length(elements);
    lxa_status_t status;
    *column_group = false;
    if (length > boundary) {
        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) elements->bytes.data;
        lxa_dom_namespace_t ns;
        lxa_span_t name;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[length - 1],
                                                  &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[length - 1],
                                                 &name);
        if (status != LXA_OK) return status;
        *column_group = ns == LXA_DOM_NS_HTML && named(name, "colgroup");
    }
    return LXA_OK;
}


static bool
body_control_start(lxa_span_t name)
{
    return named(name, "html") || named(name, "head") || named(name, "body")
        || named(name, "frameset") || named(name, "frame");
}


static lxa_status_t
consume(lxa_html_tree_t *tree,
        lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token)
{
    lxa_status_t status;
    lxa_html_token_t adjusted;
    lxa_dom_ref_t parent, child;
    lxa_dom_doctype_data_t declaration;
    bool template_ordinary;
    bool template_cell_scope = false;
    bool template_table_scope = false;
    bool integration_object = false;
    bool pending_table_context = false, pending_column_context = false;
    bool pending_cell_context = false, pending_cell_end = false;
    bool pending_section_start = false, pending_row_start = false;
    size_t template_select_close = SIZE_MAX;
    if (token == NULL
        || (unsigned int) token->kind > LXA_HTML_PROCESSING_INSTRUCTION
        || (token->name.data == NULL && token->name.length != 0)
        || (token->data.data == NULL && token->data.length != 0)
        || (token->attribute_count != 0 && token->attributes == NULL)
        || (token->kind != LXA_HTML_START_TAG
            && token->attribute_count != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (token->kind != LXA_HTML_TEXT) {
        status = tree->mode == LXA_TREE_TEMPLATE_TEXT
                 ? flush_template_table_text(tree) : flush_table_text(tree);
        if (status != LXA_OK) return status;
    }
    if (tree->skip_pre_newline) {
        tree->skip_pre_newline = false;
        if (token->kind == LXA_HTML_TEXT && token->data.length != 0
            && token->data.data[0] == '\n') {
            adjusted = *token;
            ++adjusted.data.data;
            --adjusted.data.length;
            if (adjusted.data.length == 0) return LXA_OK;
            token = &adjusted;
        }
    }
    if (tree->mode == LXA_TREE_TEMPLATE_TEXT) {
reprocess_template_token:
        if (token->kind == LXA_HTML_END_TAG
            && named(token->name, "template")) {
            size_t work;
            size_t length = lxa_vector_length(&tree->template_elements);
            if (length > template_boundary(tree)) {
                const lxa_dom_ref_t *open =
                    (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
                lxa_dom_namespace_t ns;
                lxa_span_t name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_namespace(tree->nodes,
                        open[length - 1], &ns);
                if (status == LXA_OK && ns == LXA_DOM_NS_SVG)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[length - 1], &name);
                if (status != LXA_OK) return status;
                if (ns == LXA_DOM_NS_SVG && named(name, "template")) {
                    status = template_pop_elements(tree, length - 1);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
            }
            status = lxa_size_add(
                length - template_boundary(tree), 1, &work);
            if (status == LXA_OK)
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, work, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = close_template(tree);
            if (status != LXA_OK) return status;
            return LXA_OK;
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && ((token->kind == LXA_HTML_START_TAG
                 && named(token->name, "col"))
                || (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "colgroup")
                        || named(token->name, "table"))))) {
            size_t count = lxa_vector_length(&tree->template_elements);
            size_t boundary = template_boundary(tree);
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            if (count > boundary) {
                lxa_span_t top;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 1], &top);
                if (status != LXA_OK) return status;
                pending_table_context = named(top, "table");
                if (named(top, "colgroup") && count > boundary + 1) {
                    lxa_span_t below;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[count - 2], &below);
                    if (status != LXA_OK) return status;
                    pending_column_context = named(below, "table");
                    pending_table_context = pending_column_context;
                }
                if (!pending_table_context
                    && token->kind == LXA_HTML_END_TAG
                    && named(token->name, "table")) {
                    size_t j = count - 1;
                    while (j > boundary) {
                        lxa_span_t ancestor;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[--j], &ancestor);
                        if (status != LXA_OK) return status;
                        if (named(ancestor, "table")) {
                            pending_table_context = true;
                            break;
                        }
                    }
                }
            }
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && table_section(token->name)
            && lxa_vector_length(&tree->template_elements)
               > template_boundary(tree)) {
            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                tree->template_elements.bytes.data;
            lxa_span_t top;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[lxa_vector_length(&tree->template_elements) - 1],
                    &top);
            if (status != LXA_OK) return status;
            pending_section_start = named(top, "table");
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && named(token->name, "tr")
            && lxa_vector_length(&tree->template_elements)
               >= template_boundary(tree) + 2) {
            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                tree->template_elements.bytes.data;
            size_t count = lxa_vector_length(&tree->template_elements);
            lxa_span_t top, below;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        2, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[count - 1], &top);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[count - 2], &below);
            if (status != LXA_OK) return status;
            pending_row_start = table_section(top) && named(below, "table");
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && (named(token->name, "td") || named(token->name, "th"))) {
            size_t count = lxa_vector_length(&tree->template_elements);
            size_t boundary = template_boundary(tree);
            if (count >= boundary + 3) {
                const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                    tree->template_elements.bytes.data;
                lxa_span_t row_name, section_name, table_name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 3, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 1], &row_name);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 2], &section_name);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 3], &table_name);
                if (status != LXA_OK) return status;
                pending_cell_context = named(row_name, "tr")
                    && table_section(section_name)
                    && named(table_name, "table");
            }
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_END_TAG
            && (named(token->name, "td") || named(token->name, "th"))) {
            size_t count = lxa_vector_length(&tree->template_elements);
            size_t boundary = template_boundary(tree);
            if (count >= boundary + 4) {
                const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                    tree->template_elements.bytes.data;
                lxa_span_t cell_name, row_name, section_name, table_name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 4, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 1], &cell_name);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 2], &row_name);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 3], &section_name);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 4], &table_name);
                if (status != LXA_OK) return status;
                pending_cell_end = cell_name.length == token->name.length
                    && memcmp(cell_name.data, token->name.data,
                              cell_name.length) == 0
                    && named(row_name, "tr") && table_section(section_name)
                    && named(table_name, "table");
            }
        }
        if (lxa_vector_length(&tree->pending_template_extras)
            > template_pending_extra_start(tree)
            && token->kind != LXA_HTML_TEXT
            && !(token->kind == LXA_HTML_START_TAG
                 && (named(token->name, "template")
                     || named(token->name, "span")
                     || template_pending_chain_block(token->name)
                     || template_pending_direct_block(token->name)
                     || named(token->name, "p")
                     || template_pending_foreign_name(token->name)
                     || named(token->name, "br")
                     || named(token->name, "img")
                     || named(token->name, "wbr")
                     || named(token->name, "hr")
                     || named(token->name, "ul")
                     || named(token->name, "ol")
                     || named(token->name, "li")
                     || pending_section_start
                     || pending_row_start
                     || (named(token->name, "col")
                         && pending_column_context)
                     || ((named(token->name, "td")
                          || named(token->name, "th"))
                         && pending_cell_context)))
            && !(token->kind == LXA_HTML_END_TAG
                 && (named(token->name, "span")
                     || template_pending_chain_block(token->name)
                     || template_pending_direct_block(token->name)
                     || named(token->name, "p")
                     || template_formatting(token->name)
                     || template_pending_foreign_name(token->name)
                     || named(token->name, "ul")
                     || named(token->name, "ol")
                     || named(token->name, "li")
                     || pending_cell_end
                     || ((named(token->name, "colgroup")
                          || named(token->name, "table"))
                         && pending_table_context))))
            return LXA_ERROR_UNSUPPORTED;
        bool pending_block_list = false;
        bool pending_list_block = false;
        bool pending_nested_list = false;
        bool pending_span_descendant = false;
        bool pending_foreign_child = false;
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && (named(token->name, "circle")
                || named(token->name, "g")
                || named(token->name, "path")
                || named(token->name, "mi")
                || named(token->name, "mo")
                || named(token->name, "mn"))
            && lxa_vector_length(&tree->template_elements)
               > template_boundary(tree)) {
            lxa_dom_namespace_t ns;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_namespace(tree->nodes,
                    template_parent(tree), &ns);
            if (status != LXA_OK) return status;
            pending_foreign_child = (named(token->name, "circle")
                || named(token->name, "g") || named(token->name, "path"))
                ? ns == LXA_DOM_NS_SVG : ns == LXA_DOM_NS_MATHML;
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && (named(token->name, "span")
                || named(token->name, "br")
                || named(token->name, "img")
                || named(token->name, "wbr")
                || named(token->name, "hr")
                || named(token->name, "svg")
                || named(token->name, "math")
                || named(token->name, "p")
                || named(token->name, "div")
                || named(token->name, "section")
                || named(token->name, "article")
                || named(token->name, "blockquote")
                || named(token->name, "address")
                || named(token->name, "figure")
                || named(token->name, "pre")
                || named(token->name, "listing")
                || heading(token->name))
            && lxa_vector_length(&tree->template_elements)
               > template_boundary(tree)) {
            size_t ancestor;
            status = template_pending_span_chain(tree,
                lxa_vector_length(&tree->template_elements), &ancestor);
            if (status != LXA_OK) return status;
            pending_span_descendant = ancestor != SIZE_MAX;
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && (named(token->name, "ul") || named(token->name, "ol")
                || template_pending_chain_block(token->name))
            && lxa_vector_length(&tree->template_elements)
               > template_boundary(tree)) {
            status = template_pending_block_chain(tree,
                lxa_vector_length(&tree->template_elements),
                &pending_block_list);
            if (status != LXA_OK) return status;
            status = template_pending_list_block_tail(tree,
                lxa_vector_length(&tree->template_elements),
                &pending_list_block);
            if (status != LXA_OK) return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "span")
            && lxa_vector_length(&tree->pending_template_extras)
               > template_pending_extra_start(tree)
            && lxa_vector_length(&tree->template_elements)
               == template_boundary(tree) + 1) {
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            lxa_span_t parent_name;
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[template_boundary(tree)], &parent_name);
            if (status != LXA_OK) return status;
            if (template_pending_chain_block(parent_name))
                pending_span_descendant = true;
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && (named(token->name, "ul") || named(token->name, "ol"))) {
            size_t list, count = lxa_vector_length(&tree->template_elements);
            lxa_span_t item_name;
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            status = template_pending_list_index(tree, &list);
            if (status != LXA_OK) return status;
            if (list != SIZE_MAX && template_pending_at_top(tree)) {
                size_t start = template_pending_extra_start(tree);
                size_t extras =
                    lxa_vector_length(&tree->pending_template_extras) - start;
                if (extras != 0 && count - list == extras + 3) {
                    const lxa_dom_ref_t *entries = (const lxa_dom_ref_t *)
                        tree->pending_template_extras.bytes.data;
                    size_t j;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, extras + 1,
                        &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[list + 1], &item_name);
                    if (status != LXA_OK) return status;
                    if (named(item_name, "li")) {
                        for (j = 0; j < extras; ++j)
                            if (open[list + 2 + j].handle
                                != entries[start + j].handle)
                                break;
                        if (j == extras) pending_nested_list = true;
                    }
                }
            }
            if (list != SIZE_MAX
                && (count == list + 2
                    || (count == list + 3
                        && template_pending_at_top(tree)))) {
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[list + 1], &item_name);
                if (status != LXA_OK) return status;
                pending_nested_list = named(item_name, "li");
            }
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && named(token->name, "li")) {
            size_t count = lxa_vector_length(&tree->template_elements);
            size_t list;
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            lxa_span_t item_name;
            bool nested_item = false;
            if (count > template_boundary(tree) + 1
                && open[count - 2].handle
                   == tree->pending_template_formatting.handle) {
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 1], &item_name);
                if (status != LXA_OK) return status;
                nested_item = named(item_name, "ul")
                              || named(item_name, "ol");
            }
            if (!nested_item) {
                status = template_pending_list_index(tree, &list);
                if (status != LXA_OK) return status;
                if (list == SIZE_MAX || count > list + 3
                    || (count == list + 3
                        && !template_pending_at_top(tree)))
                    return LXA_ERROR_UNSUPPORTED;
                if (count > list + 1) {
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, count - list - 1,
                        &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[list + 1], &item_name);
                    if (status != LXA_OK) return status;
                    if (!named(item_name, "li")) return LXA_ERROR_UNSUPPORTED;
                    status = template_pop_elements(tree, list + 1);
                    if (status != LXA_OK) return status;
                }
            }
        }
        if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
            && token->kind == LXA_HTML_START_TAG
            && !named(token->name, "template")
            && !((named(token->name, "p") || named(token->name, "div")
                  || named(token->name, "section")
                  || named(token->name, "article")
                  || named(token->name, "blockquote")
                  || named(token->name, "address")
                  || named(token->name, "figure")
                  || named(token->name, "pre")
                  || named(token->name, "listing")
                  || heading(token->name))
                 && (lxa_vector_length(&tree->template_elements)
                     == template_boundary(tree)
                     || template_pending_at_top(tree)
                     || pending_span_descendant))
            && !((named(token->name, "ul") || named(token->name, "ol"))
                 && lxa_vector_length(&tree->template_elements)
                    == template_boundary(tree))
            && !((named(token->name, "title")
                  || named(token->name, "textarea")
                  || named(token->name, "script")
                  || named(token->name, "style")
                  || named(token->name, "iframe")
                  || named(token->name, "noembed")
                  || named(token->name, "noframes")
                  || named(token->name, "xmp")
                  || named(token->name, "plaintext"))
                 && lxa_vector_length(&tree->template_elements)
                    == template_boundary(tree))
            && !((named(token->name, "svg")
                  || named(token->name, "math"))
                 && (lxa_vector_length(&tree->template_elements)
                     == template_boundary(tree)
                     || pending_span_descendant))
            && !(named(token->name, "plaintext")
                 && template_pending_at_top(tree))
            && !(named(token->name, "hr")
                 && (lxa_vector_length(&tree->template_elements)
                     == template_boundary(tree)
                     || template_pending_at_top(tree)
                     || pending_span_descendant))
            && !((named(token->name, "br") || named(token->name, "img")
                  || named(token->name, "wbr"))
                 && (lxa_vector_length(&tree->template_elements)
                     == template_boundary(tree)
                     || template_pending_at_top(tree)
                     || pending_span_descendant))
            && !(named(token->name, "span")
                 && (lxa_vector_length(&tree->template_elements)
                     == template_boundary(tree)
                     || pending_span_descendant))
            && !(template_pending_marker(token->name)
                 && (lxa_vector_length(&tree->template_elements)
                     == template_boundary(tree)
                     || template_pending_at_top(tree)))
            && !pending_block_list
            && !pending_list_block
            && !pending_nested_list
            && !pending_foreign_child
            && !pending_section_start
            && !pending_row_start
            && !(named(token->name, "col") && pending_column_context)
            && !((named(token->name, "td") || named(token->name, "th"))
                 && pending_cell_context)
            && !named(token->name, "li")) {
            /* Other starts need formatting-list rules. */
            return LXA_ERROR_UNSUPPORTED;
        }
        if (lxa_vector_length(&tree->template_elements)
            > template_boundary(tree)
            && (token->kind == LXA_HTML_START_TAG
                || token->kind == LXA_HTML_TEXT)) {
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            lxa_dom_namespace_t ns;
            size_t length = lxa_vector_length(&tree->template_elements);
            status = lxa_dom_nodes_element_namespace(tree->nodes,
                open[length - 1], &ns);
            if (status != LXA_OK) return status;
            if (ns == LXA_DOM_NS_SVG) {
                lxa_span_t current;
                bool font = false;
                if (token->kind == LXA_HTML_TEXT)
                    return insert_data_text(tree, template_parent(tree),
                                            token->data, true, false);
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[length - 1], &current);
                if (status != LXA_OK) return status;
                if (named(token->name, "svg")
                    || named(token->name, "math"))
                    return template_open_foreign(tree, token,
                        named(token->name, "svg") ? LXA_DOM_NS_SVG
                                                  : LXA_DOM_NS_MATHML);
                if (named(token->name, "font")) {
                    status = font_breakout(tree, token, &font);
                    if (status != LXA_OK) return status;
                }
                if (!named(current, "foreignObject")
                    && !named(current, "title")
                    && !named(current, "desc")
                    && (foreign_html_start_breakout(token->name) || font)) {
                    size_t keep = length;
                    while (keep > template_boundary(tree)) {
                        lxa_dom_namespace_t ancestor_ns;
                        lxa_span_t ancestor;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_namespace(
                                tree->nodes, open[keep - 1], &ancestor_ns);
                        if (status != LXA_OK) return status;
                        if (ancestor_ns == LXA_DOM_NS_HTML) break;
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[keep - 1], &ancestor);
                        if (status != LXA_OK) return status;
                        if (ancestor_ns == LXA_DOM_NS_MATHML) {
                            bool integrated =
                                mathml_text_integration(ancestor);
                            if (named(ancestor, "annotation-xml")) {
                                status = annotation_html_integration(tree,
                                    open[keep - 1], &integrated);
                                if (status != LXA_OK) return status;
                            }
                            if (integrated) break;
                        }
                        else if (ancestor_ns != LXA_DOM_NS_SVG)
                            return LXA_ERROR_UNSUPPORTED;
                        else if (named(ancestor, "foreignObject")
                                 || named(ancestor, "title")
                                 || named(ancestor, "desc"))
                            break;
                        --keep;
                    }
                    status = template_pop_elements(tree, keep);
                    if (status != LXA_OK) return status;
                    goto reprocess_template_token;
                }
                /* Only the qualified HTML integration child is routed
                 * through the template's in-body construction below. */
                if (named(current, "foreignObject")
                    || named(current, "title") || named(current, "desc")) {
                    integration_object = named(token->name, "object");
                    if (!template_formatting(token->name)
                        && !template_simple_inline(token->name)
                        && !integration_object
                        && !named(token->name, "span")
                        && !named(token->name, "p")
                        && !named(token->name, "div")
                        && !named(token->name, "button")
                        && !template_sectioning(token->name)
                        && !named(token->name, "ul")
                        && !named(token->name, "ol")
                        && !named(token->name, "li")
                        && !named(token->name, "template")
                        && !template_html_text_element(token->name)) {
                        bool ordinary;
                        status = supported_container(tree, token->name,
                                                      &ordinary);
                        if (status != LXA_OK) return status;
                        if (!ordinary) return LXA_ERROR_UNSUPPORTED;
                    }
                }
                else {
                    return template_open_foreign(tree, token, ns);
                }
            }
            if (ns == LXA_DOM_NS_MATHML) {
                lxa_span_t current;
                bool ordinary, integrated, font = false;
                if (token->kind == LXA_HTML_TEXT)
                    return insert_data_text(tree, template_parent(tree),
                                            token->data, true, false);
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[length - 1], &current);
                if (status != LXA_OK) return status;
                if (named(token->name, "svg"))
                    return template_open_foreign(tree, token, LXA_DOM_NS_SVG);
                if (named(token->name, "math")
                    || (mathml_text_integration(current)
                        && (named(token->name, "mglyph")
                            || named(token->name, "malignmark"))))
                    return template_open_foreign(tree, token,
                                                 LXA_DOM_NS_MATHML);
                integrated = mathml_text_integration(current);
                if (named(current, "annotation-xml")) {
                    status = annotation_html_integration(tree,
                        open[length - 1], &integrated);
                    if (status != LXA_OK) return status;
                }
                integration_object = integrated
                    && named(token->name, "object");
                if (integrated && table_part(token->name)
                    && !named(token->name, "table")) {
                    size_t j = length;
                    while (j > template_boundary(tree)) {
                        lxa_span_t ancestor;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[--j], &ancestor);
                        if (status != LXA_OK) return status;
                        if (named(ancestor, "table")) {
                            lxa_dom_namespace_t ancestor_ns;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_namespace(
                                    tree->nodes, open[j], &ancestor_ns);
                            if (status != LXA_OK) return status;
                            if (ancestor_ns == LXA_DOM_NS_HTML)
                                return named(token->name, "col")
                                    ? template_open_column(tree, token)
                                    : template_open_table_part(tree, token);
                        }
                    }
                    if (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                        || tree->template_insertion_mode
                           == LXA_TEMPLATE_TABLE_BODY) {
                        lxa_span_t root;
                        lxa_dom_namespace_t root_ns;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[template_boundary(tree)], &root);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_namespace(
                                tree->nodes, open[template_boundary(tree)],
                                &root_ns);
                        if (status != LXA_OK) return status;
                        if (root_ns == LXA_DOM_NS_HTML) {
                            if (tree->template_insertion_mode
                                == LXA_TEMPLATE_TABLE && table_section(root))
                                return named(token->name, "col")
                                    ? template_open_column(tree, token)
                                    : template_open_table_part(tree, token);
                            if (tree->template_insertion_mode
                                == LXA_TEMPLATE_TABLE_BODY
                                && named(root, "tr")) {
                                if (named(token->name, "tr")
                                    || named(token->name, "td")
                                    || named(token->name, "th"))
                                    return template_open_table_part(tree,
                                                                    token);
                                /* A table part other than a row or cell
                                 * closes the direct row but has no table to
                                 * receive it. */
                                status = template_pop_elements(tree,
                                                      template_boundary(tree));
                                if (status != LXA_OK) return status;
                                return LXA_OK;
                            }
                        }
                    }
                }
                if (!integrated && named(token->name, "font")) {
                    status = font_breakout(tree, token, &font);
                    if (status != LXA_OK) return status;
                }
                if (!integrated
                    && (foreign_html_start_breakout(token->name) || font)) {
                    size_t keep = length;
                    while (keep > template_boundary(tree)) {
                        lxa_dom_namespace_t ancestor_ns;
                        lxa_span_t ancestor;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_namespace(
                                tree->nodes, open[keep - 1], &ancestor_ns);
                        if (status != LXA_OK) return status;
                        if (ancestor_ns == LXA_DOM_NS_HTML) break;
                        if (ancestor_ns != LXA_DOM_NS_MATHML)
                            return LXA_ERROR_UNSUPPORTED;
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[keep - 1], &ancestor);
                        if (status != LXA_OK) return status;
                        if (mathml_text_integration(ancestor))
                            return LXA_ERROR_UNSUPPORTED;
                        --keep;
                    }
                    status = template_pop_elements(tree, keep);
                    if (status != LXA_OK) return status;
                    goto reprocess_template_token;
                }
                if (!integrated)
                    return template_open_foreign(tree, token, ns);
                if (!template_html_text_element(token->name)
                    && !body_void(token->name)
                    && !integration_object
                    && !named(token->name, "image")
                    && !named(token->name, "base")
                    && !named(token->name, "link")
                    && !named(token->name, "meta")
                    && !named(token->name, "table")
                    && !named(token->name, "template")
                    && !named(token->name, "ruby")
                    && !named(token->name, "rt")
                    && !named(token->name, "rp")) {
                    status = supported_container(tree, token->name,
                                                  &ordinary);
                    if (status != LXA_OK) return status;
                    if (!ordinary) return LXA_ERROR_UNSUPPORTED;
                }
            }
        }
        if (token->kind == LXA_HTML_START_TAG
            && body_control_start(token->name))
        {
            size_t length = lxa_vector_length(&tree->template_elements);
            if (length > template_boundary(tree)
                && !named(token->name, "html")) {
                const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                    tree->template_elements.bytes.data;
                lxa_span_t top;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[length - 1], &top);
                if (status != LXA_OK) return status;
                if (named(top, "colgroup")) {
                    status = template_pop_elements(tree, length - 1);
                    if (status != LXA_OK) return status;
                }
            }
            if (tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
                tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            return LXA_OK; /* None can enter a template fragment. */
        }
        if (tree->template_insertion_mode == LXA_TEMPLATE_BODY
            && token->kind == LXA_HTML_START_TAG
            && table_part(token->name) && !named(token->name, "table")) {
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            size_t i = lxa_vector_length(&tree->template_elements);
            bool table_found = false;
            while (i > template_boundary(tree)) {
                lxa_span_t name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                                                         open[--i], &name);
                if (status != LXA_OK) return status;
                if (named(name, "table")) {
                    table_found = true;
                    break;
                }
            }
            if (!table_found)
                return LXA_OK; /* No table in scope for in-body starts. */
        }
        if (tree->template_insertion_mode == LXA_TEMPLATE_COLUMN_GROUP
            && lxa_vector_length(&tree->template_elements)
               == template_boundary(tree)) {
            /* Without an open colgroup, ordinary starts and nonspace
             * characters are ignored in column-group mode. */
            if (token->kind == LXA_HTML_TEXT)
                return template_column_text(tree, token->data);
            if ((token->kind == LXA_HTML_START_TAG
                 && !named(token->name, "col")
                 && !named(token->name, "template")
                 && !named(token->name, "html"))
                || token->kind == LXA_HTML_END_TAG)
                return LXA_OK;
        }
        if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "svg") || named(token->name, "math"))) {
            bool html_parent =
                lxa_vector_length(&tree->template_elements)
                == template_boundary(tree);
            if (!html_parent) {
                lxa_dom_namespace_t parent_ns;
                lxa_span_t parent_name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_namespace(tree->nodes,
                        template_parent(tree), &parent_ns);
                if (status == LXA_OK && parent_ns == LXA_DOM_NS_HTML)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        template_parent(tree), &parent_name);
                if (status != LXA_OK) return status;
                html_parent = parent_ns == LXA_DOM_NS_HTML
                    && (named(parent_name, "div")
                        || named(parent_name, "p")
                        || named(parent_name, "li")
                        || named(parent_name, "section")
                        || named(parent_name, "span")
                        || named(parent_name, "td")
                        || named(parent_name, "th")
                        || named(parent_name, "caption")
                        || (template_pending_at_top(tree)
                            && template_formatting(parent_name)));
            }
            if (html_parent) {
                if (tree->pending_template_formatting.handle
                    != LXA_HANDLE_NONE) {
                    status = reconstruct_template_formatting(tree);
                    if (status != LXA_OK) return status;
                }
                return template_open_foreign(tree, token,
                    named(token->name, "svg") ? LXA_DOM_NS_SVG
                                               : LXA_DOM_NS_MATHML);
            }
        }
        if (token->kind == LXA_HTML_END_TAG
            && named(token->name, "br")
            && (lxa_vector_length(&tree->template_elements)
                > template_boundary(tree)
                || tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                || tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
                || tree->template_insertion_mode == LXA_TEMPLATE_ROW
                || tree->template_insertion_mode == LXA_TEMPLATE_BODY)) {
            adjusted = *token;
            adjusted.kind = LXA_HTML_START_TAG;
            adjusted.attributes = NULL;
            adjusted.attribute_count = 0;
            adjusted.self_closing = false;
            token = &adjusted;
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "col")
            && lxa_vector_length(&tree->template_elements)
               == template_boundary(tree))
            return template_open_column(tree, token);
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "table")) {
            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                tree->template_elements.bytes.data;
            size_t i = lxa_vector_length(&tree->template_elements);
            bool fostered = false, blocked = false;
            while (i > template_boundary(tree)) {
                lxa_span_t name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[--i], &name);
                if (status != LXA_OK) return status;
                if (named(name, "td") || named(name, "th")
                    || named(name, "caption")) {
                    blocked = true;
                    break; /* A table inside a cell or caption can nest. */
                }
                if (named(name, "select") || named(name, "option")
                    || named(name, "optgroup")) {
                    blocked = true;
                    break; /* Select/table reprocessing closes the select. */
                }
                if (named(name, "table")) {
                    if (fostered) {
                        status = lxa_budget_check(tree->nodes->budget,
                            LXA_BUDGET_NODES, 1,
                            &tree->nodes->limit_reason);
                        if (status != LXA_OK) return status;
                        status = template_pop_elements(tree, i);
                        if (status != LXA_OK) return status;
                        goto reprocess_template_token;
                    }
                    break;
                }
                if (i == template_boundary(tree)
                    && fostered
                    && (table_section(name) || named(name, "tr")))
                    return LXA_OK; /* No table to close in this fragment. */
                if (!table_context(name)) {
                    if (template_formatting(name)) {
                        blocked = true;
                        break; /* Active formatting needs reconstruction. */
                    }
                    fostered = true;
                }
            }
            if (!blocked && fostered
                && i == template_boundary(tree)
                && tree->template_insertion_mode == LXA_TEMPLATE_TABLE)
                return LXA_OK; /* No table exists in this fragment mode. */
        }
        if (lxa_vector_length(&tree->template_elements)
            > template_boundary(tree)) {
            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                tree->template_elements.bytes.data;
            size_t last = lxa_vector_length(&tree->template_elements) - 1;
            lxa_span_t top;
            lxa_dom_namespace_t ns;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes, open[last],
                                                     &top);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_namespace(tree->nodes,
                                                          open[last], &ns);
            if (status != LXA_OK) return status;
            if (ns == LXA_DOM_NS_HTML && last == template_boundary(tree)
                && named(top, "colgroup")
                && token->kind == LXA_HTML_START_TAG
                && !named(token->name, "col")
                && !named(token->name, "colgroup")
                && !table_section(token->name)
                && !named(token->name, "caption")
                && !named(token->name, "tr")
                && !named(token->name, "td")
                && !named(token->name, "th")
                && !named(token->name, "html")
                && !named(token->name, "template")) {
                if (template_html_text_element(token->name)
                    && tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                if (!named(token->name, "form"))
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1,
                        &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = template_pop_elements(tree, last);
                if (status != LXA_OK) return status;
                if (named(token->name, "form")) return LXA_OK;
                goto reprocess_template_token;
            }
            if (ns == LXA_DOM_NS_HTML
                && (named(top, "table") || named(top, "colgroup")
                    || table_section(top)
                    || named(top, "tr"))) {
                if (named(top, "colgroup")
                    && token->kind == LXA_HTML_END_TAG
                    && (table_section(token->name)
                        || named(token->name, "tr")
                        || named(token->name, "td")
                        || named(token->name, "th")
                        || named(token->name, "select")
                        || named(token->name, "frameset")
                        || named(token->name, "body")
                        || named(token->name, "html")
                        || heading(token->name)
                        || named(token->name, "li")
                        || named(token->name, "dt")
                        || named(token->name, "dd")
                        || named(token->name, "option")
                        || named(token->name, "optgroup")
                        || named(token->name, "ruby")
                        || named(token->name, "rt")
                        || named(token->name, "rp")
                        || block_container(token->name))) {
                    status = template_pop_elements(tree, last);
                    if (status != LXA_OK) return status;
                    goto reprocess_template_token;
                }
                if (token->kind == LXA_HTML_TEXT) {
                    return insert_data_text(tree, template_parent(tree),
                                            token->data, false, true);
                }
                if (token->kind == LXA_HTML_START_TAG
                    && named(token->name, "col"))
                    return template_open_column(tree, token);
                if (token->kind == LXA_HTML_START_TAG
                    && (named(token->name, "svg")
                        || named(token->name, "math"))) {
                    lxa_dom_namespace_t ns = named(token->name, "svg")
                        ? LXA_DOM_NS_SVG : LXA_DOM_NS_MATHML;
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    return template_foster_element(tree, token,
                        !token->self_closing, NULL, ns);
                }
                if (token->kind == LXA_HTML_START_TAG
                    && (named(token->name, "base")
                        || named(token->name, "link")
                        || named(token->name, "meta"))) {
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    return template_foster_element(tree, token, false, NULL,
                                                    LXA_DOM_NS_HTML);
                }
                if (token->kind == LXA_HTML_START_TAG
                    && named(token->name, "title")) {
                    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    status = template_foster_element(tree, token, false,
                                                      &child, LXA_DOM_NS_HTML);
                    if (status == LXA_OK)
                        status = enter_text_mode(tree, tokenizer, token,
                            LXA_HTML_RCDATA, child);
                    return status;
                }
                if (token->kind == LXA_HTML_START_TAG
                    && named(token->name, "noframes")) {
                    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    status = template_foster_element(tree, token, false,
                                                      &child, LXA_DOM_NS_HTML);
                    if (status == LXA_OK)
                        status = enter_text_mode(tree, tokenizer, token,
                                                 LXA_HTML_RAWTEXT, child);
                    return status;
                }
                if (token->kind == LXA_HTML_START_TAG
                    && named(top, "colgroup")
                    && (named(token->name, "style")
                        || named(token->name, "script"))) {
                    /* Column-group mode closes before reprocessing these
                     * through the head-compatible text-mode rule. */
                    status = template_pop_elements(tree, last);
                    if (status != LXA_OK) return status;
                }
                if (token->kind == LXA_HTML_START_TAG
                    && named(token->name, "form")) {
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    /* A template in scope suppresses in-table forms. */
                    return LXA_OK;
                }
                if (token->kind == LXA_HTML_START_TAG
                    && named(token->name, "input")) {
                    bool hidden, in_colgroup = named(top, "colgroup");
                    status = hidden_input(tree, token, &hidden);
                    if (status != LXA_OK) return status;
                    if (hidden) {
                        status = lxa_budget_check(tree->nodes->budget,
                            LXA_BUDGET_NODES, 1,
                            &tree->nodes->limit_reason);
                        if (status != LXA_OK) return status;
                        /* In colgroup, input closes the group before its
                         * in-table rule inserts the hidden element. */
                        if (in_colgroup) {
                            status = template_pop_elements(tree, last);
                            if (status != LXA_OK) return status;
                        }
                        return append_void(tree, template_parent(tree), token);
                    }
                }
                if (token->kind == LXA_HTML_START_TAG
                    && (named(token->name, "textarea")
                        || named(token->name, "iframe")
                        || named(token->name, "noembed")
                        || named(token->name, "xmp")
                        || named(token->name, "plaintext"))) {
                    bool textarea = named(token->name, "textarea");
                    bool plaintext = named(token->name, "plaintext");
                    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    status = template_foster_element(tree, token, false,
                                                      &child, LXA_DOM_NS_HTML);
                    if (status == LXA_OK)
                        status = enter_text_mode(tree, tokenizer, token,
                            textarea ? LXA_HTML_RCDATA
                            : plaintext ? LXA_HTML_PLAINTEXT
                            : LXA_HTML_RAWTEXT,
                            child);
                    if (status == LXA_OK && textarea)
                        tree->skip_pre_newline = true;
                    return status;
                }
                if (token->kind == LXA_HTML_START_TAG
                    && (body_void(token->name)
                        || named(token->name, "image"))) {
                    bool in_colgroup = named(top, "colgroup");
                    if (in_colgroup) {
                        status = lxa_budget_check(tree->nodes->budget,
                            LXA_BUDGET_NODES, 1,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_budget_check(tree->nodes->budget,
                                LXA_BUDGET_WORK, 2,
                                &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    if (named(token->name, "image")) {
                        adjusted = *token;
                        adjusted.name = literal("img");
                        token = &adjusted;
                    }
                    status = template_foster_element(tree, token, false, NULL,
                                                     LXA_DOM_NS_HTML);
                    return status;
                }
                if (token->kind == LXA_HTML_START_TAG
                    && (template_formatting(token->name)
                        || inline_container(token->name)
                        || named(token->name, "object")
                        || named(token->name, "marquee")
                        || named(token->name, "applet")
                        || template_crossed_inline(token->name)
                        || named(token->name, "button")
                        || named(token->name, "p")
                        || named(token->name, "div")
                        || named(token->name, "section")
                        || named(token->name, "dialog")
                        || named(token->name, "details")
                        || named(token->name, "summary")
                        || named(token->name, "fieldset")
                        || named(token->name, "legend")
                        || named(token->name, "blockquote")
                        || named(token->name, "center")
                        || named(token->name, "address")
                        || named(token->name, "figure")
                        || named(token->name, "figcaption")
                        || named(token->name, "picture")
                        || named(token->name, "video")
                        || named(token->name, "audio")
                        || named(token->name, "canvas")
                        || named(token->name, "map")
                        || named(token->name, "datalist")
                        || named(token->name, "output")
                        || named(token->name, "progress")
                        || named(token->name, "meter")
                        || named(token->name, "hgroup")
                        || named(token->name, "search")
                        || named(token->name, "dir")
                        || named(token->name, "noscript")
                        || named(token->name, "dl")
                        || named(token->name, "dt")
                        || named(token->name, "dd")
                        || named(token->name, "article")
                        || named(token->name, "main")
                        || named(token->name, "aside")
                        || named(token->name, "header")
                        || named(token->name, "footer")
                        || named(token->name, "nav")
                        || named(token->name, "ul")
                        || named(token->name, "ol")
                        || named(token->name, "menu")
                        || heading(token->name)
                        || named(token->name, "pre")
                        || named(token->name, "listing")
                        || named(token->name, "select")
                        || named(token->name, "option")
                        || named(token->name, "optgroup")
                        || named(token->name, "rt")
                        || named(token->name, "rp")
                        || named(token->name, "li"))) {
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    status = template_foster_element(tree, token, true, NULL,
                                                     LXA_DOM_NS_HTML);
                    if (status == LXA_OK
                        && (named(token->name, "pre")
                            || named(token->name, "listing")))
                        tree->skip_pre_newline = true;
                    return status;
                }
                if (token->kind == LXA_HTML_START_TAG
                    && (table_section(token->name)
                        || named(token->name, "caption")
                        || named(token->name, "colgroup")
                        || named(token->name, "tr")
                        || named(token->name, "td")
                        || named(token->name, "th")))
                    return template_open_table_part(tree, token);
                if (token->kind == LXA_HTML_START_TAG
                    && named(token->name, "table")) {
                    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                        tree->template_elements.bytes.data;
                    size_t i = last + 1;
                    bool found = false;
                    while (i > template_boundary(tree)) {
                        lxa_span_t name;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[--i], &name);
                        if (status != LXA_OK) return status;
                        if (named(name, "table")) {
                            status = template_pop_elements(tree, i);
                            if (status != LXA_OK) return status;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        return named(top, "tr") || table_section(top)
                            ? LXA_OK : LXA_ERROR_UNSUPPORTED;
                    /* Reprocess as an ordinary table start in the fragment. */
                }
                if (token->kind == LXA_HTML_START_TAG
                    && !named(token->name, "template")
                    && !named(token->name, "table")
                    && !named(token->name, "style")
                    && !named(token->name, "script")) {
                    bool ordinary;
                    status = supported_container(tree, token->name,
                                                  &ordinary);
                    if (status != LXA_OK) return status;
                    if (!ordinary) return LXA_ERROR_UNSUPPORTED;
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    return template_foster_element(tree, token, true, NULL,
                                                   LXA_DOM_NS_HTML);
                }
                if (token->kind == LXA_HTML_END_TAG
                    && named(token->name, "form")) {
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    return LXA_OK;
                }
                if (token->kind == LXA_HTML_END_TAG
                    && named(token->name, "p")) {
                    /* With no paragraph in scope, the in-body rule inserts
                     * an empty one at the foster location. */
                    adjusted = *token;
                    adjusted.kind = LXA_HTML_START_TAG;
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    return template_foster_element(tree, &adjusted, false,
                                                   NULL, LXA_DOM_NS_HTML);
                }
                if (token->kind == LXA_HTML_END_TAG
                    && named(token->name, "select"))
                    return LXA_OK; /* Input may have closed the select already. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "body")
                        || named(token->name, "html")))
                    return LXA_OK; /* The template cannot close either root. */
                if (token->kind == LXA_HTML_END_TAG
                    && heading(token->name))
                    return LXA_OK; /* A heading outside the table is out of scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "li")
                        || named(token->name, "dt")
                        || named(token->name, "dd")))
                    return LXA_OK; /* The table ends list-item scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "option")
                        || named(token->name, "optgroup")))
                    return LXA_OK; /* No select is open in this table scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "ruby")
                        || named(token->name, "rt")
                        || named(token->name, "rp")))
                    return LXA_OK; /* An annotation outside the table is out of scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && block_container(token->name))
                    return LXA_OK; /* The table ends ordinary block scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "col")
                        || named(token->name, "td")
                        || named(token->name, "th")))
                    return LXA_OK; /* No cell or column is open in this mode. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "caption")
                        || named(token->name, "frameset"))) {
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    return LXA_OK; /* No match in this table scope. */
                }
                if (token->kind == LXA_HTML_END_TAG
                    && !named(token->name, "table")
                    && !named(token->name, "colgroup")
                    && !table_section(token->name)
                    && !named(token->name, "tr"))
                    return LXA_ERROR_UNSUPPORTED;
            }
            else if (ns == LXA_DOM_NS_HTML
                     && (named(top, "caption")
                      || named(top, "td") || named(top, "th"))
                     && token->kind == LXA_HTML_START_TAG
                     && (table_section(token->name)
                         || named(token->name, "caption")
                         || named(token->name, "tr")
                         || named(token->name, "td")
                         || named(token->name, "th")))
                return template_open_table_part(tree, token);
        }
        if ((token->kind == LXA_HTML_START_TAG
             && table_part(token->name))
            || (token->kind == LXA_HTML_END_TAG
                && table_part(token->name)
                && !named(token->name, "col")
                && !named(token->name, "colgroup"))) {
            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                tree->template_elements.bytes.data;
            size_t select, i;
            bool option, group, table_scope_found = false;
            bool target_found = token->kind == LXA_HTML_START_TAG;
            status = template_select_scope(tree, &select, &option, &group);
            if (status != LXA_OK) return status;
            if (select != lxa_vector_length(&tree->template_elements)) {
                if (token->kind == LXA_HTML_START_TAG
                    && (named(token->name, "col")
                        || named(token->name, "colgroup")))
                    return LXA_OK; /* Neither start exits select mode. */
                i = select;
                while (i > template_boundary(tree)) {
                    lxa_span_t name;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[--i], &name);
                    if (status != LXA_OK) return status;
                    if (name.length == token->name.length
                        && memcmp(name.data, token->name.data,
                                  name.length) == 0)
                        target_found = true;
                    if (i == template_boundary(tree)
                        && (table_section(name) || named(name, "tr")
                            || named(name, "td") || named(name, "th")
                            || (named(name, "caption")
                                && named(token->name, "table"))))
                        table_scope_found = true;
                    if (named(name, "table")) {
                        table_scope_found = true;
                        break;
                    }
                }
                if (!table_scope_found || !target_found) return LXA_OK;
                /* Table tokens close the select in table scope before their
                 * table rule sees the open stack. */
                status = template_pop_elements(tree, select);
                if (status != LXA_OK) return status;
                goto reprocess_template_token;
            }
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "col"))
            return template_open_column(tree, token);
        if (token->kind == LXA_HTML_START_TAG
            || token->kind == LXA_HTML_END_TAG) {
            size_t select;
            bool option, group;
            size_t length = lxa_vector_length(&tree->template_elements);
            status = template_select_scope(tree, &select, &option, &group);
            if (status != LXA_OK) return status;
            if (select != length) {
                if (token->kind == LXA_HTML_END_TAG) {
                    if (named(token->name, "select")) {
                        status = template_pop_elements(tree, select);
                        if (status != LXA_OK) return status;
                    }
                    else if (named(token->name, "option") && option) {
                        status = template_pop_elements(tree, length - 1);
                        if (status != LXA_OK) return status;
                    }
                    else if (named(token->name, "optgroup")) {
                        if (group) {
                            status = template_pop_elements(tree, length - 1);
                            if (status != LXA_OK) return status;
                        }
                        else if (option && length > select + 1) {
                            lxa_span_t previous;
                            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                                tree->template_elements.bytes.data;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(
                                    tree->nodes, open[length - 2], &previous);
                            if (status != LXA_OK) return status;
                            if (named(previous, "optgroup")) {
                                status = template_pop_elements(tree, length - 2);
                                if (status != LXA_OK) return status;
                            }
                        }
                    }
                    return LXA_OK;
                }
                if (named(token->name, "select")) {
                    status = template_pop_elements(tree, select);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                if (named(token->name, "option")
                    || named(token->name, "optgroup")) {
                    template_select_close = length;
                    if (option) --template_select_close;
                    if (named(token->name, "optgroup")
                        && template_select_close > select) {
                        lxa_span_t previous;
                        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                            tree->template_elements.bytes.data;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[template_select_close - 1], &previous);
                        if (status != LXA_OK) return status;
                        if (named(previous, "optgroup"))
                            --template_select_close;
                    }
                }
                else if (named(token->name, "hr")) {
                    size_t keep = length;
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                    if (option) --keep;
                    if (keep > select + 1) {
                        lxa_span_t previous;
                        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                            tree->template_elements.bytes.data;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[keep - 1], &previous);
                        if (status != LXA_OK) return status;
                        if (named(previous, "optgroup")) --keep;
                    }
                    status = template_pop_elements(tree, keep);
                    if (status != LXA_OK) return status;
                    return append_void(tree, template_parent(tree), token);
                }
                else if (named(token->name, "input")
                         || named(token->name, "textarea")
                         || named(token->name, "keygen")) {
                    if (named(token->name, "textarea")
                        && tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                    status = template_pop_elements(tree, select);
                    if (status != LXA_OK) return status;
                    /* Reprocess against the underlying table or fragment. */
                    goto reprocess_template_token;
                }
                else if (named(token->name, "html"))
                    return apply_attributes(tree, tree->html, token);
                else if (!named(token->name, "script")
                         && !named(token->name, "template"))
                    return LXA_OK; /* Ordinary starts in select are ignored. */
            }
        }
        if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "caption")
                || named(token->name, "colgroup")
                || table_section(token->name)
                || named(token->name, "tr")
                || named(token->name, "td")
                || named(token->name, "th")))
            return template_open_table_part(tree, token);
        if (token->kind == LXA_HTML_END_TAG
            && table_section(token->name)
            && (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                || tree->template_insertion_mode == LXA_TEMPLATE_BODY)) {
            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                tree->template_elements.bytes.data;
            size_t boundary = template_boundary(tree);
            size_t length = lxa_vector_length(&tree->template_elements);
            if (length >= boundary + 2) {
                lxa_span_t section_name, row_name;
                size_t section = boundary, j = length;
                while (j > boundary) {
                    lxa_span_t name;
                    lxa_dom_namespace_t ns;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[--j], &name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_namespace(tree->nodes,
                            open[j], &ns);
                    if (status != LXA_OK) return status;
                    if (ns == LXA_DOM_NS_HTML && named(name, "table")) {
                        section = j + 1;
                        break;
                    }
                }
                if (length >= section + 2) {
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2,
                        &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[section], &section_name);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[section + 1], &row_name);
                    if (status != LXA_OK) return status;
                    if (table_section(section_name)
                        && (section_name.length != token->name.length
                            || memcmp(section_name.data, token->name.data,
                                      section_name.length) != 0)
                        && named(row_name, "tr")) {
                        for (j = section + 2; j < length; ++j) {
                            lxa_span_t name;
                            lxa_dom_namespace_t ns;
                            bool clearable;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 2,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(
                                    tree->nodes, open[j], &name);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_namespace(
                                    tree->nodes, open[j], &ns);
                            if (status != LXA_OK) return status;
                            if (named(name, "td") || named(name, "th"))
                                return LXA_OK; /* End inside a cell is ignored. */
                            status = template_fostered_descendant(name, ns, &clearable);
                            if (status != LXA_OK) return status;
                            if (!clearable) return LXA_ERROR_UNSUPPORTED;
                        }
                        status = template_pop_elements(tree, section + 1);
                        if (status != LXA_OK) return status;
                        return LXA_OK;
                    }
                    if (table_section(section_name)
                        && section_name.length == token->name.length
                        && memcmp(section_name.data, token->name.data,
                                  section_name.length) == 0
                        && template_formatting(row_name)
                        && tree->pending_template_formatting.handle
                           == LXA_HANDLE_NONE)
                        return template_retain_formatting_chain(tree,
                            section + 1, length - section - 1, section);
                }
            }
        }
        if (token->kind == LXA_HTML_END_TAG
            && (named(token->name, "table")
                || (tree->template_insertion_mode
                    == LXA_TEMPLATE_TABLE_BODY
                    && table_section(token->name)))
            && (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
                || tree->template_insertion_mode
                   == LXA_TEMPLATE_TABLE_BODY)) {
            const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                tree->template_elements.bytes.data;
            size_t j = lxa_vector_length(&tree->template_elements);
            lxa_span_t root = {0};
            lxa_dom_namespace_t root_ns = LXA_DOM_NS_HTML;
            while (j > template_boundary(tree)) {
                lxa_span_t name;
                lxa_dom_namespace_t ns;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[--j], &name);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_namespace(tree->nodes,
                        open[j], &ns);
                if (status != LXA_OK) return status;
                if (ns == LXA_DOM_NS_HTML
                    && (named(name, "table") || named(name, "td")
                        || named(name, "th")))
                    break;
                if (j == template_boundary(tree)) {
                    root = name;
                    root_ns = ns;
                }
            }
            if (j == template_boundary(tree) && root.data != NULL
                && root_ns == LXA_DOM_NS_HTML
                && (named(root, "tr") || table_section(root))) {
                status = template_pop_elements(tree, j);
                if (status != LXA_OK) return status;
                return LXA_OK;
            }
        }
        if (token->kind == LXA_HTML_END_TAG) {
            size_t i = lxa_vector_length(&tree->template_elements);
            bool formatting_above = false;
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            size_t extra_start = template_pending_extra_start(tree);
            size_t extra_count =
                lxa_vector_length(&tree->pending_template_extras)
                - extra_start;
            if (extra_count != 0 && template_formatting(token->name)) {
                lxa_span_t pending_name;
                bool pending_open;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        tree->pending_template_formatting, &pending_name);
                if (status != LXA_OK) return status;
                if (pending_name.length != token->name.length
                    || memcmp(pending_name.data, token->name.data,
                              pending_name.length) != 0) {
                    const lxa_dom_ref_t *extras = (const lxa_dom_ref_t *)
                        tree->pending_template_extras.bytes.data + extra_start;
                    size_t boundary = template_boundary(tree), j;
                    size_t matched = SIZE_MAX;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, extra_count,
                        &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                    for (j = 0; j < extra_count; ++j) {
                        lxa_span_t name;
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            extras[j], &name);
                        if (status != LXA_OK) return status;
                        if (name.length == token->name.length
                            && memcmp(name.data, token->name.data,
                                      name.length) == 0)
                            matched = j;
                    }
                    if (matched != SIZE_MAX) {
                        if (i != boundary) {
                            size_t chain_start = boundary;
                            if (matched == 0) {
                                while (chain_start < i
                                    && open[chain_start].handle
                                       != extras[0].handle) {
                                    lxa_span_t parent_name;
                                    status = lxa_budget_charge(
                                        tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
                                    if (status == LXA_OK)
                                        status = lxa_dom_nodes_element_name(
                                            tree->nodes, open[chain_start],
                                            &parent_name);
                                    if (status != LXA_OK) return status;
                                    if (!template_pending_chain_block(
                                        parent_name))
                                        return LXA_ERROR_UNSUPPORTED;
                                    ++chain_start;
                                }
                            }
                            if (i - chain_start < extra_count + 1
                                || open[chain_start + extra_count].handle
                                   != tree->pending_template_formatting.handle)
                                return LXA_ERROR_UNSUPPORTED;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, i - chain_start - 1,
                                &tree->nodes->limit_reason);
                            if (status != LXA_OK) return status;
                            for (j = 0; j < extra_count; ++j)
                                if (open[chain_start + j].handle
                                    != extras[j].handle)
                                    return LXA_ERROR_UNSUPPORTED;
                            for (j = chain_start + extra_count + 1;
                                 j < i; ++j) {
                                lxa_span_t name;
                                status = lxa_dom_nodes_element_name(
                                    tree->nodes, open[j], &name);
                                if (status != LXA_OK) return status;
                                if (!named(name, "span"))
                                    return LXA_ERROR_UNSUPPORTED;
                            }
                            status = template_pop_elements(tree, chain_start + matched);
                            if (status != LXA_OK) return status;
                        }
                        lxa_vector_erase(&tree->pending_template_extras,
                                         extra_start + matched, 1);
                        lxa_budget_release(tree->nodes->budget,
                            LXA_BUDGET_ACTIVE_FORMATTING, 1);
                        return LXA_OK;
                    }
                }
                if (pending_name.length != token->name.length
                    || memcmp(pending_name.data, token->name.data,
                              pending_name.length) != 0)
                    return LXA_ERROR_UNSUPPORTED;
                status = template_pending_on_stack(tree, &pending_open);
                if (status != LXA_OK) return status;
                if (pending_open && !template_pending_at_top(tree))
                    return LXA_ERROR_UNSUPPORTED;
                if (pending_open) {
                    status = template_pop_elements(tree, i - 1);
                    if (status != LXA_OK) return status;
                }
                forget_pending_template_formatting(tree);
                tree->pending_template_formatting =
                    ((const lxa_dom_ref_t *)
                     tree->pending_template_extras.bytes.data)
                    [extra_start + extra_count - 1];
                lxa_vector_erase(&tree->pending_template_extras,
                    extra_start + extra_count - 1, 1);
                return LXA_OK;
            }
            if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
                && i > template_boundary(tree)) {
                lxa_dom_namespace_t ns;
                lxa_span_t name, target = token->name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_namespace(tree->nodes,
                        open[i - 1], &ns);
                if (status != LXA_OK) return status;
                if (ns != LXA_DOM_NS_HTML) {
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[i - 1], &name);
                    if (status == LXA_OK && ns == LXA_DOM_NS_SVG)
                        status = lxa_html_svg_tag_name(target, &target);
                    if (status != LXA_OK) return status;
                    if (name.length != target.length
                        || memcmp(name.data, target.data, name.length) != 0)
                        return LXA_ERROR_UNSUPPORTED;
                    status = template_pop_elements(tree, i - 1);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
            }
            if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
                && !(pending_table_context
                     && (named(token->name, "colgroup")
                         || named(token->name, "table")))
                && !pending_cell_end) {
                lxa_span_t pending_name;
                bool found = false;
                status = lxa_dom_nodes_element_name(tree->nodes,
                    tree->pending_template_formatting, &pending_name);
                if (status != LXA_OK) return status;
                if (pending_name.length == token->name.length
                    && memcmp(pending_name.data, token->name.data,
                              pending_name.length) == 0) {
                    for (size_t j = template_boundary(tree); j < i; ++j) {
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                        if (status != LXA_OK) return status;
                        if (open[j].handle
                            == tree->pending_template_formatting.handle) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        forget_pending_template_formatting(tree);
                        return LXA_OK;
                    }
                    if (i > template_boundary(tree) + 1
                        && open[template_boundary(tree)].handle
                           == tree->pending_template_formatting.handle) {
                        size_t ancestor;
                        status = template_pending_span_chain(tree, i,
                                                             &ancestor);
                        if (status != LXA_OK) return status;
                        if (ancestor == template_boundary(tree)) {
                            status = template_pop_elements(tree, ancestor);
                            if (status != LXA_OK) return status;
                            forget_pending_template_formatting(tree);
                            return LXA_OK;
                        }
                    }
                }
                else if (named(token->name, "p")
                         || named(token->name, "div")
                         || named(token->name, "section")
                         || named(token->name, "article")
                         || named(token->name, "blockquote")
                         || named(token->name, "address")
                         || named(token->name, "figure")
                         || named(token->name, "pre")
                         || named(token->name, "listing")
                         || heading(token->name)) {
                    size_t boundary = template_boundary(tree);
                    lxa_span_t block_name;
                    if (template_pending_chain_block(token->name)) {
                        size_t end = i;
                        bool block_chain, list_block;
                        if (template_pending_at_top(tree)) --end;
                        status = template_pending_block_chain(tree, end,
                                                              &block_chain);
                        if (status != LXA_OK) return status;
                        status = template_pending_list_block_tail(tree, end,
                                                                  &list_block);
                        if (status != LXA_OK) return status;
                        if (block_chain || list_block) {
                            lxa_span_t top_name;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(
                                    tree->nodes, open[end - 1], &top_name);
                            if (status != LXA_OK) return status;
                            if (top_name.length == token->name.length
                                && memcmp(top_name.data, token->name.data,
                                          top_name.length) == 0) {
                                status = template_pop_elements(tree, end - 1);
                                if (status != LXA_OK) return status;
                                return LXA_OK;
                            }
                        }
                    }
                    if (template_pending_chain_block(token->name)
                        || named(token->name, "p")
                        || template_pending_direct_block(token->name)) {
                        size_t start = template_pending_extra_start(tree);
                        size_t extras =
                            lxa_vector_length(&tree->pending_template_extras)
                            - start;
                        const lxa_dom_ref_t *entries =
                            (const lxa_dom_ref_t *)
                            tree->pending_template_extras.bytes.data;
                        size_t j, block_end, matched = SIZE_MAX;
                        if (extras != 0 && i - boundary > extras
                            && open[i - 1].handle
                               == tree->pending_template_formatting.handle) {
                            block_end = i - extras - 1;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, i - boundary - 1,
                                &tree->nodes->limit_reason);
                            if (status != LXA_OK) return status;
                            for (j = 0; j < extras; ++j)
                                if (open[block_end + j].handle
                                    != entries[start + j].handle)
                                    break;
                            if (j == extras) {
                                for (j = boundary; j < block_end; ++j) {
                                    status = lxa_dom_nodes_element_name(
                                        tree->nodes, open[j], &block_name);
                                    if (status != LXA_OK) return status;
                                    if (!template_pending_chain_block(
                                            block_name)
                                        && !(j == boundary
                                             && block_end == boundary + 1
                                             && (named(block_name, "p")
                                                 || template_pending_direct_block(
                                                     block_name))))
                                        break;
                                    if ((block_name.length
                                         == token->name.length
                                         && memcmp(block_name.data,
                                                   token->name.data,
                                                   block_name.length) == 0)
                                        || (heading(block_name)
                                            && heading(token->name)))
                                        matched = j;
                                }
                                if (j == block_end && matched != SIZE_MAX) {
                                    status = template_pop_elements(tree, matched);
                                    if (status != LXA_OK) return status;
                                    return LXA_OK;
                                }
                            }
                        }
                    }
                    if (i > boundary + 2) {
                        size_t ancestor;
                        status = template_pending_span_chain(tree, i - 1,
                                                             &ancestor);
                        if (status != LXA_OK) return status;
                        if (ancestor != SIZE_MAX) {
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(
                                    tree->nodes, open[i - 1], &block_name);
                            if (status != LXA_OK) return status;
                            if (block_name.length != token->name.length
                                || memcmp(block_name.data, token->name.data,
                                          block_name.length) != 0)
                                return LXA_ERROR_UNSUPPORTED;
                            status = template_pop_elements(tree, i - 1);
                            if (status != LXA_OK) return status;
                            return LXA_OK;
                        }
                    }
                    if (i == boundary + 2
                        && open[boundary].handle
                           == tree->pending_template_formatting.handle) {
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 1], &block_name);
                        if (status != LXA_OK) return status;
                        if (block_name.length != token->name.length
                            || memcmp(block_name.data, token->name.data,
                                      block_name.length) != 0)
                            return LXA_ERROR_UNSUPPORTED;
                        status = template_pop_elements(tree, i - 1);
                        if (status != LXA_OK) return status;
                        return LXA_OK;
                    }
                    if (i != boundary + 1
                        && (i != boundary + 2
                            || open[i - 1].handle
                               != tree->pending_template_formatting.handle))
                        return LXA_ERROR_UNSUPPORTED;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, i - boundary,
                        &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[boundary], &block_name);
                    if (status != LXA_OK) return status;
                    if (block_name.length != token->name.length
                        || memcmp(block_name.data, token->name.data,
                                  block_name.length) != 0)
                        return LXA_ERROR_UNSUPPORTED;
                    status = template_pop_elements(tree, boundary);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                else if (named(token->name, "ul")
                         || named(token->name, "ol")) {
                    if (i > template_boundary(tree) + 2
                        && open[i - 3].handle
                           == tree->pending_template_formatting.handle) {
                        lxa_span_t list_name, item_name;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 2,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 2], &list_name);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 1], &item_name);
                        if (status != LXA_OK) return status;
                        if (list_name.length == token->name.length
                            && memcmp(list_name.data, token->name.data,
                                      list_name.length) == 0
                            && named(item_name, "li")) {
                            status = template_pop_elements(tree, i - 2);
                            if (status != LXA_OK) return status;
                            return LXA_OK;
                        }
                    }
                    if (i > template_boundary(tree) + 1
                        && open[i - 2].handle
                           == tree->pending_template_formatting.handle) {
                        lxa_span_t top_name;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 1], &top_name);
                        if (status != LXA_OK) return status;
                        if (top_name.length == token->name.length
                            && memcmp(top_name.data, token->name.data,
                                      top_name.length) == 0) {
                            status = template_pop_elements(tree, i - 1);
                            if (status != LXA_OK) return status;
                            return LXA_OK;
                        }
                    }
                    size_t start = template_pending_extra_start(tree);
                    size_t extras =
                        lxa_vector_length(&tree->pending_template_extras)
                        - start;
                    size_t list;
                    lxa_span_t list_name, item_name;
                    status = template_pending_list_index(tree, &list);
                    if (status != LXA_OK) return status;
                    if (extras != 0 && list != SIZE_MAX
                        && i - list >= extras + 3
                        && i - list <= extras + 5
                        && open[list + extras + 2].handle
                           == tree->pending_template_formatting.handle) {
                        const lxa_dom_ref_t *entries = (const lxa_dom_ref_t *)
                            tree->pending_template_extras.bytes.data;
                        size_t j, suffix = i - list - extras - 3;
                        bool nested = true;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, i - list - 1,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[list], &list_name);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[list + 1], &item_name);
                        if (status != LXA_OK) return status;
                        if (suffix != 0) {
                            lxa_span_t nested_name;
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[list + extras + 3], &nested_name);
                            if (status != LXA_OK) return status;
                            nested = named(nested_name, "ul")
                                     || named(nested_name, "ol");
                            if (suffix == 2) {
                                status = lxa_dom_nodes_element_name(tree->nodes,
                                    open[i - 1], &nested_name);
                                if (status != LXA_OK) return status;
                                nested = nested && named(nested_name, "li");
                            }
                        }
                        if (list_name.length == token->name.length
                            && memcmp(list_name.data, token->name.data,
                                      list_name.length) == 0
                            && named(item_name, "li") && nested) {
                            for (j = 0; j < extras; ++j)
                                if (open[list + 2 + j].handle
                                    != entries[start + j].handle)
                                    break;
                            if (j == extras) {
                                status = template_pop_elements(tree, list);
                                if (status != LXA_OK) return status;
                                return LXA_OK;
                            }
                        }
                    }
                    if (list == SIZE_MAX || i > list + 3
                        || (i == list + 3
                            && open[i - 1].handle
                               != tree->pending_template_formatting.handle))
                        return LXA_ERROR_UNSUPPORTED;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, i - list,
                        &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[list], &list_name);
                    if (status == LXA_OK && i > list + 1)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[list + 1], &item_name);
                    if (status != LXA_OK) return status;
                    if (list_name.length != token->name.length
                        || memcmp(list_name.data, token->name.data,
                                  list_name.length) != 0
                        || (i > list + 1 && !named(item_name, "li")))
                        return LXA_ERROR_UNSUPPORTED;
                    status = template_pop_elements(tree, list);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                else if (named(token->name, "li")) {
                    if (i > template_boundary(tree) + 2
                        && open[i - 3].handle
                           == tree->pending_template_formatting.handle) {
                        lxa_span_t list_name, item_name;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 2,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 2], &list_name);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 1], &item_name);
                        if (status != LXA_OK) return status;
                        if ((named(list_name, "ul")
                             || named(list_name, "ol"))
                            && named(item_name, "li")) {
                            status = template_pop_elements(tree, i - 1);
                            if (status != LXA_OK) return status;
                            return LXA_OK;
                        }
                    }
                    size_t start = template_pending_extra_start(tree);
                    size_t extras =
                        lxa_vector_length(&tree->pending_template_extras)
                        - start;
                    size_t list;
                    lxa_span_t item_name;
                    status = template_pending_list_index(tree, &list);
                    if (status != LXA_OK) return status;
                    if (extras != 0 && list != SIZE_MAX
                        && i - list == extras + 3
                        && open[i - 1].handle
                           == tree->pending_template_formatting.handle) {
                        const lxa_dom_ref_t *entries = (const lxa_dom_ref_t *)
                            tree->pending_template_extras.bytes.data;
                        size_t j;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, extras + 1,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[list + 1], &item_name);
                        if (status != LXA_OK) return status;
                        if (named(item_name, "li")) {
                            for (j = 0; j < extras; ++j)
                                if (open[list + 2 + j].handle
                                    != entries[start + j].handle)
                                    break;
                            if (j == extras) {
                                status = template_pop_elements(tree, list + 1);
                                if (status != LXA_OK) return status;
                                return LXA_OK;
                            }
                        }
                    }
                    if (list == SIZE_MAX
                        || (i != list + 2
                            && (i != list + 3
                            || open[i - 1].handle
                               != tree->pending_template_formatting.handle)))
                        return LXA_ERROR_UNSUPPORTED;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, i - list - 1,
                        &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[list + 1], &item_name);
                    if (status != LXA_OK) return status;
                    if (!named(item_name, "li"))
                        return LXA_ERROR_UNSUPPORTED;
                    status = template_pop_elements(tree, list + 1);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                else if (named(token->name, "span")) {
                    size_t ancestor;
                    status = template_pending_span_chain(tree, i, &ancestor);
                    if (status != LXA_OK) return status;
                    if (ancestor == SIZE_MAX) return LXA_ERROR_UNSUPPORTED;
                    if (i == ancestor + 1) return LXA_OK;
                    status = template_pop_elements(tree, i - 1);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                else if (template_pending_marker(token->name)) {
                    lxa_span_t top_name;
                    if (i != template_boundary(tree) + 2
                        || open[i - 2].handle
                           != tree->pending_template_formatting.handle)
                        return LXA_ERROR_UNSUPPORTED;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[i - 1], &top_name);
                    if (status != LXA_OK) return status;
                    if (top_name.length != token->name.length
                        || memcmp(top_name.data, token->name.data,
                                  top_name.length) != 0)
                        return LXA_ERROR_UNSUPPORTED;
                    status = template_pop_elements(tree, i - 1);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                else
                    return LXA_ERROR_UNSUPPORTED; /* Other ends need list rules. */
            }
            while (i > template_boundary(tree)) {
                lxa_span_t name, target = token->name;
                lxa_dom_namespace_t ns;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = lxa_dom_nodes_element_name(tree->nodes, open[--i],
                                                     &name);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_namespace(tree->nodes,
                        open[i], &ns);
                if (status == LXA_OK && ns == LXA_DOM_NS_SVG)
                    status = lxa_html_svg_tag_name(target, &target);
                if (status != LXA_OK) return status;
                if ((name.length == target.length
                     && memcmp(name.data, target.data, name.length) == 0)
                    || (ns == LXA_DOM_NS_HTML
                        && heading(token->name) && heading(name))) {
                    size_t count = lxa_vector_length(&tree->template_elements)
                                   - i;
                    if (count >= 2 && template_formatting(name)
                        && tree->pending_template_formatting.handle
                           == LXA_HANDLE_NONE
                        && i >= template_boundary(tree) + 2) {
                        lxa_span_t list_name, item_name;
                        lxa_dom_ref_t parent;
                        size_t j, formatting_count = 0;
                        bool direct_adoption = false;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 2,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 2], &list_name);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 1], &item_name);
                        if (status != LXA_OK) return status;
                        if (count >= 3 && count <= 7
                            && (named(list_name, "ul")
                                || named(list_name, "ol"))
                            && named(item_name, "li")) {
                            lxa_span_t first;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(
                                    tree->nodes, open[i + 1], &first);
                            if (status != LXA_OK) return status;
                            direct_adoption = template_adoption_block(first)
                                && !named(first, "menu")
                                && !named(first, "ul")
                                && !named(first, "ol");
                        }
                        if ((named(list_name, "ul")
                             || named(list_name, "ol"))
                            && named(item_name, "li") && !direct_adoption) {
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, count + 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_parent(tree->nodes,
                                    open[i - 1], &parent);
                            if (status != LXA_OK) return status;
                            if (parent.handle != open[i - 2].handle)
                                return LXA_ERROR_UNSUPPORTED;
                            status = lxa_dom_nodes_parent(tree->nodes,
                                open[i], &parent);
                            if (status != LXA_OK) return status;
                            if (parent.handle != open[i - 1].handle)
                                return LXA_ERROR_UNSUPPORTED;
                            for (j = 1; j < count; ++j) {
                                lxa_span_t descendant;
                                status = lxa_dom_nodes_element_name(
                                    tree->nodes, open[i + j], &descendant);
                                if (status != LXA_OK) return status;
                                if (count == 2 && j == 1
                                    && template_adoption_block(descendant))
                                    return template_close_crossed_block(tree,
                                                                        i,
                                                                        NULL,
                                                                        0);
                                if (template_formatting(descendant))
                                    ++formatting_count;
                                else if (formatting_count == 0
                                         || !template_crossed_inline(
                                             descendant))
                                    return LXA_ERROR_UNSUPPORTED;
                                status = lxa_dom_nodes_parent(tree->nodes,
                                    open[i + j], &parent);
                                if (status != LXA_OK) return status;
                                if (parent.handle
                                    != open[i + j - 1].handle)
                                    return LXA_ERROR_UNSUPPORTED;
                            }
                            return template_retain_formatting_chain(tree,
                                i + 1, count - 1, i);
                        }
                    }
                    if (template_formatting(name)
                        && (((count >= 2)
                             && !formatting_above)
                            || (count >= 3 && formatting_above))) {
                        lxa_span_t top;
                        status = lxa_dom_nodes_element_name(tree->nodes,
                                                             open[i + 1], &top);
                        if (status != LXA_OK) return status;
                        if (count == 2
                            && (named(top, "dialog")
                                || named(top, "search"))) {
                            status = template_pop_elements(tree, i);
                            if (status != LXA_OK) return status;
                            return LXA_OK;
                        }
                        if (template_adoption_block(top)
                            && (count == 2
                                || (count <= 7 && !named(top, "menu")
                                    && !named(top, "ul")
                                    && !named(top, "ol")))) {
                            size_t reconstruct_count = 0, j;
                            size_t formatting_offsets[5];
                            for (j = 2; j < count; ++j) {
                                lxa_dom_ref_t inline_parent;
                                status = lxa_budget_charge(tree->nodes->budget,
                                    LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
                                if (status == LXA_OK)
                                    status = lxa_dom_nodes_element_name(
                                        tree->nodes, open[i + j], &top);
                                if (status != LXA_OK) return status;
                                if (template_formatting(top))
                                    formatting_offsets[reconstruct_count++] = j;
                                else if (!template_crossed_inline(top))
                                    return LXA_ERROR_UNSUPPORTED;
                                status = lxa_dom_nodes_parent(tree->nodes,
                                    open[i + j], &inline_parent);
                                if (status != LXA_OK) return status;
                                if (inline_parent.handle
                                    != open[i + j - 1].handle)
                                    return LXA_ERROR_UNSUPPORTED;
                            }
                            return template_close_crossed_block(tree, i,
                                formatting_offsets, reconstruct_count);
                        }
                        if (named(top, "menu") || named(top, "ul")
                            || named(top, "ol")) {
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i + 2], &top);
                            if (status != LXA_OK) return status;
                            if (named(top, "li")) {
                                size_t spans = 0, block_spans = 0, j;
                                bool direct_block = false;
                                size_t list_formatting = 0;
                                size_t nested_blocks = 0, accounted_spans = 0;
                                size_t inline_gaps[4] = {0};
                                if (count >= 5) {
                                    lxa_span_t next;
                                    size_t preblock_formatting = 0;
                                    size_t k = 3;
                                    while (k < count) {
                                        status = lxa_budget_charge(
                                            tree->nodes->budget,
                                            LXA_BUDGET_WORK, 1,
                                            &tree->nodes->limit_reason);
                                        if (status == LXA_OK)
                                            status = lxa_dom_nodes_element_name(
                                                tree->nodes, open[i + k],
                                                &next);
                                        if (status != LXA_OK) return status;
                                        if (template_formatting(next))
                                            ++preblock_formatting;
                                        else if (preblock_formatting == 0
                                                 || !template_crossed_inline(
                                                     next))
                                            break;
                                        ++k;
                                    }
                                    if (preblock_formatting != 0 && k < count
                                        && template_crossed_list_furthest_block(
                                            next))
                                        return template_close_crossed_list_item(
                                            tree, i, 0, 0, true,
                                            count - k - 1, 0,
                                            inline_gaps, k - 3);
                                }
                                for (j = 3; j < count; ++j) {
                                    status = lxa_budget_charge(
                                        tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
                                    if (status == LXA_OK)
                                        status = lxa_dom_nodes_element_name(
                                            tree->nodes, open[i + j], &top);
                                    if (status != LXA_OK) return status;
                                    if (direct_block) {
                                        if (template_crossed_inline(top)
                                            && block_spans == j - 4 - spans
                                                              - nested_blocks)
                                            ++block_spans;
                                        else if (nested_blocks < 4
                                                 && j == 4 + spans + block_spans
                                                          + nested_blocks
                                                 && (named(top, "section")
                                                     || named(top, "div")
                                                     || named(top, "p"))) {
                                            inline_gaps[nested_blocks] =
                                                block_spans - accounted_spans;
                                            accounted_spans = block_spans;
                                            ++nested_blocks;
                                        }
                                        else if (!template_formatting(top))
                                            return LXA_ERROR_UNSUPPORTED;
                                        continue;
                                    }
                                    if (template_crossed_inline(top)
                                        && spans == j - 3) ++spans;
                                    else if (j == 3 + spans
                                             && (named(top, "div")
                                                 || named(top, "section")
                                                 || named(top, "p")))
                                        direct_block = true;
                                    else if (template_formatting(top))
                                        ++list_formatting;
                                    else if (!template_crossed_inline(top))
                                        return LXA_ERROR_UNSUPPORTED;
                                }
                                if (nested_blocks == 0 && formatting_above
                                    != (count > (direct_block
                                                 ? 4 + spans + block_spans
                                                 : 3 + spans)))
                                    return LXA_ERROR_UNSUPPORTED;
                                return template_close_crossed_list_item(
                                    tree, i, spans,
                                    direct_block
                                    ? count - 4 - spans - block_spans
                                      - nested_blocks : list_formatting,
                                    direct_block, block_spans, nested_blocks,
                                    inline_gaps, 0);
                            }
                        }
                    }
                    if (formatting_above
                        && (named(name, "ul") || named(name, "ol"))
                        && tree->template_insertion_mode == LXA_TEMPLATE_BODY
                        && tree->pending_template_formatting.handle
                           == LXA_HANDLE_NONE) {
                        lxa_dom_ref_t pending = {0};
                        size_t j, format_count = 0;
                        bool direct_chain = false;
                        for (j = i + 1; j < i + count; ++j) {
                            lxa_span_t descendant;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(tree->nodes,
                                    open[j], &descendant);
                            if (status != LXA_OK) return status;
                            if (template_formatting(descendant)) {
                                ++format_count;
                                pending = open[j];
                            }
                            else if (j == i + 1 && named(descendant, "li"))
                                direct_chain = true;
                            else
                                direct_chain = false;
                        }
                        if (pending.handle == LXA_HANDLE_NONE)
                            return LXA_ERROR_UNSUPPORTED;
                        if (format_count > 1 && !direct_chain)
                            return LXA_ERROR_UNSUPPORTED;
                        if (format_count > 1)
                            return template_retain_formatting_chain(tree,
                                i + 2, format_count, i);
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_ACTIVE_FORMATTING, 1,
                            &tree->nodes->limit_reason);
                        if (status != LXA_OK) return status;
                        tree->pending_template_formatting = pending;
                        status = template_pop_elements(tree, i);
                        if (status != LXA_OK) return status;
                        return LXA_OK;
                    }
                    if (formatting_above && named(name, "li")
                        && count >= 2 && i > template_boundary(tree)
                        && tree->template_insertion_mode == LXA_TEMPLATE_BODY
                        && tree->pending_template_formatting.handle
                           == LXA_HANDLE_NONE) {
                        lxa_span_t list_name;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 1], &list_name);
                        if (status != LXA_OK) return status;
                        if (named(list_name, "ul")
                            || named(list_name, "ol"))
                            return template_retain_formatting_chain(tree,
                                i + 1, count - 1, i);
                    }
                    if (formatting_above && named(name, "p")
                        && count >= 2 && i > template_boundary(tree)
                        && tree->pending_template_formatting.handle
                           == LXA_HANDLE_NONE) {
                        lxa_span_t parent_name;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1,
                            &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[i - 1], &parent_name);
                        if (status != LXA_OK) return status;
                        if (named(parent_name, "li")) {
                            status = template_retain_formatting_chain(tree,
                                i + 1, count - 1, i + 1);
                            if (status != LXA_OK) return status;
                            status = template_pop_elements(tree, i);
                            if (status != LXA_OK) return status;
                            return LXA_OK;
                        }
                    }
                    if (formatting_above
                        || (template_formatting(name) && count != 1))
                        return LXA_ERROR_UNSUPPORTED;
                    if (open[i].handle
                        == tree->pending_template_formatting.handle)
                        forget_pending_template_formatting(tree);
                    status = template_pop_elements(tree, i);
                    if (status != LXA_OK) return status;
                    break;
                }
                if (template_formatting(name))
                    formatting_above = true;
            }
            return LXA_OK; /* An unmatched end leaves the fragment open. */
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "template"))
            return begin_template(tree, token);
        if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "span")
                || template_pending_marker(token->name))
            && tree->pending_template_formatting.handle != LXA_HANDLE_NONE) {
            status = reconstruct_template_formatting(tree);
            if (status != LXA_OK) return status;
        }
        if (token->kind == LXA_HTML_TEXT) {
            size_t count = lxa_vector_length(&tree->template_elements);
            if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
                && count > template_boundary(tree)) {
                const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                    tree->template_elements.bytes.data;
                lxa_span_t top;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[count - 1], &top);
                if (status != LXA_OK) return status;
                if (named(top, "td") || named(top, "th")) {
                    size_t j = count - 1;
                    while (j > template_boundary(tree)) {
                        lxa_span_t ancestor;
                        status = lxa_budget_charge(tree->nodes->budget,
                            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                        if (status == LXA_OK)
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                open[--j], &ancestor);
                        if (status != LXA_OK) return status;
                        if (named(ancestor, "table"))
                            return insert_data_text(tree, template_parent(tree),
                                                    token->data, false, false);
                    }
                }
            }
            if (tree->template_fragment.handle == LXA_HANDLE_NONE)
                return LXA_ERROR_INTERNAL;
            status = reconstruct_template_formatting(tree);
            if (status != LXA_OK) return status;
            return insert_data_text(tree, template_parent(tree),
                                    token->data, false, false);
        }
        if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "base") || named(token->name, "link")
                || named(token->name, "meta")))
            return append_void(tree, template_parent(tree), token);
        if (token->kind == LXA_HTML_START_TAG
            && (body_void(token->name) || named(token->name, "image"))) {
            if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE
                && !named(token->name, "hr")) {
                status = reconstruct_template_formatting(tree);
                if (status != LXA_OK) return status;
            }
            if (named(token->name, "hr")) {
                size_t paragraph;
                bool pending_chain;
                status = template_open_paragraph(tree, &paragraph);
                if (status == LXA_ERROR_UNSUPPORTED) {
                    status = template_pending_paragraph_chain(tree, paragraph,
                                                              &pending_chain);
                    if (status != LXA_OK) return status;
                    if (!pending_chain) return LXA_ERROR_UNSUPPORTED;
                }
                if (status == LXA_OK)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                if (paragraph != lxa_vector_length(&tree->template_elements)) {
                    status = template_pop_elements(tree, paragraph);
                    if (status != LXA_OK) return status;
                }
            }
            /* The in-template fallback reprocesses these in body mode.
             * Their insertion parent is the template content fragment. */
            if (named(token->name, "image")) {
                adjusted = *token;
                adjusted.name = literal("img");
                token = &adjusted;
            }
            status = append_void(tree, template_parent(tree), token);
            if (status == LXA_OK
                && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL
                && lxa_vector_length(&tree->template_elements)
                   == template_boundary(tree))
                tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "title") || named(token->name, "style")
                || named(token->name, "script")
                || named(token->name, "noframes")
                || named(token->name, "iframe")
                || named(token->name, "noembed"))) {
            status = begin_text_element(tree, tokenizer, template_parent(tree),
                token, named(token->name, "title") ? LXA_HTML_RCDATA
                       : named(token->name, "script") ? LXA_HTML_SCRIPT_DATA
                       : LXA_HTML_RAWTEXT);
            if (status == LXA_OK
                && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL
                && (named(token->name, "iframe")
                    || named(token->name, "noembed")))
                tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "textarea")) {
            status = begin_text_element(tree, tokenizer, template_parent(tree),
                                        token, LXA_HTML_RCDATA);
            if (status == LXA_OK) {
                tree->skip_pre_newline = true;
                if (tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
                    tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            }
            return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "xmp")) {
            size_t paragraph;
            if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
            if (tree->pending_template_formatting.handle != LXA_HANDLE_NONE) {
                status = reconstruct_template_formatting(tree);
                if (status != LXA_OK) return status;
            }
            status = template_open_paragraph(tree, &paragraph);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (paragraph != lxa_vector_length(&tree->template_elements)) {
                status = template_pop_elements(tree, paragraph);
                if (status != LXA_OK) return status;
            }
            status = begin_text_element(tree, tokenizer, template_parent(tree),
                                        token, LXA_HTML_RAWTEXT);
            if (status == LXA_OK
                && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
                tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "plaintext")) {
            status = begin_text_element(tree, tokenizer, template_parent(tree),
                                        token, LXA_HTML_PLAINTEXT);
            if (status == LXA_OK
                && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
                tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            return status;
        }
        template_ordinary = false;
        template_cell_scope = false;
        template_table_scope = false;
        if (token->kind == LXA_HTML_START_TAG) {
            if (named(token->name, "form")
                || named(token->name, "object")) {
                const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                    tree->template_elements.bytes.data;
                size_t j = lxa_vector_length(&tree->template_elements);
                while (j > template_boundary(tree)) {
                    lxa_span_t ancestor;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[--j], &ancestor);
                    if (status != LXA_OK) return status;
                    if (named(ancestor, "td") || named(ancestor, "th")
                        || named(ancestor, "caption")) {
                        template_cell_scope = true;
                        break;
                    }
                    if (table_context(ancestor)) {
                        template_table_scope = true;
                        if (named(token->name, "object")) break;
                        return LXA_OK; /* Template suppresses in-table forms. */
                    }
                }
            }
            template_ordinary =
                named(token->name, "form")
                || (named(token->name, "object")
                    && (template_cell_scope || template_table_scope
                        || integration_object))
                || inline_container(token->name)
                || named(token->name, "p") || named(token->name, "div")
                || named(token->name, "address")
                || named(token->name, "dialog")
                || named(token->name, "details")
                || named(token->name, "summary")
                || named(token->name, "fieldset")
                || template_sectioning(token->name)
                || named(token->name, "pre")
                || named(token->name, "listing")
                || named(token->name, "blockquote")
                || named(token->name, "center")
                || named(token->name, "figure")
                || named(token->name, "figcaption")
                || named(token->name, "datalist")
                || named(token->name, "output")
                || named(token->name, "progress")
                || named(token->name, "meter")
                || named(token->name, "hgroup")
                || named(token->name, "search")
                || named(token->name, "dir")
                || named(token->name, "dl")
                || named(token->name, "dt")
                || named(token->name, "dd")
                || named(token->name, "noscript")
                || named(token->name, "marquee")
                || named(token->name, "applet")
                || named(token->name, "ul")
                || named(token->name, "ol")
                || named(token->name, "menu")
                || named(token->name, "li")
                || named(token->name, "ruby")
                || named(token->name, "rt")
                || named(token->name, "rp")
                || heading(token->name)
                || named(token->name, "table")
                || named(token->name, "select")
                || named(token->name, "option")
                || named(token->name, "optgroup")
                || named(token->name, "span")
                || named(token->name, "label")
                || named(token->name, "button")
                || named(token->name, "q")
                || named(token->name, "cite")
                || named(token->name, "abbr")
                || named(token->name, "sub")
                || named(token->name, "sup")
                || named(token->name, "big")
                || named(token->name, "small")
                || named(token->name, "s")
                || named(token->name, "strike")
                || named(token->name, "tt")
                || named(token->name, "u")
                || named(token->name, "var")
                || named(token->name, "nobr")
                || named(token->name, "font")
                || (named(token->name, "object")
                    && lxa_vector_length(&tree->template_elements)
                       == template_boundary(tree))
                || template_formatting(token->name);
            if (!template_ordinary) {
                status = supported_container(tree, token->name,
                                              &template_ordinary);
                if (status != LXA_OK) return status;
                /* Table and other special insertion modes still need their
                 * recovery rules. Ordinary non-simple and unknown/custom
                 * names use the in-body fallback here. */
                if (simple_container(token->name))
                    template_ordinary = false;
            }
            if (!template_ordinary && named(token->name, "object")
                && lxa_vector_length(&tree->template_elements)
                   > template_boundary(tree)) {
                lxa_span_t top;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        template_parent(tree), &top);
                if (status != LXA_OK) return status;
                template_ordinary = named(top, "object");
                if (!template_ordinary
                    && tree->template_insertion_mode == LXA_TEMPLATE_BODY)
                    template_ordinary = named(top, "p")
                        || named(top, "div") || named(top, "span")
                        || named(top, "li") || named(top, "section")
                        || named(top, "td") || named(top, "th")
                        || named(top, "caption") || template_formatting(top);
            }
        }
        if (template_ordinary) {
            size_t count, depth, i, close_at;
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            close_at = template_select_close == SIZE_MAX
                ? lxa_vector_length(&tree->template_elements)
                : template_select_close;
            if (template_select_close == SIZE_MAX
                && (named(token->name, "option")
                    || named(token->name, "optgroup"))
                && close_at > template_boundary(tree)) {
                lxa_span_t current;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[close_at - 1], &current);
                if (status != LXA_OK) return status;
                if (named(current, "option")) {
                    --close_at;
                }
                if (close_at < lxa_vector_length(&tree->template_elements)
                    && close_at > template_boundary(tree)) {
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[close_at - 1], &current);
                    if (status != LXA_OK) return status;
                    if (table_context(current)) {
                        status = template_pop_elements(tree, close_at);
                        if (status != LXA_OK) return status;
                        return template_foster_element(tree, token, true,
                                                       NULL, LXA_DOM_NS_HTML);
                    }
                }
            }
            if (named(token->name, "rt") || named(token->name, "rp")) {
                lxa_span_t top;
                if (close_at > template_boundary(tree)) {
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[close_at - 1], &top);
                    if (status != LXA_OK) return status;
                    if (named(top, "rt") || named(top, "rp")) {
                        size_t j = close_at - 1;
                        while (j > template_boundary(tree)) {
                            lxa_span_t ancestor;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(tree->nodes,
                                    open[--j], &ancestor);
                            if (status != LXA_OK) return status;
                            if (named(ancestor, "ruby")) {
                                --close_at;
                                break;
                            }
                        }
                    }
                    else if (!named(top, "ruby")) {
                        size_t j = close_at - 1;
                        bool ruby = false;
                        if (!named(top, "span")) return LXA_ERROR_UNSUPPORTED;
                        while (j > template_boundary(tree)) {
                            lxa_span_t ancestor;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(tree->nodes,
                                    open[--j], &ancestor);
                            if (status != LXA_OK) return status;
                            if (named(ancestor, "ruby")) {
                                ruby = true;
                                break;
                            }
                        }
                        if (!ruby) return LXA_ERROR_UNSUPPORTED;
                    }
                }
            }
            if (named(token->name, "li")) {
                status = template_open_list_item(tree, &close_at);
                if (status != LXA_OK) return status;
                if (close_at > template_boundary(tree)
                    && close_at < lxa_vector_length(&tree->template_elements)) {
                    lxa_span_t previous;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[close_at - 1], &previous);
                    if (status != LXA_OK) return status;
                    if (named(previous, "table")) {
                        status = template_pop_elements(tree, close_at);
                        if (status != LXA_OK) return status;
                        return template_foster_element(tree, token, true,
                                                       NULL, LXA_DOM_NS_HTML);
                    }
                }
                if (close_at == lxa_vector_length(&tree->template_elements)) {
                    status = template_open_paragraph(tree, &close_at);
                    if (status != LXA_OK) return status;
                }
            }
            else if (named(token->name, "button")) {
                for (i = close_at; i > template_boundary(tree);) {
                    lxa_span_t current;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[--i], &current);
                    if (status != LXA_OK) return status;
                    if (named(current, "button")) {
                        close_at = i;
                        break;
                    }
                    if (table_context(current)) break;
                }
                if (close_at < lxa_vector_length(&tree->template_elements)
                    && close_at > template_boundary(tree)) {
                    lxa_span_t parent_name;
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        open[close_at - 1], &parent_name);
                    if (status != LXA_OK) return status;
                    if (named(parent_name, "table")) {
                        status = template_pop_elements(tree, close_at);
                        if (status != LXA_OK) return status;
                        return template_foster_element(tree, token, true,
                                                       NULL, LXA_DOM_NS_HTML);
                    }
                }
            }
            else if (named(token->name, "p")
                     || block_container(token->name)
                     || heading(token->name)) {
                status = template_open_paragraph(tree, &close_at);
                if (status == LXA_ERROR_UNSUPPORTED
                    && (named(token->name, "p")
                        || template_pending_chain_block(token->name)
                        || template_pending_direct_block(token->name))) {
                    bool pending_chain;
                    status = template_pending_paragraph_chain(tree, close_at,
                                                              &pending_chain);
                    if (status != LXA_OK) return status;
                    if (!pending_chain) return LXA_ERROR_UNSUPPORTED;
                }
                if (status != LXA_OK) return status;
                if (heading(token->name)
                    && close_at > template_boundary(tree)) {
                    lxa_span_t current;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[close_at - 1], &current);
                    if (status != LXA_OK) return status;
                    if (heading(current)) --close_at;
                }
            }
            if (named(token->name, "a")) {
                for (i = template_boundary(tree);
                     i < lxa_vector_length(&tree->template_elements); ++i) {
                    lxa_span_t name;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                    status = lxa_dom_nodes_element_name(tree->nodes, open[i],
                                                         &name);
                    if (status != LXA_OK) return status;
                    if (named(name, "a")) return LXA_ERROR_UNSUPPORTED;
                }
            }
            status = lxa_size_add(close_at, 1, &count);
            if (status == LXA_OK)
                status = lxa_size_add(lxa_vector_length(&tree->template_stack),
                                       1, &depth);
            if (status == LXA_OK) status = lxa_size_mul(depth, 2, &depth);
            if (status == LXA_OK) status = lxa_size_add(depth, 2, &depth);
            if (status == LXA_OK) status = lxa_size_add(depth, count, &depth);
            if (status == LXA_OK
                && tree->template_return_mode == LXA_TREE_IN_BODY) {
                size_t open_count = lxa_vector_length(&tree->open_elements);
                if (open_count == 0) return LXA_ERROR_INTERNAL;
                status = lxa_size_add(depth, open_count - 1, &depth);
            }
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_TREE_DEPTH, depth, &tree->nodes->limit_reason);
            if (status == LXA_OK
                && close_at == lxa_vector_length(&tree->template_elements))
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_vector_reserve(&tree->template_elements, count);
            if (status == LXA_ERROR_LIMIT
                && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
                tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
            if (status != LXA_OK) return status;
            status = lxa_dom_nodes_append_html_element(tree->nodes,
                                                        token->name, &child);
            if (status == LXA_OK)
                status = apply_attributes(tree, child, token);
            if (status != LXA_OK) return status;
            if (close_at != lxa_vector_length(&tree->template_elements)) {
                status = template_pop_elements(tree, close_at);
                if (status != LXA_OK) return status;
            }
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                template_parent(tree), child);
            if (status == LXA_OK && named(token->name, "option"))
                status = lxa_dom_nodes_reset_option_select(tree->nodes, child);
            if (status != LXA_OK) return status;
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK) {
                status = lxa_vector_push(&tree->template_elements, &child);
                if (status != LXA_OK)
                    lxa_budget_release(tree->nodes->budget,
                                        LXA_BUDGET_OPEN_ELEMENTS, 1);
            }
            if (status == LXA_OK
                && (named(token->name, "pre")
                    || named(token->name, "listing")))
                tree->skip_pre_newline = true;
            if (status == LXA_OK
                && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL
                && lxa_vector_length(&tree->template_elements)
                   == template_boundary(tree) + 1)
                tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            return status;
        }
        if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
        if (token->kind != LXA_HTML_EOF
            && token->kind != LXA_HTML_COMMENT
            && token->kind != LXA_HTML_PROCESSING_INSTRUCTION)
            return LXA_ERROR_UNSUPPORTED; /* Element modes still needed. */
        if (token->kind == LXA_HTML_EOF) {
            size_t work;
            status = lxa_size_add(lxa_vector_length(&tree->template_stack),
                                   lxa_vector_length(&tree->template_elements),
                                   &work);
            if (status == LXA_OK) status = lxa_size_add(work, 1, &work);
            if (status == LXA_OK)
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, work, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            while (tree->mode == LXA_TREE_TEMPLATE_TEXT) {
                status = close_template(tree);
                if (status != LXA_OK) return status;
            }
        }
    }
    if (tree->mode == LXA_TREE_IN_FRAMESET
        || tree->mode == LXA_TREE_AFTER_FRAMESET) {
        if (token->kind == LXA_HTML_EOF) {
            status = pop_elements(tree, &tree->open_elements, 0);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_DONE;
            return LXA_OK;
        }
        if (token->kind == LXA_HTML_COMMENT) {
            return append_character(tree,
                tree->mode == LXA_TREE_IN_FRAMESET
                    ? current_element(tree) : tree->document,
                LXA_DOM_KIND_COMMENT, token->data);
        }
        if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "html"))
            return apply_attributes(tree, tree->html, token);
        if (token->kind == LXA_HTML_TEXT && space_only(token->data))
            return append_character(tree,
                tree->mode == LXA_TREE_IN_FRAMESET
                    ? current_element(tree) : tree->html,
                LXA_DOM_KIND_TEXT, token->data);
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "noframes")) {
            status = lxa_budget_check(tree->nodes->budget,
                LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            return status == LXA_OK
                ? begin_text_element(tree, tokenizer,
                    tree->mode == LXA_TREE_IN_FRAMESET
                        ? current_element(tree) : tree->html,
                    token, LXA_HTML_RAWTEXT) : status;
        }
        if (tree->mode == LXA_TREE_AFTER_FRAMESET)
            return token->kind == LXA_HTML_END_TAG
                && named(token->name, "html") ? LXA_OK : LXA_ERROR_UNSUPPORTED;
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "frameset")) {
            status = reserve_open_elements(tree, 1);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            return status == LXA_OK ? open_container(tree, token) : status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "frame")) {
            status = lxa_budget_check(tree->nodes->budget,
                LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            return status == LXA_OK
                ? append_void(tree, current_element(tree), token) : status;
        }
        if (token->kind == LXA_HTML_END_TAG
            && named(token->name, "frameset")) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
            if (lxa_vector_length(&tree->open_elements) == 0)
                tree->mode = LXA_TREE_AFTER_FRAMESET;
            return LXA_OK;
        }
        if ((token->kind == LXA_HTML_START_TAG
             || token->kind == LXA_HTML_END_TAG)
            && (named(token->name, "template")
                || named(token->name, "frame")))
            return LXA_OK;
        return LXA_ERROR_UNSUPPORTED;
    }
    if (token->kind == LXA_HTML_START_TAG
        && named(token->name, "frameset")
        && (tree->mode == LXA_TREE_INITIAL
            || tree->mode == LXA_TREE_BEFORE_HTML
            || tree->mode == LXA_TREE_BEFORE_HEAD
            || tree->mode == LXA_TREE_IN_HEAD
            || tree->mode == LXA_TREE_AFTER_HEAD)
        && tree->body.handle == LXA_HANDLE_NONE) {
        size_t needed = 1;
        if (tree->head.handle == LXA_HANDLE_NONE) ++needed;
        if (tree->html.handle == LXA_HANDLE_NONE) ++needed;
        status = reserve_open_elements(tree, 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget,
                LXA_BUDGET_NODES, needed, &tree->nodes->limit_reason);
        if (status == LXA_OK) status = ensure_head(tree);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_html_element(tree->nodes,
                                                        token->name, &child);
        if (status == LXA_OK) status = apply_attributes(tree, child, token);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                             tree->html, child);
        if (status == LXA_OK) status = push_element(tree, child);
        if (status == LXA_OK) tree->mode = LXA_TREE_IN_FRAMESET;
        return status;
    }
    if (token->kind == LXA_HTML_EOF
        && tree->mode == LXA_TREE_TEXT
        && (tree->text_return_mode == LXA_TREE_IN_FRAMESET
            || tree->text_return_mode == LXA_TREE_AFTER_FRAMESET)) {
        status = pop_elements(tree, &tree->open_elements, 0);
        if (status != LXA_OK) return status;
        tree->mode = LXA_TREE_DONE;
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_EOF) {
        status = ensure_body(tree);
        if (status == LXA_OK)
            status = pop_elements(tree, &tree->open_elements, 0);
        if (status == LXA_OK) tree->mode = LXA_TREE_DONE;
        return status;
    }
    if (tree->mode == LXA_TREE_TEXT) {
        lxa_span_t name;
        if (token->kind == LXA_HTML_TEXT) {
            if (token->data.length != 0
                && tree->text_return_mode == LXA_TREE_TEMPLATE_TEXT
                && tree->pending_template_formatting.handle != LXA_HANDLE_NONE
                && lxa_vector_length(&tree->template_elements)
                   == template_boundary(tree)) {
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        tree->text_element, &name);
                if (status != LXA_OK) return status;
                if (named(name, "plaintext")) {
                    lxa_dom_ref_t clone;
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = clone_formatting_element(tree,
                            tree->pending_template_formatting, &clone);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_append_constructed_child(
                            tree->nodes, tree->text_element, clone);
                    if (status != LXA_OK) return status;
                    tree->text_element = clone;
                }
            }
            return append_character(tree, tree->text_element,
                                    LXA_DOM_KIND_TEXT, token->data);
        }
        if (token->kind != LXA_HTML_END_TAG
            || lxa_dom_nodes_element_name(tree->nodes, tree->text_element,
                                           &name) != LXA_OK
            || name.length != token->name.length
            || memcmp(name.data, token->name.data, name.length) != 0)
            return LXA_ERROR_INTERNAL;
        tree->mode = tree->text_return_mode;
        tree->text_element = (lxa_dom_ref_t){0};
        return LXA_OK;
    }
    if (tree->mode == LXA_TREE_HEAD_NOSCRIPT) {
        if (token->kind == LXA_HTML_END_TAG
            && named(token->name, "noscript")) {
            tree->head_noscript = (lxa_dom_ref_t){0};
            tree->mode = LXA_TREE_IN_HEAD;
            return LXA_OK;
        }
        if (token->kind == LXA_HTML_END_TAG
            && named(token->name, "br")) {
            tree->head_noscript = (lxa_dom_ref_t){0};
            tree->mode = LXA_TREE_IN_HEAD;
            return LXA_OK;
        }
        if (token->kind == LXA_HTML_END_TAG)
            return LXA_OK;
        if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "head")
                || named(token->name, "noscript")))
            return LXA_OK;
        if (token->kind == LXA_HTML_TEXT) {
            size_t prefix = 0;
            while (prefix < token->data.length) {
                uint8_t c = token->data.data[prefix];
                if (c != ' ' && c != '\n' && c != '\r'
                    && c != '\t' && c != '\f') break;
                ++prefix;
            }
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK,
                prefix < token->data.length ? prefix + 1 : prefix,
                &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (prefix != 0) {
                lxa_span_t spaces = {token->data.data, prefix};
                status = append_character(tree, tree->head_noscript,
                                           LXA_DOM_KIND_TEXT, spaces);
                if (status != LXA_OK) return status;
                if (prefix == token->data.length) return LXA_OK;
                adjusted = *token;
                adjusted.data.data += prefix;
                adjusted.data.length -= prefix;
                token = &adjusted;
            }
        }
        if (token->kind != LXA_HTML_COMMENT
            && token->kind != LXA_HTML_PROCESSING_INSTRUCTION
            && token->kind != LXA_HTML_DOCTYPE
            && !(token->kind == LXA_HTML_START_TAG
                 && (named(token->name, "html")
                     || named(token->name, "basefont")
                     || named(token->name, "bgsound")
                     || named(token->name, "link")
                     || named(token->name, "meta")
                     || named(token->name, "style")
                     || named(token->name, "noframes")))) {
            /* Anything else closes noscript and is reprocessed in head. */
            tree->head_noscript = (lxa_dom_ref_t){0};
            tree->mode = LXA_TREE_IN_HEAD;
        }
    }
    if ((tree->mode == LXA_TREE_IN_BODY
         || tree->mode == LXA_TREE_AFTER_BODY
         || tree->mode == LXA_TREE_AFTER_HTML)
        && lxa_vector_length(&tree->open_elements) > 1) {
        lxa_dom_namespace_t ns;
        bool integration_owner = false;
        bool html_integration_child = false;
        bool breakout_font = false;
        status = lxa_dom_nodes_element_namespace(tree->nodes,
                                                  current_element(tree), &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML
            && (token->kind == LXA_HTML_START_TAG
                || token->kind == LXA_HTML_TEXT)) {
            lxa_span_t current_name;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                current_element(tree),
                                                &current_name);
            if (status != LXA_OK) return status;
            if (ns == LXA_DOM_NS_MATHML) {
                integration_owner = mathml_text_integration(current_name);
                html_integration_child = integration_owner
                    && (token->kind == LXA_HTML_TEXT
                        || (!named(token->name, "mglyph")
                            && !named(token->name, "malignmark")));
                if (named(current_name, "annotation-xml")) {
                    status = annotation_html_integration(tree,
                        current_element(tree), &integration_owner);
                    if (status != LXA_OK) return status;
                    html_integration_child = integration_owner
                        || (token->kind == LXA_HTML_START_TAG
                            && named(token->name, "svg"));
                }
            }
            else {
                integration_owner = named(current_name, "title")
                                    || named(current_name, "desc")
                                    || named(current_name, "foreignObject");
                html_integration_child = named(current_name, "foreignObject")
                    || (integration_owner
                    && (token->kind == LXA_HTML_TEXT
                        || named(token->name, "b")
                        || named(token->name, "span")
                        || named(token->name, "em")
                        || named(token->name, "strong")
                        || named(token->name, "i") || named(token->name, "code")
                        || named(token->name, "p") || named(token->name, "div")
                        || named(token->name, "a") || named(token->name, "img")
                        || named(token->name, "br")
                        || named(token->name, "td")
                        || named(token->name, "th")));
            }
        }
        if (ns != LXA_DOM_NS_HTML && !integration_owner
            && token->kind == LXA_HTML_START_TAG
            && named(token->name, "font")) {
            status = font_breakout(tree, token, &breakout_font);
            if (status != LXA_OK) return status;
        }
        if (ns != LXA_DOM_NS_HTML
            && ((token->kind == LXA_HTML_END_TAG
                 && (named(token->name, "br")
                     || named(token->name, "p")))
                || (!integration_owner && token->kind == LXA_HTML_START_TAG
                    && (named(token->name, "p") || named(token->name, "div")
                || named(token->name, "br") || named(token->name, "img")
                || named(token->name, "hr") || named(token->name, "span")
                || heading(token->name) || named(token->name, "ul")
                || named(token->name, "ol") || named(token->name, "li")
                || named(token->name, "blockquote") || named(token->name, "pre")
                || named(token->name, "dl") || named(token->name, "dt")
                || named(token->name, "dd") || named(token->name, "table")
                || named(token->name, "center")
                || named(token->name, "menu")
                || named(token->name, "head")
                || named(token->name, "body")
                || named(token->name, "meta")
                || named(token->name, "embed")
                || named(token->name, "listing")
                || named(token->name, "b") || named(token->name, "big")
                || named(token->name, "small")
                || named(token->name, "code")
                || named(token->name, "em") || named(token->name, "i")
                || named(token->name, "s") || named(token->name, "strong")
                || named(token->name, "strike") || named(token->name, "sub")
                || named(token->name, "sup") || named(token->name, "var")
                || breakout_font)))) {
            /* An HTML breakout closes foreign open elements and reprocesses
             * the token in body mode. Preflight the scan and required
             * node/stack capacity before popping; construction is unpublished
             * until the entire parse succeeds. */
            const lxa_dom_ref_t *open;
            size_t i = lxa_vector_length(&tree->open_elements);
            bool found_html = false;
            status = body_void(token->name) || named(token->name, "head")
                     || named(token->name, "body")
                     ? LXA_OK : reserve_open_elements(tree, 1);
            if (status == LXA_OK && !named(token->name, "head")
                && !named(token->name, "body"))
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            open = (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
            while (i > 0) {
                lxa_dom_namespace_t ancestor;
                status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                            1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                --i;
                status = lxa_dom_nodes_element_namespace(tree->nodes, open[i],
                                                          &ancestor);
                if (status != LXA_OK) return status;
                if (ancestor == LXA_DOM_NS_HTML) {
                    found_html = true;
                    break;
                }
            }
            if (!found_html) return LXA_ERROR_INTERNAL;
            while (lxa_vector_length(&tree->open_elements) > i + 1) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
        }
        else if (ns != LXA_DOM_NS_HTML && !html_integration_child) {
            bool reprocess;
            status = consume_foreign(tree, token, ns, &reprocess);
            if (status != LXA_OK || !reprocess) return status;
        }
    }
    if (token->kind == LXA_HTML_START_TAG && named(token->name, "image")) {
        /* HTML's image start tag is reprocessed as img. Keep the token's
         * borrowed attributes and normalize only its construction name. */
        adjusted = *token;
        adjusted.name = literal("img");
        token = &adjusted;
    }
    if (token->kind == LXA_HTML_DOCTYPE
        && (tree->mode == LXA_TREE_INITIAL
            || tree->mode == LXA_TREE_BEFORE_HTML)) {
        if (tree->doctype_seen) return LXA_OK;
        declaration = (lxa_dom_doctype_data_t){0};
        declaration.name = token->name;
        declaration.public_id = token->public_id;
        declaration.system_id = token->system_id;
        declaration.has_public_id = token->has_public_id;
        declaration.has_system_id = token->has_system_id;
        declaration.force_quirks = token->force_quirks;
        status = lxa_dom_nodes_append_doctype(tree->nodes, declaration, &child);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                             tree->document, child);
        if (status == LXA_OK) {
            tree->doctype_seen = true;
            tree->mode = LXA_TREE_BEFORE_HTML;
        }
        return status;
    }
    if (tree->mode == LXA_TREE_INITIAL) tree->mode = LXA_TREE_BEFORE_HTML;
    if (token->kind == LXA_HTML_COMMENT
        || token->kind == LXA_HTML_PROCESSING_INSTRUCTION) {
        if (tree->mode == LXA_TREE_BEFORE_HTML) parent = tree->document;
        else if (tree->mode == LXA_TREE_AFTER_HTML) parent = tree->document;
        else if (tree->mode == LXA_TREE_AFTER_BODY) parent = tree->html;
        else if (tree->mode == LXA_TREE_IN_BODY)
            parent = current_element(tree);
        else if (tree->mode == LXA_TREE_TEMPLATE_TEXT)
            parent = template_parent(tree);
        else {
            status = ensure_html(tree);
            if (status != LXA_OK) return status;
            parent = tree->mode == LXA_TREE_HEAD_NOSCRIPT
                     ? tree->head_noscript
                     : tree->mode == LXA_TREE_IN_HEAD
                       ? tree->head : tree->html;
        }
        return append_comment_or_pi(tree, parent, token);
    }
    if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
    if (tree->mode == LXA_TREE_AFTER_BODY
        || tree->mode == LXA_TREE_AFTER_HTML) {
        if (token->kind == LXA_HTML_TEXT && space_only(token->data))
            return insert_data_text(tree, tree->body, token->data,
                                    false, false);
        /* An html start uses body rules without leaving the current mode. */
        if (token->kind != LXA_HTML_START_TAG
            || !named(token->name, "html"))
            tree->mode = LXA_TREE_IN_BODY;
    }
    if (tree->mode == LXA_TREE_IN_BODY
        && token->kind == LXA_HTML_END_TAG
        && named(token->name, "br")) {
        /* In body, </br> follows the br start-tag algorithm; any end-tag
         * attributes are discarded before table fostering or insertion. */
        adjusted = *token;
        adjusted.kind = LXA_HTML_START_TAG;
        adjusted.attributes = NULL;
        adjusted.attribute_count = 0;
        adjusted.self_closing = false;
        token = &adjusted;
    }
reprocess_body_start:
    if (tree->mode == LXA_TREE_IN_BODY
        && ((token->kind == LXA_HTML_START_TAG
             && body_control_start(token->name)
             && !named(token->name, "html"))
            || (token->kind == LXA_HTML_END_TAG
                && named(token->name, "head")))) {
        bool column_group;
        status = current_html_column_group(tree, &tree->open_elements, 0,
                                            &column_group);
        if (status != LXA_OK) return status;
        if (column_group) {
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        2, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = pop_element(tree);
            if (status != LXA_OK) return status;
            if (token->kind == LXA_HTML_START_TAG
                && (named(token->name, "frame")
                    || named(token->name, "frameset")))
                return LXA_OK;
        }
    }
    /* A template end without an active template is ignored by the in-head
     * rule, including when that rule is reached from body/table/select. */
    if (token->kind == LXA_HTML_END_TAG
        && named(token->name, "template"))
        return LXA_OK;
    /* An unmatched head end after the head phase does not close the body
     * or the current table insertion scope. */
    if (token->kind == LXA_HTML_END_TAG
        && named(token->name, "head")
        && tree->mode >= LXA_TREE_AFTER_HEAD)
        return LXA_OK;
    /* After-head and in-body both ignore another head start. In table
     * context, fostering reprocesses it under the in-body rule as well. */
    if (token->kind == LXA_HTML_START_TAG
        && named(token->name, "head")
        && tree->mode >= LXA_TREE_AFTER_HEAD)
        return LXA_OK;
    if (token->kind == LXA_HTML_START_TAG && tree->mode == LXA_TREE_IN_BODY
        && lxa_vector_length(&tree->open_elements) > 1) {
        lxa_span_t top;
        status = lxa_dom_nodes_element_name(tree->nodes, current_element(tree),
                                             &top);
        if (status != LXA_OK) return status;
        if (table_context(top) && !table_part(token->name)
            && !named(token->name, "html")
            && !named(token->name, "body")
            && !((named(top, "table") || table_section(top)
                   || named(top, "tr")) && named(token->name, "form"))) {
            bool container;
            /* Table insertion modes process a template start using the
             * head rule, retaining its content fragment under this table
             * scope rather than fostering the element before the table. */
            if (named(token->name, "template"))
                return begin_template(tree, token);
            if (named(token->name, "style")
                || named(token->name, "script")) {
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                if (named(top, "colgroup")) {
                    status = pop_element(tree);
                    if (status != LXA_OK) return status;
                }
                return begin_text_element(tree, tokenizer,
                    current_element(tree), token,
                    named(token->name, "style") ? LXA_HTML_RAWTEXT
                                                : LXA_HTML_SCRIPT_DATA);
            }
            if (named(token->name, "textarea")
                || named(token->name, "title")
                || named(token->name, "iframe")
                || named(token->name, "noembed")
                || named(token->name, "noframes")
                || named(token->name, "xmp")
                || named(token->name, "plaintext")) {
                lxa_dom_ref_t table, foster_parent, element;
                bool in_colgroup = named(top, "colgroup");
                bool textarea = named(token->name, "textarea");
                lxa_html_text_mode_t text_mode =
                    named(token->name, "plaintext") ? LXA_HTML_PLAINTEXT
                    : (textarea || named(token->name, "title"))
                      ? LXA_HTML_RCDATA : LXA_HTML_RAWTEXT;
                if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = foster_location(tree, &foster_parent, &table);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_append_html_element(tree->nodes,
                        token->name, &element);
                if (status == LXA_OK)
                    status = apply_attributes(tree, element, token);
                if (status == LXA_OK)
                    status = table.handle == LXA_HANDLE_NONE
                        ? lxa_dom_nodes_append_constructed_child(
                            tree->nodes, foster_parent, element)
                        : lxa_dom_nodes_insert_constructed_before(
                            tree->nodes, foster_parent, element, table);
                if (status == LXA_OK)
                    status = enter_text_mode(tree, tokenizer, token,
                        text_mode, element);
                if (status == LXA_OK) {
                    if (textarea) tree->skip_pre_newline = true;
                    if (in_colgroup) {
                        status = pop_element(tree);
                        if (status != LXA_OK) return status;
                    }
                }
                return status;
            }
            if (named(token->name, "form")
                && tree->form.handle != LXA_HANDLE_NONE)
                return LXA_OK;
            if (named(token->name, "base")
                || named(token->name, "link")
                || named(token->name, "meta")) {
                if (named(top, "colgroup")) {
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_budget_check(tree->nodes->budget,
                            LXA_BUDGET_WORK, 2,
                            &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                    status = pop_element(tree);
                    if (status != LXA_OK) return status;
                }
                return foster_element(tree, token, true);
            }
            if (named(top, "colgroup") && body_void(token->name)) {
                bool hidden = false;
                if (named(token->name, "input"))
                    status = hidden_input(tree, token, &hidden);
                else status = LXA_OK;
                if (status == LXA_OK)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK && !hidden)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = pop_element(tree);
                if (status != LXA_OK) return status;
                return hidden ? append_void(tree, current_element(tree), token)
                              : foster_element(tree, token, true);
            }
            if (body_void(token->name)) {
                if (named(token->name, "input")) {
                    bool hidden;
                    status = hidden_input(tree, token, &hidden);
                    if (status != LXA_OK) return status;
                    if (hidden)
                        return append_void(tree, current_element(tree),
                                           token);
                }
                return foster_element(tree, token, true);
            }
            if (named(token->name, "svg") || named(token->name, "math")) {
                if (named(top, "colgroup")) {
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_budget_check(tree->nodes->budget,
                            LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                    if (status == LXA_OK) status = pop_element(tree);
                    if (status != LXA_OK) return status;
                }
                return foster_foreign(tree, token,
                    named(token->name, "svg") ? LXA_DOM_NS_SVG
                                              : LXA_DOM_NS_MATHML);
            }
            status = supported_container(tree, token->name, &container);
            if (status != LXA_OK) return status;
            /* The table-mode "anything else" rule foster-parents marquee
             * even though its special body insertion rules remain deferred. */
            if (named(token->name, "marquee")) container = true;
            if (container && named(top, "colgroup")) {
                status = reserve_open_elements(tree, 1);
                if (status == LXA_OK)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return container ? foster_element(tree, token, false)
                             : LXA_ERROR_UNSUPPORTED;
        }
    }
    if (tree->mode == LXA_TREE_IN_BODY
        && lxa_vector_length(&tree->open_elements) > 1
        && token->kind != LXA_HTML_TEXT) {
        lxa_span_t top;
        status = lxa_dom_nodes_element_name(tree->nodes,
                                            current_element(tree), &top);
        if (status != LXA_OK) return status;
        /* The in-head script rule keeps the current insertion parent and
         * switches the tokenizer to script data; the body branch below does
         * the same thing, without closing the select. */
        if ((named(top, "select") || named(top, "option")
             || named(top, "optgroup"))
            && !(token->kind == LXA_HTML_START_TAG
                 && named(token->name, "script"))) {
            bool active;
            status = select_open(tree, &active);
            if (status != LXA_OK) return status;
            if (!active
                && !(token->kind == LXA_HTML_START_TAG
                     && (named(token->name, "option")
                         || named(token->name, "optgroup")
                         || named(token->name, "hr")))
                && !(token->kind == LXA_HTML_END_TAG
                     && (named(token->name, "option")
                         || named(token->name, "optgroup"))))
                goto after_select_mode;
            /* Select mode delegates template starts to the in-head rule.
             * Keep the select and any option/group on the body stack. */
            if (token->kind == LXA_HTML_START_TAG
                && named(token->name, "template"))
                return begin_template(tree, token);
            if (token->kind == LXA_HTML_END_TAG
                && table_part(token->name))
                return named(token->name, "col") ? LXA_OK
                       : end_table_part(tree, token->name);
            if (token->kind == LXA_HTML_START_TAG
                && named(token->name, "html"))
                return apply_attributes(tree, tree->html, token);
            if (token->kind == LXA_HTML_START_TAG
                && (named(token->name, "table")
                    || named(token->name, "tr")
                    || named(token->name, "td")
                    || named(token->name, "th")
                    || named(token->name, "caption")
                    || table_section(token->name))) {
                bool in_table, in_cell, in_caption;
                select_parent_t select_parent;
                size_t needed = 1;
                size_t foreign_above_table = 0;
                status = select_table_scope(tree, &in_table, &in_cell,
                    &in_caption, &select_parent);
                if (status != LXA_OK) return status;
                if (in_table
                    && (named(token->name, "table") || in_cell
                        || in_caption
                        || select_parent != SELECT_PARENT_OTHER)) {
                    if (named(token->name, "table")) {
                        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                            tree->open_elements.bytes.data;
                        size_t i = lxa_vector_length(&tree->open_elements);
                        bool after_select = false;
                        while (i > 1) {
                            lxa_dom_namespace_t ns;
                            lxa_span_t name;
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1,
                                &tree->nodes->limit_reason);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_namespace(
                                    tree->nodes, open[--i], &ns);
                            if (status != LXA_OK) return status;
                            if (ns != LXA_DOM_NS_HTML) {
                                if (after_select) ++foreign_above_table;
                                continue;
                            }
                            status = lxa_dom_nodes_element_name(tree->nodes,
                                                                 open[i], &name);
                            if (status != LXA_OK) return status;
                            if (after_select) break;
                            if (named(name, "select")) after_select = true;
                        }
                    }
                    if ((in_caption
                         || select_parent == SELECT_PARENT_TABLE
                         || select_parent == SELECT_PARENT_COLGROUP)
                        && !named(token->name, "table")
                        && (named(token->name, "td")
                            || named(token->name, "th")))
                        needed = 3; /* implied tbody, tr and cell */
                    else if ((in_caption
                              || select_parent == SELECT_PARENT_TABLE
                              || select_parent == SELECT_PARENT_COLGROUP)
                             && named(token->name, "tr"))
                        needed = 2; /* implied tbody and row */
                    else if (select_parent == SELECT_PARENT_SECTION
                             && (named(token->name, "td")
                                 || named(token->name, "th")))
                        needed = 2; /* implied tr and cell */
                    status = reserve_open_elements(tree,
                        named(token->name, "table") ? 0 : needed);
                    if (status == LXA_OK)
                        status = lxa_budget_check(tree->nodes->budget,
                            LXA_BUDGET_NODES, needed,
                            &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                    status = close_select(tree);
                    if (status != LXA_OK) return status;
                    while (foreign_above_table != 0) {
                        --foreign_above_table;
                        status = pop_element(tree);
                        if (status != LXA_OK) return status;
                    }
                    goto reprocess_body_start;
                }
                if (in_table) return LXA_ERROR_UNSUPPORTED;
            }
            if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "input")
                || named(token->name, "textarea")
                || named(token->name, "keygen"))) {
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = close_select(tree);
                if (status != LXA_OK) return status;
                goto reprocess_body_start;
            }
            return consume_select(tree, token, top);
        }
    }
after_select_mode:
    if (token->kind == LXA_HTML_START_TAG) {
        bool container;
        if (named(token->name, "template")
            && tree->mode <= LXA_TREE_BEFORE_HEAD) {
            status = ensure_head(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_HEAD;
        }
        if (named(token->name, "template")
            && (tree->mode == LXA_TREE_IN_HEAD
                || tree->mode == LXA_TREE_AFTER_HEAD
                || tree->mode == LXA_TREE_IN_BODY))
            return begin_template(tree, token);
        /* This parser uses disabled scripting mode, including the distinct
         * in-head noscript insertion mode. */
        if (named(token->name, "noscript")
            && tree->mode <= LXA_TREE_IN_HEAD) {
            status = ensure_head(tree);
            if (status == LXA_OK)
                status = lxa_dom_nodes_append_html_element(tree->nodes,
                                                            token->name, &child);
            if (status == LXA_OK)
                status = apply_attributes(tree, child, token);
            if (status == LXA_OK)
                status = lxa_dom_nodes_append_constructed_child(tree->nodes,
                                                                 tree->head, child);
            if (status == LXA_OK) {
                tree->head_noscript = child;
                tree->mode = LXA_TREE_HEAD_NOSCRIPT;
            }
            return status;
        }
        if (named(token->name, "html")) {
            bool initial = tree->mode == LXA_TREE_BEFORE_HTML;
            status = ensure_html(tree);
            if (status == LXA_OK)
                status = apply_attributes(tree, tree->html, token);
            if (status == LXA_OK && initial)
                tree->mode = LXA_TREE_BEFORE_HEAD;
            return status;
        }
        if (named(token->name, "head")
            && (tree->mode == LXA_TREE_BEFORE_HTML
                || tree->mode == LXA_TREE_BEFORE_HEAD)) {
            status = ensure_head(tree);
            if (status == LXA_OK)
                status = apply_attributes(tree, tree->head, token);
            if (status == LXA_OK) tree->mode = LXA_TREE_IN_HEAD;
            return status;
        }
        if (named(token->name, "head")
            && tree->mode == LXA_TREE_AFTER_HEAD)
            return LXA_OK;
        if (named(token->name, "body")) {
            status = ensure_body(tree);
            if (status == LXA_OK)
                status = apply_attributes(tree, tree->body, token);
            if (status == LXA_OK) tree->mode = LXA_TREE_IN_BODY;
            return status;
        }
        if (named(token->name, "form")) {
            lxa_span_t top;
            bool in_table;
            if (tree->form.handle != LXA_HANDLE_NONE)
                return LXA_OK; /* nested start tags are ignored */
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                current_element(tree), &top);
            if (status != LXA_OK
                || (table_context(top)
                    && !named(top, "table")
                    && !table_section(top) && !named(top, "tr"))) {
                return status == LXA_OK ? LXA_ERROR_UNSUPPORTED : status;
            }
            /* Element construction can relocate the string buffer holding
             * top; retain only this decision across that mutation. */
            in_table = table_context(top);
            status = reserve_open_elements(tree, 1);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = close_paragraph(tree, NULL);
            if (status != LXA_OK) return status;
            status = open_container(tree, token);
            if (status == LXA_OK) {
                tree->form = current_element(tree);
                /* In-table forms are inserted but never stay open. */
                if (in_table) {
                    status = pop_element(tree);
                    if (status != LXA_OK) return status;
                }
            }
            return status;
        }
        if (named(token->name, "title") || named(token->name, "style")
            || (named(token->name, "noframes")
                && tree->mode <= LXA_TREE_AFTER_HEAD)) {
            if ((named(token->name, "style")
                 || named(token->name, "title"))
                && tree->mode == LXA_TREE_IN_BODY) {
                status = ensure_body(tree);
                if (status != LXA_OK) return status;
                parent = current_element(tree);
            }
            else {
                status = ensure_head(tree);
                if (status != LXA_OK) return status;
                if (tree->mode < LXA_TREE_IN_HEAD)
                    tree->mode = LXA_TREE_IN_HEAD;
                parent = tree->mode == LXA_TREE_HEAD_NOSCRIPT
                         ? tree->head_noscript : tree->head;
            }
            return begin_text_element(tree, tokenizer, parent, token,
                named(token->name, "title") ? LXA_HTML_RCDATA
                                             : LXA_HTML_RAWTEXT);
        }
        if (named(token->name, "script")) {
            if (tree->mode <= LXA_TREE_AFTER_HEAD) {
                status = ensure_head(tree);
                if (status != LXA_OK) return status;
                if (tree->mode < LXA_TREE_IN_HEAD) tree->mode = LXA_TREE_IN_HEAD;
                parent = tree->mode == LXA_TREE_HEAD_NOSCRIPT
                         ? tree->head_noscript : tree->head;
            }
            else {
                status = ensure_body(tree);
                if (status != LXA_OK) return status;
                tree->mode = LXA_TREE_IN_BODY;
                parent = current_element(tree);
            }
            return begin_text_element(tree, tokenizer, parent, token,
                                       LXA_HTML_SCRIPT_DATA);
        }
        if (named(token->name, "textarea")) {
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            status = begin_text_element(tree, tokenizer, current_element(tree),
                                        token, LXA_HTML_RCDATA);
            if (status == LXA_OK) tree->skip_pre_newline = true;
            return status;
        }
        if (named(token->name, "iframe") || named(token->name, "noembed")
            || named(token->name, "noframes")) {
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            return begin_text_element(tree, tokenizer, current_element(tree),
                                       token, LXA_HTML_RAWTEXT);
        }
        if (named(token->name, "xmp")) {
            status = ensure_body(tree);
            if (status == LXA_OK) status = close_paragraph(tree, NULL);
            if (status == LXA_OK) status = reconstruct_anchor(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            return begin_text_element(tree, tokenizer, current_element(tree),
                                       token, LXA_HTML_RAWTEXT);
        }
        if (named(token->name, "plaintext")) {
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            return begin_text_element(tree, tokenizer, current_element(tree),
                                       token, LXA_HTML_PLAINTEXT);
        }
        if (named(token->name, "base") || named(token->name, "link")
            || named(token->name, "meta")
            || named(token->name, "basefont")
            || named(token->name, "bgsound")) {
            bool in_body = tree->mode == LXA_TREE_IN_BODY;
            status = in_body ? ensure_body(tree) : ensure_head(tree);
            if (status == LXA_OK)
                status = append_void(tree, in_body ? current_element(tree)
                    : tree->mode == LXA_TREE_HEAD_NOSCRIPT
                      ? tree->head_noscript : tree->head, token);
            if (status == LXA_OK && tree->mode < LXA_TREE_IN_HEAD)
                tree->mode = LXA_TREE_IN_HEAD;
            return status;
        }
        if (body_void(token->name)) {
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            if (named(token->name, "hr")) {
                status = close_paragraph(tree, NULL);
                if (status != LXA_OK) return status;
            }
            else {
                status = reconstruct_anchor(tree);
                if (status != LXA_OK) return status;
            }
            return append_void(tree, current_element(tree), token);
        }
        if (table_part(token->name))
            return open_table_part(tree, token);
        if (named(token->name, "svg") || named(token->name, "math")) {
            lxa_dom_namespace_t ns = named(token->name, "svg")
                                     ? LXA_DOM_NS_SVG : LXA_DOM_NS_MATHML;
            status = ensure_body(tree);
            if (status == LXA_OK) status = reserve_open_elements(tree, 1);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK) status = reconstruct_anchor(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            status = open_container_ns(tree, token, ns);
            if (status == LXA_OK && token->self_closing) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return status;
        }
        if (named(token->name, "nobr")) {
            const lxa_dom_ref_t *open;
            size_t i, length;
            bool close_previous = false;
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            length = lxa_vector_length(&tree->open_elements);
            open = (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
            for (i = length; i > 1; ) {
                lxa_span_t name;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = lxa_dom_nodes_element_name(tree->nodes,
                                                     open[--i], &name);
                if (status != LXA_OK) return status;
                if (named(name, "nobr")) {
                    /* A crossed entry needs adoption-agency reconstruction.
                     * Only the current element can be closed in place. */
                    if (i != length - 1) return LXA_ERROR_UNSUPPORTED;
                    close_previous = true;
                    break;
                }
            }
            status = reserve_open_elements(tree, close_previous ? 0 : 1);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (close_previous) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            status = reconstruct_anchor(tree);
            if (status == LXA_OK)
                status = reconstruct_pending_formatting(tree);
            return status == LXA_OK ? open_container(tree, token) : status;
        }
        status = supported_container(tree, token->name, &container);
        if (status != LXA_OK) return status;
        if (container) {
            bool new_anchor = named(token->name, "a");
            bool previous_anchor_open = false;
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            if (new_anchor && tree->active_anchor.handle != LXA_HANDLE_NONE)
                status = reference_is_open(tree, tree->active_anchor,
                                           &previous_anchor_open);
            if (status != LXA_OK) return status;
            status = reserve_open_elements(tree,
                                            previous_anchor_open ? 0 : 1);
            if (status != LXA_OK) return status;
            if (named(token->name, "li") || named(token->name, "dt")
                || named(token->name, "dd")) {
                status = close_list_item(tree, !named(token->name, "li"));
                if (status != LXA_OK) return status;
            }
            if (named(token->name, "p") || heading(token->name)
                || block_container(token->name)
                || named(token->name, "li") || named(token->name, "dt")
                || named(token->name, "dd")) {
                status = close_paragraph(tree, NULL);
                if (status != LXA_OK) return status;
            }
            if (heading(token->name)) {
                status = close_open_heading(tree);
                if (status != LXA_OK) return status;
            }
            if (named(token->name, "button")) {
                status = close_open_button(tree);
                if (status != LXA_OK) return status;
            }
            if (new_anchor) {
                /* A nested anchor closes the earlier formatting entry,
                 * including a reconstructed entry from the preceding li. */
                if (tree->active_anchor.handle == LXA_HANDLE_NONE) {
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_ACTIVE_FORMATTING, 1,
                        &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                }
                status = close_active_anchor(tree);
            }
            else if (inline_container(token->name))
                status = reconstruct_anchor(tree);
            if (status == LXA_OK && inline_container(token->name))
                status = reconstruct_pending_formatting(tree);
            if (status != LXA_OK) return status;
            status = open_container(tree, token);
            if (status == LXA_OK && new_anchor) {
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_ACTIVE_FORMATTING, 1,
                    &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    tree->active_anchor = current_element(tree);
            }
            if (status == LXA_OK
                && (named(token->name, "pre")
                    || named(token->name, "listing")))
                tree->skip_pre_newline = true;
            return status;
        }
        return LXA_ERROR_UNSUPPORTED;
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "head")
        && tree->mode == LXA_TREE_IN_HEAD) {
        tree->mode = LXA_TREE_AFTER_HEAD;
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_END_TAG && tree->mode == LXA_TREE_IN_HEAD
        && !named(token->name, "body") && !named(token->name, "html")
        && !named(token->name, "br"))
        return LXA_OK; /* Any other in-head end tag is ignored. */
    if (token->kind == LXA_HTML_END_TAG && tree->mode == LXA_TREE_IN_HEAD
        && (named(token->name, "body") || named(token->name, "br"))) {
        bool br = named(token->name, "br");
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
            (tree->body.handle == LXA_HANDLE_NONE ? 1 : 0) + (br ? 1 : 0),
            &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = ensure_body(tree);
        if (status != LXA_OK) return status;
        tree->mode = br ? LXA_TREE_IN_BODY : LXA_TREE_AFTER_BODY;
        if (!br) return LXA_OK;
        /* Reprocess through the in-body start-tag rule with no attributes. */
        adjusted = *token;
        adjusted.kind = LXA_HTML_START_TAG;
        adjusted.attributes = NULL;
        adjusted.attribute_count = 0;
        adjusted.self_closing = false;
        token = &adjusted;
        goto reprocess_body_start;
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "body")
        && tree->mode == LXA_TREE_IN_BODY) {
        /* The open stack retains ordinary descendants until EOF; </body>
         * changes insertion mode without detaching or popping them. */
        tree->mode = LXA_TREE_AFTER_BODY;
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "html")) {
        if (tree->mode == LXA_TREE_AFTER_HEAD
            || tree->mode == LXA_TREE_IN_HEAD
            || tree->mode == LXA_TREE_BEFORE_HEAD) {
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
        }
        else if (tree->mode != LXA_TREE_IN_BODY
                 && tree->mode != LXA_TREE_AFTER_BODY)
            return LXA_ERROR_UNSUPPORTED;
        tree->mode = LXA_TREE_AFTER_HTML;
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_END_TAG && table_part(token->name)
        && tree->mode == LXA_TREE_IN_BODY)
        return named(token->name, "col") ? LXA_OK
               : end_table_part(tree, token->name);
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && (named(token->name, "svg") || named(token->name, "math"))) {
        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                                  tree->open_elements.bytes.data;
        size_t i = lxa_vector_length(&tree->open_elements);
        /* A breakout already removed the foreign stack. Its later close
         * tag has no matching open element and is ignored in body mode.
         * Do not swallow a close through an active integration point. */
        while (i > 0) {
            lxa_dom_namespace_t ns;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[--i],
                                                      &ns);
            if (status != LXA_OK) return status;
            if (ns != LXA_DOM_NS_HTML) return LXA_ERROR_UNSUPPORTED;
        }
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "a")
        && tree->mode == LXA_TREE_IN_BODY) {
        bool closed;
        status = close_crossed_anchor_formatting(tree, &closed);
        if (status != LXA_OK || closed) return status;
        return tree->active_anchor.handle == LXA_HANDLE_NONE
               ? LXA_OK : close_active_anchor(tree);
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "nobr")
        && tree->mode == LXA_TREE_IN_BODY) {
        lxa_span_t top;
        size_t i = lxa_vector_length(&tree->open_elements);
        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                                  tree->open_elements.bytes.data;
        while (i > 1) {
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = lxa_dom_nodes_element_name(tree->nodes, open[--i], &top);
            if (status != LXA_OK) return status;
            if (named(top, "nobr")) {
                if (i != lxa_vector_length(&tree->open_elements) - 1)
                    return LXA_ERROR_UNSUPPORTED;
                status = pop_element(tree);
                if (status != LXA_OK) return status;
                return LXA_OK;
            }
        }
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && basic_formatting(token->name)) {
        bool closed;
        if (tree->pending_formatting.handle != LXA_HANDLE_NONE) {
            lxa_span_t pending_name;
            bool pending_open;
            status = lxa_dom_nodes_element_name(tree->nodes,
                tree->pending_formatting, &pending_name);
            if (status != LXA_OK) return status;
            if (pending_name.length == token->name.length
                && memcmp(pending_name.data, token->name.data,
                          pending_name.length) == 0) {
                status = reference_is_open(tree, tree->pending_formatting,
                                            &pending_open);
                if (status != LXA_OK) return status;
                if (pending_open) {
                    if (current_element(tree).handle
                        != tree->pending_formatting.handle)
                        return LXA_ERROR_UNSUPPORTED;
                    status = pop_element(tree);
                    if (status != LXA_OK) return status;
                }
                forget_pending_formatting(tree);
                return LXA_OK;
            }
        }
        status = close_crossed_paragraph(tree, token->name, &closed);
        if (status != LXA_OK || closed) return status;
        status = close_crossed_formatting(tree, token->name, &closed);
        if (status != LXA_OK || closed) return status;
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "form")
        && tree->mode == LXA_TREE_IN_BODY) {
        lxa_dom_ref_t form = tree->form;
        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                                  tree->open_elements.bytes.data;
        size_t i;
        if (form.handle == LXA_HANDLE_NONE) return LXA_OK;
        /* Removing a form pointer does not discard open descendants; their
         * canonical parent links remain under the form until they close. */
        for (i = lxa_vector_length(&tree->open_elements); i != 0; --i) {
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (open[i - 1].handle == form.handle) {
                lxa_vector_erase(&tree->open_elements, i - 1, 1);
                lxa_budget_release(tree->nodes->budget,
                                   LXA_BUDGET_OPEN_ELEMENTS, 1);
                break;
            }
        }
        tree->form = (lxa_dom_ref_t){0};
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "p")
        && tree->mode == LXA_TREE_IN_BODY) {
        bool closed;
        status = close_paragraph(tree, &closed);
        if (status != LXA_OK || closed) return status;
        /* A stray </p> implies an empty paragraph at the insertion point. */
        if (lxa_vector_length(&tree->open_elements) == 0) {
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
        }
        status = reserve_open_elements(tree, 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        {
            lxa_html_token_t implied = {0};
            implied.name = literal("p");
            status = open_container(tree, &implied);
        }
        if (status == LXA_OK) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        return status;
    }
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && (named(token->name, "ul") || named(token->name, "ol")
            || named(token->name, "dl") || named(token->name, "menu")
            || named(token->name, "fieldset")))
        return close_list(tree, token->name);
    /* The heading end-tag rule accepts any h1-h6 in scope, and ignores a
     * stray heading end without creating content. */
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY && heading(token->name))
        return close_open_heading(tree);
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && block_container(token->name))
        return end_scoped_container(tree, token->name);
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && (named(token->name, "li") || named(token->name, "dt")
            || named(token->name, "dd")))
        return end_list_item(tree, token->name);
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && (named(token->name, "span")
            || !simple_container(token->name))) {
        bool ordinary;
        status = supported_container(tree, token->name, &ordinary);
        if (status != LXA_OK) return status;
        if (ordinary) return end_ordinary_element(tree, token->name);
    }
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && named(token->name, "option")) {
        lxa_span_t top;
        if (lxa_vector_length(&tree->open_elements) <= 1) return LXA_OK;
        status = lxa_dom_nodes_element_name(tree->nodes,
                                            current_element(tree), &top);
        if (status != LXA_OK) return status;
        /* Without an active select, the in-body option end only closes
         * the current option. A stray end must not abort the document. */
        if (!named(top, "option")) return LXA_OK;
    }
    /* An active select is handled above by consume_select(). Once a table
     * start has closed it, its later end tag has no select in scope. */
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && named(token->name, "select"))
        return LXA_OK;
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && named(token->name, "noscript")) {
        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
            tree->open_elements.bytes.data;
        size_t i = lxa_vector_length(&tree->open_elements);
        bool found = false;
        while (i > 1) {
            lxa_span_t name;
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[--i], &name);
            if (status != LXA_OK) return status;
            if (named(name, "noscript")) {
                found = true;
                break;
            }
            if (named(name, "body")) break;
        }
        if (!found)
            return LXA_OK; /* Head noscript may have closed on body text. */
    }
    if (token->kind == LXA_HTML_END_TAG && tree->mode == LXA_TREE_IN_BODY
        && lxa_vector_length(&tree->open_elements) > 1) {
        lxa_span_t top_name;
        bool container;
        status = supported_container(tree, token->name, &container);
        if (status != LXA_OK) return status;
        if (!container) return LXA_ERROR_UNSUPPORTED;
        status = lxa_dom_nodes_element_name(tree->nodes, current_element(tree),
                                             &top_name);
        if (status != LXA_OK) return status;
        if (top_name.length != token->name.length
            || memcmp(top_name.data, token->name.data, top_name.length) != 0)
            return LXA_ERROR_UNSUPPORTED;
        status = pop_element(tree);
        if (status != LXA_OK) return status;
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_TEXT) {
        if (space_only(token->data)
            && tree->mode != LXA_TREE_IN_BODY) return LXA_OK;
        if (token->data.length == 0
            || (token->data.length == 1 && token->data.data[0] == 0))
            return LXA_OK;
        status = ensure_body(tree);
        if (status != LXA_OK) return status;
        tree->mode = LXA_TREE_IN_BODY;
        if (lxa_vector_length(&tree->open_elements) > 1) {
            lxa_span_t top;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                current_element(tree), &top);
            if (status != LXA_OK) return status;
            if (table_context(top))
                return insert_data_text(tree, current_element(tree),
                                        token->data, false, true);
            if (named(top, "select") || named(top, "option")
                || named(top, "optgroup"))
                return insert_data_text(tree, current_element(tree),
                                        token->data, false, false);
        }
        status = reconstruct_anchor(tree);
        if (status != LXA_OK) return status;
        status = reconstruct_pending_formatting(tree);
        if (status != LXA_OK) return status;
        return insert_data_text(tree, current_element(tree), token->data,
                                false, false);
    }
    return LXA_ERROR_UNSUPPORTED;
}

lxa_status_t
lxa_html_tree_on_token(void *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token)
{
    lxa_html_tree_t *tree = context;
    if (tree == NULL || tree->nodes == NULL) return LXA_ERROR_ARGUMENT;
    if (tree->failure != LXA_OK) return tree->failure;
    if (tree->mode == LXA_TREE_DONE) return LXA_ERROR_ARGUMENT;
    tree->failure = consume(tree, tokenizer, token);
    if (tree->failure == LXA_OK && tokenizer != NULL
        && token != NULL && token->kind != LXA_HTML_EOF) {
        lxa_dom_namespace_t ns = LXA_DOM_NS_HTML;
        lxa_span_t name;
        bool integration = false;
        if (lxa_vector_length(&tree->open_elements) != 0)
            tree->failure = lxa_dom_nodes_element_namespace(
                tree->nodes, current_element(tree), &ns);
        if (tree->failure == LXA_OK && ns == LXA_DOM_NS_SVG) {
            tree->failure = lxa_dom_nodes_element_name(tree->nodes,
                                                        current_element(tree),
                                                        &name);
            if (tree->failure == LXA_OK)
                integration = named(name, "title") || named(name, "desc");
        }
        if (tree->failure == LXA_OK && ns == LXA_DOM_NS_MATHML) {
            tree->failure = lxa_dom_nodes_element_name(tree->nodes,
                                                        current_element(tree),
                                                        &name);
            if (tree->failure == LXA_OK && named(name, "annotation-xml"))
                tree->failure = annotation_html_integration(tree,
                    current_element(tree), &integration);
        }
        if (tree->failure == LXA_OK
            && lxa_html_tokenizer_allow_cdata(tokenizer,
                ns != LXA_DOM_NS_HTML && !integration) != LXA_HTML_OK)
            tree->failure = LXA_ERROR_INTERNAL;
    }
    /* All deliberate grammar boundaries return UNSUPPORTED. A rejected
     * store reference or token from our tokenizer is a pipeline failure. */
    if (tree->failure == LXA_ERROR_ARGUMENT)
        tree->failure = LXA_ERROR_INTERNAL;
    return tree->failure;
}
