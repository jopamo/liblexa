#include <assert.h>
#include <string.h>
#include "dom/node_store_internal.h"
#include "html/foreign_names.h"
#include "html/foreign_context.h"
#include "html/tree_categories.h"
#include "html-tags.h"
#include "html/tree.h"
#include "html/tokenizer_classes.h"

static lxa_span_t
literal(const char *text)
{
    lxa_span_t result = {(const uint8_t *) text, strlen(text)};
    return result;
}

/* All tree-builder names here are literals; carry their lengths at compile
 * time instead of rediscovering them for every token and open-stack probe. */
#define named(name, text) lxa_html_tree_name_equals((name), (text), sizeof(text) - 1)

/* Keep virtual template context for tree feedback; redirect only insertion. */
static lxa_status_t
resolve_template_location(lxa_html_tree_t *tree,
                          lxa_dom_ref_t *parent, lxa_dom_ref_t *before)
{
    lxa_dom_ref_t actual;
    lxa_status_t status;
    if (tree->template_patch.target.handle == LXA_HANDLE_NONE
        || parent->handle != tree->template_fragment.handle)
        return LXA_OK;
    *parent = tree->template_patch.target;
    *before = (lxa_dom_ref_t){0};
    if (tree->template_patch.end.handle == LXA_HANDLE_NONE) return LXA_OK;
    status = lxa_dom_nodes_parent(tree->nodes, tree->template_patch.end, &actual);
    if (status == LXA_OK && actual.handle == parent->handle)
        *before = tree->template_patch.end;
    return status;
}

static lxa_status_t
insert_constructed(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
                   lxa_dom_ref_t child, lxa_dom_ref_t before)
{
    lxa_status_t status = resolve_template_location(tree, &parent, &before);
    if (status != LXA_OK) return status;
    return before.handle == LXA_HANDLE_NONE
        ? lxa_dom_nodes_append_constructed_child(tree->nodes, parent, child)
        : lxa_dom_nodes_insert_constructed_before(tree->nodes, parent, child,
                                                   before);
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
    return lxa_html_tree_formatting_tag(name) || named(name, "span")
           || named(name, "ins") || named(name, "slot")
           || named(name, "label")
           || named(name, "abbr") || named(name, "cite")
           || named(name, "data") || named(name, "dfn")
           || named(name, "mark") || named(name, "q")
           || named(name, "time")
           || named(name, "acronym") || named(name, "var")
           || named(name, "sub") || named(name, "sup")
           || named(name, "del")
           || named(name, "samp") || named(name, "kbd");
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
    return lxa_html_foreign_annotation_encoding(value, NULL, NULL, html);
}

static lxa_status_t
element_foreign_context(lxa_html_tree_t *tree, lxa_dom_ref_t element,
                        lxa_dom_namespace_t ns, lxa_span_t name,
                        lxa_html_foreign_context_t *context)
{
    bool html = false;
    lxa_status_t status = LXA_OK;
    if (ns == LXA_DOM_NS_MATHML && named(name, "annotation-xml"))
        status = annotation_html_integration(tree, element, &html);
    if (status == LXA_OK) *context = lxa_html_foreign_context(ns, name, html);
    return status;
}

static lxa_status_t
foreign_breakout_boundary(lxa_html_tree_t *tree, lxa_dom_ref_t element,
                          bool *boundary)
{
    lxa_dom_namespace_t ns;
    lxa_span_t name;
    lxa_status_t status = lxa_dom_nodes_element_namespace(
        tree->nodes, element, &ns);
    *boundary = false;
    if (status != LXA_OK) return status;
    if (ns == LXA_DOM_NS_HTML) {
        *boundary = true;
        return LXA_OK;
    }
    status = lxa_dom_nodes_element_name(tree->nodes, element, &name);
    if (status != LXA_OK) return status;
    if (ns != LXA_DOM_NS_SVG && ns != LXA_DOM_NS_MATHML)
        return LXA_ERROR_INTERNAL;
    lxa_html_foreign_context_t context;
    status = element_foreign_context(tree, element, ns, name, &context);
    if (status == LXA_OK) *boundary = lxa_html_foreign_integration(context);
    return status;
}

static bool
select_container(lxa_span_t name)
{
    return named(name, "select") || named(name, "option")
        || named(name, "optgroup");
}

static bool
simple_container(lxa_span_t name)
{
    return block_container(name) || inline_container(name)
           || named(name, "p") || named(name, "li")
           || named(name, "dt") || named(name, "dd")
           || named(name, "noscript")
           || select_container(name)
           || lxa_html_tree_heading(name);
}

static bool
ruby_annotation(lxa_span_t name)
{
    return named(name, "rb") || named(name, "rtc")
        || named(name, "rt") || named(name, "rp");
}

static bool
container_reconstructs(lxa_span_t name)
{
    return !ruby_annotation(name)
        && (inline_container(name) || !simple_container(name)
           || named(name, "noscript") || named(name, "button")
           || select_container(name));
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
head_void(lxa_span_t name)
{
    return named(name, "base") || named(name, "basefont")
           || named(name, "bgsound") || named(name, "link")
           || named(name, "meta");
}

static bool
body_void(lxa_span_t name)
{
    return named(name, "br") || named(name, "img")
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
        if (!named(key, "type")) continue;
        *result = lxa_html_tree_ascii_case_equals(value, hidden, sizeof(hidden) - 1);
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
        || named(name, "head") || named(name, "body") || named(name, "span")
        || named(name, "br") || named(name, "img")
        || named(name, "embed") || named(name, "meta")
        || named(name, "hr") || named(name, "ul")
        || named(name, "ol") || named(name, "menu")
        || named(name, "li") || named(name, "ruby")
        || named(name, "pre") || named(name, "listing")
        || named(name, "blockquote") || named(name, "center")
        || named(name, "dl") || named(name, "dt")
        || named(name, "dd") || named(name, "table")
        || lxa_html_tree_heading(name) || named(name, "b")
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
        || lxa_html_tree_formatting_marker_tag(name)
        || ruby_annotation(name) || named(name, "ruby")
        || named(name, "bdi") || named(name, "bdo")
        || named(name, "datalist") || named(name, "output")
        || named(name, "progress") || named(name, "meter")
        || named(name, "picture")
        || named(name, "video") || named(name, "audio")
        || named(name, "map") || named(name, "canvas")
        || named(name, "legend") || named(name, "isindex")
        || named(name, "blink") || named(name, "menuitem")
        || named(name, "multicol")
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
    for (size_t i = 0; i < data.length; ++i)
        if (!lxa_html_byte_tree_space(data.data[i])) return false;
    return true;
}

static bool
null_only(lxa_span_t data)
{
    for (size_t i = 0; i < data.length; ++i)
        if (data.data[i] != 0) return false;
    return true;
}

static bool
null_or_space_only(lxa_span_t data)
{
    size_t i;
    for (i = 0; i < data.length; ++i) {
        uint8_t c = data.data[i];
        if (!lxa_html_byte_tree_space(c) && c != 0)
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
    status = lxa_dom_nodes_append_parsed_html_element(tree->nodes, literal(name), &node);
    if (status == LXA_OK)
        status = insert_constructed(tree, parent,
            node, (lxa_dom_ref_t){0});
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
    bool implicit = tree->body.handle == LXA_HANDLE_NONE;
    lxa_status_t status = ensure_head(tree);
    if (status == LXA_OK)
        status = ensure_element(tree, tree->html, "body", &tree->body);
    if (status == LXA_OK && implicit) tree->frameset_ok = true;
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

static size_t template_boundary(const lxa_html_tree_t *);
static lxa_dom_ref_t template_parent(const lxa_html_tree_t *);
static lxa_status_t template_pop_elements(lxa_html_tree_t *, size_t);
static lxa_status_t template_foster_location(lxa_html_tree_t *,
                                             lxa_dom_ref_t *, lxa_dom_ref_t *);

static lxa_vector_t *
formatting_elements(lxa_html_tree_t *tree)
{
    return tree->mode == LXA_TREE_TEMPLATE_TEXT
        ? &tree->template_elements : &tree->open_elements;
}

static size_t
formatting_boundary(const lxa_html_tree_t *tree)
{
    return tree->mode == LXA_TREE_TEMPLATE_TEXT ? template_boundary(tree) : 0;
}

static lxa_dom_ref_t
formatting_parent(const lxa_html_tree_t *tree)
{
    /* The root stack omits html; closing the frameset can empty it. */
    if (tree->mode == LXA_TREE_AFTER_AFTER_FRAMESET
        && lxa_vector_length(&tree->open_elements) == 0)
        return tree->html;
    return tree->mode == LXA_TREE_TEMPLATE_TEXT
        ? template_parent(tree) : current_element(tree);
}

static lxa_status_t
push_open_reference(lxa_html_tree_t *tree, lxa_vector_t *elements,
                     lxa_dom_ref_t element)
{
    lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_vector_push(elements, &element);
    if (status != LXA_OK)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static lxa_status_t
push_element(lxa_html_tree_t *tree, lxa_dom_ref_t element)
{
    return push_open_reference(tree, &tree->open_elements, element);
}

static lxa_status_t
clear_formatting_to_marker(lxa_html_tree_t *tree)
{
    const lxa_html_formatting_entry_t *entries =
        (const lxa_html_formatting_entry_t *) tree->formatting.bytes.data;
    size_t marker = tree->formatting_marker;
    size_t length = lxa_vector_length(&tree->formatting), previous;
    size_t start = marker == SIZE_MAX ? 0 : marker;
    lxa_status_t status;
    if (marker != SIZE_MAX && marker >= length) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    previous = marker == SIZE_MAX ? SIZE_MAX : entries[marker].previous_marker;
    size_t count = length - start;
    if ((previous != SIZE_MAX && previous >= marker)
        || count > tree->nodes->budget->used[LXA_BUDGET_ACTIVE_FORMATTING]) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    status = lxa_vector_erase(&tree->formatting, start, count);
    if (status == LXA_OK)
        status = lxa_budget_release(tree->nodes->budget,
                                   LXA_BUDGET_ACTIVE_FORMATTING, count);
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    tree->formatting_marker = previous;
    return LXA_OK;
}

static lxa_status_t
remove_open_element(lxa_html_tree_t *tree, lxa_vector_t *elements, size_t index)
{
    lxa_dom_record_t record;
    lxa_dom_ref_t ref;
    lxa_status_t status;
    if (index >= lxa_vector_length(elements)
        || tree->nodes->budget->used[LXA_BUDGET_OPEN_ELEMENTS] == 0) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    ref = ((const lxa_dom_ref_t *) elements->bytes.data)[index];
    status = lxa_dom_nodes_read(tree->nodes, ref, &record);
    if (status == LXA_OK && (record.flags & LXA_DOM_OPTION_SELECTED) != 0)
        status = lxa_dom_nodes_maybe_clone_selected_option(tree->nodes, ref);
    if (status != LXA_OK) return status;
    status = lxa_vector_erase(elements, index, 1);
    if (status == LXA_OK)
        status = lxa_budget_release(tree->nodes->budget,
                                   LXA_BUDGET_OPEN_ELEMENTS, 1);
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
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

static lxa_status_t
pop_table_elements(lxa_html_tree_t *tree, size_t keep)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->open_elements), closures = 0;
    /* Table recovery closes cells/captions, not other popped marker owners. */
    if (tree->formatting_marker != SIZE_MAX) {
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, length - keep, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        for (size_t i = keep; i < length; ++i) {
            lxa_span_t name;
            lxa_dom_namespace_t ns;
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
            if (status != LXA_OK) return status;
            if (ns == LXA_DOM_NS_HTML
                && (named(name, "td") || named(name, "th")
                    || named(name, "caption")))
                ++closures;
        }
    }
    while (lxa_vector_length(&tree->open_elements) > keep) {
        lxa_status_t status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    while (closures != 0) {
        lxa_status_t status = clear_formatting_to_marker(tree);
        if (status != LXA_OK) return status;
        --closures;
    }
    return LXA_OK;
}

static lxa_status_t find_open_reference(lxa_html_tree_t *,
    const lxa_vector_t *, size_t, lxa_dom_ref_t, size_t *);

static lxa_status_t
reference_is_open(lxa_html_tree_t *tree, lxa_dom_ref_t ref, bool *found)
{
    size_t index;
    lxa_vector_t *elements = formatting_elements(tree);
    lxa_status_t status = find_open_reference(tree, elements,
                                             formatting_boundary(tree),
                                             ref, &index);
    if (status == LXA_OK)
        *found = index != lxa_vector_length(elements);
    return status;
}

static lxa_status_t find_scoped_reference(lxa_html_tree_t *,
    const lxa_vector_t *, size_t, lxa_dom_ref_t, size_t *);

static lxa_status_t reserve_open_elements(lxa_html_tree_t *,
                                           size_t);
static lxa_status_t foster_location(lxa_html_tree_t *,
                                     lxa_dom_ref_t *, lxa_dom_ref_t *);

static lxa_status_t
root_insertion_location(lxa_html_tree_t *tree, lxa_dom_ref_t target,
                         lxa_dom_ref_t *parent, lxa_dom_ref_t *before)
{
    lxa_span_t name;
    lxa_dom_namespace_t ns;
    lxa_status_t status = lxa_dom_nodes_element_namespace(tree->nodes, target,
                                                          &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, target, &name);
    if (status != LXA_OK) return status;
    *parent = target;
    *before = (lxa_dom_ref_t){0};
    return ns == LXA_DOM_NS_HTML && table_context(name)
           ? foster_location(tree, parent, before) : LXA_OK;
}

static lxa_status_t
formatting_insertion_location(lxa_html_tree_t *tree, lxa_dom_ref_t target,
                               lxa_dom_ref_t *parent, lxa_dom_ref_t *before)
{
    lxa_span_t name;
    lxa_dom_namespace_t ns;
    lxa_status_t status;
    if (tree->mode != LXA_TREE_TEMPLATE_TEXT)
        return root_insertion_location(tree, target, parent, before);
    *parent = target;
    *before = (lxa_dom_ref_t){0};
    if (target.handle == tree->template_fragment.handle) return LXA_OK;
    status = lxa_dom_nodes_element_namespace(tree->nodes, target, &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, target, &name);
    if (status != LXA_OK) return status;
    return ns == LXA_DOM_NS_HTML && table_context(name)
        ? template_foster_location(tree, parent, before) : LXA_OK;
}

static lxa_status_t
formatting_pop_elements(lxa_html_tree_t *tree, size_t keep)
{
    return pop_elements(tree, formatting_elements(tree), keep);
}

static lxa_status_t
reconstruct_element_at(lxa_html_tree_t *tree, lxa_vector_t *elements,
                        lxa_dom_ref_t old, lxa_dom_ref_t parent,
                        lxa_dom_ref_t before, lxa_dom_ref_t *out)
{
    lxa_dom_ref_t clone;
    lxa_status_t status = lxa_dom_nodes_clone_element(tree->nodes, old, &clone);
    if (status == LXA_OK)
        status = insert_constructed(tree, parent, clone, before);
    if (status == LXA_OK) status = push_open_reference(tree, elements, clone);
    if (status == LXA_OK) *out = clone;
    return status;
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

static lxa_html_formatting_entry_t *
formatting_entries(lxa_html_tree_t *tree)
{
    return (lxa_html_formatting_entry_t *) tree->formatting.bytes.data;
}

static size_t
formatting_start(const lxa_html_tree_t *tree)
{
    return tree->formatting_marker == SIZE_MAX
           ? 0 : tree->formatting_marker + 1;
}

static lxa_status_t
remove_formatting(lxa_html_tree_t *tree, size_t index)
{
    lxa_status_t status;
    if (index >= lxa_vector_length(&tree->formatting)
        || (tree->formatting_marker != SIZE_MAX
            && index <= tree->formatting_marker)
        || tree->nodes->budget->used[LXA_BUDGET_ACTIVE_FORMATTING] == 0) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    /* Entries removed here follow the last marker, so marker indices stay put. */
    status = lxa_vector_erase(&tree->formatting, index, 1);
    if (status == LXA_OK)
        status = lxa_budget_release(tree->nodes->budget,
                                   LXA_BUDGET_ACTIVE_FORMATTING, 1);
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return LXA_OK;
}

static lxa_status_t
find_formatting(lxa_html_tree_t *tree, lxa_span_t target, size_t *index)
{
    size_t i = lxa_vector_length(&tree->formatting);
    *index = i;
    while (i > formatting_start(tree)) {
        lxa_span_t name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                formatting_entries(tree)[--i].element, &name);
        if (status != LXA_OK) return status;
        if (name.length == target.length
            && memcmp(name.data, target.data, name.length) == 0) {
            *index = i;
            break;
        }
    }
    return LXA_OK;
}

static lxa_status_t
find_formatting_reference(lxa_html_tree_t *tree, lxa_dom_ref_t ref,
                           size_t *index)
{
    size_t i = lxa_vector_length(&tree->formatting);
    *index = i;
    while (i > formatting_start(tree)) {
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (formatting_entries(tree)[--i].element.handle == ref.handle) {
            *index = i;
            break;
        }
    }
    return LXA_OK;
}

static lxa_status_t
reserve_formatting(lxa_html_tree_t *tree, size_t count)
{
    size_t required;
    lxa_status_t status = lxa_budget_check(tree->nodes->budget,
        LXA_BUDGET_ACTIVE_FORMATTING, count, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&tree->formatting),
                              count, &required);
    if (status == LXA_OK)
        status = lxa_vector_reserve(&tree->formatting, required);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static lxa_status_t
append_formatting(lxa_html_tree_t *tree, lxa_dom_ref_t ref, bool marker)
{
    lxa_html_formatting_entry_t entry = {
        .element = ref, .previous_marker = tree->formatting_marker
    };
    size_t index = lxa_vector_length(&tree->formatting);
    lxa_status_t status;
    if (entry.previous_marker != SIZE_MAX && entry.previous_marker >= index) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_ACTIVE_FORMATTING, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK) {
        status = lxa_vector_push(&tree->formatting, &entry);
        if (status != LXA_OK)
            lxa_budget_release(tree->nodes->budget,
                                LXA_BUDGET_ACTIVE_FORMATTING, 1);
    }
    if (status == LXA_OK && marker) tree->formatting_marker = index;
    return status;
}

static lxa_status_t
formatting_token_equal(lxa_html_tree_t *tree, lxa_dom_ref_t element,
                       const lxa_html_token_t *token, bool *equal)
{
    lxa_span_t name;
    lxa_dom_ref_t attribute = {0};
    size_t count = 0;
    lxa_status_t status = lxa_dom_nodes_element_name(tree->nodes, element,
                                                     &name);
    *equal = false;
    if (status != LXA_OK) return status;
    if (name.length != token->name.length
        || memcmp(name.data, token->name.data, name.length) != 0)
        return LXA_OK;
    for (;;) {
        lxa_span_t key, value;
        bool matched = false;
        status = lxa_dom_nodes_attribute_next(tree->nodes, element, attribute,
                                               &attribute);
        if (status != LXA_OK) return status;
        if (attribute.handle == LXA_HANDLE_NONE) break;
        if (count == token->attribute_count) return LXA_OK;
        ++count;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                   &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_attribute_read(tree->nodes, attribute,
                                                   &key, &value);
        if (status != LXA_OK) return status;
        for (size_t i = 0; i < token->attribute_count; ++i) {
            const lxa_html_attribute_t *candidate = &token->attributes[i];
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (key.length != candidate->name.length) continue;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                key.length / 8 + (key.length % 8 != 0),
                &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (memcmp(key.data, candidate->name.data, key.length) != 0)
                continue;
            if (value.length != candidate->value.length) return LXA_OK;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                value.length / 8 + (value.length % 8 != 0),
                &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (value.length != 0
                && memcmp(value.data, candidate->value.data, value.length) != 0)
                return LXA_OK;
            matched = true;
            break;
        }
        if (!matched) return LXA_OK;
    }
    *equal = count == token->attribute_count;
    return LXA_OK;
}

static lxa_status_t
plan_formatting_add(lxa_html_tree_t *tree, const lxa_html_token_t *token,
                     size_t *drop)
{
    size_t i = lxa_vector_length(&tree->formatting), matches = 0;
    *drop = SIZE_MAX;
    while (i > formatting_start(tree)) {
        bool equal;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = formatting_token_equal(tree,
                formatting_entries(tree)[--i].element, token, &equal);
        if (status != LXA_OK) return status;
        if (equal && ++matches >= 3) *drop = i;
    }
    return reserve_formatting(tree, *drop == SIZE_MAX ? 1 : 0);
}

static lxa_status_t
reserve_formatting_elements(lxa_html_tree_t *tree, size_t count)
{
    size_t required, depth;
    lxa_status_t status;
    if (tree->mode != LXA_TREE_TEMPLATE_TEXT)
        return reserve_open_elements(tree, count);
    status = lxa_size_add(lxa_vector_length(&tree->template_elements), count,
                          &required);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&tree->template_stack), 1,
                              &depth);
    if (status == LXA_OK) status = lxa_size_mul(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, required, &depth);
    if (status == LXA_OK && tree->template_return_mode == LXA_TREE_IN_BODY) {
        size_t root = lxa_vector_length(&tree->open_elements);
        if (root == 0) return LXA_ERROR_INTERNAL;
        status = lxa_size_add(depth, root - 1, &depth);
    }
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_TREE_DEPTH,
                                   depth, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                                   count, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_vector_reserve(&tree->template_elements, required);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static lxa_status_t
reconstruct_formatting(lxa_html_tree_t *tree)
{
    size_t start = formatting_start(tree);
    size_t i = lxa_vector_length(&tree->formatting), end = i;
    lxa_status_t status;
    while (i > start) {
        bool open;
        status = reference_is_open(tree, formatting_entries(tree)[i - 1].element,
                                    &open);
        if (status != LXA_OK) return status;
        if (open) break;
        --i;
    }
    if (i == end) return LXA_OK;
    status = reserve_formatting_elements(tree, end - i);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   end - i, &tree->nodes->limit_reason);
    while (status == LXA_OK && i < end) {
        lxa_dom_ref_t clone, parent, before;
        status = formatting_insertion_location(tree, formatting_parent(tree),
                                                &parent, &before);
        if (status == LXA_OK)
            status = reconstruct_element_at(tree, formatting_elements(tree),
                formatting_entries(tree)[i].element, parent, before, &clone);
        if (status == LXA_OK) formatting_entries(tree)[i++].element = clone;
    }
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
        if (lxa_html_tree_special_element(name, ns)
            && !(ns == LXA_DOM_NS_HTML
                 && (named(name, "address") || named(name, "div")
                     || named(name, "p"))))
            break;
    }
    return LXA_OK;
}

static lxa_status_t find_scoped_end_target(lxa_html_tree_t *,
    const lxa_vector_t *, size_t, lxa_span_t, lxa_html_scope_kind_t, size_t *);

/* A block or <hr> closes a paragraph even when inline descendants are open.
 * The search stops at the nearest scope boundary; failed work preflight
 * leaves the open stack untouched. */
static lxa_status_t
close_paragraph(lxa_html_tree_t *tree, bool *closed)
{
    size_t i;
    if (closed != NULL) *closed = false;
    lxa_status_t status = find_scoped_end_target(tree, &tree->open_elements,
        0, literal("p"), LXA_HTML_SCOPE_BUTTON, &i);
    if (status != LXA_OK || i == lxa_vector_length(&tree->open_elements))
        return status;
    while (lxa_vector_length(&tree->open_elements) > i) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    if (closed != NULL) *closed = true;
    return LXA_OK;
}

static lxa_status_t find_ordinary_end_target(lxa_html_tree_t *,
    const lxa_vector_t *, size_t, lxa_span_t, size_t *);

static lxa_status_t current_html_column_group(lxa_html_tree_t *,
    const lxa_vector_t *, size_t, bool *);
static lxa_status_t plan_ordinary_end_target(lxa_html_tree_t *,
    const lxa_vector_t *, size_t, lxa_span_t, size_t *);

static lxa_status_t
end_ordinary_element(lxa_html_tree_t *tree, lxa_span_t target)
{
    size_t index;
    lxa_status_t status = plan_ordinary_end_target(tree, &tree->open_elements,
                                                  0, target, &index);
    if (status != LXA_OK) return status;
    while (lxa_vector_length(&tree->open_elements) > index) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

typedef struct {
    lxa_html_tree_t *tree;
    const lxa_dom_ref_t *entries;
    size_t length;
} lxa_html_tree_scope_view_t;

static inline lxa_status_t
tree_scope_read(const lxa_html_tree_scope_view_t *view, size_t index,
                lxa_span_t *name, lxa_dom_namespace_t *ns)
{
    lxa_status_t status = lxa_dom_nodes_element_namespace(
        view->tree->nodes, view->entries[index], ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(
            view->tree->nodes, view->entries[index], name);
    return status;
}

static inline lxa_status_t
tree_scope_identity(const lxa_html_tree_scope_view_t *view, size_t index,
                     uint64_t *identity)
{
    *identity = view->entries[index].handle;
    return LXA_OK;
}

#define LXA_SCOPE_NAME tree_scope_search
#define LXA_ORDINARY_NAME tree_ordinary_end_search
#define LXA_COLUMN_GROUP_NAME tree_current_column_group
#define LXA_SCOPE_PLAN_NAME tree_scope_plan
#define LXA_ORDINARY_PLAN_NAME tree_ordinary_end_plan
#define LXA_OPEN_REFERENCE_NAME tree_open_reference_search
#define LXA_SCOPED_REFERENCE_NAME tree_scoped_reference_search
#define LXA_SCOPE_IDENTITY tree_scope_identity
#define LXA_SCOPE_VIEW lxa_html_tree_scope_view_t
#define LXA_SCOPE_LENGTH(view) ((view)->length)
#define LXA_SCOPE_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_SCOPE_REASON(view) (&(view)->tree->nodes->limit_reason)
#define LXA_SCOPE_READ tree_scope_read
#include "html/end_target_find.inc"
#undef LXA_SCOPE_READ
#undef LXA_SCOPE_REASON
#undef LXA_SCOPE_BUDGET
#undef LXA_SCOPE_LENGTH
#undef LXA_SCOPE_VIEW
#undef LXA_SCOPE_NAME
#undef LXA_ORDINARY_NAME
#undef LXA_COLUMN_GROUP_NAME
#undef LXA_SCOPE_PLAN_NAME
#undef LXA_ORDINARY_PLAN_NAME
#undef LXA_OPEN_REFERENCE_NAME
#undef LXA_SCOPED_REFERENCE_NAME
#undef LXA_SCOPE_IDENTITY

static lxa_status_t
find_open_reference(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                     size_t boundary, lxa_dom_ref_t ref, size_t *index)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_open_reference_search(&view, boundary, ref.handle, index);
}

static lxa_status_t
find_scoped_reference(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                       size_t boundary, lxa_dom_ref_t ref, size_t *index)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_scoped_reference_search(&view, boundary, ref.handle, index);
}

static lxa_status_t
current_html_column_group(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                          size_t boundary, bool *column_group)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_current_column_group(&view, boundary, column_group);
}

static lxa_status_t
plan_ordinary_end_target(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                         size_t boundary, lxa_span_t target, size_t *index)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    lxa_html_end_target_plan_t plan;
    lxa_status_t status = tree_ordinary_end_plan(&view, boundary, target, &plan);
    if (status == LXA_OK) *index = plan.index;
    return status;
}

/* Any other end tag searches to a matching HTML element or a special element. */
static lxa_status_t
find_ordinary_end_target(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                         size_t boundary, lxa_span_t target, size_t *index)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_ordinary_end_search(&view, boundary, target, index);
}

static lxa_status_t
find_scoped_end_target(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                       size_t boundary, lxa_span_t target, lxa_html_scope_kind_t scope,
                       size_t *index)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_scope_search(&view, boundary, target, scope, index);
}

static lxa_status_t
plan_ruby_start(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                 size_t boundary, lxa_span_t target, size_t *keep)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) elements->bytes.data;
    size_t index;
    bool except_rtc = named(target, "rp") || named(target, "rt");
    lxa_status_t status = find_scoped_end_target(tree, elements, boundary,
        (lxa_span_t){(const unsigned char *) "ruby", 4}, LXA_HTML_SCOPE_NORMAL, &index);
    *keep = lxa_vector_length(elements);
    if (status != LXA_OK || index == *keep) return status;
    while (*keep > index + 1) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                    1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes,
                open[*keep - 1], &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                open[*keep - 1], &name);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML || !lxa_html_tree_implied_end_tag(name)
            || (except_rtc && named(name, "rtc")))
            break;
        --*keep;
    }
    return LXA_OK;
}

/* Close an HTML element through descendants up to the requested scope. */
static lxa_status_t
end_scoped_element(lxa_html_tree_t *tree, lxa_span_t target,
                   lxa_html_scope_kind_t scope)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) tree->open_elements.bytes.data,
        lxa_vector_length(&tree->open_elements)
    };
    lxa_html_end_target_plan_t plan;
    lxa_status_t status = tree_scope_plan(&view, 0, target, scope, &plan);
    if (status != LXA_OK) return status;
    if (plan.index == plan.search_length) {
        if (plan.search_length != view.length) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        return LXA_OK;
    }
    if (scope == LXA_HTML_SCOPE_TABLE)
        return pop_table_elements(tree, plan.index);
    while (lxa_vector_length(&tree->open_elements) > plan.index) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    return lxa_html_tree_formatting_marker_tag(target) ? clear_formatting_to_marker(tree)
                                        : LXA_OK;
}

static lxa_status_t
close_current_heading(lxa_html_tree_t *tree)
{
    lxa_dom_ref_t current = current_element(tree);
    lxa_dom_namespace_t ns;
    lxa_span_t name;
    lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_namespace(tree->nodes, current, &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, current, &name);
    if (status != LXA_OK) return status;
    if (ns == LXA_DOM_NS_HTML && lxa_html_tree_heading(name)) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

static lxa_status_t
close_open_button(lxa_html_tree_t *tree)
{
    size_t i;
    lxa_status_t status = find_scoped_end_target(tree, &tree->open_elements,
        0, literal("button"), LXA_HTML_SCOPE_NORMAL, &i);
    if (status != LXA_OK || i == lxa_vector_length(&tree->open_elements))
        return status;
    while (lxa_vector_length(&tree->open_elements) > i) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

static lxa_status_t
insert_text(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
            lxa_dom_ref_t before, lxa_span_t bytes)
{
    lxa_dom_ref_t previous, child, actual;
    lxa_dom_record_t record;
    lxa_status_t status;
    if (bytes.length == 0) return LXA_OK;
    status = resolve_template_location(tree, &parent, &before);
    if (status == LXA_OK)
        status = lxa_dom_nodes_last_child(tree->nodes, parent, &previous);
    if (status == LXA_OK && before.handle != LXA_HANDLE_NONE) {
        status = lxa_dom_nodes_parent(tree->nodes, before, &actual);
        if (status == LXA_OK && actual.handle != parent.handle)
            return LXA_ERROR_ARGUMENT;
        if (status == LXA_OK)
            status = lxa_dom_nodes_prev_sibling(tree->nodes, before, &previous);
    }
    if (status != LXA_OK) return status;
    if (previous.handle != LXA_HANDLE_NONE) {
        status = lxa_dom_nodes_read(tree->nodes, previous, &record);
        if (status != LXA_OK) return status;
        if (record.parent != (lxa_id_t) parent.handle
            || record.next != (before.handle == LXA_HANDLE_NONE
                              ? LXA_ID_NONE : (lxa_id_t) before.handle))
            return LXA_ERROR_ARGUMENT;
        if (record.kind == LXA_DOM_KIND_TEXT)
            return lxa_dom_nodes_append_text_data(tree->nodes, previous, bytes);
    }
    status = lxa_dom_nodes_append_character(
        tree->nodes, LXA_DOM_KIND_TEXT, bytes, &child);
    if (status != LXA_OK) return status;
    return insert_constructed(tree, parent,
        child, before);
}

static lxa_status_t
append_character(lxa_html_tree_t *tree,
                 lxa_dom_ref_t parent, lxa_dom_kind_t kind, lxa_span_t bytes)
{
    lxa_dom_ref_t child;
    lxa_status_t status;
    if (kind == LXA_DOM_KIND_TEXT)
        return insert_text(tree, parent, (lxa_dom_ref_t){0}, bytes);
    status = lxa_dom_nodes_append_character(tree->nodes, kind, bytes, &child);
    return status == LXA_OK
           ? insert_constructed(tree, parent,
               child, (lxa_dom_ref_t){0})
           : status;
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
           ? insert_constructed(tree, parent,
               child, (lxa_dom_ref_t){0})
           : status;
}

static lxa_status_t
prepare_prebody_text(lxa_html_tree_t *tree, lxa_span_t *bytes)
{
    size_t prefix = 0;
    lxa_status_t status;
    bool before_head = tree->mode == LXA_TREE_BEFORE_HTML
                       || tree->mode == LXA_TREE_BEFORE_HEAD;
    while (prefix < bytes->length
           && lxa_html_byte_tree_space(bytes->data[prefix]))
        ++prefix;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
        prefix + (prefix < bytes->length), &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (!before_head && prefix != 0) {
        status = append_character(tree,
            tree->mode == LXA_TREE_IN_HEAD ? tree->head : tree->html,
            LXA_DOM_KIND_TEXT, (lxa_span_t){bytes->data, prefix});
        if (status != LXA_OK) return status;
    }
    if (prefix != 0) bytes->data += prefix;
    bytes->length -= prefix;
    if (bytes->length == 0) return LXA_OK;
    if (before_head) {
        status = ensure_head(tree);
        if (status != LXA_OK) return status;
    }
    tree->mode = LXA_TREE_AFTER_HEAD;
    return LXA_OK;
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
formatting_end_ordinary(lxa_html_tree_t *tree, lxa_span_t subject)
{
    lxa_vector_t *elements = formatting_elements(tree);
    size_t index;
    lxa_status_t status = find_ordinary_end_target(tree, elements,
        formatting_boundary(tree), subject, &index);
    if (status == LXA_OK) {
        status = formatting_pop_elements(tree, index);
        if (status != LXA_OK) return status;
    }
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
        status = lxa_dom_nodes_detach_constructed_child(tree->nodes, node);
    if (status == LXA_OK)
        status = lxa_dom_nodes_parent(tree->nodes, node, &current);
    if (status != LXA_OK || current.handle != LXA_HANDLE_NONE) return status;
    status = resolve_template_location(tree, &parent, &before);
    if (status != LXA_OK) return status;
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
adopt_formatting(lxa_html_tree_t *tree, lxa_span_t subject)
{
    lxa_status_t status;
    lxa_dom_namespace_t ns;
    lxa_span_t name;
    size_t index;
    lxa_vector_t *elements = formatting_elements(tree);
    size_t boundary = formatting_boundary(tree);
    if (lxa_vector_length(elements) > boundary) {
        lxa_dom_ref_t current = formatting_parent(tree);
        status = lxa_dom_nodes_element_namespace(tree->nodes, current, &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, current, &name);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML && name.length == subject.length
            && memcmp(name.data, subject.data, name.length) == 0) {
            status = find_formatting_reference(tree, current, &index);
            if (status != LXA_OK) return status;
            if (index == lxa_vector_length(&tree->formatting)) {
                status = formatting_pop_elements(tree, lxa_vector_length(elements) - 1);
                if (status != LXA_OK) return status;
                return LXA_OK;
            }
        }
    }
    for (unsigned int outer = 0; outer < 8; ++outer) {
        lxa_dom_ref_t formatting, furthest, common, last, clone;
        const lxa_dom_ref_t *open;
        size_t formatting_index, stack_index, furthest_index, bookmark, node;
        size_t scoped_index;
        status = find_formatting(tree, subject, &formatting_index);
        if (status != LXA_OK) return status;
        if (formatting_index == lxa_vector_length(&tree->formatting))
            return formatting_end_ordinary(tree, subject);
        formatting = formatting_entries(tree)[formatting_index].element;
        status = find_open_reference(tree, elements, boundary,
                                     formatting, &stack_index);
        if (status != LXA_OK) return status;
        if (stack_index == lxa_vector_length(elements)) {
            return remove_formatting(tree, formatting_index);
        }
        status = find_scoped_reference(tree, elements, boundary,
                                         formatting, &scoped_index);
        if (status != LXA_OK || scoped_index == lxa_vector_length(elements))
            return status;
        open = (const lxa_dom_ref_t *) elements->bytes.data;
        furthest_index = stack_index + 1;
        while (furthest_index < lxa_vector_length(elements)) {
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                       1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_namespace(tree->nodes,
                    open[furthest_index], &ns);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[furthest_index], &name);
            if (status != LXA_OK) return status;
            if (lxa_html_tree_special_element(name, ns)) break;
            ++furthest_index;
        }
        if (furthest_index == lxa_vector_length(elements)) {
            status = formatting_pop_elements(tree, stack_index);
            if (status != LXA_OK) return status;
            return remove_formatting(tree, formatting_index);
        }
        if (stack_index == boundary && tree->mode != LXA_TREE_TEMPLATE_TEXT)
            return LXA_ERROR_INTERNAL;
        common = stack_index == boundary ? tree->template_fragment
                                         : open[stack_index - 1];
        furthest = open[furthest_index];
        last = furthest;
        bookmark = formatting_index;
        node = furthest_index;
        for (size_t inner = 1; ; ++inner) {
            lxa_dom_ref_t ref;
            size_t entry;
            open = (const lxa_dom_ref_t *) elements->bytes.data;
            ref = open[--node];
            if (ref.handle == formatting.handle) break;
            status = find_formatting_reference(tree, ref, &entry);
            if (status != LXA_OK) return status;
            if (inner > 3 && entry != lxa_vector_length(&tree->formatting)) {
                status = remove_formatting(tree, entry);
                if (status != LXA_OK) return status;
                if (entry < bookmark) --bookmark;
                entry = lxa_vector_length(&tree->formatting);
            }
            if (entry == lxa_vector_length(&tree->formatting)) {
                status = remove_open_element(tree, elements, node);
                if (status != LXA_OK) return status;
                --furthest_index;
                continue;
            }
            status = lxa_dom_nodes_clone_element(tree->nodes, ref, &clone);
            /* Construction attaches leaves; attach replacements before moving
             * subtrees so failures retain valid parent/sibling links. */
            if (status == LXA_OK)
                status = insert_constructed(tree, common,
                    clone, (lxa_dom_ref_t){0});
            if (status != LXA_OK) return status;
            formatting_entries(tree)[entry].element = clone;
            ((lxa_dom_ref_t *) elements->bytes.data)[node] = clone;
            if (last.handle == furthest.handle) bookmark = entry + 1;
            status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                               clone, last);
            if (status != LXA_OK) return status;
            last = clone;
        }
        {
            lxa_dom_ref_t parent, before;
            status = formatting_insertion_location(tree, common, &parent, &before);
            if (status == LXA_OK)
                status = place_adoption_node(tree, last, parent, before);
        }
        if (status == LXA_OK)
            status = lxa_dom_nodes_clone_element(tree->nodes, formatting, &clone);
        if (status == LXA_OK)
            status = insert_constructed(tree, furthest,
                clone, (lxa_dom_ref_t){0});
        if (status != LXA_OK) return status;
        for (;;) {
            lxa_dom_ref_t child;
            status = lxa_dom_nodes_first_child(tree->nodes, furthest, &child);
            if (status != LXA_OK) return status;
            if (child.handle == clone.handle) break;
            if (child.handle == LXA_HANDLE_NONE) return LXA_ERROR_INTERNAL;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                       1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_reparent_constructed_child(tree->nodes,
                                                                   clone, child);
            if (status != LXA_OK) return status;
        }
        status = find_formatting_reference(tree, formatting, &formatting_index);
        if (status != LXA_OK) return status;
        if (formatting_index == lxa_vector_length(&tree->formatting))
            return LXA_ERROR_INTERNAL;
        if (formatting_index < bookmark) --bookmark;
        status = remove_formatting(tree, formatting_index);
        if (status != LXA_OK) return status;
        {
            lxa_html_formatting_entry_t entry = {.element = clone};
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_ACTIVE_FORMATTING, 1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = lxa_vector_insert(&tree->formatting, bookmark, &entry);
            if (status != LXA_OK) {
                lxa_budget_release(tree->nodes->budget,
                                    LXA_BUDGET_ACTIVE_FORMATTING, 1);
                return status;
            }
        }
        if (stack_index >= lxa_vector_length(elements)
            || furthest_index <= stack_index
            || furthest_index >= lxa_vector_length(elements)
            || tree->nodes->budget->used[LXA_BUDGET_OPEN_ELEMENTS] == 0) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        status = lxa_vector_erase(elements, stack_index, 1);
        if (status != LXA_OK) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        /* Removing formatting shifts furthest up by one. Insert below it. */
        status = lxa_vector_insert(elements, furthest_index, &clone);
        if (status != LXA_OK) {
            lxa_status_t released = lxa_budget_release(tree->nodes->budget,
                LXA_BUDGET_OPEN_ELEMENTS, 1);
            if (released != LXA_OK) {
                assert(false);
                return LXA_ERROR_INTERNAL;
            }
            return status;
        }
    }
    return LXA_OK;
}

static lxa_status_t
prepare_formatting_start(lxa_html_tree_t *tree,
                         const lxa_html_token_t *token, size_t *drop)
{
    lxa_status_t status;
    lxa_vector_t *elements = formatting_elements(tree);
    size_t boundary = formatting_boundary(tree);
    if (named(token->name, "a")) {
        size_t index;
        status = find_formatting(tree, token->name, &index);
        if (status != LXA_OK) return status;
        if (index != lxa_vector_length(&tree->formatting)) {
            lxa_dom_ref_t old = formatting_entries(tree)[index].element;
            status = adopt_formatting(tree, token->name);
            if (status == LXA_OK)
                status = find_formatting_reference(tree, old, &index);
            if (status != LXA_OK) return status;
            if (index != lxa_vector_length(&tree->formatting)) {
                status = remove_formatting(tree, index);
                if (status != LXA_OK) return status;
            }
            status = find_open_reference(tree, elements, boundary,
                                          old, &index);
            if (status != LXA_OK) return status;
            if (index != lxa_vector_length(elements)) {
                status = remove_open_element(tree, elements, index);
                if (status != LXA_OK) return status;
            }
        }
    }
    if (named(token->name, "nobr")) {
        size_t index;
        status = reconstruct_formatting(tree);
        if (status == LXA_OK)
            status = find_scoped_end_target(tree, elements, boundary,
                token->name, LXA_HTML_SCOPE_NORMAL, &index);
        if (status == LXA_OK
            && index != lxa_vector_length(elements)) {
            status = adopt_formatting(tree, token->name);
            if (status == LXA_OK) status = reconstruct_formatting(tree);
        }
        return status == LXA_OK ? plan_formatting_add(tree, token, drop) : status;
    }
    status = plan_formatting_add(tree, token, drop);
    if (status == LXA_OK) status = reconstruct_formatting(tree);
    return status;
}

static lxa_status_t
flush_table_text(lxa_html_tree_t *tree)
{
    lxa_dom_ref_t table, parent;
    lxa_span_t bytes, top;
    size_t prefix = 0;
    lxa_status_t status;
    if (tree->table_text.length == 0) return LXA_OK;
    bytes.data = tree->table_text.data;
    bytes.length = tree->table_text.length;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                               bytes.length, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (null_or_space_only(bytes)) {
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
        status = reconstruct_formatting(tree);
        if (status == LXA_OK)
            status = root_insertion_location(tree, current_element(tree),
                                              &parent, &table);
        if (status != LXA_OK) return status;
        status = insert_text(tree, parent, table, bytes);
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

static lxa_status_t flush_template_table_text(lxa_html_tree_t *tree);

/* DATA nulls have tree-context semantics. Scan bounded token spans so text
 * across nulls is retained without allocating or treating NUL as a terminator. */
static lxa_status_t
insert_data_text(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
                 lxa_span_t bytes, bool foreign, bool table)
{
    static const uint8_t replacement[] = {0xef, 0xbf, 0xbd};
    size_t offset = 0;
    bool check_column_group = table && !foreign;
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
        if (check_column_group) {
            bool column_group;
            check_column_group = false;
            status = current_html_column_group(tree, formatting_elements(tree),
                formatting_boundary(tree), &column_group);
            if (status != LXA_OK) return status;
            /* Pending nonspace closes colgroup at flush. Otherwise preserve
             * whitespace there and reprocess NUL after the pop. */
            if (column_group && space_only((lxa_span_t) {
                    tree->table_text.data, tree->table_text.length
                })) {
                status = tree->mode == LXA_TREE_TEMPLATE_TEXT
                    ? flush_template_table_text(tree) : flush_table_text(tree);
                if (status == LXA_OK)
                    status = formatting_pop_elements(tree,
                        lxa_vector_length(formatting_elements(tree)) - 1);
                if (status != LXA_OK) return status;
            }
        }
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
    if (token->attribute_count == 0) return LXA_OK;
    status = lxa_dom_nodes_element_namespace(tree->nodes, element, &ns);
    if (status != LXA_OK) return status;
    for (i = 0; i < token->attribute_count; ++i) {
        lxa_dom_ref_t existing, attribute;
        lxa_html_attribute_t entry = token->attributes[i];
        lxa_dom_attr_namespace_t attr_ns = LXA_DOM_ATTR_NS_NONE;
        if (ns != LXA_DOM_NS_HTML) {
            if (named(entry.name, "xml:lang") || named(entry.name, "xml:space"))
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
            ? lxa_dom_nodes_append_parsed_html_attribute(tree->nodes, entry.name,
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
connect_selectedcontent(lxa_html_tree_t *tree, lxa_dom_ref_t element)
{
    if (tree->mode != LXA_TREE_TEMPLATE_TEXT)
        return lxa_dom_nodes_connect_selectedcontent(tree->nodes, element);
    if (tree->template_patch.target.handle == LXA_HANDLE_NONE) return LXA_OK;
    return lxa_dom_nodes_maybe_connect_selectedcontent(tree->nodes, element);
}

static lxa_status_t
open_container_at(lxa_html_tree_t *tree,
                  const lxa_html_token_t *token, lxa_dom_namespace_t ns,
                  lxa_dom_ref_t parent, lxa_dom_ref_t before)
{
    lxa_dom_ref_t child;
    lxa_span_t name = token->name;
    bool marker = ns == LXA_DOM_NS_HTML && lxa_html_tree_formatting_marker_tag(name);
    lxa_status_t status = marker ? reserve_formatting(tree, 1) : LXA_OK;
    if (status != LXA_OK) return status;
    if (ns == LXA_DOM_NS_SVG) {
        status = lxa_html_svg_tag_name(name, &name);
        if (status != LXA_OK) return status;
    }
    status = lxa_dom_nodes_append_namespaced_element(tree->nodes,
                                                      name, ns, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, parent,
            child, before);
    if (status == LXA_OK && ns == LXA_DOM_NS_HTML && named(name, "option"))
        status = lxa_dom_nodes_reset_option_select(tree->nodes, child);
    if (status == LXA_OK && ns == LXA_DOM_NS_HTML && named(name, "selectedcontent"))
        status = connect_selectedcontent(tree, child);
    if (status == LXA_OK) status = push_element(tree, child);
    if (status == LXA_OK && marker)
        status = append_formatting(tree, child, true);
    return status;
}

static lxa_status_t
open_container_ns(lxa_html_tree_t *tree,
                  const lxa_html_token_t *token, lxa_dom_namespace_t ns)
{
    return open_container_at(tree, token, ns, current_element(tree),
                             (lxa_dom_ref_t){0});
}

static lxa_status_t
open_container(lxa_html_tree_t *tree,
               const lxa_html_token_t *token)
{
    return open_container_ns(tree, token, LXA_DOM_NS_HTML);
}

static lxa_status_t
plan_foreign_breakout(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                       size_t boundary, size_t *keep)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) elements->bytes.data;
    size_t i = lxa_vector_length(elements);
    while (i > boundary) {
        bool integrated;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = foreign_breakout_boundary(tree, open[i - 1], &integrated);
        if (status != LXA_OK) return status;
        if (integrated) break;
        --i;
    }
    *keep = i;
    return LXA_OK;
}

static lxa_status_t
plan_foreign_end(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                 size_t boundary, lxa_span_t name, size_t *keep,
                 bool *reprocess)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) elements->bytes.data;
    size_t i = lxa_vector_length(elements);
    *keep = i;
    *reprocess = false;
    while (i > boundary) {
        lxa_dom_namespace_t ns;
        lxa_span_t top, target = name;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML) {
            *reprocess = true;
            return LXA_OK;
        }
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &top);
        if (status == LXA_OK && ns == LXA_DOM_NS_SVG)
            status = lxa_html_svg_tag_name(target, &target);
        if (status != LXA_OK) return status;
        if (top.length == target.length) {
            size_t blocks = top.length / 8 + (top.length % 8 != 0);
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, blocks, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (memcmp(top.data, target.data, top.length) == 0) {
                *keep = i;
                return LXA_OK;
            }
        }
    }
    /* html and the active template are virtual HTML stack boundaries. */
    *reprocess = true;
    return LXA_OK;
}

/* Foreign descendants retain their parent's namespace unless an integration
 * point dispatches the token through HTML construction instead. */
static lxa_status_t
consume_foreign(lxa_html_tree_t *tree,
                const lxa_html_token_t *token, lxa_dom_namespace_t ns,
                bool *reprocess)
{
    lxa_status_t status;
    *reprocess = false;
    if (token->kind == LXA_HTML_COMMENT
        || token->kind == LXA_HTML_PROCESSING_INSTRUCTION)
        return append_comment_or_pi(tree, current_element(tree), token);
    if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
    if (token->kind == LXA_HTML_TEXT) {
        if (tree->frameset_ok && !null_or_space_only(token->data))
            tree->frameset_ok = false;
        return insert_data_text(tree, current_element(tree), token->data,
                                true, false);
    }
    if (token->kind == LXA_HTML_END_TAG) {
        size_t keep;
        status = plan_foreign_end(tree, &tree->open_elements, 0,
                                  token->name, &keep, reprocess);
        if (status != LXA_OK) return status;
        while (lxa_vector_length(&tree->open_elements) > keep) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        return LXA_OK;
    }
    if (token->kind != LXA_HTML_START_TAG) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    status = reserve_open_elements(tree, 1);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   1, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = open_container_ns(tree, token, ns);
    if (status == LXA_OK && token->self_closing) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    return status;
}

static lxa_status_t
append_void(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
            const lxa_html_token_t *token);

/* Preflight document/template select actions after foreign end dispatch.
 * Matching foreign end tags have already popped and returned. */
static lxa_status_t
plan_select_token(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                  size_t boundary, const lxa_html_token_t *token,
                  size_t *keep, bool *ignore)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) elements->bytes.data;
    size_t length = lxa_vector_length(elements), select = length, i = length;
    bool option = named(token->name, "option");
    bool group = named(token->name, "optgroup");
    bool foreign_end = token->kind == LXA_HTML_END_TAG;
    bool foreign_boundary = false;
    lxa_status_t status;
    *keep = length;
    *ignore = false;
    if (token->kind == LXA_HTML_END_TAG) {
        if (!named(token->name, "select")) return LXA_OK;
        *ignore = true;
    }
    else if (token->kind != LXA_HTML_START_TAG
             || (!named(token->name, "select")
                 && !named(token->name, "input")
                 && !named(token->name, "hr") && !option && !group))
        return LXA_OK;
    if (token->kind == LXA_HTML_START_TAG && length > boundary) {
        lxa_dom_namespace_t ns;
        status = lxa_dom_nodes_element_namespace(tree->nodes,
                                                  open[length - 1], &ns);
        if (status != LXA_OK || ns != LXA_DOM_NS_HTML) return status;
    }
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                   &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[--i], &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (token->kind == LXA_HTML_END_TAG && i == length - 1
            && ns == LXA_DOM_NS_HTML && named(name, "colgroup")) {
            *keep = i;
            return LXA_OK;
        }
        if (foreign_end && ns != LXA_DOM_NS_HTML) {
            foreign_boundary |= lxa_html_tree_scope_boundary(name, ns, LXA_HTML_SCOPE_NORMAL);
            continue;
        }
        foreign_end = false;
        if (foreign_boundary) return LXA_OK;
        if (ns == LXA_DOM_NS_HTML && named(name, "select")) {
            select = i;
            break;
        }
        if (lxa_html_tree_scope_boundary(name, ns, LXA_HTML_SCOPE_NORMAL)) break;
    }
    if (select != length && (named(token->name, "select")
                             || named(token->name, "input"))) {
        *keep = select;
        *ignore = named(token->name, "select");
        return LXA_OK;
    }
    if (!option && !group && !named(token->name, "hr")) return LXA_OK;
    if (select == length && !option && !group) return LXA_OK;
    i = length;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        bool implied = false;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                   &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[i - 1], &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[i - 1], &name);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) break;
        if (select == length) {
            if (named(name, "option")) --i;
            break;
        }
        if (option && named(name, "optgroup")) break;
        implied = lxa_html_tree_implied_end_tag(name);
        if (!implied) break;
        --i;
    }
    *keep = i;
    return LXA_OK;
}

static lxa_status_t
table_in_scope(lxa_html_tree_t *tree, const lxa_vector_t *elements,
               size_t boundary, bool *found)
{
    size_t index;
    *found = false;
    lxa_status_t status = find_scoped_end_target(tree, elements, boundary,
        literal("table"), LXA_HTML_SCOPE_TABLE, &index);
    if (status == LXA_OK) *found = index != lxa_vector_length(elements);
    return status;
}

static lxa_status_t
observe_inserted_meta(lxa_html_tree_t *tree, const lxa_html_token_t *token,
                      lxa_dom_namespace_t ns)
{
    if (tree->on_meta == NULL || ns != LXA_DOM_NS_HTML
        || !named(token->name, "meta"))
        return LXA_OK;
    return tree->on_meta(tree->meta_context, token);
}

static lxa_status_t
foster_element(lxa_html_tree_t *tree,
               const lxa_html_token_t *token, bool is_void)
{
    lxa_dom_ref_t table, parent, child;
    bool formatting = lxa_html_tree_formatting_tag(token->name);
    bool marker = lxa_html_tree_formatting_marker_tag(token->name);
    size_t drop = SIZE_MAX;
    lxa_status_t status = LXA_OK;
    if (formatting) status = prepare_formatting_start(tree, token, &drop);
    else if (!is_void && container_reconstructs(token->name))
        status = reconstruct_formatting(tree);
    else if (is_void && !head_void(token->name)
             && !named(token->name, "hr")
             && !named(token->name, "param") && !named(token->name, "source")
             && !named(token->name, "track"))
        status = reconstruct_formatting(tree);
    if (status == LXA_OK && marker) status = reserve_formatting(tree, 1);
    if (status == LXA_OK)
        status = root_insertion_location(tree, current_element(tree),
                                          &parent, &table);
    if (status != LXA_OK) return status;
    if (!is_void) {
        status = reserve_open_elements(tree, 1);
        if (status != LXA_OK) return status;
    }
    status = lxa_dom_nodes_append_parsed_html_element(tree->nodes,
                                                token->name, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, parent, child, table);
    if (status == LXA_OK)
        status = observe_inserted_meta(tree, token, LXA_DOM_NS_HTML);
    if (status == LXA_OK && named(token->name, "option"))
        status = lxa_dom_nodes_reset_option_select(tree->nodes, child);
    if (status == LXA_OK && named(token->name, "selectedcontent"))
        status = connect_selectedcontent(tree, child);
    if (status == LXA_OK && !is_void) status = push_element(tree, child);
    if (status == LXA_OK && formatting) {
        if (drop != SIZE_MAX) status = remove_formatting(tree, drop);
        if (status == LXA_OK) status = append_formatting(tree, child, false);
    }
    if (status == LXA_OK && marker)
        status = append_formatting(tree, child, true);
    if (status == LXA_OK
        && (named(token->name, "pre") || named(token->name, "listing")))
        tree->skip_pre_newline = true;
    return status;
}

static lxa_status_t
foster_foreign(lxa_html_tree_t *tree,
               const lxa_html_token_t *token, lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t table, parent, child;
    lxa_span_t name = token->name;
    lxa_status_t status = reconstruct_formatting(tree);
    if (status == LXA_OK) status = reserve_open_elements(tree, 1);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   1, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = root_insertion_location(tree, current_element(tree),
                                          &parent, &table);
    if (status == LXA_OK && ns == LXA_DOM_NS_SVG)
        status = lxa_html_svg_tag_name(name, &name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_namespaced_element(tree->nodes,
                                                          name, ns, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, parent, child, table);
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
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        --i;
        status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) continue;
        if ((group == TABLE_CELL
             && (named(name, "td") || named(name, "th")))
            || (group == TABLE_ROW && named(name, "tr"))
            || (group == TABLE_SECTION && table_section(name))) {
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            return pop_table_elements(tree, i);
        }
        if (named(name, "table")
            || (group == TABLE_CELL && table_context(name))
            || (group == TABLE_ROW && table_section(name)))
            break;
    }
    assert(false);
    return LXA_ERROR_INTERNAL;
}

static lxa_status_t
close_caption_for_table_start(lxa_html_tree_t *tree,
                              lxa_span_t incoming)
{
    size_t i;
    size_t needed = named(incoming, "td") || named(incoming, "th") ? 3
                    : named(incoming, "tr") || named(incoming, "col") ? 2 : 1;
    lxa_status_t status = find_scoped_end_target(tree, &tree->open_elements,
        0, literal("caption"), LXA_HTML_SCOPE_TABLE, &i);
    if (status != LXA_OK || i == lxa_vector_length(&tree->open_elements))
        return status;
    /* Reprocessing may allocate implicit tbody/tr nodes. */
    size_t removed = lxa_vector_length(&tree->open_elements) - i;
    size_t slots = named(incoming, "col") ? 1 : needed;
    status = reserve_open_elements(tree, slots > removed ? slots - removed : 0);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget,
            LXA_BUDGET_NODES, needed, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    return pop_table_elements(tree, i);
}

static lxa_status_t
open_table_part(lxa_html_tree_t *tree,
                const lxa_html_token_t *token)
{
    lxa_html_token_t implied = {0};
    lxa_span_t top;
    bool section, row, cell;
    bool column = named(token->name, "col") || named(token->name, "colgroup");
    size_t extra = 1;
    lxa_status_t status = ensure_body(tree);
    if (status != LXA_OK) return status;
    tree->mode = LXA_TREE_IN_BODY;
    if (!named(token->name, "table")) {
        bool found;
        status = table_in_scope(tree, &tree->open_elements, 0, &found);
        if (status != LXA_OK || !found) return status;
    }
    status = lxa_dom_nodes_element_name(tree->nodes,
                                        current_element(tree), &top);
    if (status != LXA_OK) return status;
    if (!named(token->name, "table")
        && (!table_context(top)
            || (column && !named(top, "table") && !named(top, "colgroup")))) {
        const lxa_dom_ref_t *open =
            (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
        size_t i = lxa_vector_length(&tree->open_elements);
        while (i > 1) {
            lxa_span_t name;
            lxa_dom_namespace_t ns;
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_namespace(tree->nodes,
                                                          open[--i], &ns);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                                                     open[i], &name);
            if (status != LXA_OK) return status;
            if (ns != LXA_DOM_NS_HTML
                || (!table_context(name) && !named(name, "td")
                    && !named(name, "th") && !named(name, "caption")))
                continue;
            if (column && (table_section(name) || named(name, "tr")
                           || named(name, "td") || named(name, "th")))
                continue;
            if (named(name, "table") || table_section(name)
                || named(name, "tr")) {
                size_t needed = named(token->name, "td")
                    || named(token->name, "th")
                    ? (named(name, "table") ? 3
                       : table_section(name) ? 2 : 1)
                    : named(token->name, "tr") || named(token->name, "col")
                      ? (named(name, "table") ? 2 : 1) : 1;
                size_t removed = lxa_vector_length(&tree->open_elements)
                                 - (i + 1);
                size_t slots = named(token->name, "col") ? 1 : needed;
                status = reserve_open_elements(tree,
                    slots > removed ? slots - removed : 0);
                if (status == LXA_OK)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, needed, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = pop_table_elements(tree, i + 1);
                if (status != LXA_OK) return status;
                status = lxa_dom_nodes_element_name(tree->nodes,
                                                    current_element(tree), &top);
                if (status != LXA_OK) return status;
            }
            break;
        }
    }
    if (named(token->name, "table")) {
        bool close_existing = table_context(top);
        tree->frameset_ok = false;
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
        bool reserve_after_close = close_existing
            || tree->document_mode != LXA_HTML_QUIRKS;
        status = reserve_open_elements(tree, reserve_after_close ? 0 : 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (close_existing) {
            status = end_scoped_element(tree, literal("table"), LXA_HTML_SCOPE_TABLE);
            if (status != LXA_OK) return status;
        }
        status = tree->document_mode == LXA_HTML_QUIRKS ? LXA_OK
                 : close_paragraph(tree, NULL);
        if (status == LXA_OK && reserve_after_close)
            status = reserve_open_elements(tree, 1);
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
        status = reserve_open_elements(tree, needed - 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       needed, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = pop_element(tree);
        if (status != LXA_OK) return status;
        top = literal("table");
    }
    if (named(token->name, "colgroup")) {
        if (!named(top, "table")) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        status = reserve_open_elements(tree, 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
        return status == LXA_OK ? open_container(tree, token) : status;
    }
    if (named(token->name, "col")) {
        if (!named(top, "table") && !named(top, "colgroup")) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
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
        if (!named(top, "table")) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
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
          || (named(top, "tr") && cell))) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
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
append_closed_element_at(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
                        lxa_dom_ref_t before, const lxa_html_token_t *token,
                        lxa_dom_ref_t *out)
{
    lxa_dom_ref_t element;
    lxa_status_t status = lxa_dom_nodes_append_parsed_html_element(
        tree->nodes, token->name, &element);
    if (status == LXA_OK)
        status = apply_attributes(tree, element, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, parent,
            element, before);
    if (status == LXA_OK)
        status = observe_inserted_meta(tree, token, LXA_DOM_NS_HTML);
    if (status == LXA_OK && out != NULL) *out = element;
    return status;
}

static lxa_status_t
append_void(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
            const lxa_html_token_t *token)
{
    return append_closed_element_at(tree, parent, (lxa_dom_ref_t){0}, token, NULL);
}

static lxa_status_t
set_tokenizer_text_mode(
    lxa_html_tree_t *tree, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token,
    lxa_html_text_mode_t text_mode)
{
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    return lxa_html_tokenizer_set_text_mode_status(tokenizer, text_mode,
        text_mode == LXA_HTML_PLAINTEXT ? literal("") : token->name,
        &tree->nodes->limit_reason);
}

static lxa_status_t
enter_text_mode(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_html_text_mode_t text_mode, lxa_dom_ref_t element)
{
    lxa_status_t status = set_tokenizer_text_mode(tree, tokenizer, token, text_mode);
    if (status != LXA_OK) return status;
    tree->text_element = element;
    tree->text_return_mode = tree->mode;
    tree->mode = LXA_TREE_TEXT;
    return LXA_OK;
}

static void
leave_text_mode(lxa_html_tree_t *tree)
{
    tree->mode = tree->text_return_mode;
    tree->text_element = (lxa_dom_ref_t){0};
}

static lxa_status_t
begin_text_element_at(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, lxa_dom_ref_t parent, lxa_dom_ref_t before,
    const lxa_html_token_t *token, lxa_html_text_mode_t text_mode)
{
    lxa_dom_ref_t element;
    lxa_status_t status;
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    status = lxa_dom_nodes_append_parsed_html_element(tree->nodes, token->name,
                                                &element);
    if (status == LXA_OK)
        status = apply_attributes(tree, element, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, parent,
            element, before);
    return status == LXA_OK
           ? enter_text_mode(tree, tokenizer, token, text_mode, element)
           : status;
}

static lxa_status_t
begin_text_element(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, lxa_dom_ref_t parent,
    const lxa_html_token_t *token, lxa_html_text_mode_t text_mode)
{
    return begin_text_element_at(tree, tokenizer, parent, (lxa_dom_ref_t){0},
                                  token, text_mode);
}

static lxa_status_t
begin_root_xmp(lxa_html_tree_t *tree, lxa_html_tokenizer_t *tokenizer,
                const lxa_html_token_t *token)
{
    lxa_dom_ref_t parent, before;
    lxa_span_t top;
    lxa_dom_namespace_t ns;
    lxa_status_t status;
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    status = ensure_body(tree);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES, 1,
                                   &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_namespace(tree->nodes,
                                                  current_element(tree), &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes,
                                             current_element(tree), &top);
    if (status != LXA_OK) return status;
    if (ns == LXA_DOM_NS_HTML && named(top, "colgroup")) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    status = close_paragraph(tree, NULL);
    if (status == LXA_OK) status = reconstruct_formatting(tree);
    if (status == LXA_OK)
        status = root_insertion_location(tree, current_element(tree),
                                          &parent, &before);
    if (status != LXA_OK) return status;
    tree->mode = LXA_TREE_IN_BODY;
    tree->frameset_ok = false;
    return begin_text_element_at(tree, tokenizer, parent, before, token,
                                   LXA_HTML_RAWTEXT);
}

static lxa_status_t
begin_root_plaintext(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token)
{
    lxa_dom_ref_t parent, before;
    lxa_dom_namespace_t ns;
    lxa_span_t top;
    bool column_group;
    lxa_status_t status;
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    status = ensure_body(tree);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget,
            LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK) status = close_paragraph(tree, NULL);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_namespace(tree->nodes,
            current_element(tree), &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes,
            current_element(tree), &top);
    if (status != LXA_OK) return status;
    column_group = ns == LXA_DOM_NS_HTML && named(top, "colgroup");
    status = reserve_open_elements(tree, column_group ? 0 : 1);
    if (status == LXA_OK)
        status = root_insertion_location(tree, current_element(tree),
                                          &parent, &before);
    if (status != LXA_OK) return status;
    if (column_group) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    status = open_container_at(tree, token, LXA_DOM_NS_HTML, parent, before);
    if (status != LXA_OK) return status;
    tree->mode = LXA_TREE_IN_BODY;
    return set_tokenizer_text_mode(tree, tokenizer, token, LXA_HTML_PLAINTEXT);
}

static lxa_status_t
begin_root_frameset(lxa_html_tree_t *tree, const lxa_html_token_t *token,
                    bool replace_body)
{
    lxa_dom_ref_t child, parent;
    lxa_budget_t after = *tree->nodes->budget;
    size_t pop_count = replace_body
        ? lxa_vector_length(&tree->open_elements) : 0;
    size_t needed = 1;
    lxa_status_t status = lxa_budget_release(&after, LXA_BUDGET_OPEN_ELEMENTS,
                                             pop_count);
    if (status != LXA_OK) return LXA_ERROR_INTERNAL;
    if (tree->head.handle == LXA_HANDLE_NONE) ++needed;
    if (tree->html.handle == LXA_HANDLE_NONE) ++needed;
    status = lxa_budget_check(&after, LXA_BUDGET_OPEN_ELEMENTS, 1,
                              &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget,
            LXA_BUDGET_NODES, needed, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_vector_reserve(&tree->open_elements, 1);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status == LXA_OK)
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
            pop_count, &tree->nodes->limit_reason);
    if (status == LXA_OK) status = ensure_head(tree);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_parsed_html_element(tree->nodes,
                                                    token->name, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, tree->html,
            child, (lxa_dom_ref_t){0});
    if (status != LXA_OK) return status;
    if (replace_body) {
        status = lxa_dom_nodes_parent(tree->nodes, tree->body, &parent);
        if (status == LXA_OK && parent.handle != LXA_HANDLE_NONE)
            status = lxa_dom_nodes_detach_constructed_child(tree->nodes,
                                                             tree->body);
        if (status != LXA_OK) return status;
        while (lxa_vector_length(&tree->open_elements) != 0) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        tree->body = (lxa_dom_ref_t){0};
    }
    status = push_element(tree, child);
    if (status == LXA_OK) tree->mode = LXA_TREE_IN_FRAMESET;
    return status;
}

lxa_status_t
lxa_html_tree_init(lxa_html_tree_t *tree,
                                    lxa_dom_nodes_t *nodes)
{
    lxa_status_t status;
    if (tree == NULL || nodes == NULL)
        return LXA_ERROR_ARGUMENT;
    memset(tree, 0, sizeof(*tree));
    tree->frameset_ok = true;
    tree->formatting_marker = SIZE_MAX;
    status = lxa_vector_init(&tree->open_elements, &nodes->allocator,
                             sizeof(lxa_dom_ref_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&tree->open_elements, nodes->budget);
    if (status == LXA_OK)
        status = lxa_vector_init(&tree->formatting, &nodes->allocator,
                                 sizeof(lxa_html_formatting_entry_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&tree->formatting, nodes->budget);
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
        status = lxa_buffer_init(&tree->table_text, &nodes->allocator);
    if (status == LXA_OK)
        status = lxa_buffer_bind_budget(&tree->table_text, nodes->budget);
    if (status != LXA_OK) {
        lxa_buffer_destroy(&tree->table_text);
        lxa_vector_destroy(&tree->template_elements);
        lxa_vector_destroy(&tree->template_stack);
        lxa_vector_destroy(&tree->formatting);
        lxa_vector_destroy(&tree->open_elements);
        return status;
    }
    status = lxa_dom_nodes_append_draft(nodes, LXA_DOM_KIND_DOCUMENT,
                                        &tree->document);
    if (status != LXA_OK) {
        lxa_buffer_destroy(&tree->table_text);
        lxa_vector_destroy(&tree->template_elements);
        lxa_vector_destroy(&tree->template_stack);
        lxa_vector_destroy(&tree->formatting);
        lxa_vector_destroy(&tree->open_elements);
        return status;
    }
    tree->nodes = nodes;
    tree->mode = LXA_TREE_INITIAL;
    tree->document_mode = LXA_HTML_NO_QUIRKS;
    return LXA_OK;
}

void
lxa_html_tree_destroy(lxa_html_tree_t *tree)
{
    if (tree == NULL) return;
    if (tree->nodes != NULL)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                            lxa_vector_length(&tree->formatting));
    if (tree->nodes != NULL)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                            lxa_vector_length(&tree->open_elements));
    if (tree->nodes != NULL)
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
            lxa_vector_length(&tree->template_stack)
            + lxa_vector_length(&tree->template_elements)
            + (tree->template_fragment.handle != LXA_HANDLE_NONE ? 1 : 0));
    lxa_buffer_destroy(&tree->table_text);
    lxa_vector_destroy(&tree->template_elements);
    lxa_vector_destroy(&tree->template_stack);
    lxa_vector_destroy(&tree->formatting);
    lxa_vector_destroy(&tree->open_elements);
    memset(tree, 0, sizeof(*tree));
}

static lxa_status_t
template_pop_elements(lxa_html_tree_t *tree, size_t keep)
{
    return pop_elements(tree, &tree->template_elements, keep);
}

static bool
template_boundary_valid(const lxa_html_tree_t *tree)
{
    size_t length = lxa_vector_length(&tree->template_stack);
    const lxa_html_template_frame_t *frames =
        (const lxa_html_template_frame_t *) tree->template_stack.bytes.data;
    if (length == 0) return true;
    return frames[length - 1].element_count
               <= lxa_vector_length(&tree->template_elements)
        && (length == 1 || frames[length - 1].element_count
                          >= frames[length - 2].element_count);
}

static lxa_status_t
close_template(lxa_html_tree_t *tree, bool explicit_end)
{
    size_t length = lxa_vector_length(&tree->template_stack);
    size_t elements = lxa_vector_length(&tree->template_elements);
    lxa_html_template_frame_t frame = {0};
    size_t boundary = 0;
    lxa_status_t status;
    if (!template_boundary_valid(tree)) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    if (length != 0) {
        lxa_dom_kind_t kind;
        const lxa_html_template_frame_t *frames =
            (const lxa_html_template_frame_t *) tree->template_stack.bytes.data;
        frame = frames[length - 1];
        if ((unsigned) frame.insertion_mode > LXA_TEMPLATE_BODY
            || frame.fragment.handle == tree->template_fragment.handle
            || lxa_dom_nodes_kind(tree->nodes, frame.fragment, &kind) != LXA_OK
            || kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
            || lxa_dom_nodes_validate_content_patch(tree->nodes, &frame.patch)
               != LXA_OK) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        boundary = frame.element_count;
    }
    /* Child pops must leave one credit for the active template itself. */
    if (elements - boundary
           >= tree->nodes->budget->used[LXA_BUDGET_OPEN_ELEMENTS]) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    if (explicit_end) {
        size_t work;
        status = lxa_size_add(elements - boundary, 1, &work);
        if (status == LXA_OK)
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, work, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
    }
    if (explicit_end && tree->template_patch.target.handle != LXA_HANDLE_NONE) {
        size_t keep = lxa_vector_length(&tree->template_elements);
        const lxa_dom_ref_t *open =
            (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
        while (keep > boundary) {
            lxa_span_t name;
            lxa_dom_namespace_t ns;
            status = lxa_dom_nodes_element_namespace(tree->nodes,
                open[keep - 1], &ns);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[keep - 1], &name);
            if (status != LXA_OK) return status;
            if (ns != LXA_DOM_NS_HTML
                || !(lxa_html_tree_implied_end_tag(name) || named(name, "caption")
                     || named(name, "colgroup") || named(name, "tbody")
                     || named(name, "td") || named(name, "tfoot")
                     || named(name, "th") || named(name, "thead")
                     || named(name, "tr")))
                break;
            --keep;
        }
        status = template_pop_elements(tree, keep);
        if (status != LXA_OK) return status;
        lxa_dom_ref_t markers[] = {
            tree->template_patch.start, tree->template_patch.end
        };
        for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
            lxa_dom_ref_t parent;
            if (markers[i].handle == LXA_HANDLE_NONE) continue;
            status = lxa_dom_nodes_parent(tree->nodes, markers[i], &parent);
            if (status == LXA_OK && parent.handle != LXA_HANDLE_NONE)
                status = lxa_dom_nodes_detach_constructed_child(tree->nodes,
                                                                 markers[i]);
            if (status != LXA_OK) return status;
        }
        tree->template_patch = (lxa_dom_content_patch_t){0};
    }
    status = template_pop_elements(tree, boundary);
    if (status != LXA_OK) return status;
    status = clear_formatting_to_marker(tree);
    if (status != LXA_OK) return status;
    status = lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
    if (status == LXA_OK && length != 0)
        status = lxa_vector_erase(&tree->template_stack, length - 1, 1);
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    if (length != 0) {
        tree->template_fragment = frame.fragment;
        tree->template_patch = frame.patch;
        tree->template_insertion_mode = frame.insertion_mode;
    }
    else {
        tree->template_fragment = (lxa_dom_ref_t){0};
        tree->template_patch = (lxa_dom_content_patch_t){0};
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

static lxa_dom_ref_t
template_parent(const lxa_html_tree_t *tree)
{
    size_t length = lxa_vector_length(&tree->template_elements);
    return length == template_boundary(tree) ? tree->template_fragment
        : ((const lxa_dom_ref_t *) tree->template_elements.bytes.data)[length - 1];
}

static lxa_status_t
template_foster_location(lxa_html_tree_t *tree,
                         lxa_dom_ref_t *parent, lxa_dom_ref_t *before)
{
    return foster_location_in(tree, &tree->template_elements,
        template_boundary(tree), tree->template_fragment, parent, before);
}

static lxa_status_t
template_insertion_location(lxa_html_tree_t *tree,
                            lxa_dom_ref_t *parent, lxa_dom_ref_t *before)
{
    lxa_span_t name;
    lxa_dom_namespace_t ns;
    lxa_status_t status;
    *parent = template_parent(tree);
    *before = (lxa_dom_ref_t){0};
    if (lxa_vector_length(&tree->template_elements) == template_boundary(tree))
        return LXA_OK;
    status = lxa_dom_nodes_element_namespace(tree->nodes, *parent, &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, *parent, &name);
    if (status != LXA_OK) return status;
    return ns == LXA_DOM_NS_HTML && table_context(name)
           ? template_foster_location(tree, parent, before) : LXA_OK;
}

static lxa_status_t
template_foster_element(lxa_html_tree_t *tree,
                        const lxa_html_token_t *token, bool keep_open,
                        lxa_dom_ref_t *created, lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t before, parent, child;
    size_t depth, after, drop = SIZE_MAX;
    bool formatting = ns == LXA_DOM_NS_HTML && lxa_html_tree_formatting_tag(token->name);
    bool marker = ns == LXA_DOM_NS_HTML && lxa_html_tree_formatting_marker_tag(token->name);
    lxa_status_t status = LXA_OK;
    if (formatting) status = prepare_formatting_start(tree, token, &drop);
    else if (ns != LXA_DOM_NS_HTML
             || (keep_open && container_reconstructs(token->name))
             || (body_void(token->name) && !named(token->name, "hr")
                 && !named(token->name, "param") && !named(token->name, "source")
                 && !named(token->name, "track")))
        status = reconstruct_formatting(tree);
    if (status == LXA_OK && marker) status = reserve_formatting(tree, 1);
    if (status == LXA_OK)
        status = template_insertion_location(tree, &parent, &before);
    if (status != LXA_OK) return status;
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
            ? lxa_dom_nodes_append_parsed_html_element(tree->nodes,
                token->name, &child)
            : lxa_dom_nodes_append_namespaced_element(tree->nodes,
                token->name, ns, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, parent,
            child, before);
    if (status == LXA_OK)
        status = observe_inserted_meta(tree, token, ns);
    if (status == LXA_OK && ns == LXA_DOM_NS_HTML
        && named(token->name, "option"))
        status = lxa_dom_nodes_reset_option_select(tree->nodes, child);
    if (status == LXA_OK && ns == LXA_DOM_NS_HTML
        && named(token->name, "selectedcontent"))
        status = connect_selectedcontent(tree, child);
    if (status == LXA_OK && keep_open)
        status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK && keep_open) {
        status = lxa_vector_push(&tree->template_elements, &child);
        if (status != LXA_OK)
            lxa_budget_release(tree->nodes->budget,
                                LXA_BUDGET_OPEN_ELEMENTS, 1);
    }
    if (status == LXA_OK && formatting) {
        if (drop != SIZE_MAX) status = remove_formatting(tree, drop);
        if (status == LXA_OK) status = append_formatting(tree, child, false);
    }
    if (status == LXA_OK && marker)
        status = append_formatting(tree, child, true);
    if (status == LXA_OK && created != NULL) *created = child;
    return status;
}

static lxa_status_t
flush_template_table_text(lxa_html_tree_t *tree)
{
    lxa_dom_ref_t table = {0}, parent;
    lxa_span_t bytes = {tree->table_text.data, tree->table_text.length};
    size_t i = lxa_vector_length(&tree->template_elements), prefix = 0;
    bool colgroup = false;
    lxa_status_t status;
    if (bytes.length == 0) return LXA_OK;
    if (i < template_boundary(tree)) return LXA_ERROR_INTERNAL;
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                bytes.length, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (null_or_space_only(bytes)) {
        status = append_character(tree, template_parent(tree),
                                   LXA_DOM_KIND_TEXT, bytes);
    }
    else {
        if (i > template_boundary(tree)) {
            lxa_span_t top;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                 template_parent(tree), &top);
            if (status != LXA_OK) return status;
            colgroup = named(top, "colgroup");
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
        status = reconstruct_formatting(tree);
        if (status == LXA_OK)
            status = template_insertion_location(tree, &parent, &table);
        if (status == LXA_OK) status = insert_text(tree, parent, table, bytes);
    }
    if (status == LXA_OK) lxa_buffer_clear(&tree->table_text);
    return status;
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
    return inline_container(name) && !lxa_html_tree_formatting_tag(name);
}

static bool
template_simple_text_element(lxa_span_t name)
{
    return named(name, "title") || named(name, "style")
        || named(name, "script") || named(name, "textarea")
        || named(name, "iframe") || named(name, "noembed")
        || named(name, "noframes");
}

static bool
template_html_text_element(lxa_span_t name)
{
    return template_simple_text_element(name) || named(name, "xmp")
        || named(name, "plaintext");
}

static lxa_status_t
template_open_list_item(lxa_html_tree_t *tree, bool definition, size_t *index)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t i = lxa_vector_length(&tree->template_elements);
    *index = i;
    while (i > template_boundary(tree)) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = lxa_dom_nodes_element_name(tree->nodes, open[--i], &name);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML
            && ((!definition && named(name, "li"))
                || (definition && (named(name, "dt") || named(name, "dd"))))) {
            *index = i;
            return LXA_OK;
        }
        /* A special block stops the search before the earlier item. */
        if (lxa_html_tree_special_element(name, ns)
            && !(ns == LXA_DOM_NS_HTML
                 && (named(name, "address") || named(name, "div")
                     || named(name, "p"))))
            return LXA_OK;
    }
    return LXA_OK;
}

static lxa_status_t
begin_template(lxa_html_tree_t *tree,
               const lxa_html_token_t *token)
{
    lxa_dom_ref_t child, fragment, parent, before = {0}, attr = {0};
    lxa_dom_content_patch_t patch = {0};
    lxa_html_template_frame_t frame;
    bool nested = tree->mode == LXA_TREE_TEMPLATE_TEXT, shadow = false;
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
    if (status == LXA_OK) status = reserve_formatting(tree, 1);
    if (status == LXA_OK && nested)
        status = lxa_vector_reserve(&tree->template_stack, depth - 1);
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    if (status != LXA_OK) return status;
    status = lxa_dom_nodes_append_parsed_html_element(tree->nodes, token->name, &child);
    if (status == LXA_OK)
        status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_draft(tree->nodes,
            LXA_DOM_KIND_DOCUMENT_FRAGMENT, &fragment);
    if (status == LXA_OK)
        status = lxa_dom_nodes_bind_template(tree->nodes, child, fragment);
    if (status != LXA_OK) return status;
    parent = nested ? template_parent(tree)
                   : tree->mode == LXA_TREE_IN_HEAD
                     || tree->mode == LXA_TREE_AFTER_HEAD
                     ? tree->head : current_element(tree);
    status = resolve_template_location(tree, &parent, &before);
    if (status == LXA_OK && token->attribute_count != 0)
        status = lxa_dom_nodes_find_attribute(tree->nodes, child,
            literal("shadowrootmode"), &attr);
    if (status == LXA_OK && token->attribute_count != 0
        && attr.handle != LXA_HANDLE_NONE) {
        lxa_span_t name, value;
        status = lxa_dom_nodes_attribute_read(tree->nodes, attr, &name, &value);
        if (status == LXA_OK)
            shadow = lxa_html_tree_ascii_case_equals(value, "open", 4)
                     || lxa_html_tree_ascii_case_equals(value, "closed", 6);
    }
    if (status == LXA_OK && token->attribute_count != 0 && !shadow) {
        status = lxa_dom_nodes_find_attribute(tree->nodes, child,
                                              literal("for"), &attr);
        if (status == LXA_OK && attr.handle != LXA_HANDLE_NONE) {
            lxa_dom_ref_t scope = parent;
            lxa_span_t name, value;
            status = lxa_dom_nodes_attribute_read(tree->nodes, attr, &name, &value);
            if (status == LXA_OK && scope.handle == tree->body.handle)
                status = lxa_dom_nodes_parent(tree->nodes, scope, &scope);
            if (status == LXA_OK)
                status = lxa_dom_nodes_prepare_content_patch(tree->nodes,
                    scope, value, &patch);
        }
    }
    if (status == LXA_OK && patch.target.handle == LXA_HANDLE_NONE)
        status = insert_constructed(tree, parent, child, before);
    if (status != LXA_OK) return status;
    if (!nested) tree->template_return_mode = tree->mode;
    if (nested) {
        frame.fragment = tree->template_fragment;
        frame.patch = tree->template_patch;
        frame.element_count = lxa_vector_length(&tree->template_elements);
        frame.insertion_mode = tree->template_insertion_mode;
        status = lxa_vector_push(&tree->template_stack, &frame);
        if (status != LXA_OK) return status;
    }
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
                                1, &tree->nodes->limit_reason);
    if (status == LXA_OK) {
        status = append_formatting(tree, child, true);
        if (status != LXA_OK)
            lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
    }
    if (status != LXA_OK) {
        if (nested)
            lxa_vector_erase(&tree->template_stack,
                lxa_vector_length(&tree->template_stack) - 1, 1);
        return status;
    }
    tree->template_fragment = fragment;
    tree->template_patch = patch;
    tree->template_insertion_mode = LXA_TEMPLATE_INITIAL;
    tree->mode = LXA_TREE_TEMPLATE_TEXT;
    tree->frameset_ok = false;
    return LXA_OK;
}

static lxa_status_t
template_push_table_part(lxa_html_tree_t *tree,
                         const lxa_html_token_t *token)
{
    lxa_dom_ref_t child;
    bool marker = lxa_html_tree_formatting_marker_tag(token->name);
    lxa_status_t status = marker ? reserve_formatting(tree, 1) : LXA_OK;
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_parsed_html_element(
            tree->nodes, token->name, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, template_parent(tree),
            child, (lxa_dom_ref_t){0});
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
    if (status == LXA_OK && marker)
        status = append_formatting(tree, child, true);
    return status;
}

static lxa_status_t
template_pop_table_elements(lxa_html_tree_t *tree, size_t keep)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->template_elements), markers = 0;
    lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_WORK, length - keep, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    for (size_t i = keep; i < length; ++i) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML
            && (named(name, "td") || named(name, "th") || named(name, "caption")))
            ++markers;
    }
    status = template_pop_elements(tree, keep);
    if (status != LXA_OK) return status;
    while (markers != 0) {
        status = clear_formatting_to_marker(tree);
        if (status != LXA_OK) return status;
        --markers;
    }
    return LXA_OK;
}

static lxa_status_t
template_open_foreign(lxa_html_tree_t *tree,
                      const lxa_html_token_t *token, lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t child;
    lxa_span_t name = token->name;
    size_t count, depth;
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
        status = insert_constructed(tree, template_parent(tree),
            child, (lxa_dom_ref_t){0});
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
    if (tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
        tree->template_insertion_mode = LXA_TEMPLATE_BODY;
    return LXA_OK;
}

static lxa_status_t
template_fostered_descendant(lxa_html_tree_t *tree, lxa_span_t name,
                             lxa_dom_namespace_t ns, bool *result)
{
    if (ns != LXA_DOM_NS_HTML) {
        *result = true;
        return LXA_OK;
    }
    *result = block_container(name) || lxa_html_tree_heading(name)
        || named(name, "p") || named(name, "li")
        || named(name, "form")
        || named(name, "dt") || named(name, "dd")
        || named(name, "ruby") || template_simple_inline(name)
        || named(name, "rt") || named(name, "rp")
        || named(name, "datalist") || named(name, "output")
        || named(name, "progress") || named(name, "meter")
        || named(name, "map") || named(name, "picture")
        || named(name, "canvas") || named(name, "video")
        || named(name, "audio") || named(name, "object")
        || named(name, "legend")
        || named(name, "marquee") || named(name, "applet")
        || named(name, "noscript") || named(name, "select")
        || named(name, "option")
        || named(name, "optgroup")
        || named(name, "ins") || named(name, "slot")
        || named(name, "data") || named(name, "dfn")
        || named(name, "mark") || named(name, "time")
        || named(name, "del") || named(name, "samp")
        || named(name, "kbd") || named(name, "sub")
        || named(name, "sup") || named(name, "var")
        || lxa_html_tree_formatting_tag(name);
    if (!*result)
        return supported_container(tree, name, result);
    return LXA_OK;
}

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
    lxa_span_t top;
    lxa_status_t status;
    if (!section && !row && !cell && !caption && !group) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    while (i > template_boundary(tree)) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[--i], &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) {
            ordinary_above = true;
            continue;
        }
        if (context == SIZE_MAX && table_context(name)) context = i;
        if (named(name, "table")) {
            table = i;
            break;
        }
        if (named(name, "caption")) {
            if (caption_open) {
                assert(false);
                return LXA_ERROR_INTERNAL;
            }
            caption_open = true;
            caption_index = i;
        }
        else if (caption_open) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
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
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[template_boundary(tree)], &root);
            if (status != LXA_OK) return status;
            if (named(root, "caption") || !table_context(root)) {
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, cell ? 3 : row || column ? 2 : 1,
                    &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = template_pop_table_elements(tree,
                                                      template_boundary(tree));
                if (status != LXA_OK) return status;
                return template_open_table_part(tree, token);
            }
        }
        /* A direct row leaves the template in table-body mode even after
         * the row closes; the next cell then needs another implied row. */
        if (length != template_boundary(tree)) {
            if (cell_index == template_boundary(tree)
                && tree->template_insertion_mode == LXA_TEMPLATE_ROW) {
                if (!cell) {
                    status = template_pop_table_elements(tree, cell_index);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                keep = cell_index;
                goto reserve_part;
            }
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
                if (!named(root, "tr")) {
                    assert(false);
                    return LXA_ERROR_INTERNAL;
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
                    if (status != LXA_OK) return status;
                    if (j == template_boundary(tree) + 1
                        && (named(name, "td") || named(name, "th")))
                        continue;
                    status = template_fostered_descendant(tree, name, ns,
                                                          &clearable);
                    if (status != LXA_OK) return status;
                    if (!clearable) {
                        assert(false);
                        return LXA_ERROR_INTERNAL;
                    }
                }
                status = template_pop_table_elements(tree,
                                                      template_boundary(tree));
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
                     && (section || caption || group))) {
                assert(false);
                return LXA_ERROR_INTERNAL;
            }
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
                if (!table_section(top)) {
                    assert(false);
                    return LXA_ERROR_INTERNAL;
                }
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
                if (!named(parent, "tr")) {
                    assert(false);
                    return LXA_ERROR_INTERNAL;
                }
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
                if (!named(parent, "tr") || !table_section(top)) {
                    assert(false);
                    return LXA_ERROR_INTERNAL;
                }
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
                if (!table_section(top)) {
                    assert(false);
                    return LXA_ERROR_INTERNAL;
                }
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
                        status = template_fostered_descendant(tree, name, ns,
                                                              &clearable);
                    if (status != LXA_OK) return status;
                    if (!clearable) {
                        assert(false);
                        return LXA_ERROR_INTERNAL;
                    }
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
                        status = template_fostered_descendant(tree, name, ns,
                                                              &clearable);
                    if (status != LXA_OK) return status;
                    if (!clearable) {
                        assert(false);
                        return LXA_ERROR_INTERNAL;
                    }
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
                             == LXA_TEMPLATE_TABLE_BODY && named(top, "tr"))) {
                        assert(false);
                        return LXA_ERROR_INTERNAL;
                    }
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
                             || (!cell && !section && !caption && !group)))) {
                assert(false);
                return LXA_ERROR_INTERNAL;
            }
            else if (length == template_boundary(tree) + 2) {
                status = lxa_dom_nodes_element_name(tree->nodes,
                                                    open[length - 2], &top);
                if (status != LXA_OK) return status;
                if (!table_section(top)) {
                    assert(false);
                    return LXA_ERROR_INTERNAL;
                }
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
        }
        goto reserve_part;
    }
    clear_ordinary = ordinary_above && !caption_open
                     && cell_index == SIZE_MAX;
    if (caption_open && caption_index != table + 1) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
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
        else if (!named(top, "table")) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
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
        else if (!table_section(top)) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
    }
    else {
        if (named(top, "table") || caption_open || group_open) {
            extra = 3;
            if (caption_open || group_open) keep = table + 1;
        }
        else if (table_section(top)) extra = 2;
        else if (cell_index != SIZE_MAX) keep = cell_index;
        else if (!named(top, "tr")) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
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
    status = template_pop_table_elements(tree, keep);
    if (status != LXA_OK) return status;
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
    const size_t boundary = template_boundary(tree);
    size_t depth, count, after;
    bool add_group;
    lxa_status_t status;
    if (tree->template_insertion_mode == LXA_TEMPLATE_COLUMN_GROUP) {
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = append_void(tree, template_parent(tree), token);
        return status;
    }
    if (length == boundary) {
        if (tree->template_insertion_mode == LXA_TEMPLATE_ROW
            || tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY)
            return LXA_OK; /* No table can receive this column. */
    }
    else {
        status = lxa_dom_nodes_element_name(tree->nodes, open[length - 1],
                                            &top);
        if (status != LXA_OK) return status;
        if (length == boundary + 1
            && tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
            && named(top, "tr")) {
            status = template_pop_elements(tree, boundary);
            if (status != LXA_OK) return status;
            return LXA_OK;
        }
        if (tree->template_insertion_mode == LXA_TEMPLATE_TABLE
            && !named(top, "table") && !named(top, "colgroup")) {
            size_t i = length;
            bool table = false, formatting = false;
            while (i > boundary) {
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
                if (lxa_html_tree_formatting_tag(name)) formatting = true;
            }
            if (!table && !formatting) {
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 2, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = template_pop_table_elements(tree,
                                                      boundary);
                if (status != LXA_OK) return status;
                return template_open_column(tree, token);
            }
        }
    }
    if (length == boundary
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
append_space_text(lxa_html_tree_t *tree, lxa_dom_ref_t parent, lxa_span_t bytes,
                  bool reconstruct)
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
    if (status == LXA_OK && spaces.length != 0) {
        if (reconstruct) {
            status = reconstruct_formatting(tree);
            if (status == LXA_OK) parent = formatting_parent(tree);
        }
        if (status == LXA_OK)
            status = append_character(tree, parent,
                LXA_DOM_KIND_TEXT, (lxa_span_t){spaces.data, spaces.length});
    }
    if (status == LXA_ERROR_LIMIT
        && tree->nodes->limit_reason == LXA_BUDGET_COUNT)
        tree->nodes->limit_reason = LXA_BUDGET_MEMORY;
    lxa_buffer_destroy(&spaces);
    return status;
}

static lxa_status_t
template_plan_paragraph_close(lxa_html_tree_t *tree, size_t *index)
{
    return find_scoped_end_target(tree, &tree->template_elements,
        template_boundary(tree), literal("p"), LXA_HTML_SCOPE_BUTTON, index);
}

static bool
body_control_start(lxa_span_t name)
{
    return named(name, "html") || named(name, "head") || named(name, "body")
        || named(name, "frameset") || named(name, "frame");
}

static bool
template_paragraph_container(lxa_span_t name)
{
    return (block_container(name) && !named(name, "button"))
        || named(name, "p") || lxa_html_tree_heading(name);
}

static bool
template_marker_container(lxa_span_t name)
{
    return named(name, "object") || named(name, "marquee")
        || named(name, "applet");
}

static bool
template_list_item(lxa_span_t name)
{
    return named(name, "li") || named(name, "dt") || named(name, "dd");
}

static bool
template_scoped_end(lxa_span_t name)
{
    return named(name, "p") || template_list_item(name)
        || named(name, "form") || block_container(name) || lxa_html_tree_heading(name);
}

static bool
template_ordinary_end(lxa_span_t name)
{
    if (block_container(name) || lxa_html_tree_heading(name) || table_part(name)
        || template_marker_container(name)
        || named(name, "p") || named(name, "li") || named(name, "dt")
        || named(name, "dd") || named(name, "select") || named(name, "form")
        || named(name, "template") || named(name, "body") || named(name, "html")
        || named(name, "br"))
        return false;
    return !lxa_html_tree_formatting_tag(name);
}

static lxa_status_t
template_close_elements(lxa_html_tree_t *tree, size_t index)
{
    return template_pop_elements(tree, index);
}

static lxa_status_t
begin_template_xmp(lxa_html_tree_t *tree, lxa_html_tokenizer_t *tokenizer,
                   const lxa_html_token_t *token)
{
    size_t paragraph, length = lxa_vector_length(&tree->template_elements);
    lxa_dom_ref_t parent, before;
    lxa_status_t status;
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    status = template_plan_paragraph_close(tree, &paragraph);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES, 1,
                                   &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (paragraph != length) {
        status = template_pop_elements(tree, paragraph);
        if (status != LXA_OK) return status;
    }
    if (lxa_vector_length(&tree->template_elements) > template_boundary(tree)) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_dom_nodes_element_namespace(tree->nodes,
                                                  template_parent(tree), &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                 template_parent(tree), &name);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML && named(name, "colgroup")) {
            status = template_pop_elements(tree,
            lxa_vector_length(&tree->template_elements) - 1);
            if (status != LXA_OK) return status;
        }
    }
    status = reconstruct_formatting(tree);
    if (status == LXA_OK)
        status = template_insertion_location(tree, &parent, &before);
    if (status == LXA_OK)
        status = begin_text_element_at(tree, tokenizer, parent, before, token,
                                         LXA_HTML_RAWTEXT);
    if (status == LXA_OK
        && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
        tree->template_insertion_mode = LXA_TEMPLATE_BODY;
    return status;
}

static lxa_status_t
template_end_missing_table(lxa_html_tree_t *tree)
{
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->template_elements);
    size_t boundary = template_boundary(tree), keep = length;
    lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
        LXA_BUDGET_WORK, length - boundary, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    for (size_t i = length; i > boundary; ) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        --i;
        status = lxa_dom_nodes_element_namespace(tree->nodes, open[i], &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) continue;
        /* A missing table cannot close a cell. */
        if (named(name, "td") || named(name, "th"))
            break;
        if (named(name, "tr")) keep = i;
        else if (table_section(name) || named(name, "caption")
                 || named(name, "colgroup")) {
            keep = i;
            break;
        }
    }
    if (keep == length) return LXA_OK;
    return template_pop_table_elements(tree, keep);
}

static lxa_status_t
template_end_formatting_boundary(lxa_html_tree_t *tree,
                                 const lxa_html_token_t *token, bool *handled)
{
    size_t index, length = lxa_vector_length(&tree->template_elements);
    bool table = named(token->name, "table");
    bool part = table_part(token->name);
    lxa_status_t status;
    *handled = false;
    if (token->kind != LXA_HTML_END_TAG
        || (!part && !lxa_html_tree_formatting_marker_tag(token->name)))
        return LXA_OK;
    status = find_scoped_end_target(tree, &tree->template_elements,
        template_boundary(tree), token->name,
        part ? LXA_HTML_SCOPE_TABLE : LXA_HTML_SCOPE_NORMAL, &index);
    if (status != LXA_OK) return status;
    if (index == length) {
        if (table) {
            status = template_end_missing_table(tree);
            if (status != LXA_OK) return status;
        }
        else if (!named(token->name, "col")
                 && length > template_boundary(tree)) {
            lxa_span_t name;
            lxa_dom_namespace_t ns;
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_namespace(tree->nodes,
                    template_parent(tree), &ns);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    template_parent(tree), &name);
            if (status != LXA_OK) return status;
            if (ns == LXA_DOM_NS_HTML && named(name, "colgroup")) {
                status = template_pop_table_elements(tree, length - 1);
                if (status != LXA_OK) return status;
            }
        }
        *handled = true;
        return LXA_OK;
    }
    if (part) {
        status = template_pop_table_elements(tree, index);
        if (status != LXA_OK) return status;
        *handled = true;
        return LXA_OK;
    }
    status = template_pop_elements(tree, index);
    if (status != LXA_OK) return status;
    status = clear_formatting_to_marker(tree);
    if (status != LXA_OK) return status;
    *handled = true;
    return LXA_OK;
}

static bool
template_table_context(const lxa_html_tree_t *tree)
{
    return tree->template_insertion_mode == LXA_TEMPLATE_TABLE
        || tree->template_insertion_mode == LXA_TEMPLATE_TABLE_BODY
        || tree->template_insertion_mode == LXA_TEMPLATE_ROW;
}

static lxa_status_t
plan_table_context(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                     size_t boundary, bool virtual_table,
                     size_t *keep, bool *in_table)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
        elements->bytes.data;
    size_t length = lxa_vector_length(elements), i = length;
    lxa_status_t status;
    *keep = length;
    *in_table = virtual_table;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                    &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes, open[--i], &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, open[i], &name);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML) continue;
        if (named(name, "td") || named(name, "th") || named(name, "caption")) {
            *in_table = false;
            return LXA_OK;
        }
        if (table_context(name)) {
            *in_table = true;
            if (i == length - 1 && named(name, "colgroup")) {
                status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                            1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                *keep = i;
            }
            break;
        }
    }
    return LXA_OK;
}

static lxa_status_t
insert_hidden_table_input(lxa_html_tree_t *tree,
                          const lxa_html_token_t *token, bool *handled)
{
    lxa_vector_t *elements;
    size_t keep, boundary;
    bool hidden, in_table;
    lxa_status_t status;
    *handled = false;
    if (token->kind != LXA_HTML_START_TAG || !named(token->name, "input"))
        return LXA_OK;
    status = hidden_input(tree, token, &hidden);
    if (status != LXA_OK || !hidden) return status;
    elements = formatting_elements(tree);
    boundary = formatting_boundary(tree);
    status = plan_table_context(tree, elements, boundary,
        tree->mode == LXA_TREE_TEMPLATE_TEXT && template_table_context(tree),
        &keep, &in_table);
    if (status != LXA_OK || !in_table) return status;
    if (keep == boundary) {
        if (tree->mode != LXA_TREE_TEMPLATE_TEXT) return LXA_ERROR_INTERNAL;
        status = append_void(tree, tree->template_fragment, token);
    }
    else {
        const lxa_dom_ref_t *open =
            (const lxa_dom_ref_t *) elements->bytes.data;
        status = append_void(tree, open[keep - 1], token);
    }
    if (status == LXA_OK) status = formatting_pop_elements(tree, keep);
    if (status == LXA_OK) *handled = true;
    return status;
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
    if (token == NULL
        || (unsigned int) token->kind > LXA_HTML_PROCESSING_INSTRUCTION
        || (token->name.data == NULL && token->name.length != 0)
        || ((token->kind == LXA_HTML_START_TAG || token->kind == LXA_HTML_END_TAG)
            && token->name.length == 0)
        || (token->data.data == NULL && token->data.length != 0)
        || (token->attribute_count != 0 && token->attributes == NULL)
        || token->attribute_count > SIZE_MAX / sizeof(*token->attributes)
        || (token->kind != LXA_HTML_START_TAG
            && token->attribute_count != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (token->kind == LXA_HTML_DOCTYPE) {
        declaration = (lxa_dom_doctype_data_t){0};
        declaration.name = token->name;
        declaration.public_id = token->public_id;
        declaration.system_id = token->system_id;
        declaration.has_public_id = token->has_public_id;
        declaration.has_system_id = token->has_system_id;
        declaration.force_quirks = token->force_quirks;
        if (!lxa_dom_doctype_shape_valid(&declaration)) return LXA_ERROR_ARGUMENT;
    }
    for (size_t i = 0; i < token->attribute_count; ++i) {
        const lxa_html_attribute_t *attribute = &token->attributes[i];
        if (!lxa_dom_attribute_shape_valid(attribute->name, attribute->value))
            return LXA_ERROR_ARGUMENT;
    }
    if (tree->mode == LXA_TREE_INITIAL) {
        if (token->kind == LXA_HTML_COMMENT
            || token->kind == LXA_HTML_PROCESSING_INSTRUCTION)
            return append_comment_or_pi(tree, tree->document, token);
        if (token->kind == LXA_HTML_TEXT && space_only(token->data))
            return LXA_OK;
        if (token->kind != LXA_HTML_DOCTYPE) {
            tree->document_mode = LXA_HTML_QUIRKS;
            tree->mode = LXA_TREE_BEFORE_HTML;
        }
    }
    if (token->kind == LXA_HTML_EOF && tree->mode == LXA_TREE_TEXT) {
        leave_text_mode(tree);
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
        if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
        if (token->kind == LXA_HTML_COMMENT
            || token->kind == LXA_HTML_PROCESSING_INSTRUCTION)
            return append_comment_or_pi(tree, template_parent(tree), token);
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
            if (ns != LXA_DOM_NS_HTML && token->kind == LXA_HTML_TEXT) {
                bool integrated;
                status = foreign_breakout_boundary(tree, open[length - 1],
                                                     &integrated);
                if (status != LXA_OK) return status;
                if (!integrated)
                    return insert_data_text(tree, template_parent(tree),
                                            token->data, true, false);
                if (null_only(token->data))
                    return LXA_OK;
                status = reconstruct_formatting(tree);
                return status == LXA_OK
                    ? insert_data_text(tree, template_parent(tree),
                                       token->data, false, false) : status;
            }
            if (ns == LXA_DOM_NS_SVG) {
                lxa_span_t current;
                lxa_html_foreign_context_t context;
                bool font = false;
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[length - 1], &current);
                if (status != LXA_OK) return status;
                status = element_foreign_context(tree, open[length - 1],
                                                 ns, current, &context);
                if (status != LXA_OK) return status;
                if (named(token->name, "font")) {
                    status = font_breakout(tree, token, &font);
                    if (status != LXA_OK) return status;
                }
                if (!lxa_html_foreign_integration(context)
                    && (foreign_html_start_breakout(token->name) || font)) {
                    size_t keep;
                    status = plan_foreign_breakout(tree,
                        &tree->template_elements, template_boundary(tree),
                        &keep);
                    if (status != LXA_OK) return status;
                    status = template_pop_elements(tree, keep);
                    if (status != LXA_OK) return status;
                    goto reprocess_template_token;
                }
                /* Integration-point starts continue through the HTML consumers. */
                if (!lxa_html_foreign_uses_html(context, token)) {
                    return template_open_foreign(tree, token, ns);
                }
            }
            if (ns == LXA_DOM_NS_MATHML) {
                lxa_span_t current;
                lxa_html_foreign_context_t context;
                bool integrated, font = false;
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[length - 1], &current);
                if (status != LXA_OK) return status;
                context = lxa_html_foreign_context(ns, current, false);
                if (lxa_html_foreign_annotation_svg(context, token))
                    return template_open_foreign(tree, token, LXA_DOM_NS_SVG);
                status = element_foreign_context(tree, open[length - 1],
                                                 ns, current, &context);
                if (status != LXA_OK) return status;
                if (lxa_html_foreign_integration(context)
                    && !lxa_html_foreign_uses_html(context, token))
                    return template_open_foreign(tree, token,
                                                 LXA_DOM_NS_MATHML);
                integrated = lxa_html_foreign_integration(context);
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
                    size_t keep;
                    status = plan_foreign_breakout(tree,
                        &tree->template_elements, template_boundary(tree),
                        &keep);
                    if (status != LXA_OK) return status;
                    status = template_pop_elements(tree, keep);
                    if (status != LXA_OK) return status;
                    goto reprocess_template_token;
                }
                if (!integrated)
                    return template_open_foreign(tree, token, ns);
            }
        }
        if (token->kind == LXA_HTML_END_TAG
            && lxa_vector_length(&tree->template_elements)
               > template_boundary(tree)) {
            lxa_dom_namespace_t ns;
            status = lxa_dom_nodes_element_namespace(tree->nodes,
                template_parent(tree), &ns);
            if (status != LXA_OK) return status;
            if (ns != LXA_DOM_NS_HTML) {
                size_t keep;
                bool reprocess;
                if (named(token->name, "br") || named(token->name, "p")) {
                    status = plan_foreign_breakout(tree,
                        &tree->template_elements, template_boundary(tree),
                        &keep);
                    if (status != LXA_OK) return status;
                    status = template_pop_elements(tree, keep);
                    if (status != LXA_OK) return status;
                }
                else {
                    status = plan_foreign_end(tree, &tree->template_elements,
                        template_boundary(tree), token->name, &keep, &reprocess);
                    if (status != LXA_OK) return status;
                    if (!reprocess) {
                        status = template_pop_elements(tree, keep);
                        if (status != LXA_OK) return status;
                        return LXA_OK;
                    }
                }
            }
        }
        if (token->kind == LXA_HTML_END_TAG && named(token->name, "br")
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
        if (token->kind == LXA_HTML_END_TAG
            && named(token->name, "template")) {
            return close_template(tree, true);
        }
        {
            bool handled;
            status = template_end_formatting_boundary(tree, token, &handled);
            if (status != LXA_OK || handled) return status;
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
        if (tree->template_insertion_mode == LXA_TEMPLATE_INITIAL
            && token->kind == LXA_HTML_START_TAG
            && table_part(token->name) && !named(token->name, "table")) {
            tree->template_insertion_mode = named(token->name, "col")
                ? LXA_TEMPLATE_COLUMN_GROUP
                : named(token->name, "tr") ? LXA_TEMPLATE_TABLE_BODY
                : named(token->name, "td") || named(token->name, "th")
                    ? LXA_TEMPLATE_ROW : LXA_TEMPLATE_TABLE;
        }
        if (tree->template_insertion_mode == LXA_TEMPLATE_BODY
            && token->kind == LXA_HTML_START_TAG
            && table_part(token->name) && !named(token->name, "table")) {
            bool table_found = false;
            status = table_in_scope(tree, &tree->template_elements,
                                     template_boundary(tree), &table_found);
            if (status != LXA_OK) return status;
            if (!table_found)
                return LXA_OK; /* No table in scope for in-body starts. */
        }
        if (tree->template_insertion_mode == LXA_TEMPLATE_COLUMN_GROUP) {
            bool group_open = false;
            if (lxa_vector_length(&tree->template_elements)
                > template_boundary(tree)) {
                lxa_span_t name;
                lxa_dom_namespace_t ns;
                status = lxa_budget_charge(tree->nodes->budget,
                    LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_namespace(tree->nodes,
                        template_parent(tree), &ns);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_element_name(tree->nodes,
                        template_parent(tree), &name);
                if (status != LXA_OK) return status;
                group_open = ns == LXA_DOM_NS_HTML && named(name, "colgroup");
            }
            /* Without an open colgroup, ordinary starts and nonspace
             * characters are ignored in column-group mode. */
            if (!group_open && token->kind == LXA_HTML_TEXT)
                return append_space_text(tree, template_parent(tree),
                                         token->data, false);
            if (!group_open && ((token->kind == LXA_HTML_START_TAG
                 && !named(token->name, "col")
                 && !named(token->name, "template")
                 && !named(token->name, "html"))
                || token->kind == LXA_HTML_END_TAG))
                return LXA_OK;
        }
        {
            bool handled;
            status = insert_hidden_table_input(tree, token, &handled);
            if (status != LXA_OK || handled) return status;
        }
        {
            size_t keep;
            bool ignore;
            status = plan_select_token(tree, &tree->template_elements,
                template_boundary(tree), token, &keep, &ignore);
            if (status != LXA_OK) return status;
            status = template_pop_elements(tree, keep);
            if (status != LXA_OK) return status;
            if (ignore) return LXA_OK;
        }
        if (token->kind == LXA_HTML_START_TAG && named(token->name, "form")) {
            size_t keep, boundary = template_boundary(tree);
            bool in_table;
            bool virtual_table = template_table_context(tree);
            status = plan_table_context(tree, &tree->template_elements,
                boundary, virtual_table, &keep, &in_table);
            if (status != LXA_OK) return status;
            if (in_table) {
                const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                    tree->template_elements.bytes.data;
                parent = keep == boundary ? tree->template_fragment
                                          : open[keep - 1];
                status = append_void(tree, parent, token);
                if (status == LXA_OK) {
                    status = template_pop_elements(tree, keep);
                    if (status != LXA_OK) return status;
                }
                return status;
            }
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
                    && !table_context(parent_name);
                if (parent_ns != LXA_DOM_NS_HTML) {
                    status = foreign_breakout_boundary(tree,
                        template_parent(tree), &html_parent);
                    if (status != LXA_OK) return status;
                }
            }
            if (html_parent) {
                status = reconstruct_formatting(tree);
                if (status != LXA_OK) return status;
                return template_open_foreign(tree, token,
                    named(token->name, "svg") ? LXA_DOM_NS_SVG
                                               : LXA_DOM_NS_MATHML);
            }
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "col")
            && lxa_vector_length(&tree->template_elements)
               == template_boundary(tree))
            return template_open_column(tree, token);
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "table")) {
            size_t keep, index, boundary = template_boundary(tree);
            size_t length = lxa_vector_length(&tree->template_elements);
            bool in_table;
            bool virtual_table = template_table_context(tree);
            status = plan_table_context(tree, &tree->template_elements,
                boundary, virtual_table, &keep, &in_table);
            if (status == LXA_OK && in_table)
                status = find_scoped_end_target(tree, &tree->template_elements,
                    boundary, literal("table"), LXA_HTML_SCOPE_TABLE, &index);
            if (status != LXA_OK) return status;
            if (in_table) {
                if (index == length) {
                    /* A virtual table mode has no table to close and reprocess. */
                    status = template_pop_elements(tree, keep);
                    if (status != LXA_OK) return status;
                    return LXA_OK;
                }
                status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                           1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = template_pop_table_elements(tree, index);
                if (status != LXA_OK) return status;
                goto reprocess_template_token;
            }
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
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = template_pop_elements(tree, last);
                if (status != LXA_OK) return status;
                goto reprocess_template_token;
            }
            if (ns == LXA_DOM_NS_HTML
                && (named(top, "table") || named(top, "colgroup")
                    || table_section(top)
                    || named(top, "tr"))) {
                if (named(top, "colgroup")
                    && token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "select")
                        || named(token->name, "frameset")
                        || named(token->name, "body")
                        || named(token->name, "html")
                        || lxa_html_tree_heading(token->name)
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
                    && head_void(token->name)) {
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
                    && named(token->name, "xmp"))
                    return begin_template_xmp(tree, tokenizer, token);
                if (token->kind == LXA_HTML_START_TAG
                    && (named(token->name, "textarea")
                        || named(token->name, "iframe")
                        || named(token->name, "noembed")
                        || named(token->name, "plaintext"))) {
                    bool textarea = named(token->name, "textarea");
                    bool plaintext = named(token->name, "plaintext");
                    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    status = template_foster_element(tree, token, plaintext,
                                                      &child, LXA_DOM_NS_HTML);
                    if (status == LXA_OK && plaintext)
                        status = set_tokenizer_text_mode(tree, tokenizer, token,
                                                         LXA_HTML_PLAINTEXT);
                    else if (status == LXA_OK)
                        status = enter_text_mode(tree, tokenizer, token,
                            textarea ? LXA_HTML_RCDATA
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
                    && (lxa_html_tree_formatting_tag(token->name)
                        || inline_container(token->name)
                        || named(token->name, "object")
                        || named(token->name, "marquee")
                        || named(token->name, "applet")
                        || named(token->name, "ruby")
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
                        || lxa_html_tree_heading(token->name)
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
                    && !named(token->name, "template")
                    && !named(token->name, "table")
                    && !named(token->name, "style")
                    && !named(token->name, "script")) {
                    bool ordinary;
                    status = supported_container(tree, token->name,
                                                  &ordinary);
                    if (status != LXA_OK) return status;
                    if (!ordinary) {
                        assert(false);
                        return LXA_ERROR_INTERNAL;
                    }
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
                    && (named(token->name, "body")
                        || named(token->name, "html")))
                    return LXA_OK; /* The template cannot close either root. */
                if (token->kind == LXA_HTML_END_TAG
                    && lxa_html_tree_heading(token->name))
                    return LXA_OK; /* A heading outside the table is out of scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "li")
                        || named(token->name, "dt")
                        || named(token->name, "dd")))
                    return LXA_OK; /* The table ends list-item scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "option")
                        || named(token->name, "optgroup")))
                    return LXA_OK; /* Unmatched ends stop at this table element. */
                if (token->kind == LXA_HTML_END_TAG
                    && (named(token->name, "ruby")
                        || named(token->name, "rt")
                        || named(token->name, "rp")))
                    return LXA_OK; /* An annotation outside the table is out of scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && block_container(token->name))
                    return LXA_OK; /* The table ends ordinary block scope. */
                if (token->kind == LXA_HTML_END_TAG
                    && named(token->name, "frameset")) {
                    if (named(top, "colgroup")) {
                        status = template_pop_elements(tree, last);
                        if (status != LXA_OK) return status;
                    }
                    return LXA_OK; /* No match in this table scope. */
                }
                if (token->kind == LXA_HTML_END_TAG
                    && template_ordinary_end(token->name)) {
                    size_t index;
                    status = plan_ordinary_end_target(tree,
                        &tree->template_elements,
                        template_boundary(tree), token->name, &index);
                    if (status != LXA_OK) return status;
                    return template_close_elements(tree, index);
                }
                if (token->kind == LXA_HTML_END_TAG
                    && lxa_html_tree_formatting_tag(token->name) && named(top, "colgroup")) {
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status != LXA_OK) return status;
                    status = template_pop_elements(tree, last);
                    if (status != LXA_OK) return status;
                    goto reprocess_template_token;
                }
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
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "col"))
            return template_open_column(tree, token);
        if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "caption")
                || named(token->name, "colgroup")
                || table_section(token->name)
                || named(token->name, "tr")
                || named(token->name, "td")
                || named(token->name, "th")))
            return template_open_table_part(tree, token);
        if (token->kind == LXA_HTML_END_TAG
            && template_scoped_end(token->name) && !named(token->name, "p")) {
            size_t index;
            status = find_scoped_end_target(tree, &tree->template_elements,
                template_boundary(tree), token->name,
                named(token->name, "li") ? LXA_HTML_SCOPE_LIST_ITEM : LXA_HTML_SCOPE_NORMAL,
                &index);
            if (status != LXA_OK) return status;
            return index == lxa_vector_length(&tree->template_elements)
                ? LXA_OK : template_close_elements(tree, index);
        }
        if (token->kind == LXA_HTML_END_TAG && named(token->name, "p")
            && (tree->template_insertion_mode != LXA_TEMPLATE_INITIAL
                || lxa_vector_length(&tree->template_elements)
                   > template_boundary(tree))) {
            size_t index;
            status = find_scoped_end_target(tree, &tree->template_elements,
                template_boundary(tree), token->name, LXA_HTML_SCOPE_BUTTON, &index);
            if (status != LXA_OK) return status;
            if (index == lxa_vector_length(&tree->template_elements)) {
                lxa_dom_ref_t paragraph, before;
                status = template_insertion_location(tree, &parent, &before);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_append_parsed_html_element(tree->nodes,
                        literal("p"), &paragraph);
                if (status != LXA_OK) return status;
                return insert_constructed(tree, parent,
                    paragraph, before);
            }
            return template_close_elements(tree, index);
        }
        if (token->kind == LXA_HTML_END_TAG
            && template_ordinary_end(token->name)) {
            size_t index;
            status = find_ordinary_end_target(tree, &tree->template_elements,
                template_boundary(tree), token->name, &index);
            if (status != LXA_OK) return status;
            if (index == lxa_vector_length(&tree->template_elements))
                return LXA_OK;
            return template_close_elements(tree, index);
        }
        if (token->kind == LXA_HTML_END_TAG && lxa_html_tree_formatting_tag(token->name))
            return adopt_formatting(tree, token->name);
        if (token->kind == LXA_HTML_END_TAG) return LXA_OK;
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "template"))
            return begin_template(tree, token);
        if (token->kind == LXA_HTML_TEXT) {
            if (null_only(token->data))
                return LXA_OK;
            if (tree->template_fragment.handle == LXA_HANDLE_NONE)
                return LXA_ERROR_INTERNAL;
            if (lxa_vector_length(&tree->template_elements)
                == template_boundary(tree)
                && template_table_context(tree))
                return insert_data_text(tree, tree->template_fragment,
                                         token->data, false, true);
            status = reconstruct_formatting(tree);
            if (status != LXA_OK) return status;
            return insert_data_text(tree, template_parent(tree),
                                    token->data, false, false);
        }
        if (token->kind == LXA_HTML_START_TAG && head_void(token->name))
            return append_void(tree, template_parent(tree), token);
        if (token->kind == LXA_HTML_START_TAG
            && (body_void(token->name) || named(token->name, "image"))) {
            if (!named(token->name, "hr")
                && !named(token->name, "param")
                && !named(token->name, "source")
                && !named(token->name, "track")) {
                status = reconstruct_formatting(tree);
                if (status != LXA_OK) return status;
            }
            if (named(token->name, "hr")) {
                size_t paragraph;
                status = template_plan_paragraph_close(tree, &paragraph);
                if (status == LXA_OK)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                if (paragraph != lxa_vector_length(&tree->template_elements)) {
                    status = template_pop_elements(tree, paragraph);
                    if (status != LXA_OK) return status;
                }
            }
            if (named(token->name, "image")) {
                adjusted = *token;
                adjusted.name = literal("img");
                token = &adjusted;
            }
            {
                lxa_dom_ref_t parent, before;
                status = template_insertion_location(tree, &parent, &before);
                if (status == LXA_OK)
                    status = append_closed_element_at(tree, parent, before,
                                                       token, NULL);
            }
            if (status == LXA_OK
                && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
                tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && template_simple_text_element(token->name)
            && !named(token->name, "textarea")) {
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
            return begin_template_xmp(tree, tokenizer, token);
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "button")) {
            size_t index;
            status = find_scoped_end_target(tree, &tree->template_elements,
                template_boundary(tree), token->name, LXA_HTML_SCOPE_NORMAL, &index);
            if (status == LXA_OK)
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = template_close_elements(tree, index);
            if (status == LXA_OK)
                status = reconstruct_formatting(tree);
            if (status != LXA_OK) return status;
        }
        template_ordinary = false;
        if (token->kind == LXA_HTML_START_TAG) {
            template_ordinary =
                named(token->name, "form")
                || template_marker_container(token->name)
                || inline_container(token->name)
                || named(token->name, "plaintext")
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
                || named(token->name, "ul")
                || named(token->name, "ol")
                || named(token->name, "menu")
                || named(token->name, "li")
                || named(token->name, "ruby")
                || named(token->name, "rt")
                || named(token->name, "rp")
                || lxa_html_tree_heading(token->name)
                || named(token->name, "table")
                || select_container(token->name)
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
                || lxa_html_tree_formatting_tag(token->name);
            if (!template_ordinary) {
                status = supported_container(tree, token->name,
                                              &template_ordinary);
                if (status != LXA_OK) return status;
            }
        }
        if (template_ordinary) {
            size_t count, depth, close_at, drop = SIZE_MAX;
            lxa_dom_ref_t parent, before = {0};
            bool formatting = lxa_html_tree_formatting_tag(token->name);
            bool marker = lxa_html_tree_formatting_marker_tag(token->name);
            const lxa_dom_ref_t *open;
            if (named(token->name, "plaintext") && tokenizer == NULL)
                return LXA_ERROR_ARGUMENT;
            if (formatting) status = prepare_formatting_start(tree, token, &drop);
            else if (container_reconstructs(token->name)
                     && !named(token->name, "table")
                     && !named(token->name, "form")
                     && !named(token->name, "button")
                     && !named(token->name, "plaintext"))
                status = reconstruct_formatting(tree);
            if (status == LXA_OK && marker) status = reserve_formatting(tree, 1);
            if (status != LXA_OK) return status;
            open = (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
            close_at = lxa_vector_length(&tree->template_elements);
            if (ruby_annotation(token->name)) {
                status = plan_ruby_start(tree, &tree->template_elements,
                    template_boundary(tree), token->name, &close_at);
                if (status != LXA_OK) return status;
            }
            if (template_list_item(token->name)) {
                status = template_open_list_item(tree,
                                                 !named(token->name, "li"),
                                                 &close_at);
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
                    status = template_plan_paragraph_close(tree, &close_at);
                    if (status != LXA_OK) return status;
                }
            }
            else if (template_paragraph_container(token->name)
                     || named(token->name, "form")
                     || named(token->name, "plaintext")
                     || (named(token->name, "table")
                         && tree->document_mode != LXA_HTML_QUIRKS)) {
                status = template_plan_paragraph_close(tree, &close_at);
                if (status != LXA_OK) return status;
                if (lxa_html_tree_heading(token->name)
                    && close_at > template_boundary(tree)) {
                    lxa_span_t current;
                    status = lxa_budget_charge(tree->nodes->budget,
                        LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                    if (status == LXA_OK)
                        status = lxa_dom_nodes_element_name(tree->nodes,
                            open[close_at - 1], &current);
                    if (status != LXA_OK) return status;
                    if (lxa_html_tree_heading(current)) --close_at;
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
            status = lxa_dom_nodes_append_parsed_html_element(tree->nodes,
                                                        token->name, &child);
            if (status == LXA_OK)
                status = apply_attributes(tree, child, token);
            if (status != LXA_OK) return status;
            if (close_at != lxa_vector_length(&tree->template_elements)) {
                status = template_pop_elements(tree, close_at);
                if (status != LXA_OK) return status;
            }
            parent = template_parent(tree);
            if (!named(token->name, "table"))
                status = template_insertion_location(tree, &parent, &before);
            if (status == LXA_OK)
                status = insert_constructed(tree, parent,
                    child, before);
            if (status == LXA_OK && named(token->name, "option"))
                status = lxa_dom_nodes_reset_option_select(tree->nodes, child);
            if (status == LXA_OK && named(token->name, "selectedcontent"))
                status = connect_selectedcontent(tree, child);
            if (status != LXA_OK) return status;
            status = lxa_budget_charge(tree->nodes->budget,
                LXA_BUDGET_OPEN_ELEMENTS, 1, &tree->nodes->limit_reason);
            if (status == LXA_OK) {
                status = lxa_vector_push(&tree->template_elements, &child);
                if (status != LXA_OK)
                    lxa_budget_release(tree->nodes->budget,
                                        LXA_BUDGET_OPEN_ELEMENTS, 1);
            }
            if (status == LXA_OK && formatting) {
                if (drop != SIZE_MAX) status = remove_formatting(tree, drop);
                if (status == LXA_OK)
                    status = append_formatting(tree, child, false);
            }
            if (status == LXA_OK && marker)
                status = append_formatting(tree, child, true);
            if (status == LXA_OK
                && (named(token->name, "pre")
                    || named(token->name, "listing")))
                tree->skip_pre_newline = true;
            if (status == LXA_OK
                && tree->template_insertion_mode == LXA_TEMPLATE_INITIAL)
                tree->template_insertion_mode = LXA_TEMPLATE_BODY;
            if (status == LXA_OK && named(token->name, "plaintext"))
                status = set_tokenizer_text_mode(tree, tokenizer, token,
                                                 LXA_HTML_PLAINTEXT);
            return status;
        }
        if (token->kind != LXA_HTML_EOF) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
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
                status = close_template(tree, false);
                if (status != LXA_OK) return status;
            }
        }
    }
    if (tree->mode == LXA_TREE_IN_FRAMESET
        || tree->mode == LXA_TREE_AFTER_FRAMESET
        || tree->mode == LXA_TREE_AFTER_AFTER_FRAMESET) {
        if (token->kind == LXA_HTML_EOF) {
            status = pop_elements(tree, &tree->open_elements, 0);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_DONE;
            return LXA_OK;
        }
        parent = tree->mode == LXA_TREE_IN_FRAMESET
                 ? current_element(tree)
                 : tree->mode == LXA_TREE_AFTER_AFTER_FRAMESET
                   ? formatting_parent(tree) : tree->html;
        if (token->kind == LXA_HTML_COMMENT
            || token->kind == LXA_HTML_PROCESSING_INSTRUCTION)
            return append_comment_or_pi(tree,
                tree->mode == LXA_TREE_AFTER_AFTER_FRAMESET
                    ? tree->document : parent, token);
        if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "html"))
            return apply_attributes(tree, tree->html, token);
        if (token->kind == LXA_HTML_TEXT)
            return append_space_text(tree, parent, token->data,
                tree->mode == LXA_TREE_AFTER_AFTER_FRAMESET);
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "noframes")) {
            status = lxa_budget_check(tree->nodes->budget,
                LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            return status == LXA_OK
                ? begin_text_element(tree, tokenizer,
                    parent, token, LXA_HTML_RAWTEXT) : status;
        }
        if (tree->mode != LXA_TREE_IN_FRAMESET) {
            if (token->kind == LXA_HTML_END_TAG && named(token->name, "html"))
                tree->mode = LXA_TREE_AFTER_AFTER_FRAMESET;
            return LXA_OK;
        }
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
            return append_character(tree, tree->text_element,
                                    LXA_DOM_KIND_TEXT, token->data);
        }
        if (token->kind != LXA_HTML_END_TAG
            || lxa_dom_nodes_element_name(tree->nodes, tree->text_element,
                                           &name) != LXA_OK
            || name.length != token->name.length
            || memcmp(name.data, token->name.data, name.length) != 0)
            return LXA_ERROR_INTERNAL;
        leave_text_mode(tree);
        return LXA_OK;
    }
    if (tree->mode == LXA_TREE_HEAD_NOSCRIPT) {
        if (token->kind == LXA_HTML_END_TAG
            && named(token->name, "noscript")) {
            tree->head_noscript = (lxa_dom_ref_t){0};
            tree->mode = LXA_TREE_IN_HEAD;
            return LXA_OK;
        }
        if (token->kind == LXA_HTML_END_TAG && !named(token->name, "br"))
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
    if (token->kind == LXA_HTML_START_TAG
        && named(token->name, "frameset")
        && (tree->mode == LXA_TREE_INITIAL
            || tree->mode == LXA_TREE_BEFORE_HTML
            || tree->mode == LXA_TREE_BEFORE_HEAD
            || tree->mode == LXA_TREE_IN_HEAD
            || tree->mode == LXA_TREE_AFTER_HEAD)
        && tree->body.handle == LXA_HANDLE_NONE) {
        return begin_root_frameset(tree, token, false);
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
            lxa_html_foreign_context_t context;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                current_element(tree),
                                                &current_name);
            if (status != LXA_OK) return status;
            status = element_foreign_context(tree, current_element(tree),
                                             ns, current_name, &context);
            if (status != LXA_OK) return status;
            integration_owner = lxa_html_foreign_integration(context);
            html_integration_child = lxa_html_foreign_uses_html(context, token);
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
                    && (foreign_html_start_breakout(token->name)
                        || breakout_font)))) {
            /* An HTML breakout closes foreign open elements and reprocesses
             * the token in body mode. Preflight the scan and node admission
             * before popping. The body rule checks stack admission after
             * foreign entries are released. */
            size_t keep;
            bool needs_node = !named(token->name, "head")
                              && !named(token->name, "body");
            status = plan_foreign_breakout(tree, &tree->open_elements, 0,
                                           &keep);
            if (status != LXA_OK) return status;
            if (keep == 0) return LXA_ERROR_INTERNAL;
            if (token->kind == LXA_HTML_END_TAG && named(token->name, "p")) {
                lxa_vector_t view = tree->open_elements;
                size_t index;
                view.bytes.length = keep * sizeof(lxa_dom_ref_t);
                status = find_scoped_end_target(tree, &view, 0,
                    token->name, LXA_HTML_SCOPE_BUTTON, &index);
                if (status != LXA_OK) return status;
                needs_node = index == keep;
            }
            if (needs_node) {
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
            }
            while (lxa_vector_length(&tree->open_elements) > keep) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
        }
        else if (ns != LXA_DOM_NS_HTML
            && !html_integration_child)
        {
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
    if (token->kind == LXA_HTML_DOCTYPE && tree->mode == LXA_TREE_INITIAL) {
        lxa_html_document_mode_t document_mode;
        status = lxa_html_tree_doctype_mode(token, tree->nodes->budget,
            &tree->nodes->limit_reason, &document_mode);
        if (status != LXA_OK) return status;
        status = lxa_dom_nodes_append_doctype(tree->nodes, declaration, &child);
        if (status == LXA_OK)
            status = insert_constructed(tree, tree->document,
                child, (lxa_dom_ref_t){0});
        if (status == LXA_OK) {
            tree->document_mode = document_mode;
            tree->mode = LXA_TREE_BEFORE_HTML;
        }
        return status;
    }
    if (token->kind == LXA_HTML_COMMENT
        || token->kind == LXA_HTML_PROCESSING_INSTRUCTION) {
        if (tree->mode == LXA_TREE_BEFORE_HTML) parent = tree->document;
        else if (tree->mode == LXA_TREE_AFTER_HTML) parent = tree->document;
        else if (tree->mode == LXA_TREE_AFTER_BODY) parent = tree->html;
        else if (tree->mode == LXA_TREE_IN_BODY)
            parent = current_element(tree);
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
        if (token->kind == LXA_HTML_TEXT && space_only(token->data)) {
            status = reconstruct_formatting(tree);
            if (status != LXA_OK) return status;
            return insert_data_text(tree, current_element(tree), token->data,
                                    false, false);
        }
        /* An html start uses body rules without leaving the current mode. */
        if (token->kind != LXA_HTML_START_TAG
            || !named(token->name, "html"))
            tree->mode = LXA_TREE_IN_BODY;
    }
    if (token->kind == LXA_HTML_END_TAG
        && (tree->mode == LXA_TREE_BEFORE_HTML
            || tree->mode == LXA_TREE_BEFORE_HEAD)) {
        if (!named(token->name, "head") && !named(token->name, "body")
            && !named(token->name, "html") && !named(token->name, "br"))
            return LXA_OK;
        if (tree->mode == LXA_TREE_BEFORE_HTML) {
            status = ensure_html(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_BEFORE_HEAD;
        }
        status = ensure_head(tree);
        if (status != LXA_OK) return status;
        tree->mode = LXA_TREE_IN_HEAD;
    }
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_AFTER_HEAD) {
        if (!named(token->name, "body") && !named(token->name, "html")
            && !named(token->name, "br"))
            return LXA_OK;
        status = ensure_body(tree);
        if (status != LXA_OK) return status;
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
        }
    }
    if (token->kind == LXA_HTML_START_TAG
        && tree->mode == LXA_TREE_IN_BODY) {
        if (named(token->name, "frameset")) {
            const lxa_dom_ref_t *open =
                (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
            if (!tree->frameset_ok || tree->body.handle == LXA_HANDLE_NONE
                || lxa_vector_length(&tree->open_elements) == 0
                || open[0].handle != tree->body.handle)
                return LXA_OK;
            return begin_root_frameset(tree, token, true);
        }
        if (named(token->name, "frame")) return LXA_OK;
    }
    /* A template end without an active template is ignored by the in-head
     * rule, including when that rule is reached from body/table/select. */
    if (token->kind == LXA_HTML_END_TAG
        && named(token->name, "template"))
        return LXA_OK;
    /* Ignore unmatched head ends after column-group recovery. */
    if (token->kind == LXA_HTML_END_TAG
        && named(token->name, "head")
        && tree->mode >= LXA_TREE_AFTER_HEAD) {
        return LXA_OK;
    }
    /* In-head, after-head and in-body ignore another head start. In table
     * context, fostering reprocesses it under the in-body rule as well. */
    if (token->kind == LXA_HTML_START_TAG
        && named(token->name, "head")
        && tree->mode >= LXA_TREE_IN_HEAD)
        return LXA_OK;
    if (tree->mode == LXA_TREE_IN_BODY) {
        size_t keep;
        bool ignore;
        {
            bool handled;
            status = insert_hidden_table_input(tree, token, &handled);
            if (status != LXA_OK || handled) return status;
        }
        status = plan_select_token(tree, &tree->open_elements, 0, token,
                                   &keep, &ignore);
        if (status != LXA_OK) return status;
        while (lxa_vector_length(&tree->open_elements) > keep) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        if (ignore) return LXA_OK;
    }
    if (token->kind == LXA_HTML_START_TAG && tree->mode == LXA_TREE_IN_BODY
        && lxa_vector_length(&tree->open_elements) > 1) {
        lxa_span_t top;
        status = lxa_dom_nodes_element_name(tree->nodes, current_element(tree),
                                             &top);
        if (status != LXA_OK) return status;
        if (table_context(top) && !table_part(token->name)
            && !named(token->name, "html")
            && !named(token->name, "body")
            && !named(token->name, "form")) {
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
            if (named(token->name, "plaintext"))
                return begin_root_plaintext(tree, tokenizer, token);
            if (named(token->name, "xmp"))
                return begin_root_xmp(tree, tokenizer, token);
            if (named(token->name, "textarea")
                || named(token->name, "title")
                || named(token->name, "iframe")
                || named(token->name, "noembed")
                || named(token->name, "noframes")) {
                lxa_dom_ref_t table, foster_parent, element;
                bool in_colgroup = named(top, "colgroup");
                bool textarea = named(token->name, "textarea");
                lxa_html_text_mode_t text_mode =
                    (textarea || named(token->name, "title"))
                      ? LXA_HTML_RCDATA : LXA_HTML_RAWTEXT;
                if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = foster_location(tree, &foster_parent, &table);
                if (status == LXA_OK)
                    status = lxa_dom_nodes_append_parsed_html_element(tree->nodes,
                        token->name, &element);
                if (status == LXA_OK)
                    status = apply_attributes(tree, element, token);
                if (status == LXA_OK)
                    status = insert_constructed(tree, foster_parent,
                        element, table);
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
            if (head_void(token->name)) {
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
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = lxa_budget_check(tree->nodes->budget,
                        LXA_BUDGET_WORK, 2, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = pop_element(tree);
                if (status != LXA_OK) return status;
                return foster_element(tree, token, true);
            }
            if (body_void(token->name)) {
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
            if (container && named(top, "colgroup")) {
                status = reserve_open_elements(tree, 0);
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
            if (container) return foster_element(tree, token, false);
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
    }
    if (token->kind == LXA_HTML_START_TAG) {
        bool container;
        if (named(token->name, "frame")) {
            status = ensure_body(tree);
            if (status == LXA_OK) tree->mode = LXA_TREE_IN_BODY;
            return status;
        }
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
                status = lxa_dom_nodes_append_parsed_html_element(tree->nodes,
                                                            token->name, &child);
            if (status == LXA_OK)
                status = apply_attributes(tree, child, token);
            if (status == LXA_OK)
                status = insert_constructed(tree, tree->head,
                    child, (lxa_dom_ref_t){0});
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
        if (named(token->name, "body")) {
            status = ensure_body(tree);
            if (status == LXA_OK) {
                tree->frameset_ok = false;
                status = apply_attributes(tree, tree->body, token);
            }
            if (status == LXA_OK) tree->mode = LXA_TREE_IN_BODY;
            return status;
        }
        if (named(token->name, "form")) {
            size_t keep;
            bool in_table;
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            status = plan_table_context(tree, &tree->open_elements, 0, false,
                                            &keep, &in_table);
            if (status != LXA_OK) return status;
            if (tree->form.handle != LXA_HANDLE_NONE) {
                while (lxa_vector_length(&tree->open_elements) > keep) {
                    status = pop_element(tree);
                    if (status != LXA_OK) return status;
                }
                return LXA_OK;
            }
            if (in_table) {
                const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                    tree->open_elements.bytes.data;
                if (keep == 0) return LXA_ERROR_INTERNAL;
                status = append_closed_element_at(tree, open[keep - 1],
                    (lxa_dom_ref_t){0}, token, &child);
                if (status == LXA_OK) {
                    while (lxa_vector_length(&tree->open_elements) > keep) {
                        status = pop_element(tree);
                        if (status != LXA_OK) return status;
                    }
                    tree->form = child;
                }
                return status;
            }
            status = lxa_budget_check(tree->nodes->budget,
                LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = close_paragraph(tree, NULL);
            if (status == LXA_OK) status = reserve_open_elements(tree, 1);
            if (status != LXA_OK) return status;
            status = open_container(tree, token);
            if (status == LXA_OK) tree->form = current_element(tree);
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
            tree->frameset_ok = false;
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
            if (named(token->name, "iframe")) tree->frameset_ok = false;
            return begin_text_element(tree, tokenizer, current_element(tree),
                                       token, LXA_HTML_RAWTEXT);
        }
        if (named(token->name, "xmp")) {
            return begin_root_xmp(tree, tokenizer, token);
        }
        if (named(token->name, "plaintext")) {
            return begin_root_plaintext(tree, tokenizer, token);
        }
        if (head_void(token->name)) {
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
            if (tree->frameset_ok) {
                if (named(token->name, "input")) {
                    bool hidden;
                    status = hidden_input(tree, token, &hidden);
                    if (status != LXA_OK) return status;
                    if (!hidden) tree->frameset_ok = false;
                }
                else if (!named(token->name, "param")
                         && !named(token->name, "source")
                         && !named(token->name, "track"))
                    tree->frameset_ok = false;
            }
            if (named(token->name, "hr")) {
                lxa_dom_ref_t parent, before;
                status = close_paragraph(tree, NULL);
                if (status == LXA_OK)
                    status = root_insertion_location(tree, current_element(tree),
                                                      &parent, &before);
                if (status != LXA_OK) return status;
                return append_closed_element_at(tree, parent, before, token, NULL);
            }
            else if (!named(token->name, "param")
                     && !named(token->name, "source")
                     && !named(token->name, "track")) {
                status = reconstruct_formatting(tree);
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
            if (status == LXA_OK) status = reconstruct_formatting(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            status = open_container_ns(tree, token, ns);
            if (status == LXA_OK && token->self_closing) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            return status;
        }
        status = supported_container(tree, token->name, &container);
        if (status != LXA_OK) return status;
        if (container) {
            lxa_dom_ref_t before;
            bool formatting = lxa_html_tree_formatting_tag(token->name);
            size_t drop = SIZE_MAX;
            bool reconstruct = container_reconstructs(token->name);
            bool closes_paragraph = named(token->name, "p")
                || lxa_html_tree_heading(token->name)
                || (block_container(token->name)
                    && !named(token->name, "button"))
                || named(token->name, "li") || named(token->name, "dt")
                || named(token->name, "dd");
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
            tree->mode = LXA_TREE_IN_BODY;
            if (tree->frameset_ok
                && (named(token->name, "pre") || named(token->name, "listing")
                || named(token->name, "li") || named(token->name, "dt")
                || named(token->name, "dd") || named(token->name, "button")
                || named(token->name, "select")
                || lxa_html_tree_formatting_marker_tag(token->name)))
                tree->frameset_ok = false;
            if (!formatting && !ruby_annotation(token->name)
                && !named(token->name, "button")
                && (!closes_paragraph || lxa_html_tree_heading(token->name))) {
                status = reserve_open_elements(tree, lxa_html_tree_heading(token->name) ? 0 : 1);
                if (status != LXA_OK) return status;
            }
            if (ruby_annotation(token->name)) {
                size_t keep;
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status == LXA_OK)
                    status = plan_ruby_start(tree, &tree->open_elements, 0,
                                              token->name, &keep);
                if (status != LXA_OK) return status;
                while (lxa_vector_length(&tree->open_elements) > keep) {
                    status = pop_element(tree);
                    if (status != LXA_OK) return status;
                }
                status = reserve_open_elements(tree, 1);
                if (status != LXA_OK) return status;
            }
            if (named(token->name, "li") || named(token->name, "dt")
                || named(token->name, "dd")) {
                status = close_list_item(tree, !named(token->name, "li"));
                if (status != LXA_OK) return status;
            }
            if (closes_paragraph) {
                status = close_paragraph(tree, NULL);
                if (status == LXA_OK && !lxa_html_tree_heading(token->name))
                    status = reserve_open_elements(tree, 1);
                if (status != LXA_OK) return status;
            }
            if (lxa_html_tree_heading(token->name)) {
                status = close_current_heading(tree);
                if (status == LXA_OK) status = reserve_open_elements(tree, 1);
                if (status != LXA_OK) return status;
            }
            if (named(token->name, "button")) {
                status = close_open_button(tree);
                if (status == LXA_OK) status = reserve_open_elements(tree, 1);
                if (status != LXA_OK) return status;
            }
            if (formatting) {
                status = prepare_formatting_start(tree, token, &drop);
                if (status == LXA_OK) status = reserve_open_elements(tree, 1);
            }
            else if (reconstruct) status = reconstruct_formatting(tree);
            if (status != LXA_OK) return status;
            status = root_insertion_location(tree, current_element(tree),
                                              &parent, &before);
            if (status == LXA_OK)
                status = open_container_at(tree, token, LXA_DOM_NS_HTML,
                                            parent, before);
            if (status == LXA_OK && formatting) {
                if (drop != SIZE_MAX) status = remove_formatting(tree, drop);
                if (status == LXA_OK)
                    status = append_formatting(tree, current_element(tree), false);
            }
            if (status == LXA_OK
                && (named(token->name, "pre")
                    || named(token->name, "listing")))
                tree->skip_pre_newline = true;
            return status;
        }
        assert(false);
        return LXA_ERROR_INTERNAL;
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
    if (token->kind == LXA_HTML_END_TAG
        && (named(token->name, "body") || named(token->name, "html"))
        && tree->mode == LXA_TREE_IN_BODY) {
        lxa_vector_t elements = tree->open_elements;
        size_t index;
        bool column_group;
        status = current_html_column_group(tree, &elements, 0, &column_group);
        if (status != LXA_OK) return status;
        if (column_group) elements.bytes.length -= sizeof(lxa_dom_ref_t);
        status = find_scoped_end_target(tree, &elements, 0,
            literal("body"), LXA_HTML_SCOPE_NORMAL, &index);
        if (status == LXA_OK && column_group)
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                        &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (column_group) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        if (index == lxa_vector_length(&elements)) return LXA_OK;
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
                 && tree->mode != LXA_TREE_AFTER_BODY) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        tree->mode = LXA_TREE_AFTER_HTML;
        return LXA_OK;
    }
    if (token->kind == LXA_HTML_END_TAG && table_part(token->name)
        && tree->mode == LXA_TREE_IN_BODY)
        return named(token->name, "col") ? LXA_OK
               : end_scoped_element(tree, token->name, LXA_HTML_SCOPE_TABLE);
    if (token->kind == LXA_HTML_END_TAG && lxa_html_tree_formatting_tag(token->name)
        && tree->mode == LXA_TREE_IN_BODY) {
        bool column_group;
        status = current_html_column_group(tree, &tree->open_elements, 0,
                                            &column_group);
        if (status != LXA_OK) return status;
        if (column_group) {
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 2,
                                        &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        return adopt_formatting(tree, token->name);
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "form")
        && tree->mode == LXA_TREE_IN_BODY) {
        lxa_vector_t view = tree->open_elements;
        bool column_group;
        lxa_dom_ref_t form = tree->form;
        const lxa_dom_ref_t *open = (const lxa_dom_ref_t *)
                                  tree->open_elements.bytes.data;
        size_t index, keep;
        status = current_html_column_group(tree, &view, 0, &column_group);
        if (status != LXA_OK) return status;
        if (column_group) view.bytes.length -= sizeof(lxa_dom_ref_t);
        keep = lxa_vector_length(&view);
        index = keep;
        if (form.handle != LXA_HANDLE_NONE)
            status = find_scoped_reference(tree, &view, 0, form, &index);
        if (status == LXA_OK && column_group)
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
                                        &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (index == keep) {
            if (column_group) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            tree->form = (lxa_dom_ref_t){0};
            return LXA_OK;
        }
        while (keep > index + 1) {
            lxa_span_t name;
            lxa_dom_namespace_t ns;
            status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK,
                                        1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_namespace(tree->nodes,
                    open[keep - 1], &ns);
            if (status == LXA_OK)
                status = lxa_dom_nodes_element_name(tree->nodes,
                    open[keep - 1], &name);
            if (status != LXA_OK) return status;
            if (ns != LXA_DOM_NS_HTML || !lxa_html_tree_implied_end_tag(name)) break;
            --keep;
        }
        tree->form = (lxa_dom_ref_t){0};
        while (lxa_vector_length(&tree->open_elements) > keep) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        /* Ordinary descendants retain their open references and DOM parents. */
        return remove_open_element(tree, &tree->open_elements, index);
    }
    if (token->kind == LXA_HTML_END_TAG && named(token->name, "p")
        && tree->mode == LXA_TREE_IN_BODY) {
        bool closed, column_group;
        lxa_dom_namespace_t ns;
        lxa_span_t top;
        lxa_dom_ref_t before;
        status = close_paragraph(tree, &closed);
        if (status != LXA_OK || closed) return status;
        /* A stray </p> implies an empty paragraph at the insertion point. */
        if (lxa_vector_length(&tree->open_elements) == 0) {
            status = ensure_body(tree);
            if (status != LXA_OK) return status;
        }
        status = lxa_dom_nodes_element_namespace(tree->nodes,
            current_element(tree), &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes,
                current_element(tree), &top);
        if (status != LXA_OK) return status;
        column_group = ns == LXA_DOM_NS_HTML && named(top, "colgroup");
        status = reserve_open_elements(tree, column_group ? 0 : 1);
        if (status == LXA_OK)
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                       1, &tree->nodes->limit_reason);
        if (status != LXA_OK) return status;
        status = root_insertion_location(tree, current_element(tree),
                                          &parent, &before);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_parsed_html_element(tree->nodes,
                literal("p"), &child);
        if (status == LXA_OK)
            status = insert_constructed(tree, parent,
                child, before);
        if (status == LXA_OK) {
            if (column_group) {
                status = pop_element(tree);
                if (status != LXA_OK) return status;
            }
            status = push_element(tree, child);
        }
        if (status == LXA_OK) {
            status = pop_element(tree);
            if (status != LXA_OK) return status;
        }
        return status;
    }
    /* The heading end-tag rule accepts any h1-h6 in scope, and ignores a
     * stray heading end without creating content. */
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY && lxa_html_tree_heading(token->name))
        return end_scoped_element(tree, token->name, LXA_HTML_SCOPE_NORMAL);
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && (block_container(token->name) || lxa_html_tree_formatting_marker_tag(token->name)))
        return end_scoped_element(tree, token->name, LXA_HTML_SCOPE_NORMAL);
    if (token->kind == LXA_HTML_END_TAG
        && tree->mode == LXA_TREE_IN_BODY
        && (named(token->name, "li") || named(token->name, "dt")
            || named(token->name, "dd")))
        return end_scoped_element(tree, token->name,
            named(token->name, "li") ? LXA_HTML_SCOPE_LIST_ITEM : LXA_HTML_SCOPE_NORMAL);
    if (token->kind == LXA_HTML_END_TAG && tree->mode == LXA_TREE_IN_BODY)
        return end_ordinary_element(tree, token->name);
    if (token->kind == LXA_HTML_TEXT) {
        if (tree->mode == LXA_TREE_BEFORE_HTML
            || tree->mode == LXA_TREE_BEFORE_HEAD
            || tree->mode == LXA_TREE_IN_HEAD
            || tree->mode == LXA_TREE_AFTER_HEAD) {
            adjusted = *token;
            status = prepare_prebody_text(tree, &adjusted.data);
            if (status != LXA_OK) return status;
            token = &adjusted;
        }
        if (token->data.length == 0) return LXA_OK;
        status = ensure_body(tree);
        if (status != LXA_OK) return status;
        tree->mode = LXA_TREE_IN_BODY;
        if (tree->frameset_ok && !null_or_space_only(token->data))
            tree->frameset_ok = false;
        if (lxa_vector_length(&tree->open_elements) > 1) {
            lxa_span_t top;
            status = lxa_dom_nodes_element_name(tree->nodes,
                                                current_element(tree), &top);
            if (status != LXA_OK) return status;
            if (table_context(top))
                return insert_data_text(tree, current_element(tree),
                                        token->data, false, true);
        }
        if (null_only(token->data))
            return LXA_OK;
        status = reconstruct_formatting(tree);
        if (status != LXA_OK) return status;
        return insert_data_text(tree, current_element(tree), token->data,
                                false, false);
    }
    assert(false);
    return LXA_ERROR_INTERNAL;
}

lxa_status_t
lxa_html_tree_on_token(void *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token)
{
    lxa_html_tree_t *tree = context;
    if (tree == NULL || tree->nodes == NULL) return LXA_ERROR_ARGUMENT;
    if (tree->failure != LXA_OK) return tree->failure;
    if ((unsigned) tree->mode > LXA_TREE_AFTER_AFTER_FRAMESET) {
        assert(false);
        tree->failure = LXA_ERROR_INTERNAL;
        return tree->failure;
    }
    if (tree->mode == LXA_TREE_DONE) return LXA_ERROR_ARGUMENT;
    if (((tree->mode == LXA_TREE_TEMPLATE_TEXT
          || (tree->mode == LXA_TREE_TEXT
              && tree->text_return_mode == LXA_TREE_TEMPLATE_TEXT))
         != (tree->template_fragment.handle != LXA_HANDLE_NONE))
        || (unsigned) tree->document_mode > LXA_HTML_QUIRKS
        || (tree->mode == LXA_TREE_TEXT
            && (tree->text_element.handle == LXA_HANDLE_NONE
                || (tree->text_return_mode != LXA_TREE_IN_HEAD
                    && tree->text_return_mode != LXA_TREE_HEAD_NOSCRIPT
                    && tree->text_return_mode != LXA_TREE_TEMPLATE_TEXT
                    && tree->text_return_mode != LXA_TREE_AFTER_HEAD
                    && tree->text_return_mode != LXA_TREE_IN_BODY
                    && tree->text_return_mode != LXA_TREE_IN_FRAMESET
                    && tree->text_return_mode != LXA_TREE_AFTER_FRAMESET
                    && tree->text_return_mode != LXA_TREE_AFTER_AFTER_FRAMESET)))) {
        assert(false);
        tree->failure = LXA_ERROR_INTERNAL;
        return tree->failure;
    }
    if (tree->template_fragment.handle != LXA_HANDLE_NONE
        && ((unsigned) tree->template_insertion_mode > LXA_TEMPLATE_BODY
            || !template_boundary_valid(tree)
            || (tree->template_return_mode != LXA_TREE_IN_HEAD
                && tree->template_return_mode != LXA_TREE_AFTER_HEAD
                && tree->template_return_mode != LXA_TREE_IN_BODY))) {
        assert(false);
        tree->failure = LXA_ERROR_INTERNAL;
        return tree->failure;
    }
    tree->failure = consume(tree, tokenizer, token);
    if (tree->failure == LXA_OK && tokenizer != NULL
        && token != NULL && token->kind != LXA_HTML_EOF) {
        lxa_dom_namespace_t ns = LXA_DOM_NS_HTML;
        lxa_dom_ref_t current = {0};
        if (tree->template_fragment.handle != LXA_HANDLE_NONE) {
            if (lxa_vector_length(&tree->template_elements)
                > template_boundary(tree))
                current = template_parent(tree);
        }
        else if (lxa_vector_length(&tree->open_elements) != 0)
            current = current_element(tree);
        if (current.handle != LXA_HANDLE_NONE)
            tree->failure = lxa_dom_nodes_element_namespace(
                tree->nodes, current, &ns);
        if (tree->failure == LXA_OK
            && lxa_html_tokenizer_allow_cdata(tokenizer,
                ns != LXA_DOM_NS_HTML) != LXA_HTML_OK)
            tree->failure = LXA_ERROR_INTERNAL;
    }
    /* A rejected store reference or token from our tokenizer is a pipeline
     * failure, not an invalid caller argument. */
    if (tree->failure == LXA_ERROR_ARGUMENT)
        tree->failure = LXA_ERROR_INTERNAL;
    return tree->failure;
}
