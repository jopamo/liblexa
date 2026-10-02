#include <assert.h>
#include <string.h>
#include "dom/node_store_internal.h"
#include "html/foreign_names.h"
#include "html/foreign_context.h"
#include "html/tree_categories.h"
#include "html/formatting_policy.h"
#include "html/formatting_marker.h"
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
root_create(lxa_html_tree_t *tree, lxa_html_root_element_t element)
{
    switch (element) {
        case LXA_HTML_ROOT_ELEMENT_HTML:
            return ensure_element(tree, tree->document, "html", &tree->html);
        case LXA_HTML_ROOT_ELEMENT_HEAD:
            return ensure_element(tree, tree->html, "head", &tree->head);
        case LXA_HTML_ROOT_ELEMENT_BODY:
            return ensure_element(tree, tree->html, "body", &tree->body);
        default: assert(false); return LXA_ERROR_INTERNAL;
    }
}

static lxa_status_t
root_open_body(lxa_html_tree_t *tree)
{
    lxa_status_t status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS,
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

#define LXA_ROOT_ENSURE_NAME(name) ensure_##name
#define LXA_ROOT_ENSURE_VIEW lxa_html_tree_t
#define LXA_ROOT_ENSURE_HAS(view, element) LXA_ROOT_HAS_##element(view)
#define LXA_ROOT_HAS_HTML(view) ((view)->html.handle != LXA_HANDLE_NONE)
#define LXA_ROOT_HAS_HEAD(view) ((view)->head.handle != LXA_HANDLE_NONE)
#define LXA_ROOT_HAS_BODY(view) ((view)->body.handle != LXA_HANDLE_NONE)
#define LXA_ROOT_ENSURE_CREATE root_create
#define LXA_ROOT_ENSURE_OPEN_LENGTH(view) lxa_vector_length(&(view)->open_elements)
#define LXA_ROOT_ENSURE_OPEN_BODY root_open_body
#define LXA_ROOT_ENSURE_FRAMESET(view) ((view)->frameset_ok = true)
#include "html/root_ensure.inc"
#undef LXA_ROOT_ENSURE_NAME
#undef LXA_ROOT_ENSURE_VIEW
#undef LXA_ROOT_ENSURE_HAS
#undef LXA_ROOT_HAS_HTML
#undef LXA_ROOT_HAS_HEAD
#undef LXA_ROOT_HAS_BODY
#undef LXA_ROOT_ENSURE_CREATE
#undef LXA_ROOT_ENSURE_OPEN_LENGTH
#undef LXA_ROOT_ENSURE_OPEN_BODY
#undef LXA_ROOT_ENSURE_FRAMESET

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
    lxa_status_t status;
    if (!lxa_html_formatting_marker_valid(length, marker)) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    previous = marker == SIZE_MAX ? SIZE_MAX : entries[marker].previous_marker;
    lxa_html_formatting_clear_plan_t plan;
    status = lxa_html_formatting_clear_plan(length, marker, previous,
        tree->nodes->budget->used[LXA_BUDGET_ACTIVE_FORMATTING], &plan);
    if (status != LXA_OK) return status;
    status = lxa_vector_erase(&tree->formatting, plan.keep, plan.removed);
    if (status == LXA_OK)
        status = lxa_budget_release(tree->nodes->budget,
                                   LXA_BUDGET_ACTIVE_FORMATTING, plan.removed);
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    tree->formatting_marker = plan.previous;
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
tree_pop_table(lxa_html_tree_t *, size_t, bool);

#define LXA_FRAMESET_END_NAME tree_frameset_end
#define LXA_FRAMESET_END_VIEW lxa_html_tree_t
#define LXA_FRAMESET_END_POP(view) pop_element(view)
#define LXA_FRAMESET_END_LENGTH(view) lxa_vector_length(&(view)->open_elements)
#include "html/frameset_end.inc"
#undef LXA_FRAMESET_END_NAME
#undef LXA_FRAMESET_END_VIEW
#undef LXA_FRAMESET_END_POP
#undef LXA_FRAMESET_END_LENGTH

static lxa_status_t
pop_table_elements(lxa_html_tree_t *tree, size_t keep)
{
    return tree_pop_table(tree, keep, false);
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
    return ns == LXA_DOM_NS_HTML && lxa_html_tree_table_context(name)
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
    return ns == LXA_DOM_NS_HTML && lxa_html_tree_table_context(name)
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
    if (!lxa_html_formatting_entry_removable(lxa_vector_length(&tree->formatting),
                                             tree->formatting_marker, index)
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

static inline lxa_status_t
formatting_reference(const lxa_html_tree_t *tree, size_t index, uint64_t *identity)
{
    const lxa_html_formatting_entry_t *entries =
        (const lxa_html_formatting_entry_t *) tree->formatting.bytes.data;
    *identity = entries[index].element.handle;
    return LXA_OK;
}

#define LXA_FORMATTING_REFERENCE_NAME tree_find_formatting_reference
#define LXA_FORMATTING_REFERENCE_VIEW lxa_html_tree_t
#define LXA_FORMATTING_REFERENCE_LENGTH(view) lxa_vector_length(&(view)->formatting)
#define LXA_FORMATTING_REFERENCE_READ formatting_reference
#define LXA_FORMATTING_REFERENCE_BUDGET(view) ((view)->nodes->budget)
#define LXA_FORMATTING_REFERENCE_REASON(view) (&(view)->nodes->limit_reason)
#include "html/formatting_reference.inc"
#undef LXA_FORMATTING_REFERENCE_NAME
#undef LXA_FORMATTING_REFERENCE_VIEW
#undef LXA_FORMATTING_REFERENCE_LENGTH
#undef LXA_FORMATTING_REFERENCE_READ
#undef LXA_FORMATTING_REFERENCE_BUDGET
#undef LXA_FORMATTING_REFERENCE_REASON

static lxa_status_t
find_formatting_reference(lxa_html_tree_t *tree, lxa_dom_ref_t ref, size_t *index)
{
    return tree_find_formatting_reference(tree, formatting_start(tree), ref.handle, index);
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
    if (!lxa_html_formatting_marker_valid(index, entry.previous_marker)) {
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

typedef struct {
    lxa_html_tree_t *tree;
    lxa_dom_ref_t element;
} tree_formatting_view_t;

static inline lxa_status_t
tree_formatting_name(const tree_formatting_view_t *view, lxa_span_t *name)
{
    return lxa_dom_nodes_element_name(view->tree->nodes, view->element, name);
}

static inline lxa_status_t
tree_formatting_next(const tree_formatting_view_t *view, lxa_dom_ref_t cursor,
                     lxa_dom_ref_t *out)
{
    return lxa_dom_nodes_attribute_next(view->tree->nodes, view->element, cursor, out);
}

static inline lxa_status_t
tree_formatting_read(const tree_formatting_view_t *view, lxa_dom_ref_t cursor,
                     lxa_span_t *name, lxa_span_t *value)
{
    return lxa_dom_nodes_attribute_read(view->tree->nodes, cursor, name, value);
}

#define LXA_FORMATTING_EQUAL_NAME tree_formatting_equal
#define LXA_FORMATTING_VIEW tree_formatting_view_t
#define LXA_FORMATTING_CURSOR lxa_dom_ref_t
#define LXA_FORMATTING_DONE(cursor) ((cursor).handle == LXA_HANDLE_NONE)
#define LXA_FORMATTING_NAME tree_formatting_name
#define LXA_FORMATTING_NEXT tree_formatting_next
#define LXA_FORMATTING_READ tree_formatting_read
#define LXA_FORMATTING_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_FORMATTING_REASON(view) (&(view)->tree->nodes->limit_reason)
#include "html/formatting_equal.inc"
#undef LXA_FORMATTING_EQUAL_NAME
#undef LXA_FORMATTING_VIEW
#undef LXA_FORMATTING_CURSOR
#undef LXA_FORMATTING_DONE
#undef LXA_FORMATTING_NAME
#undef LXA_FORMATTING_NEXT
#undef LXA_FORMATTING_READ
#undef LXA_FORMATTING_BUDGET
#undef LXA_FORMATTING_REASON

typedef struct {
    lxa_html_tree_t *tree;
    size_t length;
} tree_formatting_list_view_t;

static inline lxa_status_t
tree_formatting_list_open(const tree_formatting_list_view_t *view, size_t index, bool *open)
{
    return reference_is_open(view->tree, formatting_entries(view->tree)[index].element, open);
}

static inline lxa_status_t
tree_formatting_list_name(const tree_formatting_list_view_t *view, size_t index,
                           lxa_span_t *name)
{
    return lxa_dom_nodes_element_name(view->tree->nodes,
        formatting_entries(view->tree)[index].element, name);
}

static inline lxa_status_t
tree_formatting_list_equal(const tree_formatting_list_view_t *view, size_t index,
                            const lxa_html_token_t *token, bool *equal)
{
    const tree_formatting_view_t item = {
        view->tree, formatting_entries(view->tree)[index].element
    };
    return tree_formatting_equal(&item, token, equal);
}

#define LXA_NOAH_PLAN_NAME tree_noah_plan
#define LXA_NOAH_VIEW tree_formatting_list_view_t
#define LXA_NOAH_LENGTH(view) ((view)->length)
#define LXA_NOAH_EQUAL tree_formatting_list_equal
#define LXA_NOAH_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_NOAH_REASON(view) (&(view)->tree->nodes->limit_reason)
#include "html/formatting_noah.inc"
#undef LXA_NOAH_PLAN_NAME
#undef LXA_NOAH_VIEW
#undef LXA_NOAH_LENGTH
#undef LXA_NOAH_EQUAL
#undef LXA_NOAH_BUDGET
#undef LXA_NOAH_REASON

#define LXA_FORMATTING_FIND_NAME tree_find_formatting
#define LXA_FORMATTING_FIND_VIEW tree_formatting_list_view_t
#define LXA_FORMATTING_FIND_LENGTH(view) ((view)->length)
#define LXA_FORMATTING_FIND_READ tree_formatting_list_name
#define LXA_FORMATTING_FIND_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_FORMATTING_FIND_REASON(view) (&(view)->tree->nodes->limit_reason)
#include "html/formatting_find.inc"
#undef LXA_FORMATTING_FIND_NAME
#undef LXA_FORMATTING_FIND_VIEW
#undef LXA_FORMATTING_FIND_LENGTH
#undef LXA_FORMATTING_FIND_READ
#undef LXA_FORMATTING_FIND_BUDGET
#undef LXA_FORMATTING_FIND_REASON

#define LXA_RECONSTRUCT_PLAN_NAME tree_reconstruct_plan
#define LXA_RECONSTRUCT_VIEW tree_formatting_list_view_t
#define LXA_RECONSTRUCT_LENGTH(view) ((view)->length)
#define LXA_RECONSTRUCT_OPEN tree_formatting_list_open
#include "html/formatting_reconstruct.inc"
#undef LXA_RECONSTRUCT_PLAN_NAME
#undef LXA_RECONSTRUCT_VIEW
#undef LXA_RECONSTRUCT_LENGTH
#undef LXA_RECONSTRUCT_OPEN

static lxa_status_t
find_formatting(lxa_html_tree_t *tree, lxa_span_t target, size_t *index)
{
    const tree_formatting_list_view_t view = {tree, lxa_vector_length(&tree->formatting)};
    return tree_find_formatting(&view, formatting_start(tree), target, index);
}

static lxa_status_t
plan_formatting_add(lxa_html_tree_t *tree, const lxa_html_token_t *token,
                     size_t *drop)
{
    const tree_formatting_list_view_t view = {tree, lxa_vector_length(&tree->formatting)};
    size_t planned;
    lxa_status_t status = tree_noah_plan(&view, formatting_start(tree), token, &planned);
    if (status == LXA_OK) status = reserve_formatting(tree, planned == SIZE_MAX ? 1 : 0);
    if (status == LXA_OK) *drop = planned;
    return status;
}

static lxa_status_t
template_start_mode(lxa_html_tree_t *tree, const lxa_html_token_t *token,
                     lxa_dom_namespace_t ns)
{
    return lxa_html_tree_template_start_mode(tree->template_insertion_mode,
        token->name, ns, &tree->template_insertion_mode);
}

static lxa_status_t
check_template_depth(lxa_html_tree_t *tree, size_t templates, size_t elements,
                      bool in_body)
{
    size_t ancestors = 0;
    if (in_body) {
        size_t root = lxa_vector_length(&tree->open_elements);
        if (root == 0) return LXA_ERROR_INTERNAL;
        ancestors = root - 1;
    }
    return lxa_html_tree_template_depth_check(tree->nodes->budget, ancestors,
        templates, elements, &tree->nodes->limit_reason);
}

static lxa_status_t
check_template_elements(lxa_html_tree_t *tree, size_t elements)
{
    size_t templates;
    lxa_status_t status = lxa_size_add(lxa_vector_length(&tree->template_stack),
                                       1, &templates);
    if (status == LXA_OK)
        status = check_template_depth(tree, templates, elements,
            tree->template_return_mode == LXA_TREE_IN_BODY);
    return status;
}

static lxa_status_t
reserve_formatting_elements(lxa_html_tree_t *tree, size_t count)
{
    size_t required;
    lxa_status_t status;
    if (tree->mode != LXA_TREE_TEMPLATE_TEXT)
        return reserve_open_elements(tree, count);
    status = lxa_size_add(lxa_vector_length(&tree->template_elements), count,
                          &required);
    if (status == LXA_OK)
        status = check_template_elements(tree, required);
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
    const tree_formatting_list_view_t view = {tree, lxa_vector_length(&tree->formatting)};
    size_t i, end = view.length;
    lxa_status_t status = tree_reconstruct_plan(&view, formatting_start(tree), &i);
    if (status != LXA_OK) return status;
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

static lxa_status_t
root_data_table(lxa_html_tree_t *tree, bool *table)
{
    lxa_span_t name;
    lxa_status_t status = lxa_dom_nodes_element_name(tree->nodes, current_element(tree), &name);
    if (status == LXA_OK) *table = lxa_html_tree_table_context(name);
    return status;
}

#define LXA_ROOT_DATA_NAME tree_prepare_root_data
#define LXA_ROOT_DATA_VIEW lxa_html_tree_t
#define LXA_ROOT_DATA_BODY ensure_body
#define LXA_ROOT_DATA_MODE(view) ((view)->mode)
#define LXA_ROOT_DATA_FRAMESET(view) ((view)->frameset_ok)
#define LXA_ROOT_DATA_LENGTH(view) lxa_vector_length(&(view)->open_elements)
#define LXA_ROOT_DATA_TABLE_QUERY root_data_table
#define LXA_ROOT_DATA_RECONSTRUCT reconstruct_formatting
#include "html/root_data_prepare.inc"
#undef LXA_ROOT_DATA_NAME
#undef LXA_ROOT_DATA_VIEW
#undef LXA_ROOT_DATA_BODY
#undef LXA_ROOT_DATA_MODE
#undef LXA_ROOT_DATA_FRAMESET
#undef LXA_ROOT_DATA_LENGTH
#undef LXA_ROOT_DATA_TABLE_QUERY
#undef LXA_ROOT_DATA_RECONSTRUCT

static lxa_status_t plan_list_item(lxa_html_tree_t *, const lxa_vector_t *,
                                   size_t, bool, size_t *);

/* A new item closes the preceding item in this list, including any
 * still-open descendants. An inner list stops the search at its own root. */
static lxa_status_t
close_list_item(lxa_html_tree_t *tree, bool definition)
{
    size_t length = lxa_vector_length(&tree->open_elements), index;
    if (length <= 1) return LXA_OK;
    lxa_status_t status = plan_list_item(tree, &tree->open_elements, 1, definition, &index);
    if (status != LXA_OK) return status;
    while (lxa_vector_length(&tree->open_elements) > index) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
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

static lxa_status_t
insert_missing_root_paragraph(lxa_html_tree_t *tree)
{
    lxa_dom_namespace_t ns;
    lxa_span_t top;
    lxa_dom_ref_t parent, before, child;
    lxa_status_t status = lxa_dom_nodes_element_namespace(tree->nodes, current_element(tree), &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(tree->nodes, current_element(tree), &top);
    if (status != LXA_OK) return status;
    bool column_group = ns == LXA_DOM_NS_HTML && named(top, "colgroup");
    status = reserve_open_elements(tree, column_group ? 0 : 1);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = root_insertion_location(tree, current_element(tree), &parent, &before);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_parsed_html_element(tree->nodes, literal("p"), &child);
    if (status == LXA_OK) status = insert_constructed(tree, parent, child, before);
    if (status == LXA_OK && column_group) status = pop_element(tree);
    if (status == LXA_OK) status = push_element(tree, child);
    if (status == LXA_OK) status = pop_element(tree);
    return status;
}

#define LXA_ROOT_PARAGRAPH_END_NAME tree_end_root_paragraph
#define LXA_ROOT_PARAGRAPH_END_VIEW lxa_html_tree_t
#define LXA_ROOT_PARAGRAPH_END_CLOSE close_paragraph
#define LXA_ROOT_PARAGRAPH_END_LENGTH(view) lxa_vector_length(&(view)->open_elements)
#define LXA_ROOT_PARAGRAPH_END_BODY ensure_body
#define LXA_ROOT_PARAGRAPH_END_MISSING insert_missing_root_paragraph
#include "html/root_paragraph_end.inc"
#undef LXA_ROOT_PARAGRAPH_END_NAME
#undef LXA_ROOT_PARAGRAPH_END_VIEW
#undef LXA_ROOT_PARAGRAPH_END_CLOSE
#undef LXA_ROOT_PARAGRAPH_END_LENGTH
#undef LXA_ROOT_PARAGRAPH_END_BODY
#undef LXA_ROOT_PARAGRAPH_END_MISSING

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

static inline lxa_status_t
tree_root_table_read(const lxa_html_tree_scope_view_t *view, size_t index,
    lxa_span_t *name, lxa_dom_namespace_t *ns)
{
    lxa_status_t status = lxa_dom_nodes_element_namespace(view->tree->nodes,
        view->entries[index], ns);
    if (status == LXA_OK && *ns == LXA_DOM_NS_HTML)
        status = lxa_dom_nodes_element_name(view->tree->nodes, view->entries[index], name);
    return status;
}

#define LXA_ROOT_TABLE_PART_CONTEXT_NAME tree_root_table_part_context_plan
#define LXA_ROOT_TABLE_PART_CONTEXT_VIEW lxa_html_tree_scope_view_t
#define LXA_ROOT_TABLE_PART_CONTEXT_LENGTH(view) ((view)->length)
#define LXA_ROOT_TABLE_PART_CONTEXT_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_ROOT_TABLE_PART_CONTEXT_REASON(view) (&(view)->tree->nodes->limit_reason)
#define LXA_ROOT_TABLE_PART_CONTEXT_READ tree_scope_read
#include "html/root_table_part_context_plan.inc"
#undef LXA_ROOT_TABLE_PART_CONTEXT_NAME
#undef LXA_ROOT_TABLE_PART_CONTEXT_VIEW
#undef LXA_ROOT_TABLE_PART_CONTEXT_LENGTH
#undef LXA_ROOT_TABLE_PART_CONTEXT_BUDGET
#undef LXA_ROOT_TABLE_PART_CONTEXT_REASON
#undef LXA_ROOT_TABLE_PART_CONTEXT_READ

static inline lxa_status_t
tree_root_part_close_read(const lxa_html_tree_scope_view_t *view, size_t index,
    lxa_span_t *name, lxa_dom_namespace_t *ns)
{
    lxa_status_t status = lxa_dom_nodes_element_name(view->tree->nodes,
        view->entries[index], name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_namespace(view->tree->nodes, view->entries[index], ns);
    return status;
}

#define LXA_ROOT_PART_CLOSE_NAME tree_root_table_part_close_plan
#define LXA_ROOT_PART_CLOSE_VIEW lxa_html_tree_scope_view_t
#define LXA_ROOT_PART_CLOSE_LENGTH(view) ((view)->length)
#define LXA_ROOT_PART_CLOSE_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_ROOT_PART_CLOSE_REASON(view) (&(view)->tree->nodes->limit_reason)
#define LXA_ROOT_PART_CLOSE_READ tree_root_part_close_read
#include "html/root_table_part_close_plan.inc"
#undef LXA_ROOT_PART_CLOSE_NAME
#undef LXA_ROOT_PART_CLOSE_VIEW
#undef LXA_ROOT_PART_CLOSE_LENGTH
#undef LXA_ROOT_PART_CLOSE_BUDGET
#undef LXA_ROOT_PART_CLOSE_REASON
#undef LXA_ROOT_PART_CLOSE_READ

#define LXA_ROOT_TABLE_CLOSE_NAME tree_root_table_close_plan
#define LXA_ROOT_TABLE_CLOSE_VIEW lxa_html_tree_scope_view_t
#define LXA_ROOT_TABLE_CLOSE_LENGTH(view) ((view)->length)
#define LXA_ROOT_TABLE_CLOSE_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_ROOT_TABLE_CLOSE_REASON(view) (&(view)->tree->nodes->limit_reason)
#define LXA_ROOT_TABLE_CLOSE_READ tree_root_table_read
#include "html/root_table_close_plan.inc"
#undef LXA_ROOT_TABLE_CLOSE_NAME
#undef LXA_ROOT_TABLE_CLOSE_VIEW
#undef LXA_ROOT_TABLE_CLOSE_LENGTH
#undef LXA_ROOT_TABLE_CLOSE_BUDGET
#undef LXA_ROOT_TABLE_CLOSE_REASON
#undef LXA_ROOT_TABLE_CLOSE_READ

#define LXA_SPECIAL_NAME tree_special_find
#define LXA_SPECIAL_VIEW lxa_html_tree_scope_view_t
#define LXA_SPECIAL_LENGTH(view) ((view)->length)
#define LXA_SPECIAL_READ tree_scope_read
#define LXA_SPECIAL_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_SPECIAL_REASON(view) (&(view)->tree->nodes->limit_reason)
#include "html/special_find.inc"
#undef LXA_SPECIAL_NAME
#undef LXA_SPECIAL_VIEW
#undef LXA_SPECIAL_LENGTH
#undef LXA_SPECIAL_READ
#undef LXA_SPECIAL_BUDGET
#undef LXA_SPECIAL_REASON

#define LXA_SCOPE_NAME tree_scope_search
#define LXA_ORDINARY_NAME tree_ordinary_end_search
#define LXA_COLUMN_GROUP_NAME tree_current_column_group
#define LXA_SCOPE_PLAN_NAME tree_scope_plan
#define LXA_ORDINARY_PLAN_NAME tree_ordinary_end_plan
#define LXA_OPEN_REFERENCE_NAME tree_open_reference_search
#define LXA_SCOPED_REFERENCE_NAME tree_scoped_reference_search
#define LXA_IMPLIED_PLAN_NAME tree_implied_end_plan
#define LXA_RUBY_PLAN_NAME tree_ruby_start_plan
#define LXA_SELECT_PLAN_NAME tree_select_plan
#define LXA_TABLE_CONTEXT_PLAN_NAME tree_table_context_plan
#define LXA_TEMPLATE_MISSING_TABLE_PLAN_NAME tree_template_missing_table_plan
#define LXA_TEMPLATE_END_PLAN_NAME tree_template_end_plan
#define LXA_TEMPLATE_TABLE_SCAN_PLAN_NAME tree_template_table_scan
#define LXA_TEMPLATE_MATHML_TABLE_PLAN_NAME tree_template_mathml_table_plan
#define LXA_FOSTERED_SUFFIX_CHECK_NAME tree_fostered_suffix_check
#define LXA_VIRTUAL_TABLE_SUFFIX_CHECK_NAME tree_virtual_table_suffix_check
#define LXA_VIRTUAL_TABLE_ITEM_PLAN_NAME tree_virtual_table_item_plan
#define LXA_TEMPLATE_TABLE_START_PLAN_NAME tree_template_table_start_plan
#define LXA_TEMPLATE_CLOSE_PLAN_NAME tree_template_close_plan
#define LXA_TABLE_MARKERS_PLAN_NAME tree_table_markers_plan
#define LXA_COLUMN_START_PLAN_NAME tree_column_start_plan
#define LXA_FORM_END_PLAN_NAME tree_form_end_plan
#define LXA_LIST_ITEM_PLAN_NAME tree_list_item_plan
#define LXA_SCOPE_NAMESPACE(view, index, ns) \
    lxa_dom_nodes_element_namespace((view)->tree->nodes, (view)->entries[index], ns)
#define LXA_SCOPE_IDENTITY tree_scope_identity
#define LXA_SCOPE_VIEW lxa_html_tree_scope_view_t
#define LXA_SCOPE_LENGTH(view) ((view)->length)
#define LXA_SCOPE_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_SCOPE_REASON(view) (&(view)->tree->nodes->limit_reason)
#define LXA_SCOPE_READ tree_scope_read
#define LXA_SCOPE_READ_NAME(view, index, name) \
    lxa_dom_nodes_element_name((view)->tree->nodes, (view)->entries[index], name)
#include "html/end_target_find.inc"
#undef LXA_SCOPE_READ
#undef LXA_SCOPE_READ_NAME
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
#undef LXA_IMPLIED_PLAN_NAME
#undef LXA_RUBY_PLAN_NAME
#undef LXA_SELECT_PLAN_NAME
#undef LXA_TABLE_CONTEXT_PLAN_NAME
#undef LXA_TEMPLATE_MISSING_TABLE_PLAN_NAME
#undef LXA_TEMPLATE_END_PLAN_NAME
#undef LXA_TEMPLATE_TABLE_SCAN_PLAN_NAME
#undef LXA_TEMPLATE_MATHML_TABLE_PLAN_NAME
#undef LXA_FOSTERED_SUFFIX_CHECK_NAME
#undef LXA_VIRTUAL_TABLE_SUFFIX_CHECK_NAME
#undef LXA_VIRTUAL_TABLE_ITEM_PLAN_NAME
#undef LXA_TEMPLATE_TABLE_START_PLAN_NAME
#undef LXA_TEMPLATE_CLOSE_PLAN_NAME
#undef LXA_TABLE_MARKERS_PLAN_NAME
#undef LXA_COLUMN_START_PLAN_NAME
#undef LXA_FORM_END_PLAN_NAME
#undef LXA_LIST_ITEM_PLAN_NAME
#undef LXA_SCOPE_NAMESPACE
#undef LXA_SCOPE_IDENTITY

#define LXA_TABLE_POP_NAME tree_table_recovery
#define LXA_TABLE_POP_VIEW lxa_html_tree_scope_view_t
#define LXA_TABLE_POP_HAS_MARKER(view) ((view)->tree->formatting_marker != SIZE_MAX)
#define LXA_TABLE_POP_PLAN tree_table_markers_plan
#define LXA_TABLE_POP_ELEMENTS(view, keep, template_context) \
    pop_elements((view)->tree, template_context ? &(view)->tree->template_elements \
                                              : &(view)->tree->open_elements, keep)
#define LXA_TABLE_POP_CLEAR(view) clear_formatting_to_marker((view)->tree)
#include "html/table_pop.inc"
#undef LXA_TABLE_POP_NAME
#undef LXA_TABLE_POP_VIEW
#undef LXA_TABLE_POP_HAS_MARKER
#undef LXA_TABLE_POP_PLAN
#undef LXA_TABLE_POP_ELEMENTS
#undef LXA_TABLE_POP_CLEAR

static lxa_status_t
tree_pop_table(lxa_html_tree_t *tree, size_t keep, bool template_context)
{
    const lxa_vector_t *elements = template_context ? &tree->template_elements : &tree->open_elements;
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data, lxa_vector_length(elements)
    };
    return tree_table_recovery(&view, keep, template_context);
}

#define LXA_FORM_END_EXECUTE_NAME tree_end_form
#define LXA_FORM_END_VIEW lxa_html_tree_scope_view_t
#define LXA_FORM_END_PLAN tree_form_end_plan
#define LXA_FORM_END_CLEAR(view, owner) ((view)->tree->form = (lxa_dom_ref_t){0})
#define LXA_FORM_END_POP(view, keep) \
    pop_elements((view)->tree, &(view)->tree->open_elements, keep)
#define LXA_FORM_END_REMOVE(view, index) \
    remove_open_element((view)->tree, &(view)->tree->open_elements, index)
#include "html/form_end.inc"
#undef LXA_FORM_END_EXECUTE_NAME
#undef LXA_FORM_END_VIEW
#undef LXA_FORM_END_PLAN
#undef LXA_FORM_END_CLEAR
#undef LXA_FORM_END_POP
#undef LXA_FORM_END_REMOVE

#define LXA_BODY_END_NAME tree_end_body
#define LXA_BODY_END_VIEW lxa_html_tree_scope_view_t
#define LXA_BODY_END_PLAN(view, out) \
    tree_scope_plan(view, 0, literal("body"), LXA_HTML_SCOPE_NORMAL, out)
#define LXA_BODY_END_LENGTH(view) ((view)->length)
#define LXA_BODY_END_POP(view, keep) pop_elements((view)->tree, &(view)->tree->open_elements, keep)
#include "html/body_end.inc"
#undef LXA_BODY_END_NAME
#undef LXA_BODY_END_VIEW
#undef LXA_BODY_END_PLAN
#undef LXA_BODY_END_LENGTH
#undef LXA_BODY_END_POP

#define LXA_CURRENT_NAMESPACE_NAME tree_current_namespace
#define LXA_CURRENT_NAMESPACE_VIEW lxa_html_tree_scope_view_t
#define LXA_CURRENT_NAMESPACE_LENGTH(view) ((view)->length)
#define LXA_CURRENT_NAMESPACE_READ(view, index, ns) \
    lxa_dom_nodes_element_namespace((view)->tree->nodes, (view)->entries[index], ns)
#include "html/current_namespace.inc"
#undef LXA_CURRENT_NAMESPACE_NAME
#undef LXA_CURRENT_NAMESPACE_VIEW
#undef LXA_CURRENT_NAMESPACE_LENGTH
#undef LXA_CURRENT_NAMESPACE_READ

static inline lxa_status_t
tree_foreign_namespace(const lxa_html_tree_scope_view_t *view, size_t index,
                        lxa_dom_namespace_t *ns)
{
    return lxa_dom_nodes_element_namespace(view->tree->nodes, view->entries[index], ns);
}

static inline lxa_status_t
tree_foreign_name(const lxa_html_tree_scope_view_t *view, size_t index,
                   lxa_span_t *name)
{
    return lxa_dom_nodes_element_name(view->tree->nodes, view->entries[index], name);
}

static inline lxa_status_t
tree_foreign_breakout_boundary(const lxa_html_tree_scope_view_t *view,
                               size_t index, bool *boundary)
{
    return foreign_breakout_boundary(view->tree, view->entries[index], boundary);
}

#define LXA_FOREIGN_VIEW lxa_html_tree_scope_view_t
#define LXA_FOREIGN_LENGTH(view) ((view)->length)
#define LXA_FOREIGN_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_FOREIGN_REASON(view) (&(view)->tree->nodes->limit_reason)
#define LXA_FOREIGN_NAMESPACE tree_foreign_namespace
#define LXA_FOREIGN_NAME tree_foreign_name
#define LXA_FOREIGN_BREAKOUT_BOUNDARY tree_foreign_breakout_boundary
#define LXA_FOREIGN_END_PLAN_NAME tree_foreign_end_plan
#define LXA_FOREIGN_BREAKOUT_PLAN_NAME tree_foreign_breakout_plan
#include "html/foreign_stack_plan.inc"
#undef LXA_FOREIGN_VIEW
#undef LXA_FOREIGN_LENGTH
#undef LXA_FOREIGN_BUDGET
#undef LXA_FOREIGN_REASON
#undef LXA_FOREIGN_NAMESPACE
#undef LXA_FOREIGN_NAME
#undef LXA_FOREIGN_BREAKOUT_BOUNDARY
#undef LXA_FOREIGN_END_PLAN_NAME
#undef LXA_FOREIGN_BREAKOUT_PLAN_NAME

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
current_html_column_group(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                          size_t boundary, bool *column_group)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_current_column_group(&view, boundary, column_group);
}

#define LXA_ROOT_GROUP_NAME tree_recover_root_group
#define LXA_ROOT_GROUP_VIEW lxa_html_tree_t
#define LXA_ROOT_GROUP_QUERY(view, out) current_html_column_group(view, &(view)->open_elements, 0, out)
#define LXA_ROOT_GROUP_BUDGET(view) ((view)->nodes->budget)
#define LXA_ROOT_GROUP_REASON(view) (&(view)->nodes->limit_reason)
#define LXA_ROOT_GROUP_POP pop_element
#include "html/root_group_recover.inc"
#undef LXA_ROOT_GROUP_NAME
#undef LXA_ROOT_GROUP_VIEW
#undef LXA_ROOT_GROUP_QUERY
#undef LXA_ROOT_GROUP_BUDGET
#undef LXA_ROOT_GROUP_REASON
#undef LXA_ROOT_GROUP_POP

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
plan_list_item(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                size_t boundary, bool definition, size_t *index)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_list_item_plan(&view, boundary, definition, index);
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
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_ruby_start_plan(&view, boundary,
        named(target, "rp") || named(target, "rt"), keep);
}

#define LXA_SCOPED_END_NAME tree_scoped_end
#define LXA_SCOPED_END_VIEW lxa_html_tree_scope_view_t
#define LXA_SCOPED_END_PLAN(view, target, scope, out) tree_scope_plan(view, 0, target, scope, out)
#define LXA_SCOPED_END_LENGTH(view) ((view)->length)
#define LXA_SCOPED_END_POP(view, keep) pop_elements((view)->tree, &(view)->tree->open_elements, keep)
#define LXA_SCOPED_END_TABLE(view, keep) pop_table_elements((view)->tree, keep)
#define LXA_SCOPED_END_CLEAR(view) clear_formatting_to_marker((view)->tree)
#include "html/scoped_end.inc"
#undef LXA_SCOPED_END_NAME
#undef LXA_SCOPED_END_VIEW
#undef LXA_SCOPED_END_PLAN
#undef LXA_SCOPED_END_LENGTH
#undef LXA_SCOPED_END_POP
#undef LXA_SCOPED_END_TABLE
#undef LXA_SCOPED_END_CLEAR

/* Close an HTML element through descendants up to the requested scope. */
static lxa_status_t
end_scoped_element(lxa_html_tree_t *tree, lxa_span_t target,
                   lxa_html_scope_kind_t scope)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) tree->open_elements.bytes.data,
        lxa_vector_length(&tree->open_elements)
    };
    return tree_scoped_end(&view, target, scope);
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

#define LXA_ROOT_CONTAINER_NAME tree_prepare_root_container
#define LXA_ROOT_CONTAINER_VIEW lxa_html_tree_t
#define LXA_ROOT_CONTAINER_RESERVE reserve_open_elements
#define LXA_ROOT_CONTAINER_NODE(view) lxa_budget_check((view)->nodes->budget, \
    LXA_BUDGET_NODES, 1, &(view)->nodes->limit_reason)
#define LXA_ROOT_CONTAINER_RUBY(view, name, out) plan_ruby_start(view, &(view)->open_elements, 0, name, out)
#define LXA_ROOT_CONTAINER_POP(view, keep) pop_elements(view, &(view)->open_elements, keep)
#define LXA_ROOT_CONTAINER_LIST close_list_item
#define LXA_ROOT_CONTAINER_PARAGRAPH(view) close_paragraph(view, NULL)
#define LXA_ROOT_CONTAINER_HEADING close_current_heading
#define LXA_ROOT_CONTAINER_BUTTON close_open_button
#include "html/root_container_prepare.inc"
#undef LXA_ROOT_CONTAINER_NAME
#undef LXA_ROOT_CONTAINER_VIEW
#undef LXA_ROOT_CONTAINER_RESERVE
#undef LXA_ROOT_CONTAINER_NODE
#undef LXA_ROOT_CONTAINER_RUBY
#undef LXA_ROOT_CONTAINER_POP
#undef LXA_ROOT_CONTAINER_LIST
#undef LXA_ROOT_CONTAINER_PARAGRAPH
#undef LXA_ROOT_CONTAINER_HEADING
#undef LXA_ROOT_CONTAINER_BUTTON

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

#define LXA_ROOT_END_NAME tree_prebody_end
#define LXA_ROOT_END_VIEW lxa_html_tree_t
#define LXA_ROOT_END_HTML ensure_html
#define LXA_ROOT_END_HEAD ensure_head
#define LXA_ROOT_END_BODY ensure_body
#include "html/root_prebody_end.inc"
#undef LXA_ROOT_END_NAME
#undef LXA_ROOT_END_VIEW
#undef LXA_ROOT_END_HTML
#undef LXA_ROOT_END_HEAD
#undef LXA_ROOT_END_BODY

#define LXA_ROOT_FINISH_NAME tree_finish_root
#define LXA_ROOT_FINISH_VIEW lxa_html_tree_t
#define LXA_ROOT_FINISH_BODY ensure_body
#define LXA_ROOT_FINISH_POP(view) pop_elements(view, &(view)->open_elements, 0)
#include "html/root_finish.inc"
#undef LXA_ROOT_FINISH_NAME
#undef LXA_ROOT_FINISH_VIEW
#undef LXA_ROOT_FINISH_BODY
#undef LXA_ROOT_FINISH_POP

#define LXA_PREBODY_TEXT_NAME prepare_prebody_text
#define LXA_PREBODY_TEXT_VIEW lxa_html_tree_t
#define LXA_PREBODY_TEXT_PLAN(view, bytes, out) lxa_html_tree_prebody_text_plan( \
    (view)->mode, bytes, (view)->nodes->budget, &(view)->nodes->limit_reason, out)
#define LXA_PREBODY_TEXT_EMIT(view, bytes) append_character(view, \
    (view)->mode == LXA_TREE_IN_HEAD ? (view)->head : (view)->html, LXA_DOM_KIND_TEXT, bytes)
#define LXA_PREBODY_TEXT_HEAD ensure_head
#define LXA_PREBODY_TEXT_MODE(view) ((view)->mode)
#include "html/prebody_text_execute.inc"
#undef LXA_PREBODY_TEXT_NAME
#undef LXA_PREBODY_TEXT_VIEW
#undef LXA_PREBODY_TEXT_PLAN
#undef LXA_PREBODY_TEXT_EMIT
#undef LXA_PREBODY_TEXT_HEAD
#undef LXA_PREBODY_TEXT_MODE

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

typedef struct {
    lxa_html_tree_t *tree;
    lxa_html_tree_scope_view_t open;
    size_t boundary;
} tree_adopt_view_t;

static inline lxa_status_t
tree_adopt_reference(const tree_adopt_view_t *view, size_t index, uint64_t *identity)
{
    return formatting_reference(view->tree, index, identity);
}

#define LXA_ADOPT_NAME tree_adopt_plan
#define LXA_ADOPT_VIEW tree_adopt_view_t
#define LXA_ADOPT_FORMATTING_LENGTH(view) lxa_vector_length(&(view)->tree->formatting)
#define LXA_ADOPT_OPEN_LENGTH(view) ((view)->open.length)
#define LXA_ADOPT_FIND(view, subject, index) find_formatting((view)->tree, subject, index)
#define LXA_ADOPT_REFERENCE tree_adopt_reference
#define LXA_ADOPT_OPEN(view, identity, index) \
    tree_open_reference_search(&(view)->open, (view)->boundary, identity, index)
#define LXA_ADOPT_SCOPED(view, identity, index) \
    tree_scoped_reference_search(&(view)->open, (view)->boundary, identity, index)
#define LXA_ADOPT_SPECIAL(view, start, index) tree_special_find(&(view)->open, start, index)
#include "html/formatting_adopt.inc"
#undef LXA_ADOPT_NAME
#undef LXA_ADOPT_VIEW
#undef LXA_ADOPT_FORMATTING_LENGTH
#undef LXA_ADOPT_OPEN_LENGTH
#undef LXA_ADOPT_FIND
#undef LXA_ADOPT_REFERENCE
#undef LXA_ADOPT_OPEN
#undef LXA_ADOPT_SCOPED
#undef LXA_ADOPT_SPECIAL

typedef struct {
    lxa_html_tree_t *tree;
    lxa_vector_t *elements;
    size_t boundary;
    lxa_dom_ref_t common;
} tree_adoption_view_t;

static inline lxa_status_t
tree_adoption_read(const tree_adoption_view_t *view, size_t index,
    lxa_span_t *name, lxa_dom_namespace_t *ns, lxa_dom_ref_t *identity)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) view->elements->bytes.data;
    *identity = open[index];
    lxa_status_t status = lxa_dom_nodes_element_namespace(view->tree->nodes, open[index], ns);
    return status == LXA_OK
        ? lxa_dom_nodes_element_name(view->tree->nodes, open[index], name) : status;
}

static inline lxa_status_t
tree_adoption_open_identity(const tree_adoption_view_t *view, size_t index, lxa_dom_ref_t *identity)
{
    *identity = ((const lxa_dom_ref_t *) view->elements->bytes.data)[index];
    return LXA_OK;
}

static inline lxa_status_t
tree_adoption_identity(const tree_adoption_view_t *view, size_t index, lxa_dom_ref_t *identity)
{
    *identity = formatting_entries(view->tree)[index].element;
    return LXA_OK;
}

static inline lxa_status_t
tree_adoption_plan(const tree_adoption_view_t *view, lxa_span_t subject,
    lxa_html_formatting_adopt_plan_t *out)
{
    const tree_adopt_view_t search = {view->tree, {
        view->tree, (const lxa_dom_ref_t *) view->elements->bytes.data,
        lxa_vector_length(view->elements)
    }, view->boundary};
    return tree_adopt_plan(&search, subject, out);
}

static lxa_status_t
tree_adoption_begin(tree_adoption_view_t *view, size_t index)
{
    if (index == view->boundary && view->tree->mode != LXA_TREE_TEMPLATE_TEXT)
        return LXA_ERROR_INTERNAL;
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) view->elements->bytes.data;
    view->common = index == view->boundary ? view->tree->template_fragment : open[index - 1];
    return LXA_OK;
}

static lxa_status_t
tree_adoption_inner(tree_adoption_view_t *view, size_t entry, size_t node,
    lxa_dom_ref_t last, lxa_dom_ref_t *identity)
{
    lxa_dom_ref_t clone;
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) view->elements->bytes.data;
    lxa_status_t status = lxa_dom_nodes_clone_element(view->tree->nodes, open[node], &clone);
    /* Attach replacements before moving subtrees so failure retains valid links. */
    if (status == LXA_OK)
        status = insert_constructed(view->tree, view->common, clone, (lxa_dom_ref_t){0});
    if (status != LXA_OK) return status;
    formatting_entries(view->tree)[entry].element = clone;
    ((lxa_dom_ref_t *) view->elements->bytes.data)[node] = clone;
    status = lxa_dom_nodes_reparent_constructed_child(view->tree->nodes, clone, last);
    if (status == LXA_OK) *identity = clone;
    return status;
}

static lxa_status_t
tree_adoption_final(tree_adoption_view_t *view, lxa_dom_ref_t last, lxa_dom_ref_t formatting,
    lxa_dom_ref_t furthest, lxa_dom_ref_t *identity)
{
    lxa_dom_ref_t parent, before, clone;
    lxa_status_t status = formatting_insertion_location(view->tree, view->common, &parent, &before);
    if (status == LXA_OK)
        status = place_adoption_node(view->tree, last, parent, before);
    if (status == LXA_OK)
        status = lxa_dom_nodes_clone_element(view->tree->nodes, formatting, &clone);
    if (status == LXA_OK)
        status = insert_constructed(view->tree, furthest, clone, (lxa_dom_ref_t){0});
    if (status != LXA_OK) return status;
    for (;;) {
        lxa_dom_ref_t child;
        status = lxa_dom_nodes_first_child(view->tree->nodes, furthest, &child);
        if (status != LXA_OK) return status;
        if (child.handle == clone.handle) break;
        if (child.handle == LXA_HANDLE_NONE) return LXA_ERROR_INTERNAL;
        status = lxa_budget_charge(view->tree->nodes->budget, LXA_BUDGET_WORK,
                                   1, &view->tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_reparent_constructed_child(view->tree->nodes, clone, child);
        if (status != LXA_OK) return status;
    }
    *identity = clone;
    return LXA_OK;
}

static lxa_status_t
tree_adoption_commit(tree_adoption_view_t *view, lxa_dom_ref_t clone, size_t formatting_index,
    size_t bookmark, size_t stack_index, size_t furthest_index)
{
    lxa_html_tree_t *tree = view->tree;
    lxa_status_t status = remove_formatting(tree, formatting_index);
    if (status != LXA_OK) return status;
    lxa_html_formatting_entry_t entry = {.element = clone};
    status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                               1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_vector_insert(&tree->formatting, bookmark, &entry);
    if (status != LXA_OK) {
        lxa_budget_release(tree->nodes->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1);
        return status;
    }
    if (stack_index >= lxa_vector_length(view->elements) || furthest_index <= stack_index
        || furthest_index >= lxa_vector_length(view->elements)
        || tree->nodes->budget->used[LXA_BUDGET_OPEN_ELEMENTS] == 0) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    status = lxa_vector_erase(view->elements, stack_index, 1);
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    /* Removing formatting shifts furthest up by one. Insert below it. */
    status = lxa_vector_insert(view->elements, furthest_index, &clone);
    if (status != LXA_OK) {
        lxa_status_t released = lxa_budget_release(tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1);
        if (released != LXA_OK) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
    }
    return status;
}

#define LXA_ADOPTION_NAME tree_adoption
#define LXA_ADOPTION_VIEW tree_adoption_view_t
#define LXA_ADOPTION_REFERENCE lxa_dom_ref_t
#define LXA_ADOPTION_SAME(a, b) ((a).handle == (b).handle)
#define LXA_ADOPTION_LENGTH(view) lxa_vector_length((view)->elements)
#define LXA_ADOPTION_BOUNDARY(view) ((view)->boundary)
#define LXA_ADOPTION_FORMATTING_LENGTH(view) lxa_vector_length(&(view)->tree->formatting)
#define LXA_ADOPTION_READ tree_adoption_read
#define LXA_ADOPTION_OPEN_IDENTITY tree_adoption_open_identity
#define LXA_ADOPTION_IDENTITY tree_adoption_identity
#define LXA_ADOPTION_FIND(view, identity, index) find_formatting_reference((view)->tree, identity, index)
#define LXA_ADOPTION_PLAN tree_adoption_plan
#define LXA_ADOPTION_ORDINARY(view, subject) formatting_end_ordinary((view)->tree, subject)
#define LXA_ADOPTION_POP(view, keep) formatting_pop_elements((view)->tree, keep)
#define LXA_ADOPTION_REMOVE_FORMATTING(view, index) remove_formatting((view)->tree, index)
#define LXA_ADOPTION_REMOVE_OPEN(view, index) remove_open_element((view)->tree, (view)->elements, index)
#define LXA_ADOPTION_BEGIN tree_adoption_begin
#define LXA_ADOPTION_INNER tree_adoption_inner
#define LXA_ADOPTION_FINAL tree_adoption_final
#define LXA_ADOPTION_COMMIT tree_adoption_commit
#define LXA_ADOPTION_INTERNAL(view) (assert(false), LXA_ERROR_INTERNAL)
#include "html/formatting_adoption.inc"
#undef LXA_ADOPTION_NAME
#undef LXA_ADOPTION_VIEW
#undef LXA_ADOPTION_REFERENCE
#undef LXA_ADOPTION_SAME
#undef LXA_ADOPTION_LENGTH
#undef LXA_ADOPTION_BOUNDARY
#undef LXA_ADOPTION_FORMATTING_LENGTH
#undef LXA_ADOPTION_READ
#undef LXA_ADOPTION_OPEN_IDENTITY
#undef LXA_ADOPTION_IDENTITY
#undef LXA_ADOPTION_FIND
#undef LXA_ADOPTION_PLAN
#undef LXA_ADOPTION_ORDINARY
#undef LXA_ADOPTION_POP
#undef LXA_ADOPTION_REMOVE_FORMATTING
#undef LXA_ADOPTION_REMOVE_OPEN
#undef LXA_ADOPTION_BEGIN
#undef LXA_ADOPTION_INNER
#undef LXA_ADOPTION_FINAL
#undef LXA_ADOPTION_COMMIT
#undef LXA_ADOPTION_INTERNAL

static lxa_status_t
adopt_formatting(lxa_html_tree_t *tree, lxa_span_t subject)
{
    tree_adoption_view_t view = {tree, formatting_elements(tree), formatting_boundary(tree), {0}};
    return tree_adoption(&view, subject);
}

#define LXA_FORMATTING_START_NAME tree_prepare_formatting_start
#define LXA_FORMATTING_START_VIEW tree_adoption_view_t
#define LXA_FORMATTING_START_REFERENCE lxa_dom_ref_t
#define LXA_FORMATTING_START_FIND(view, name, index) find_formatting((view)->tree, name, index)
#define LXA_FORMATTING_START_IDENTITY tree_adoption_identity
#define LXA_FORMATTING_START_ADOPT(view, name) adopt_formatting((view)->tree, name)
#define LXA_FORMATTING_START_FIND_REFERENCE(view, identity, index) \
    find_formatting_reference((view)->tree, identity, index)
#define LXA_FORMATTING_START_FORMATTING_LENGTH(view) lxa_vector_length(&(view)->tree->formatting)
#define LXA_FORMATTING_START_REMOVE_FORMATTING(view, index) remove_formatting((view)->tree, index)
#define LXA_FORMATTING_START_OPEN(view, identity, index) \
    find_open_reference((view)->tree, (view)->elements, (view)->boundary, identity, index)
#define LXA_FORMATTING_START_OPEN_LENGTH(view) lxa_vector_length((view)->elements)
#define LXA_FORMATTING_START_REMOVE_OPEN(view, index) \
    remove_open_element((view)->tree, (view)->elements, index)
#define LXA_FORMATTING_START_RECONSTRUCT(view) reconstruct_formatting((view)->tree)
#define LXA_FORMATTING_START_SCOPED(view, name, index) \
    find_scoped_end_target((view)->tree, (view)->elements, (view)->boundary, \
                          name, LXA_HTML_SCOPE_NORMAL, index)
#define LXA_FORMATTING_START_PLAN(view, token, drop) plan_formatting_add((view)->tree, token, drop)
#include "html/formatting_start.inc"
#undef LXA_FORMATTING_START_NAME
#undef LXA_FORMATTING_START_VIEW
#undef LXA_FORMATTING_START_REFERENCE
#undef LXA_FORMATTING_START_FIND
#undef LXA_FORMATTING_START_IDENTITY
#undef LXA_FORMATTING_START_ADOPT
#undef LXA_FORMATTING_START_FIND_REFERENCE
#undef LXA_FORMATTING_START_FORMATTING_LENGTH
#undef LXA_FORMATTING_START_REMOVE_FORMATTING
#undef LXA_FORMATTING_START_OPEN
#undef LXA_FORMATTING_START_OPEN_LENGTH
#undef LXA_FORMATTING_START_REMOVE_OPEN
#undef LXA_FORMATTING_START_RECONSTRUCT
#undef LXA_FORMATTING_START_SCOPED
#undef LXA_FORMATTING_START_PLAN

static lxa_status_t
prepare_formatting_start(lxa_html_tree_t *tree,
                         const lxa_html_token_t *token, size_t *drop)
{
    tree_adoption_view_t view = {tree, formatting_elements(tree), formatting_boundary(tree), {0}};
    return tree_prepare_formatting_start(&view, token, drop);
}

static lxa_status_t
root_table_text_group(lxa_html_tree_t *tree, bool *group)
{
    lxa_span_t top;
    lxa_status_t status = lxa_dom_nodes_element_name(tree->nodes, current_element(tree), &top);
    if (status == LXA_OK) *group = named(top, "colgroup");
    return status;
}

static lxa_status_t
root_table_text_foster(lxa_html_tree_t *tree, lxa_span_t bytes)
{
    lxa_dom_ref_t parent, table;
    lxa_status_t status = root_insertion_location(tree, current_element(tree), &parent, &table);
    return status == LXA_OK ? insert_text(tree, parent, table, bytes) : status;
}

#define LXA_TABLE_TEXT_FLUSH_NAME tree_flush_table_text
#define LXA_TABLE_TEXT_FLUSH_VIEW lxa_html_tree_t
#define LXA_TABLE_TEXT_FLUSH_BUDGET(view) ((view)->nodes->budget)
#define LXA_TABLE_TEXT_FLUSH_REASON(view) (&(view)->nodes->limit_reason)
#define LXA_TABLE_TEXT_FLUSH_APPEND(view, bytes, prefix) \
    ((void) (prefix), append_character(view, current_element(view), LXA_DOM_KIND_TEXT, bytes))
#define LXA_TABLE_TEXT_FLUSH_GROUP root_table_text_group
#define LXA_TABLE_TEXT_FLUSH_PREFLIGHT(view) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_WORK, 2, &(view)->nodes->limit_reason)
#define LXA_TABLE_TEXT_FLUSH_POP pop_element
#define LXA_TABLE_TEXT_FLUSH_RECONSTRUCT reconstruct_formatting
#define LXA_TABLE_TEXT_FLUSH_FOSTER root_table_text_foster
#include "html/table_text_flush.inc"
#undef LXA_TABLE_TEXT_FLUSH_NAME
#undef LXA_TABLE_TEXT_FLUSH_VIEW
#undef LXA_TABLE_TEXT_FLUSH_BUDGET
#undef LXA_TABLE_TEXT_FLUSH_REASON
#undef LXA_TABLE_TEXT_FLUSH_APPEND
#undef LXA_TABLE_TEXT_FLUSH_GROUP
#undef LXA_TABLE_TEXT_FLUSH_PREFLIGHT
#undef LXA_TABLE_TEXT_FLUSH_POP
#undef LXA_TABLE_TEXT_FLUSH_RECONSTRUCT
#undef LXA_TABLE_TEXT_FLUSH_FOSTER

static lxa_status_t
flush_table_text(lxa_html_tree_t *tree)
{
    if (tree->table_text.length == 0) return LXA_OK;
    lxa_status_t status = tree_flush_table_text(tree,
        (lxa_span_t){tree->table_text.data, tree->table_text.length});
    if (status == LXA_OK) lxa_buffer_clear(&tree->table_text);
    return status;
}

static lxa_status_t
queue_table_text(lxa_html_tree_t *tree, lxa_span_t bytes)
{
    return lxa_html_tree_queue_table_text(&tree->table_text, bytes, &tree->nodes->limit_reason);
}

static lxa_status_t flush_template_table_text(lxa_html_tree_t *tree);

#define LXA_TABLE_DATA_NULL_NAME tree_table_data_null
#define LXA_TABLE_DATA_NULL_VIEW lxa_html_tree_t
#define LXA_TABLE_DATA_NULL_GROUP(view, out) current_html_column_group(view, \
    formatting_elements(view), formatting_boundary(view), out)
#define LXA_TABLE_DATA_NULL_FLUSH(view, pending) \
    ((view)->mode == LXA_TREE_TEMPLATE_TEXT ? flush_template_table_text(view) : flush_table_text(view))
#define LXA_TABLE_DATA_NULL_POP(view) formatting_pop_elements(view, \
    lxa_vector_length(formatting_elements(view)) - 1)
#include "html/table_data_null.inc"
#undef LXA_TABLE_DATA_NULL_NAME
#undef LXA_TABLE_DATA_NULL_VIEW
#undef LXA_TABLE_DATA_NULL_GROUP
#undef LXA_TABLE_DATA_NULL_FLUSH
#undef LXA_TABLE_DATA_NULL_POP

typedef struct {
    lxa_html_tree_t *tree;
    lxa_dom_ref_t parent;
} tree_data_view_t;

static lxa_status_t
tree_data_append(tree_data_view_t *view, lxa_span_t bytes, bool table)
{
    return table ? queue_table_text(view->tree, bytes)
        : append_character(view->tree, view->parent, LXA_DOM_KIND_TEXT, bytes);
}

#define LXA_DATA_EXECUTE_NAME tree_execute_data
#define LXA_DATA_EXECUTE_VIEW tree_data_view_t
#define LXA_DATA_EXECUTE_BUDGET(view) ((view)->tree->nodes->budget)
#define LXA_DATA_EXECUTE_REASON(view) (&(view)->tree->nodes->limit_reason)
#define LXA_DATA_EXECUTE_APPEND tree_data_append
#define LXA_DATA_EXECUTE_NULL(view) tree_table_data_null((view)->tree, \
    ((lxa_span_t){(view)->tree->table_text.data, (view)->tree->table_text.length}))
#include "html/data_execute.inc"
#undef LXA_DATA_EXECUTE_NAME
#undef LXA_DATA_EXECUTE_VIEW
#undef LXA_DATA_EXECUTE_BUDGET
#undef LXA_DATA_EXECUTE_REASON
#undef LXA_DATA_EXECUTE_APPEND
#undef LXA_DATA_EXECUTE_NULL

static lxa_status_t
insert_data_text(lxa_html_tree_t *tree, lxa_dom_ref_t parent,
    lxa_span_t bytes, bool foreign, bool table)
{
    tree_data_view_t view = {tree, parent};
    return tree_execute_data(&view, bytes, foreign, table);
}

#define LXA_AFTER_BODY_NAME tree_execute_after_body
#define LXA_AFTER_BODY_VIEW lxa_html_tree_t
#define LXA_AFTER_BODY_RECONSTRUCT reconstruct_formatting
#define LXA_AFTER_BODY_EMIT(view, token) \
    insert_data_text(view, current_element(view), (token)->data, false, false)
#define LXA_AFTER_BODY_MODE(view) ((view)->mode)
#include "html/after_body_execute.inc"
#undef LXA_AFTER_BODY_NAME
#undef LXA_AFTER_BODY_VIEW
#undef LXA_AFTER_BODY_RECONSTRUCT
#undef LXA_AFTER_BODY_EMIT
#undef LXA_AFTER_BODY_MODE

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

#define LXA_ROOT_START_NAME tree_start_root
#define LXA_ROOT_START_VIEW lxa_html_tree_t
#define LXA_ROOT_START_HTML ensure_html
#define LXA_ROOT_START_HEAD ensure_head
#define LXA_ROOT_START_BODY ensure_body
#define LXA_ROOT_START_FRAMESET(view) ((view)->frameset_ok = false)
#define LXA_ROOT_START_ATTRIBUTES(view, element, token) \
    apply_attributes(view, (element) == LXA_HTML_ROOT_ELEMENT_HTML ? (view)->html \
        : (element) == LXA_HTML_ROOT_ELEMENT_HEAD ? (view)->head : (view)->body, token)
#include "html/root_start.inc"
#undef LXA_ROOT_START_NAME
#undef LXA_ROOT_START_VIEW
#undef LXA_ROOT_START_HTML
#undef LXA_ROOT_START_HEAD
#undef LXA_ROOT_START_BODY
#undef LXA_ROOT_START_FRAMESET
#undef LXA_ROOT_START_ATTRIBUTES

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

#define LXA_FRAMESET_START_NAME tree_start_nested_frameset
#define LXA_FRAMESET_START_VIEW lxa_html_tree_t
#define LXA_FRAMESET_START_PREPARE(view) reserve_open_elements(view, 1)
#define LXA_FRAMESET_START_HEAD(view) LXA_OK
#define LXA_FRAMESET_START_ADMIT(view, token) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, 1, &(view)->nodes->limit_reason)
#define LXA_FRAMESET_START_REPLACE(view) LXA_OK
#define LXA_FRAMESET_START_PUSH(view) open_container(view, token)
#define LXA_FRAMESET_START_MODE(view) ((view)->mode)
#include "html/frameset_start.inc"
#undef LXA_FRAMESET_START_NAME
#undef LXA_FRAMESET_START_VIEW
#undef LXA_FRAMESET_START_PREPARE
#undef LXA_FRAMESET_START_HEAD
#undef LXA_FRAMESET_START_ADMIT
#undef LXA_FRAMESET_START_REPLACE
#undef LXA_FRAMESET_START_PUSH
#undef LXA_FRAMESET_START_MODE

static lxa_status_t
plan_foreign_breakout(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                       size_t boundary, size_t *keep)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    return tree_foreign_breakout_plan(&view, boundary, keep);
}

static lxa_status_t
root_breakout_paragraph(lxa_html_tree_t *tree, size_t keep, lxa_span_t name, size_t *index)
{
    lxa_vector_t view = tree->open_elements;
    view.bytes.length = keep * sizeof(lxa_dom_ref_t);
    return find_scoped_end_target(tree, &view, 0, name, LXA_HTML_SCOPE_BUTTON, index);
}

#define LXA_ROOT_BREAKOUT_NAME tree_execute_root_breakout
#define LXA_ROOT_BREAKOUT_VIEW lxa_html_tree_t
#define LXA_ROOT_BREAKOUT_PLAN(view, out) plan_foreign_breakout(view, &(view)->open_elements, 0, out)
#define LXA_ROOT_BREAKOUT_PARAGRAPH root_breakout_paragraph
#define LXA_ROOT_BREAKOUT_NODE(view) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, 1, &(view)->nodes->limit_reason)
#define LXA_ROOT_BREAKOUT_POP(view, keep) pop_elements(view, &(view)->open_elements, keep)
#include "html/root_foreign_breakout_execute.inc"
#undef LXA_ROOT_BREAKOUT_NAME
#undef LXA_ROOT_BREAKOUT_VIEW
#undef LXA_ROOT_BREAKOUT_PLAN
#undef LXA_ROOT_BREAKOUT_PARAGRAPH
#undef LXA_ROOT_BREAKOUT_NODE
#undef LXA_ROOT_BREAKOUT_POP

static lxa_status_t
plan_foreign_end(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                 size_t boundary, lxa_span_t name, size_t *keep,
                 bool *reprocess)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    lxa_html_foreign_end_plan_t plan;
    lxa_status_t status = tree_foreign_end_plan(&view, boundary, name, &plan);
    if (status == LXA_OK) {
        *keep = plan.keep;
        *reprocess = plan.reprocess;
    }
    return status;
}

/* Foreign descendants retain their parent's namespace unless an integration
 * point dispatches the token through HTML construction instead. */
typedef struct {
    lxa_html_tree_t *tree;
    lxa_dom_namespace_t namespace_id;
} tree_foreign_execute_view_t;

static lxa_status_t
foreign_data(tree_foreign_execute_view_t *view, lxa_span_t bytes)
{
    lxa_html_tree_t *tree = view->tree;
    tree->frameset_ok = lxa_html_tree_foreign_data_frameset(tree->frameset_ok, bytes);
    return insert_data_text(tree, current_element(tree), bytes, true, false);
}

static lxa_status_t
foreign_pop(tree_foreign_execute_view_t *view, size_t keep)
{
    lxa_status_t status = LXA_OK;
    while (status == LXA_OK && lxa_vector_length(&view->tree->open_elements) > keep)
        status = pop_element(view->tree);
    return status;
}

static lxa_status_t
foreign_start(tree_foreign_execute_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_tree_t *tree = view->tree;
    lxa_status_t status = reserve_open_elements(tree, 1);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   1, &tree->nodes->limit_reason);
    if (status == LXA_OK) status = open_container_ns(tree, token, view->namespace_id);
    return status;
}

#define LXA_FOREIGN_EXEC_NAME tree_execute_foreign
#define LXA_FOREIGN_EXEC_VIEW tree_foreign_execute_view_t
#define LXA_FOREIGN_EXEC_MISC(view, token) append_comment_or_pi((view)->tree, current_element((view)->tree), token)
#define LXA_FOREIGN_EXEC_DATA foreign_data
#define LXA_FOREIGN_EXEC_END(view, name, keep, reprocess) \
    plan_foreign_end((view)->tree, &(view)->tree->open_elements, 0, name, keep, reprocess)
#define LXA_FOREIGN_EXEC_POP foreign_pop
#define LXA_FOREIGN_EXEC_START foreign_start
#define LXA_FOREIGN_EXEC_LENGTH(view) lxa_vector_length(&(view)->tree->open_elements)
#include "html/foreign_execute.inc"
#undef LXA_FOREIGN_EXEC_NAME
#undef LXA_FOREIGN_EXEC_VIEW
#undef LXA_FOREIGN_EXEC_MISC
#undef LXA_FOREIGN_EXEC_DATA
#undef LXA_FOREIGN_EXEC_END
#undef LXA_FOREIGN_EXEC_POP
#undef LXA_FOREIGN_EXEC_START
#undef LXA_FOREIGN_EXEC_LENGTH

static lxa_status_t
execute_head_noscript_spaces(lxa_html_tree_t *tree, lxa_span_t bytes)
{
    return append_character(tree, tree->head_noscript, LXA_DOM_KIND_TEXT, bytes);
}

#define LXA_HEAD_NOSCRIPT_EXEC_NAME tree_execute_head_noscript
#define LXA_HEAD_NOSCRIPT_EXEC_VIEW lxa_html_tree_t
#define LXA_HEAD_NOSCRIPT_EXEC_PLAN(view, token, out) \
    lxa_html_tree_head_noscript_plan(token, (view)->nodes->budget, &(view)->nodes->limit_reason, out)
#define LXA_HEAD_NOSCRIPT_EXEC_EMIT execute_head_noscript_spaces
#define LXA_HEAD_NOSCRIPT_EXEC_MODE(view) ((view)->mode)
#define LXA_HEAD_NOSCRIPT_EXEC_CLEAR(view) ((view)->head_noscript = (lxa_dom_ref_t){0})
#include "html/head_noscript_execute.inc"
#undef LXA_HEAD_NOSCRIPT_EXEC_NAME
#undef LXA_HEAD_NOSCRIPT_EXEC_VIEW
#undef LXA_HEAD_NOSCRIPT_EXEC_PLAN
#undef LXA_HEAD_NOSCRIPT_EXEC_EMIT
#undef LXA_HEAD_NOSCRIPT_EXEC_MODE
#undef LXA_HEAD_NOSCRIPT_EXEC_CLEAR

#define LXA_HEAD_NOSCRIPT_DISPATCH_NAME tree_dispatch_head_noscript
#define LXA_HEAD_NOSCRIPT_DISPATCH_VIEW lxa_html_tree_t
#define LXA_HEAD_NOSCRIPT_DISPATCH_MODE(view) ((view)->mode)
#define LXA_HEAD_NOSCRIPT_DISPATCH_EXEC tree_execute_head_noscript
#include "html/head_noscript_dispatch.inc"
#undef LXA_HEAD_NOSCRIPT_DISPATCH_NAME
#undef LXA_HEAD_NOSCRIPT_DISPATCH_VIEW
#undef LXA_HEAD_NOSCRIPT_DISPATCH_MODE
#undef LXA_HEAD_NOSCRIPT_DISPATCH_EXEC

static lxa_status_t
root_foreign_route(tree_foreign_execute_view_t *view, const lxa_html_token_t *token,
    lxa_html_foreign_root_route_t *route)
{
    lxa_html_tree_t *tree = view->tree;
    if ((tree->mode != LXA_TREE_IN_BODY && tree->mode != LXA_TREE_AFTER_BODY
            && tree->mode != LXA_TREE_AFTER_HTML)
        || lxa_vector_length(&tree->open_elements) <= 1) {
        *route = LXA_HTML_FOREIGN_ROOT_HTML;
        return LXA_OK;
    }
    lxa_status_t status = lxa_dom_nodes_element_namespace(tree->nodes,
        current_element(tree), &view->namespace_id);
    if (status != LXA_OK) return status;
    lxa_html_foreign_context_t integration = LXA_HTML_FOREIGN_NONE;
    if (view->namespace_id != LXA_DOM_NS_HTML
        && (token->kind == LXA_HTML_START_TAG || token->kind == LXA_HTML_TEXT)) {
        lxa_span_t name;
        status = lxa_dom_nodes_element_name(tree->nodes, current_element(tree), &name);
        if (status == LXA_OK)
            status = element_foreign_context(tree, current_element(tree),
                view->namespace_id, name, &integration);
        if (status != LXA_OK) return status;
    }
    return lxa_html_foreign_root_plan(view->namespace_id, integration, token,
        tree->nodes->budget, &tree->nodes->limit_reason, route);
}

#define LXA_ROOT_FOREIGN_DISPATCH_NAME tree_dispatch_root_foreign
#define LXA_ROOT_FOREIGN_DISPATCH_VIEW tree_foreign_execute_view_t
#define LXA_ROOT_FOREIGN_DISPATCH_ROUTE root_foreign_route
#define LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT(view, token) tree_execute_root_breakout((view)->tree, token)
#define LXA_ROOT_FOREIGN_DISPATCH_CONSUME tree_execute_foreign
#include "html/root_foreign_dispatch.inc"
#undef LXA_ROOT_FOREIGN_DISPATCH_NAME
#undef LXA_ROOT_FOREIGN_DISPATCH_VIEW
#undef LXA_ROOT_FOREIGN_DISPATCH_ROUTE
#undef LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT
#undef LXA_ROOT_FOREIGN_DISPATCH_CONSUME

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
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    lxa_html_select_plan_t plan;
    lxa_status_t status = tree_select_plan(&view, boundary,
        token->kind, token->name, &plan);
    if (status == LXA_OK) {
        *keep = plan.keep;
        *ignore = plan.ignore;
    }
    return status;
}

static lxa_status_t
root_select_pop(const lxa_html_tree_scope_view_t *view, size_t keep)
{
    lxa_status_t status = LXA_OK;
    while (status == LXA_OK && lxa_vector_length(&view->tree->open_elements) > keep)
        status = pop_element(view->tree);
    return status;
}

#define LXA_SELECT_EXECUTE_NAME tree_execute_root_select
#define LXA_SELECT_EXECUTE_VIEW lxa_html_tree_scope_view_t
#define LXA_SELECT_EXECUTE_PLAN(view, kind, name, out) tree_select_plan(view, 0, kind, name, out)
#define LXA_SELECT_EXECUTE_POP root_select_pop
#include "html/select_execute.inc"
#undef LXA_SELECT_EXECUTE_NAME
#undef LXA_SELECT_EXECUTE_VIEW
#undef LXA_SELECT_EXECUTE_PLAN
#undef LXA_SELECT_EXECUTE_POP

#define LXA_ROOT_START_DISPATCH_NAME tree_dispatch_root_start
#define LXA_ROOT_START_DISPATCH_VIEW lxa_html_tree_t
#define LXA_ROOT_START_DISPATCH_EXECUTE tree_start_root
#include "html/root_start_dispatch.inc"
#undef LXA_ROOT_START_DISPATCH_NAME
#undef LXA_ROOT_START_DISPATCH_VIEW
#undef LXA_ROOT_START_DISPATCH_EXECUTE

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

#define LXA_ROOT_FOSTER_GROUP_NAME tree_recover_foster_group
#define LXA_ROOT_FOSTER_GROUP_VIEW lxa_html_tree_t
#define LXA_ROOT_FOSTER_GROUP_RESERVE(view) reserve_open_elements(view, 0)
#define LXA_ROOT_FOSTER_GROUP_NODE(view) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, 1, &(view)->nodes->limit_reason)
#define LXA_ROOT_FOSTER_GROUP_WORK(view) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_WORK, 2, &(view)->nodes->limit_reason)
#define LXA_ROOT_FOSTER_GROUP_POP pop_element
#include "html/root_foster_group_recover.inc"
#undef LXA_ROOT_FOSTER_GROUP_NAME
#undef LXA_ROOT_FOSTER_GROUP_VIEW
#undef LXA_ROOT_FOSTER_GROUP_RESERVE
#undef LXA_ROOT_FOSTER_GROUP_NODE
#undef LXA_ROOT_FOSTER_GROUP_WORK
#undef LXA_ROOT_FOSTER_GROUP_POP

static lxa_status_t
tree_foster_html_admit(lxa_html_tree_t *tree, const lxa_html_token_t *token,
    bool keep_open, bool formatting, bool marker, size_t drop)
{
    lxa_dom_ref_t table, parent, child;
    lxa_status_t status = root_insertion_location(tree, current_element(tree),
                                          &parent, &table);
    if (status != LXA_OK) return status;
    if (keep_open) {
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
    if (status == LXA_OK && keep_open) status = push_element(tree, child);
    if (status == LXA_OK && formatting) {
        if (drop != SIZE_MAX) status = remove_formatting(tree, drop);
        if (status == LXA_OK) status = append_formatting(tree, child, false);
    }
    if (status == LXA_OK && marker)
        status = append_formatting(tree, child, true);
    return status;
}

#define LXA_FOSTER_HTML_NAME tree_start_foster_html
#define LXA_FOSTER_HTML_VIEW lxa_html_tree_t
#define LXA_FOSTER_HTML_PREPARE prepare_formatting_start
#define LXA_FOSTER_HTML_RECONSTRUCT reconstruct_formatting
#define LXA_FOSTER_HTML_MARKER(view) reserve_formatting(view, 1)
#define LXA_FOSTER_HTML_ADMIT tree_foster_html_admit
#include "html/foster_html_start.inc"
#undef LXA_FOSTER_HTML_NAME
#undef LXA_FOSTER_HTML_VIEW
#undef LXA_FOSTER_HTML_PREPARE
#undef LXA_FOSTER_HTML_RECONSTRUCT
#undef LXA_FOSTER_HTML_MARKER
#undef LXA_FOSTER_HTML_ADMIT

static lxa_status_t
foster_element(lxa_html_tree_t *tree, const lxa_html_token_t *token, bool is_void)
{
    bool newline;
    lxa_status_t status = tree_start_foster_html(tree, token, !is_void, &newline);
    if (status == LXA_OK && newline) tree->skip_pre_newline = true;
    return status;
}

static lxa_status_t
tree_root_foreign_reserve(lxa_html_tree_t *tree)
{
    lxa_status_t status = reserve_open_elements(tree, 1);
    return status == LXA_OK ? lxa_budget_check(tree->nodes->budget,
        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason) : status;
}

static lxa_status_t
tree_foster_foreign_insert(lxa_html_tree_t *tree, const lxa_html_token_t *token,
    lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t parent, table;
    lxa_status_t status = root_insertion_location(tree, current_element(tree), &parent, &table);
    return status == LXA_OK ? open_container_at(tree, token, ns, parent, table) : status;
}

#define LXA_ROOT_FOSTER_FOREIGN_NAME foster_foreign
#define LXA_ROOT_FOSTER_FOREIGN_VIEW lxa_html_tree_t
#define LXA_ROOT_FOSTER_FOREIGN_RECONSTRUCT reconstruct_formatting
#define LXA_ROOT_FOSTER_FOREIGN_RESERVE tree_root_foreign_reserve
#define LXA_ROOT_FOSTER_FOREIGN_INSERT tree_foster_foreign_insert
#define LXA_ROOT_FOSTER_FOREIGN_POP pop_element
#include "html/root_foster_foreign.inc"
#undef LXA_ROOT_FOSTER_FOREIGN_NAME
#undef LXA_ROOT_FOSTER_FOREIGN_VIEW
#undef LXA_ROOT_FOSTER_FOREIGN_RECONSTRUCT
#undef LXA_ROOT_FOSTER_FOREIGN_RESERVE
#undef LXA_ROOT_FOSTER_FOREIGN_INSERT
#undef LXA_ROOT_FOSTER_FOREIGN_POP

static lxa_status_t
tree_root_part_close_plan(lxa_html_tree_t *tree, lxa_html_table_group_t group, size_t *index)
{
    const lxa_html_tree_scope_view_t view = {tree,
        (const lxa_dom_ref_t *) tree->open_elements.bytes.data,
        lxa_vector_length(&tree->open_elements)};
    return tree_root_table_part_close_plan(&view, 1, group, index);
}

#define LXA_ROOT_PART_CLOSE_EXEC_NAME close_open_table_part
#define LXA_ROOT_PART_CLOSE_EXEC_VIEW lxa_html_tree_t
#define LXA_ROOT_PART_CLOSE_EXEC_PLAN tree_root_part_close_plan
#define LXA_ROOT_PART_CLOSE_EXEC_LENGTH(view) lxa_vector_length(&(view)->open_elements)
#define LXA_ROOT_PART_CLOSE_EXEC_NODE(view) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, 1, &(view)->nodes->limit_reason)
#define LXA_ROOT_PART_CLOSE_EXEC_POP pop_table_elements
#include "html/root_table_part_close.inc"
#undef LXA_ROOT_PART_CLOSE_EXEC_NAME
#undef LXA_ROOT_PART_CLOSE_EXEC_VIEW
#undef LXA_ROOT_PART_CLOSE_EXEC_PLAN
#undef LXA_ROOT_PART_CLOSE_EXEC_LENGTH
#undef LXA_ROOT_PART_CLOSE_EXEC_NODE
#undef LXA_ROOT_PART_CLOSE_EXEC_POP

static lxa_status_t
tree_root_caption_find(lxa_html_tree_t *tree, size_t *index)
{
    return find_scoped_end_target(tree, &tree->open_elements, 0,
        literal("caption"), LXA_HTML_SCOPE_TABLE, index);
}

#define LXA_ROOT_CAPTION_NAME close_caption_for_table_start
#define LXA_ROOT_CAPTION_VIEW lxa_html_tree_t
#define LXA_ROOT_CAPTION_FIND tree_root_caption_find
#define LXA_ROOT_CAPTION_LENGTH(view) lxa_vector_length(&(view)->open_elements)
#define LXA_ROOT_CAPTION_RESERVE reserve_open_elements
#define LXA_ROOT_CAPTION_NODE(view, count) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, count, &(view)->nodes->limit_reason)
#define LXA_ROOT_CAPTION_POP pop_table_elements
#include "html/root_caption_recover.inc"
#undef LXA_ROOT_CAPTION_NAME
#undef LXA_ROOT_CAPTION_VIEW
#undef LXA_ROOT_CAPTION_FIND
#undef LXA_ROOT_CAPTION_LENGTH
#undef LXA_ROOT_CAPTION_RESERVE
#undef LXA_ROOT_CAPTION_NODE
#undef LXA_ROOT_CAPTION_POP

static lxa_status_t
tree_root_table_top(lxa_html_tree_t *tree, lxa_span_t *top)
{
    return lxa_dom_nodes_element_name(tree->nodes, current_element(tree), top);
}

static lxa_status_t
tree_root_table_plan(lxa_html_tree_t *tree, lxa_span_t top, bool *close_existing)
{
    const lxa_html_tree_scope_view_t view = {tree,
        (const lxa_dom_ref_t *) tree->open_elements.bytes.data,
        lxa_vector_length(&tree->open_elements)};
    return tree_root_table_close_plan(&view, 1,
        lxa_html_tree_table_context(top), close_existing);
}

#define LXA_ROOT_TABLE_START_NAME tree_start_root_table
#define LXA_ROOT_TABLE_START_VIEW lxa_html_tree_t
#define LXA_ROOT_TABLE_START_BODY ensure_body
#define LXA_ROOT_TABLE_START_TOP tree_root_table_top
#define LXA_ROOT_TABLE_START_FRAMESET(view) ((view)->frameset_ok)
#define LXA_ROOT_TABLE_START_PLAN tree_root_table_plan
#define LXA_ROOT_TABLE_START_RESERVE reserve_open_elements
#define LXA_ROOT_TABLE_START_NODE(view) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, 1, &(view)->nodes->limit_reason)
#define LXA_ROOT_TABLE_START_CLOSE(view) end_scoped_element(view, literal("table"), LXA_HTML_SCOPE_TABLE)
#define LXA_ROOT_TABLE_START_PARAGRAPH(view) close_paragraph(view, NULL)
#define LXA_ROOT_TABLE_START_INSERT open_container
#include "html/root_table_start.inc"
#undef LXA_ROOT_TABLE_START_NAME
#undef LXA_ROOT_TABLE_START_VIEW
#undef LXA_ROOT_TABLE_START_BODY
#undef LXA_ROOT_TABLE_START_TOP
#undef LXA_ROOT_TABLE_START_FRAMESET
#undef LXA_ROOT_TABLE_START_PLAN
#undef LXA_ROOT_TABLE_START_RESERVE
#undef LXA_ROOT_TABLE_START_NODE
#undef LXA_ROOT_TABLE_START_CLOSE
#undef LXA_ROOT_TABLE_START_PARAGRAPH
#undef LXA_ROOT_TABLE_START_INSERT

#define LXA_TABLE_PART_ADMIT_NAME tree_admit_table_part
#define LXA_TABLE_PART_ADMIT_VIEW lxa_html_tree_t
#define LXA_TABLE_PART_ADMIT_RESERVE reserve_open_elements
#define LXA_TABLE_PART_ADMIT_NODE(view, count) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, count, &(view)->nodes->limit_reason)
#define LXA_TABLE_PART_ADMIT_INSERT open_container
#define LXA_TABLE_PART_ADMIT_VOID(view, token) append_void(view, current_element(view), token)
#include "html/table_part_admit.inc"
#undef LXA_TABLE_PART_ADMIT_NAME
#undef LXA_TABLE_PART_ADMIT_VIEW
#undef LXA_TABLE_PART_ADMIT_RESERVE
#undef LXA_TABLE_PART_ADMIT_NODE
#undef LXA_TABLE_PART_ADMIT_INSERT
#undef LXA_TABLE_PART_ADMIT_VOID

static lxa_status_t
tree_root_table_part_plan(lxa_html_tree_t *tree, lxa_span_t target,
    lxa_html_root_table_part_context_plan_t *plan)
{
    lxa_span_t top;
    lxa_status_t status = tree_root_table_top(tree, &top);
    if (status != LXA_OK) return status;
    const lxa_html_tree_scope_view_t view = {tree,
        (const lxa_dom_ref_t *) tree->open_elements.bytes.data,
        lxa_vector_length(&tree->open_elements)};
    return tree_root_table_part_context_plan(&view, 1, top, target, plan);
}

static lxa_status_t
tree_root_table_part_postpop(lxa_html_tree_t *tree)
{
    lxa_span_t top;
    return tree_root_table_top(tree, &top);
}

#define LXA_ROOT_TABLE_PART_RECOVER_NAME tree_recover_table_part_context
#define LXA_ROOT_TABLE_PART_RECOVER_VIEW lxa_html_tree_t
#define LXA_ROOT_TABLE_PART_RECOVER_BODY ensure_body
#define LXA_ROOT_TABLE_PART_RECOVER_SCOPE(view, out) table_in_scope(view, &(view)->open_elements, 0, out)
#define LXA_ROOT_TABLE_PART_RECOVER_PLAN tree_root_table_part_plan
#define LXA_ROOT_TABLE_PART_RECOVER_RESERVE reserve_open_elements
#define LXA_ROOT_TABLE_PART_RECOVER_NODE(view, count) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, count, &(view)->nodes->limit_reason)
#define LXA_ROOT_TABLE_PART_RECOVER_POP pop_table_elements
#define LXA_ROOT_TABLE_PART_RECOVER_POSTPOP tree_root_table_part_postpop
#include "html/root_table_part_context_recover.inc"
#undef LXA_ROOT_TABLE_PART_RECOVER_NAME
#undef LXA_ROOT_TABLE_PART_RECOVER_VIEW
#undef LXA_ROOT_TABLE_PART_RECOVER_BODY
#undef LXA_ROOT_TABLE_PART_RECOVER_SCOPE
#undef LXA_ROOT_TABLE_PART_RECOVER_PLAN
#undef LXA_ROOT_TABLE_PART_RECOVER_RESERVE
#undef LXA_ROOT_TABLE_PART_RECOVER_NODE
#undef LXA_ROOT_TABLE_PART_RECOVER_POP
#undef LXA_ROOT_TABLE_PART_RECOVER_POSTPOP

#define LXA_ROOT_PART_START_NAME tree_start_root_part
#define LXA_ROOT_PART_START_VIEW lxa_html_tree_t
#define LXA_ROOT_PART_START_CONTEXT tree_recover_table_part_context
#define LXA_ROOT_PART_START_CAPTION close_caption_for_table_start
#define LXA_ROOT_PART_START_TOP tree_root_table_top
#define LXA_ROOT_PART_START_CLOSE close_open_table_part
#define LXA_ROOT_PART_START_RESERVE reserve_open_elements
#define LXA_ROOT_PART_START_NODE(view, count) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, count, &(view)->nodes->limit_reason)
#define LXA_ROOT_PART_START_POP pop_element
#define LXA_ROOT_PART_START_ADMIT tree_admit_table_part
#include "html/root_table_part_start.inc"
#undef LXA_ROOT_PART_START_NAME
#undef LXA_ROOT_PART_START_VIEW
#undef LXA_ROOT_PART_START_CONTEXT
#undef LXA_ROOT_PART_START_CAPTION
#undef LXA_ROOT_PART_START_TOP
#undef LXA_ROOT_PART_START_CLOSE
#undef LXA_ROOT_PART_START_RESERVE
#undef LXA_ROOT_PART_START_NODE
#undef LXA_ROOT_PART_START_POP
#undef LXA_ROOT_PART_START_ADMIT

static lxa_status_t
open_table_part(lxa_html_tree_t *tree, const lxa_html_token_t *token)
{
    return named(token->name, "table")
        ? tree_start_root_table(tree, token, tree->document_mode == LXA_HTML_QUIRKS, &tree->mode)
        : tree_start_root_part(tree, token, &tree->mode);
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
    const lxa_html_token_t *token)
{
    return lxa_html_tree_text_start_feedback(tokenizer, token->name,
        &tree->nodes->limit_reason);
}

static lxa_status_t
enter_text_mode(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token,
    lxa_dom_ref_t element)
{
    lxa_status_t status = lxa_html_text_context_enter(&tree->text_context,
        tokenizer, token->name, tree->mode, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    tree->text_element = element;
    tree->mode = LXA_TREE_TEXT;
    return LXA_OK;
}

typedef struct {
    lxa_html_tree_t *tree;
    lxa_html_tokenizer_t *tokenizer;
    lxa_dom_ref_t element;
} tree_foster_text_view_t;

static lxa_status_t
tree_foster_text_insert(tree_foster_text_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_tree_t *tree = view->tree;
    lxa_dom_ref_t table, parent;
    lxa_status_t status = lxa_budget_check(tree->nodes->budget,
        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK) status = foster_location(tree, &parent, &table);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_parsed_html_element(tree->nodes, token->name, &view->element);
    if (status == LXA_OK) status = apply_attributes(tree, view->element, token);
    if (status == LXA_OK) status = insert_constructed(tree, parent, view->element, table);
    return status;
}

#define LXA_ROOT_FOSTER_TEXT_NAME tree_start_foster_text
#define LXA_ROOT_FOSTER_TEXT_VIEW tree_foster_text_view_t
#define LXA_ROOT_FOSTER_TEXT_INSERT tree_foster_text_insert
#define LXA_ROOT_FOSTER_TEXT_ENTER(view, token) \
    enter_text_mode((view)->tree, (view)->tokenizer, token, (view)->element)
#define LXA_ROOT_FOSTER_TEXT_POP(view) pop_element((view)->tree)
#include "html/root_foster_text.inc"
#undef LXA_ROOT_FOSTER_TEXT_NAME
#undef LXA_ROOT_FOSTER_TEXT_VIEW
#undef LXA_ROOT_FOSTER_TEXT_INSERT
#undef LXA_ROOT_FOSTER_TEXT_ENTER
#undef LXA_ROOT_FOSTER_TEXT_POP

static lxa_status_t
begin_root_foster_text(lxa_html_tree_t *tree, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, bool column_group)
{
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    tree_foster_text_view_t view = {tree, tokenizer, {0}};
    return tree_start_foster_text(&view, token, column_group, &tree->skip_pre_newline);
}

static lxa_status_t
leave_text_mode(lxa_html_tree_t *tree, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = lxa_html_text_context_leave(&tree->text_context, mode);
    if (status != LXA_OK) return status;
    tree->text_element = (lxa_dom_ref_t){0};
    return LXA_OK;
}

static lxa_status_t
tree_text_target(lxa_html_tree_t *tree, lxa_span_t *target)
{
    return lxa_dom_nodes_element_name(tree->nodes, tree->text_element, target) == LXA_OK
        ? LXA_OK : LXA_ERROR_INTERNAL;
}

#define LXA_TEXT_TOKEN_NAME tree_text_token
#define LXA_TEXT_TOKEN_VIEW lxa_html_tree_t
#define LXA_TEXT_TOKEN_TARGET tree_text_target
#define LXA_TEXT_TOKEN_APPEND(view, token) \
    append_character(view, (view)->text_element, LXA_DOM_KIND_TEXT, (token)->data)
#define LXA_TEXT_TOKEN_RETURN leave_text_mode
#include "html/text_token_execute.inc"
#undef LXA_TEXT_TOKEN_NAME
#undef LXA_TEXT_TOKEN_VIEW
#undef LXA_TEXT_TOKEN_TARGET
#undef LXA_TEXT_TOKEN_APPEND
#undef LXA_TEXT_TOKEN_RETURN

static lxa_status_t
begin_text_element_at(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, lxa_dom_ref_t parent, lxa_dom_ref_t before,
    const lxa_html_token_t *token)
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
           ? enter_text_mode(tree, tokenizer, token, element)
           : status;
}

#define LXA_ROOT_TEXT_PREPARE_NAME tree_prepare_root_text
#define LXA_ROOT_TEXT_PREPARE_VIEW lxa_html_tree_t
#define LXA_ROOT_TEXT_PREPARE_BODY ensure_body
#define LXA_ROOT_TEXT_PREPARE_HEAD ensure_head
#define LXA_ROOT_TEXT_PREPARE_FRAMESET(view) ((view)->frameset_ok)
#include "html/root_text_prepare.inc"
#undef LXA_ROOT_TEXT_PREPARE_NAME
#undef LXA_ROOT_TEXT_PREPARE_VIEW
#undef LXA_ROOT_TEXT_PREPARE_BODY
#undef LXA_ROOT_TEXT_PREPARE_HEAD
#undef LXA_ROOT_TEXT_PREPARE_FRAMESET

static lxa_status_t
begin_text_element(lxa_html_tree_t *tree,
    lxa_html_tokenizer_t *tokenizer, lxa_dom_ref_t parent,
    const lxa_html_token_t *token)
{
    return begin_text_element_at(tree, tokenizer, parent, (lxa_dom_ref_t){0},
                                  token);
}

#define LXA_ROOT_TABLE_TEXT_NAME tree_start_table_text
#define LXA_ROOT_TABLE_TEXT_VIEW tree_foster_text_view_t
#define LXA_ROOT_TABLE_TEXT_PREPARE(view) \
    lxa_budget_check((view)->tree->nodes->budget, LXA_BUDGET_NODES, 1, &(view)->tree->nodes->limit_reason)
#define LXA_ROOT_TABLE_TEXT_POP(view) pop_element((view)->tree)
#define LXA_ROOT_TABLE_TEXT_ENTER(view, token) \
    begin_text_element((view)->tree, (view)->tokenizer, current_element((view)->tree), token)
#include "html/root_table_text_start.inc"
#undef LXA_ROOT_TABLE_TEXT_NAME
#undef LXA_ROOT_TABLE_TEXT_VIEW
#undef LXA_ROOT_TABLE_TEXT_PREPARE
#undef LXA_ROOT_TABLE_TEXT_POP
#undef LXA_ROOT_TABLE_TEXT_ENTER

static lxa_status_t
begin_root_table_text(lxa_html_tree_t *tree, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token, bool column_group)
{
    tree_foster_text_view_t view = {tree, tokenizer, {0}};
    return tree_start_table_text(&view, token, column_group);
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
    return begin_text_element_at(tree, tokenizer, parent, before, token);
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
    return set_tokenizer_text_mode(tree, tokenizer, token);
}

typedef struct {
    lxa_html_tree_t *tree;
    lxa_dom_ref_t child;
    bool replace_body;
} root_frameset_view_t;

static lxa_status_t
root_frameset_prepare(root_frameset_view_t *view)
{
    lxa_html_tree_t *tree = view->tree;
    lxa_budget_t after = *tree->nodes->budget;
    size_t pop_count = view->replace_body
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
    return status;
}

static lxa_status_t
root_frameset_admit(root_frameset_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_tree_t *tree = view->tree;
    lxa_status_t status = lxa_dom_nodes_append_parsed_html_element(tree->nodes, token->name, &view->child);
    if (status == LXA_OK) status = apply_attributes(tree, view->child, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, tree->html,
            view->child, (lxa_dom_ref_t){0});
    return status;
}

static lxa_status_t
root_frameset_replace(root_frameset_view_t *view)
{
    if (!view->replace_body) return LXA_OK;
    lxa_html_tree_t *tree = view->tree;
    lxa_dom_ref_t parent;
    lxa_status_t status = lxa_dom_nodes_parent(tree->nodes, tree->body, &parent);
    if (status == LXA_OK && parent.handle != LXA_HANDLE_NONE)
        status = lxa_dom_nodes_detach_constructed_child(tree->nodes, tree->body);
    if (status != LXA_OK) return status;
    while (lxa_vector_length(&tree->open_elements) != 0) {
        status = pop_element(tree);
        if (status != LXA_OK) return status;
    }
    tree->body = (lxa_dom_ref_t){0};
    return LXA_OK;
}

#define LXA_FRAMESET_START_NAME tree_start_root_frameset
#define LXA_FRAMESET_START_VIEW root_frameset_view_t
#define LXA_FRAMESET_START_PREPARE root_frameset_prepare
#define LXA_FRAMESET_START_HEAD(view) ensure_head((view)->tree)
#define LXA_FRAMESET_START_ADMIT root_frameset_admit
#define LXA_FRAMESET_START_REPLACE root_frameset_replace
#define LXA_FRAMESET_START_PUSH(view) push_element((view)->tree, (view)->child)
#define LXA_FRAMESET_START_MODE(view) ((view)->tree->mode)
#include "html/frameset_start.inc"
#undef LXA_FRAMESET_START_NAME
#undef LXA_FRAMESET_START_VIEW
#undef LXA_FRAMESET_START_PREPARE
#undef LXA_FRAMESET_START_HEAD
#undef LXA_FRAMESET_START_ADMIT
#undef LXA_FRAMESET_START_REPLACE
#undef LXA_FRAMESET_START_PUSH
#undef LXA_FRAMESET_START_MODE

static lxa_status_t
begin_root_frameset(lxa_html_tree_t *tree, const lxa_html_token_t *token,
                    bool replace_body)
{
    root_frameset_view_t view = {tree, {0}, replace_body};
    return tree_start_root_frameset(&view, token);
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
    return lxa_html_tree_template_boundary_valid(length,
        lxa_vector_length(&tree->template_elements),
        length == 0 ? 0 : frames[length - 1].element_count,
        length < 2 ? 0 : frames[length - 2].element_count);
}

typedef struct {
    lxa_html_tree_scope_view_t scope;
    lxa_html_template_frame_t frame;
    size_t parents;
} tree_template_close_view_t;

static lxa_status_t
template_close_patch(const tree_template_close_view_t *view)
{
    lxa_html_tree_t *tree = view->scope.tree;
    lxa_dom_ref_t markers[] = {
        tree->template_patch.start, tree->template_patch.end
    };
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        lxa_dom_ref_t parent;
        if (markers[i].handle == LXA_HANDLE_NONE) continue;
        lxa_status_t status = lxa_dom_nodes_parent(tree->nodes, markers[i], &parent);
        if (status == LXA_OK && parent.handle != LXA_HANDLE_NONE)
            status = lxa_dom_nodes_detach_constructed_child(tree->nodes, markers[i]);
        if (status != LXA_OK) return status;
    }
    tree->template_patch = (lxa_dom_content_patch_t){0};
    return LXA_OK;
}

static lxa_status_t
template_close_release(const tree_template_close_view_t *view, size_t boundary)
{
    (void) boundary;
    if (lxa_budget_release(view->scope.tree->nodes->budget, LXA_BUDGET_OPEN_ELEMENTS, 1) != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return LXA_OK;
}

static lxa_status_t
template_close_publish(const tree_template_close_view_t *view)
{
    lxa_html_tree_t *tree = view->scope.tree;
    if (view->parents != 0) {
        if (lxa_vector_erase(&tree->template_stack, view->parents - 1, 1) != LXA_OK) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        tree->template_fragment = view->frame.fragment;
        tree->template_patch = view->frame.patch;
        tree->template_insertion_mode = view->frame.insertion_mode;
    }
    else {
        tree->template_fragment = (lxa_dom_ref_t){0};
        tree->template_patch = (lxa_dom_content_patch_t){0};
        tree->template_insertion_mode = LXA_TEMPLATE_INITIAL;
        tree->mode = tree->template_return_mode;
    }
    return LXA_OK;
}

#define LXA_TEMPLATE_CLOSE_NAME tree_template_close
#define LXA_TEMPLATE_CLOSE_VIEW tree_template_close_view_t
#define LXA_TEMPLATE_CLOSE_PLAN(view, boundary, explicit_end, has_patch, out) \
    tree_template_close_plan(&(view)->scope, boundary, explicit_end, has_patch, out)
#define LXA_TEMPLATE_CLOSE_POP(view, keep) template_pop_elements((view)->scope.tree, keep)
#define LXA_TEMPLATE_CLOSE_PATCH template_close_patch
#define LXA_TEMPLATE_CLOSE_CLEAR(view) clear_formatting_to_marker((view)->scope.tree)
#define LXA_TEMPLATE_CLOSE_RELEASE template_close_release
#define LXA_TEMPLATE_CLOSE_PUBLISH template_close_publish
#include "html/template_close.inc"
#undef LXA_TEMPLATE_CLOSE_NAME
#undef LXA_TEMPLATE_CLOSE_VIEW
#undef LXA_TEMPLATE_CLOSE_PLAN
#undef LXA_TEMPLATE_CLOSE_POP
#undef LXA_TEMPLATE_CLOSE_PATCH
#undef LXA_TEMPLATE_CLOSE_CLEAR
#undef LXA_TEMPLATE_CLOSE_RELEASE
#undef LXA_TEMPLATE_CLOSE_PUBLISH

static lxa_status_t
close_template(lxa_html_tree_t *tree, bool explicit_end)
{
    size_t length = lxa_vector_length(&tree->template_stack);
    size_t elements = lxa_vector_length(&tree->template_elements);
    lxa_html_template_frame_t frame = {0};
    size_t boundary = 0;
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
    if (!lxa_html_tree_template_credit_valid(elements, boundary,
            tree->nodes->budget->used[LXA_BUDGET_OPEN_ELEMENTS])) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    const tree_template_close_view_t view = {
        {tree, (const lxa_dom_ref_t *) tree->template_elements.bytes.data, elements},
        frame, length
    };
    return tree_template_close(&view, boundary, explicit_end,
        tree->template_patch.target.handle != LXA_HANDLE_NONE);
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
    return ns == LXA_DOM_NS_HTML && lxa_html_tree_table_context(name)
           ? template_foster_location(tree, parent, before) : LXA_OK;
}

static lxa_status_t
template_foster_element(lxa_html_tree_t *tree,
                        const lxa_html_token_t *token, bool keep_open,
                        lxa_dom_ref_t *created, lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t before, parent, child;
    size_t after, drop = SIZE_MAX;
    bool formatting = ns == LXA_DOM_NS_HTML && lxa_html_tree_formatting_tag(token->name);
    bool marker = ns == LXA_DOM_NS_HTML && lxa_html_tree_formatting_marker_tag(token->name);
    lxa_status_t status = LXA_OK;
    if (formatting) status = prepare_formatting_start(tree, token, &drop);
    else if (lxa_html_tree_foster_reconstructs(token->name, ns, keep_open))
        status = reconstruct_formatting(tree);
    if (status == LXA_OK && marker) status = reserve_formatting(tree, 1);
    if (status == LXA_OK)
        status = template_insertion_location(tree, &parent, &before);
    if (status != LXA_OK) return status;
    if (keep_open) {
        status = lxa_size_add(
            lxa_vector_length(&tree->template_elements), 1, &after);
        if (status == LXA_OK)
            status = check_template_elements(tree, after);
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
template_table_text_group(lxa_html_tree_t *tree, bool *group)
{
    *group = false;
    if (lxa_vector_length(&tree->template_elements) == template_boundary(tree)) return LXA_OK;
    lxa_span_t top;
    lxa_status_t status = lxa_dom_nodes_element_name(tree->nodes, template_parent(tree), &top);
    if (status == LXA_OK) *group = named(top, "colgroup");
    return status;
}

static lxa_status_t
template_table_text_foster(lxa_html_tree_t *tree, lxa_span_t bytes)
{
    lxa_dom_ref_t parent, table = {0};
    lxa_status_t status = template_insertion_location(tree, &parent, &table);
    return status == LXA_OK ? insert_text(tree, parent, table, bytes) : status;
}

#define LXA_TABLE_TEXT_FLUSH_NAME tree_flush_template_table_text
#define LXA_TABLE_TEXT_FLUSH_VIEW lxa_html_tree_t
#define LXA_TABLE_TEXT_FLUSH_BUDGET(view) ((view)->nodes->budget)
#define LXA_TABLE_TEXT_FLUSH_REASON(view) (&(view)->nodes->limit_reason)
#define LXA_TABLE_TEXT_FLUSH_APPEND(view, bytes, prefix) \
    ((void) (prefix), append_character(view, template_parent(view), LXA_DOM_KIND_TEXT, bytes))
#define LXA_TABLE_TEXT_FLUSH_GROUP template_table_text_group
#define LXA_TABLE_TEXT_FLUSH_PREFLIGHT(view) LXA_OK
#define LXA_TABLE_TEXT_FLUSH_POP(view) \
    template_pop_elements(view, lxa_vector_length(&(view)->template_elements) - 1)
#define LXA_TABLE_TEXT_FLUSH_RECONSTRUCT reconstruct_formatting
#define LXA_TABLE_TEXT_FLUSH_FOSTER template_table_text_foster
#include "html/table_text_flush.inc"
#undef LXA_TABLE_TEXT_FLUSH_NAME
#undef LXA_TABLE_TEXT_FLUSH_VIEW
#undef LXA_TABLE_TEXT_FLUSH_BUDGET
#undef LXA_TABLE_TEXT_FLUSH_REASON
#undef LXA_TABLE_TEXT_FLUSH_APPEND
#undef LXA_TABLE_TEXT_FLUSH_GROUP
#undef LXA_TABLE_TEXT_FLUSH_PREFLIGHT
#undef LXA_TABLE_TEXT_FLUSH_POP
#undef LXA_TABLE_TEXT_FLUSH_RECONSTRUCT
#undef LXA_TABLE_TEXT_FLUSH_FOSTER

static lxa_status_t
flush_template_table_text(lxa_html_tree_t *tree)
{
    if (tree->table_text.length == 0) return LXA_OK;
    if (lxa_vector_length(&tree->template_elements) < template_boundary(tree)) return LXA_ERROR_INTERNAL;
    lxa_status_t status = tree_flush_template_table_text(tree,
        (lxa_span_t){tree->table_text.data, tree->table_text.length});
    if (status == LXA_OK) lxa_buffer_clear(&tree->table_text);
    return status;
}

static lxa_status_t
begin_template(lxa_html_tree_t *tree,
               const lxa_html_token_t *token)
{
    lxa_dom_ref_t child, fragment, parent, before = {0}, attr = {0};
    lxa_dom_content_patch_t patch = {0};
    lxa_html_template_frame_t frame;
    bool nested = tree->mode == LXA_TREE_TEMPLATE_TEXT, shadow = false;
    size_t depth;
    lxa_status_t status;
    status = lxa_size_add(lxa_vector_length(&tree->template_stack),
                          nested ? 2 : 1, &depth);
    if (status == LXA_OK)
        status = check_template_depth(tree, depth,
            lxa_vector_length(&tree->template_elements),
            tree->mode == LXA_TREE_IN_BODY
                || (nested && tree->template_return_mode == LXA_TREE_IN_BODY));
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
    return tree_pop_table(tree, keep, true);
}

static lxa_status_t
template_open_foreign(lxa_html_tree_t *tree,
                      const lxa_html_token_t *token, lxa_dom_namespace_t ns)
{
    lxa_dom_ref_t child;
    lxa_span_t name = token->name;
    size_t count;
    lxa_status_t status = lxa_size_add(
        lxa_vector_length(&tree->template_elements), 1, &count);
    if (status == LXA_OK)
        status = check_template_elements(tree, count);
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
    return template_start_mode(tree, token, ns);
}

static lxa_status_t
template_open_table_part(lxa_html_tree_t *tree,
                         const lxa_html_token_t *token)
{
    lxa_html_token_t implied = {0};
    size_t length = lxa_vector_length(&tree->template_elements);
    bool column = named(token->name, "col");
    if (!lxa_html_tree_table_part(token->name) || named(token->name, "table")) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) tree->template_elements.bytes.data, length
    };
    lxa_html_template_table_start_plan_t plan;
    lxa_status_t status = tree_template_table_start_plan(&view,
        template_boundary(tree), tree->template_insertion_mode, token->name, &plan);
    if (status != LXA_OK) {
        assert(status != LXA_ERROR_INTERNAL);
        return status;
    }
    switch (plan.action) {
        case LXA_HTML_TABLE_START_IGNORE: return LXA_OK;
        case LXA_HTML_TABLE_START_POP_IGNORE:
        case LXA_HTML_TABLE_START_POP_REPROCESS:
            if (plan.nodes_before_pop != 0) {
                status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                    plan.nodes_before_pop, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
            }
            status = plan.table_pop
                ? template_pop_table_elements(tree, plan.insertion.keep)
                : template_pop_elements(tree, plan.insertion.keep);
            if (status != LXA_OK) return status;
            if (plan.action == LXA_HTML_TABLE_START_POP_IGNORE) return LXA_OK;
            return template_open_table_part(tree, token);
        case LXA_HTML_TABLE_START_INSERT: break;
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
    size_t keep = plan.insertion.keep, extra = plan.insertion.extra, after;
    bool implied_section = plan.insertion.implied_section;
    bool implied_row = plan.insertion.implied_row;
    status = lxa_size_add(keep, extra, &after);
    if (status == LXA_OK)
        status = check_template_elements(tree, after);
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
    lxa_html_token_t implied = {0};
    size_t length = lxa_vector_length(&tree->template_elements), after;
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) tree->template_elements.bytes.data, length
    };
    lxa_html_column_start_plan_t plan;
    lxa_status_t status = tree_column_start_plan(&view, template_boundary(tree),
        tree->template_insertion_mode, &plan);
    if (status != LXA_OK) return status;
    switch (plan.action) {
        case LXA_HTML_COLUMN_START_VIRTUAL:
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                1, &tree->nodes->limit_reason);
            if (status == LXA_OK)
                status = append_void(tree, template_parent(tree), token);
            return status;
        case LXA_HTML_COLUMN_START_IGNORE: return LXA_OK;
        case LXA_HTML_COLUMN_START_POP_IGNORE:
            return template_pop_elements(tree, plan.keep);
        case LXA_HTML_COLUMN_START_POP_REPROCESS:
            status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                plan.nodes_before_pop, &tree->nodes->limit_reason);
            if (status != LXA_OK) return status;
            status = template_pop_table_elements(tree, plan.keep);
            if (status != LXA_OK) return status;
            return template_open_column(tree, token);
        case LXA_HTML_COLUMN_START_TABLE_PART:
            return template_open_table_part(tree, token);
        case LXA_HTML_COLUMN_START_APPEND:
        case LXA_HTML_COLUMN_START_IMPLY_GROUP: break;
        default:
            assert(false);
            return LXA_ERROR_INTERNAL;
    }
    bool add_group = plan.action == LXA_HTML_COLUMN_START_IMPLY_GROUP;
    size_t count;
    count = add_group ? 2 : 1;
    status = lxa_size_add(length, count, &after);
    if (status == LXA_OK)
        status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
                                   count, &tree->nodes->limit_reason);
    if (status == LXA_OK)
        status = check_template_elements(tree, after);
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
    lxa_status_t status = lxa_buffer_init(&spaces, &tree->nodes->allocator);
    if (status != LXA_OK) return status;
    status = lxa_buffer_bind_budget(&spaces, tree->nodes->budget);
    if (status == LXA_OK)
        status = lxa_html_tree_filter_spaces(bytes, &spaces, &tree->nodes->limit_reason);
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
tree_template_column_group_plan(lxa_html_tree_t *tree, const lxa_html_token_t *token,
    lxa_html_column_group_action_t *action)
{
    bool group_open = false;
    if (tree->template_insertion_mode == LXA_TEMPLATE_COLUMN_GROUP
        && lxa_vector_length(&tree->template_elements) > template_boundary(tree)) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(tree->nodes->budget,
            LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_namespace(tree->nodes, template_parent(tree), &ns);
        if (status == LXA_OK)
            status = lxa_dom_nodes_element_name(tree->nodes, template_parent(tree), &name);
        if (status != LXA_OK) return status;
        group_open = ns == LXA_DOM_NS_HTML && named(name, "colgroup");
    }
    return lxa_html_tree_template_column_group(tree->template_insertion_mode, group_open,
        token->kind, token->name, action);
}

#define LXA_TEMPLATE_COLUMN_GROUP_NAME tree_execute_template_column_group
#define LXA_TEMPLATE_COLUMN_GROUP_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_COLUMN_GROUP_PLAN tree_template_column_group_plan
#define LXA_TEMPLATE_COLUMN_GROUP_EMIT(view, bytes) \
    append_space_text(view, template_parent(view), bytes, false)
#include "html/template_column_group_execute.inc"
#undef LXA_TEMPLATE_COLUMN_GROUP_NAME
#undef LXA_TEMPLATE_COLUMN_GROUP_VIEW
#undef LXA_TEMPLATE_COLUMN_GROUP_PLAN
#undef LXA_TEMPLATE_COLUMN_GROUP_EMIT

static lxa_status_t
template_plan_paragraph_close(lxa_html_tree_t *tree, size_t *index)
{
    return find_scoped_end_target(tree, &tree->template_elements,
        template_boundary(tree), literal("p"), LXA_HTML_SCOPE_BUTTON, index);
}

static lxa_status_t
template_container_read(const lxa_html_tree_scope_view_t *view, size_t index, lxa_span_t *name)
{
    return lxa_dom_nodes_element_name(view->tree->nodes, view->entries[index], name);
}

#define LXA_TEMPLATE_CONTAINER_NAME tree_template_container_plan
#define LXA_TEMPLATE_CONTAINER_VIEW lxa_html_tree_scope_view_t
#define LXA_TEMPLATE_CONTAINER_LENGTH(view) ((view)->length)
#define LXA_TEMPLATE_CONTAINER_RUBY(view, boundary, name, keep) \
    tree_ruby_start_plan(view, boundary, named(name, "rp") || named(name, "rt"), keep)
#define LXA_TEMPLATE_CONTAINER_LIST tree_list_item_plan
#define LXA_TEMPLATE_CONTAINER_PARAGRAPH(view, boundary, keep) \
    tree_scope_search(view, boundary, literal("p"), LXA_HTML_SCOPE_BUTTON, keep)
#define LXA_TEMPLATE_CONTAINER_READ template_container_read
#define LXA_TEMPLATE_CONTAINER_WORK(view, amount) \
    lxa_budget_charge((view)->tree->nodes->budget, LXA_BUDGET_WORK, amount, &(view)->tree->nodes->limit_reason)
#include "html/template_container_plan.inc"
#undef LXA_TEMPLATE_CONTAINER_NAME
#undef LXA_TEMPLATE_CONTAINER_VIEW
#undef LXA_TEMPLATE_CONTAINER_LENGTH
#undef LXA_TEMPLATE_CONTAINER_RUBY
#undef LXA_TEMPLATE_CONTAINER_LIST
#undef LXA_TEMPLATE_CONTAINER_PARAGRAPH
#undef LXA_TEMPLATE_CONTAINER_READ
#undef LXA_TEMPLATE_CONTAINER_WORK

static lxa_status_t
template_close_elements(lxa_html_tree_t *tree, size_t index)
{
    return template_pop_elements(tree, index);
}

static lxa_status_t
template_empty_paragraph(lxa_html_tree_t *tree)
{
    lxa_dom_ref_t parent, before, paragraph;
    lxa_status_t status = template_insertion_location(tree, &parent, &before);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_parsed_html_element(tree->nodes, literal("p"), &paragraph);
    return status == LXA_OK ? insert_constructed(tree, parent, paragraph, before) : status;
}

#define LXA_TEMPLATE_BODY_END_NAME template_body_end
#define LXA_TEMPLATE_BODY_END_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_BODY_END_LENGTH(tree) lxa_vector_length(&(tree)->template_elements)
#define LXA_TEMPLATE_BODY_END_BOUNDARY template_boundary
#define LXA_TEMPLATE_BODY_END_MODE(tree) ((tree)->template_insertion_mode)
#define LXA_TEMPLATE_BODY_END_SCOPED(tree, name, scope, index) \
    find_scoped_end_target(tree, &(tree)->template_elements, template_boundary(tree), name, scope, index)
#define LXA_TEMPLATE_BODY_END_ORDINARY(tree, name, index) \
    find_ordinary_end_target(tree, &(tree)->template_elements, template_boundary(tree), name, index)
#define LXA_TEMPLATE_BODY_END_POP template_close_elements
#define LXA_TEMPLATE_BODY_END_ADOPT adopt_formatting
#define LXA_TEMPLATE_BODY_END_PARAGRAPH template_empty_paragraph
#include "html/template_body_end.inc"
#undef LXA_TEMPLATE_BODY_END_NAME
#undef LXA_TEMPLATE_BODY_END_VIEW
#undef LXA_TEMPLATE_BODY_END_LENGTH
#undef LXA_TEMPLATE_BODY_END_BOUNDARY
#undef LXA_TEMPLATE_BODY_END_MODE
#undef LXA_TEMPLATE_BODY_END_SCOPED
#undef LXA_TEMPLATE_BODY_END_ORDINARY
#undef LXA_TEMPLATE_BODY_END_POP
#undef LXA_TEMPLATE_BODY_END_ADOPT
#undef LXA_TEMPLATE_BODY_END_PARAGRAPH

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
        status = begin_text_element_at(tree, tokenizer, parent, before, token);
    if (status == LXA_OK)
        status = template_start_mode(tree, token, LXA_DOM_NS_HTML);
    return status;
}

static lxa_status_t
template_end_plan(lxa_html_tree_t *tree, lxa_html_token_kind_t kind,
    lxa_span_t name, lxa_html_template_end_plan_t *plan)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) tree->template_elements.bytes.data,
        lxa_vector_length(&tree->template_elements)
    };
    return tree_template_end_plan(&view, template_boundary(tree), kind, name, plan);
}

#define LXA_TEMPLATE_END_NAME template_end_effects
#define LXA_TEMPLATE_END_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_END_PLAN template_end_plan
#define LXA_TEMPLATE_END_POP_TABLE template_pop_table_elements
#define LXA_TEMPLATE_END_POP template_pop_elements
#define LXA_TEMPLATE_END_CLEAR clear_formatting_to_marker
#include "html/template_end.inc"
#undef LXA_TEMPLATE_END_NAME
#undef LXA_TEMPLATE_END_VIEW
#undef LXA_TEMPLATE_END_PLAN
#undef LXA_TEMPLATE_END_POP_TABLE
#undef LXA_TEMPLATE_END_POP
#undef LXA_TEMPLATE_END_CLEAR

static lxa_status_t
template_end_formatting_boundary(lxa_html_tree_t *tree,
    const lxa_html_token_t *token, bool *handled)
{
    return template_end_effects(tree, token->kind, token->name, handled);
}

static lxa_status_t
plan_table_context(lxa_html_tree_t *tree, const lxa_vector_t *elements,
                     size_t boundary, bool virtual_table,
                     size_t *keep, bool *in_table)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) elements->bytes.data,
        lxa_vector_length(elements)
    };
    lxa_html_table_context_plan_t plan;
    lxa_status_t status = tree_table_context_plan(&view, boundary, virtual_table, &plan);
    if (status == LXA_OK) {
        *keep = plan.keep;
        *in_table = plan.in_table;
    }
    return status;
}

static lxa_status_t
hidden_table_plan(lxa_html_tree_t *tree, lxa_html_table_context_plan_t *plan)
{
    return plan_table_context(tree, formatting_elements(tree), formatting_boundary(tree),
        tree->mode == LXA_TREE_TEMPLATE_TEXT
            && lxa_html_tree_template_table_context(tree->template_insertion_mode),
        &plan->keep, &plan->in_table);
}

static lxa_status_t
hidden_table_insert(lxa_html_tree_t *tree, const lxa_html_token_t *token, size_t keep)
{
    if (keep == formatting_boundary(tree)) {
        if (tree->mode != LXA_TREE_TEMPLATE_TEXT) return LXA_ERROR_INTERNAL;
        return append_void(tree, tree->template_fragment, token);
    }
    const lxa_dom_ref_t *open =
        (const lxa_dom_ref_t *) formatting_elements(tree)->bytes.data;
    return append_void(tree, open[keep - 1], token);
}

#define LXA_HIDDEN_TABLE_NAME insert_hidden_table_input
#define LXA_HIDDEN_TABLE_VIEW lxa_html_tree_t
#define LXA_HIDDEN_TABLE_BUDGET(view) ((view)->nodes->budget)
#define LXA_HIDDEN_TABLE_REASON(view) (&(view)->nodes->limit_reason)
#define LXA_HIDDEN_TABLE_PLAN hidden_table_plan
#define LXA_HIDDEN_TABLE_INSERT hidden_table_insert
#define LXA_HIDDEN_TABLE_POP formatting_pop_elements
#include "html/hidden_table_input.inc"
#undef LXA_HIDDEN_TABLE_NAME
#undef LXA_HIDDEN_TABLE_VIEW
#undef LXA_HIDDEN_TABLE_BUDGET
#undef LXA_HIDDEN_TABLE_REASON
#undef LXA_HIDDEN_TABLE_PLAN
#undef LXA_HIDDEN_TABLE_INSERT
#undef LXA_HIDDEN_TABLE_POP

#define LXA_TEMPLATE_FORM_NAME tree_template_form_token
#define LXA_TEMPLATE_FORM_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_FORM_PLAN hidden_table_plan
#define LXA_TEMPLATE_FORM_INSERT hidden_table_insert
#define LXA_TEMPLATE_FORM_POP template_pop_elements
#include "html/template_form_token.inc"
#undef LXA_TEMPLATE_FORM_NAME
#undef LXA_TEMPLATE_FORM_VIEW
#undef LXA_TEMPLATE_FORM_PLAN
#undef LXA_TEMPLATE_FORM_INSERT
#undef LXA_TEMPLATE_FORM_POP

#define LXA_TEMPLATE_HTML_FOREIGN_NAME tree_template_html_foreign_start
#define LXA_TEMPLATE_HTML_FOREIGN_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_HTML_FOREIGN_CURRENT(view) \
    (lxa_vector_length(&(view)->template_elements) > template_boundary(view))
#define LXA_TEMPLATE_HTML_FOREIGN_BUDGET(view) ((view)->nodes->budget)
#define LXA_TEMPLATE_HTML_FOREIGN_REASON(view) (&(view)->nodes->limit_reason)
#define LXA_TEMPLATE_HTML_FOREIGN_NAMESPACE(view, out) \
    lxa_dom_nodes_element_namespace((view)->nodes, template_parent(view), out)
#define LXA_TEMPLATE_HTML_FOREIGN_CURRENT_NAME(view, out) \
    lxa_dom_nodes_element_name((view)->nodes, template_parent(view), out)
#define LXA_TEMPLATE_HTML_FOREIGN_INTEGRATION(view, out) \
    foreign_breakout_boundary(view, template_parent(view), out)
#define LXA_TEMPLATE_HTML_FOREIGN_RECONSTRUCT reconstruct_formatting
#define LXA_TEMPLATE_HTML_FOREIGN_START template_open_foreign
#include "html/template_html_foreign_start.inc"
#undef LXA_TEMPLATE_HTML_FOREIGN_NAME
#undef LXA_TEMPLATE_HTML_FOREIGN_VIEW
#undef LXA_TEMPLATE_HTML_FOREIGN_CURRENT
#undef LXA_TEMPLATE_HTML_FOREIGN_BUDGET
#undef LXA_TEMPLATE_HTML_FOREIGN_REASON
#undef LXA_TEMPLATE_HTML_FOREIGN_NAMESPACE
#undef LXA_TEMPLATE_HTML_FOREIGN_CURRENT_NAME
#undef LXA_TEMPLATE_HTML_FOREIGN_INTEGRATION
#undef LXA_TEMPLATE_HTML_FOREIGN_RECONSTRUCT
#undef LXA_TEMPLATE_HTML_FOREIGN_START

static lxa_status_t
tree_template_table_scope(lxa_html_tree_t *tree, size_t *index)
{
    return find_scoped_end_target(tree, &tree->template_elements, template_boundary(tree),
        literal("table"), LXA_HTML_SCOPE_TABLE, index);
}

#define LXA_TEMPLATE_TABLE_TOKEN_NAME tree_template_table_token
#define LXA_TEMPLATE_TABLE_TOKEN_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_TABLE_TOKEN_PLAN hidden_table_plan
#define LXA_TEMPLATE_TABLE_TOKEN_SCOPE tree_template_table_scope
#define LXA_TEMPLATE_TABLE_TOKEN_LENGTH(view) lxa_vector_length(&(view)->template_elements)
#define LXA_TEMPLATE_TABLE_TOKEN_POP template_pop_elements
#define LXA_TEMPLATE_TABLE_TOKEN_PREFLIGHT(view) \
    lxa_budget_check((view)->nodes->budget, LXA_BUDGET_NODES, 1, &(view)->nodes->limit_reason)
#define LXA_TEMPLATE_TABLE_TOKEN_POP_TABLE template_pop_table_elements
#include "html/template_table_token.inc"
#undef LXA_TEMPLATE_TABLE_TOKEN_NAME
#undef LXA_TEMPLATE_TABLE_TOKEN_VIEW
#undef LXA_TEMPLATE_TABLE_TOKEN_PLAN
#undef LXA_TEMPLATE_TABLE_TOKEN_SCOPE
#undef LXA_TEMPLATE_TABLE_TOKEN_LENGTH
#undef LXA_TEMPLATE_TABLE_TOKEN_POP
#undef LXA_TEMPLATE_TABLE_TOKEN_PREFLIGHT
#undef LXA_TEMPLATE_TABLE_TOKEN_POP_TABLE

static lxa_status_t
admit_head_noscript(lxa_html_tree_t *tree, const lxa_html_token_t *token)
{
    lxa_dom_ref_t child;
    lxa_status_t status = lxa_dom_nodes_append_parsed_html_element(tree->nodes, token->name, &child);
    if (status == LXA_OK) status = apply_attributes(tree, child, token);
    if (status == LXA_OK)
        status = insert_constructed(tree, tree->head, child, (lxa_dom_ref_t){0});
    if (status == LXA_OK) tree->head_noscript = child;
    return status;
}

#define LXA_HEAD_NOSCRIPT_NAME tree_start_head_noscript
#define LXA_HEAD_NOSCRIPT_VIEW lxa_html_tree_t
#define LXA_HEAD_NOSCRIPT_HEAD ensure_head
#define LXA_HEAD_NOSCRIPT_ADMIT admit_head_noscript
#define LXA_HEAD_NOSCRIPT_MODE(view) ((view)->mode)
#include "html/head_noscript_start.inc"
#undef LXA_HEAD_NOSCRIPT_NAME
#undef LXA_HEAD_NOSCRIPT_VIEW
#undef LXA_HEAD_NOSCRIPT_HEAD
#undef LXA_HEAD_NOSCRIPT_ADMIT
#undef LXA_HEAD_NOSCRIPT_MODE

typedef struct {
    lxa_html_tree_t *tree;
    lxa_html_tokenizer_t *tokenizer;
} tree_frameset_execute_view_t;

static lxa_dom_ref_t
frameset_parent(lxa_html_tree_t *tree)
{
    return tree->mode == LXA_TREE_IN_FRAMESET ? current_element(tree)
        : tree->mode == LXA_TREE_AFTER_AFTER_FRAMESET ? formatting_parent(tree) : tree->html;
}

static lxa_status_t
frameset_child(tree_frameset_execute_view_t *view, const lxa_html_token_t *token, bool text)
{
    lxa_html_tree_t *tree = view->tree;
    lxa_status_t status = lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
        1, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    return text ? begin_text_element(tree, view->tokenizer, frameset_parent(tree), token)
        : append_void(tree, current_element(tree), token);
}

#define LXA_FRAMESET_EXECUTE_NAME tree_execute_frameset
#define LXA_FRAMESET_EXECUTE_VIEW tree_frameset_execute_view_t
#define LXA_FRAMESET_EXECUTE_FINISH(view) tree_finish_root((view)->tree, &(view)->tree->mode)
#define LXA_FRAMESET_EXECUTE_COMMENT(view, token) append_comment_or_pi((view)->tree, \
    (view)->tree->mode == LXA_TREE_AFTER_AFTER_FRAMESET ? (view)->tree->document \
        : frameset_parent((view)->tree), token)
#define LXA_FRAMESET_EXECUTE_HTML(view, token) apply_attributes((view)->tree, (view)->tree->html, token)
#define LXA_FRAMESET_EXECUTE_SPACES(view, token) append_space_text((view)->tree, \
    frameset_parent((view)->tree), (token)->data, (view)->tree->mode == LXA_TREE_AFTER_AFTER_FRAMESET)
#define LXA_FRAMESET_EXECUTE_TEXT(view, token) frameset_child(view, token, true)
#define LXA_FRAMESET_EXECUTE_START(view, token) tree_start_nested_frameset((view)->tree, token)
#define LXA_FRAMESET_EXECUTE_FRAME(view, token) frameset_child(view, token, false)
#define LXA_FRAMESET_EXECUTE_END(view, token, action) \
    tree_frameset_end((view)->tree, action, &(view)->tree->mode)
#include "html/frameset_execute.inc"
#undef LXA_FRAMESET_EXECUTE_NAME
#undef LXA_FRAMESET_EXECUTE_VIEW
#undef LXA_FRAMESET_EXECUTE_FINISH
#undef LXA_FRAMESET_EXECUTE_COMMENT
#undef LXA_FRAMESET_EXECUTE_HTML
#undef LXA_FRAMESET_EXECUTE_SPACES
#undef LXA_FRAMESET_EXECUTE_TEXT
#undef LXA_FRAMESET_EXECUTE_START
#undef LXA_FRAMESET_EXECUTE_FRAME
#undef LXA_FRAMESET_EXECUTE_END

#define LXA_ROOT_END_PRELUDE_NAME tree_end_prelude
#define LXA_ROOT_END_PRELUDE_VIEW lxa_html_tree_t
#define LXA_ROOT_END_PRELUDE_GROUP tree_recover_root_group
#include "html/root_end_prelude.inc"
#undef LXA_ROOT_END_PRELUDE_NAME
#undef LXA_ROOT_END_PRELUDE_VIEW
#undef LXA_ROOT_END_PRELUDE_GROUP

static lxa_status_t
tree_root_void_insert(lxa_html_tree_t *tree, const lxa_html_token_t *token,
    bool head, bool hr, lxa_html_root_mode_t mode)
{
    if (head)
        return append_void(tree, mode == LXA_TREE_IN_BODY ? current_element(tree)
            : mode == LXA_TREE_HEAD_NOSCRIPT ? tree->head_noscript : tree->head, token);
    if (hr) {
        lxa_dom_ref_t parent, before;
        lxa_status_t status = root_insertion_location(tree, current_element(tree), &parent, &before);
        return status == LXA_OK ? append_closed_element_at(tree, parent, before, token, NULL) : status;
    }
    return append_void(tree, current_element(tree), token);
}

#define LXA_ROOT_VOID_NAME tree_start_root_void
#define LXA_ROOT_VOID_VIEW lxa_html_tree_t
#define LXA_ROOT_VOID_BODY ensure_body
#define LXA_ROOT_VOID_HEAD ensure_head
#define LXA_ROOT_VOID_FRAMESET(view) ((view)->frameset_ok)
#define LXA_ROOT_VOID_BUDGET(view) ((view)->nodes->budget)
#define LXA_ROOT_VOID_REASON(view) (&(view)->nodes->limit_reason)
#define LXA_ROOT_VOID_PARAGRAPH(view) close_paragraph(view, NULL)
#define LXA_ROOT_VOID_RECONSTRUCT reconstruct_formatting
#define LXA_ROOT_VOID_INSERT tree_root_void_insert
#include "html/root_void_start.inc"
#undef LXA_ROOT_VOID_NAME
#undef LXA_ROOT_VOID_VIEW
#undef LXA_ROOT_VOID_BODY
#undef LXA_ROOT_VOID_HEAD
#undef LXA_ROOT_VOID_FRAMESET
#undef LXA_ROOT_VOID_BUDGET
#undef LXA_ROOT_VOID_REASON
#undef LXA_ROOT_VOID_PARAGRAPH
#undef LXA_ROOT_VOID_RECONSTRUCT
#undef LXA_ROOT_VOID_INSERT

static lxa_status_t
tree_head_end_prepare(lxa_html_tree_t *tree, lxa_html_head_end_action_t action)
{
    if (action == LXA_HTML_HEAD_END_HTML) return LXA_OK;
    return lxa_budget_check(tree->nodes->budget, LXA_BUDGET_NODES,
        (tree->body.handle == LXA_HANDLE_NONE ? 1 : 0) + (action == LXA_HTML_HEAD_END_BR ? 1 : 0),
        &tree->nodes->limit_reason);
}

#define LXA_ROOT_HEAD_END_NAME tree_head_end
#define LXA_ROOT_HEAD_END_VIEW lxa_html_tree_t
#define LXA_ROOT_HEAD_END_PREPARE tree_head_end_prepare
#define LXA_ROOT_HEAD_END_BODY ensure_body
#include "html/root_head_end.inc"
#undef LXA_ROOT_HEAD_END_NAME
#undef LXA_ROOT_HEAD_END_VIEW
#undef LXA_ROOT_HEAD_END_PREPARE
#undef LXA_ROOT_HEAD_END_BODY

static lxa_status_t
tree_body_end_body(lxa_html_tree_t *tree, bool html, lxa_html_root_mode_t *mode)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) tree->open_elements.bytes.data,
        lxa_vector_length(&tree->open_elements)
    };
    return tree_end_body(&view, html, mode);
}

static lxa_status_t
tree_body_end_form(lxa_html_tree_t *tree)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) tree->open_elements.bytes.data,
        lxa_vector_length(&tree->open_elements)
    };
    return tree_end_form(&view, &tree->form.handle);
}

static lxa_status_t
tree_body_end_formatting(lxa_html_tree_t *tree, lxa_span_t name)
{
    lxa_status_t status = tree_recover_root_group(tree);
    return status == LXA_OK ? adopt_formatting(tree, name) : status;
}

#define LXA_ROOT_BODY_END_NAME tree_body_end_token
#define LXA_ROOT_BODY_END_VIEW lxa_html_tree_t
#define LXA_ROOT_BODY_END_BODY tree_body_end_body
#define LXA_ROOT_BODY_END_FORMATTING tree_body_end_formatting
#define LXA_ROOT_BODY_END_FORM tree_body_end_form
#define LXA_ROOT_BODY_END_PARAGRAPH tree_end_root_paragraph
#define LXA_ROOT_BODY_END_SCOPED end_scoped_element
#define LXA_ROOT_BODY_END_ORDINARY end_ordinary_element
#include "html/root_body_end.inc"
#undef LXA_ROOT_BODY_END_NAME
#undef LXA_ROOT_BODY_END_VIEW
#undef LXA_ROOT_BODY_END_BODY
#undef LXA_ROOT_BODY_END_FORMATTING
#undef LXA_ROOT_BODY_END_FORM
#undef LXA_ROOT_BODY_END_PARAGRAPH
#undef LXA_ROOT_BODY_END_SCOPED
#undef LXA_ROOT_BODY_END_ORDINARY

static lxa_status_t
tree_root_form_plan(lxa_html_tree_t *tree, lxa_html_table_context_plan_t *plan)
{
    return plan_table_context(tree, &tree->open_elements, 0, false,
        &plan->keep, &plan->in_table);
}

static lxa_status_t
tree_root_form_closed(lxa_html_tree_t *tree, const lxa_html_token_t *token, size_t keep)
{
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
    lxa_dom_ref_t child;
    lxa_status_t status = append_closed_element_at(tree, open[keep - 1],
        (lxa_dom_ref_t){0}, token, &child);
    if (status == LXA_OK) status = pop_elements(tree, &tree->open_elements, keep);
    if (status == LXA_OK) tree->form = child;
    return status;
}

static lxa_status_t
tree_root_form_open(lxa_html_tree_t *tree, const lxa_html_token_t *token)
{
    lxa_status_t status = lxa_budget_check(tree->nodes->budget,
        LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
    if (status == LXA_OK) status = close_paragraph(tree, NULL);
    if (status == LXA_OK) status = reserve_open_elements(tree, 1);
    if (status == LXA_OK) status = open_container(tree, token);
    if (status == LXA_OK) tree->form = current_element(tree);
    return status;
}

#define LXA_ROOT_FORM_NAME tree_start_root_form
#define LXA_ROOT_FORM_VIEW lxa_html_tree_t
#define LXA_ROOT_FORM_BODY ensure_body
#define LXA_ROOT_FORM_PLAN tree_root_form_plan
#define LXA_ROOT_FORM_OWNED(view) ((view)->form.handle != LXA_HANDLE_NONE)
#define LXA_ROOT_FORM_POP(view, keep) pop_elements(view, &(view)->open_elements, keep)
#define LXA_ROOT_FORM_CLOSED tree_root_form_closed
#define LXA_ROOT_FORM_OPEN tree_root_form_open
#include "html/root_form_start.inc"
#undef LXA_ROOT_FORM_NAME
#undef LXA_ROOT_FORM_VIEW
#undef LXA_ROOT_FORM_BODY
#undef LXA_ROOT_FORM_PLAN
#undef LXA_ROOT_FORM_OWNED
#undef LXA_ROOT_FORM_POP
#undef LXA_ROOT_FORM_CLOSED
#undef LXA_ROOT_FORM_OPEN

static lxa_status_t
tree_root_container_insert(lxa_html_tree_t *tree, const lxa_html_token_t *token)
{
    lxa_dom_ref_t parent, before;
    lxa_status_t status = root_insertion_location(tree, current_element(tree), &parent, &before);
    return status == LXA_OK
        ? open_container_at(tree, token, LXA_DOM_NS_HTML, parent, before) : status;
}

#define LXA_ROOT_CONTAINER_START_NAME tree_start_root_container
#define LXA_ROOT_CONTAINER_START_VIEW lxa_html_tree_t
#define LXA_ROOT_CONTAINER_START_BODY ensure_body
#define LXA_ROOT_CONTAINER_START_FRAMESET(view) ((view)->frameset_ok)
#define LXA_ROOT_CONTAINER_START_PREPARE(view, name) tree_prepare_root_container(view, name, false)
#define LXA_ROOT_CONTAINER_START_RECONSTRUCT reconstruct_formatting
#define LXA_ROOT_CONTAINER_START_INSERT tree_root_container_insert
#include "html/root_container_start.inc"
#undef LXA_ROOT_CONTAINER_START_NAME
#undef LXA_ROOT_CONTAINER_START_VIEW
#undef LXA_ROOT_CONTAINER_START_BODY
#undef LXA_ROOT_CONTAINER_START_FRAMESET
#undef LXA_ROOT_CONTAINER_START_PREPARE
#undef LXA_ROOT_CONTAINER_START_RECONSTRUCT
#undef LXA_ROOT_CONTAINER_START_INSERT

static lxa_status_t
tree_root_formatting_prepare(lxa_html_tree_t *tree, const lxa_html_token_t *token, size_t *drop)
{
    lxa_status_t status = tree_prepare_root_container(tree, token->name, true);
    return status == LXA_OK ? prepare_formatting_start(tree, token, drop) : status;
}

static lxa_status_t
tree_root_formatting_admit(lxa_html_tree_t *tree, const lxa_html_token_t *token, size_t drop)
{
    lxa_dom_ref_t parent, before;
    lxa_status_t status = reserve_open_elements(tree, 1);
    if (status == LXA_OK)
        status = root_insertion_location(tree, current_element(tree), &parent, &before);
    if (status == LXA_OK)
        status = open_container_at(tree, token, LXA_DOM_NS_HTML, parent, before);
    if (status == LXA_OK && drop != SIZE_MAX) status = remove_formatting(tree, drop);
    if (status == LXA_OK) status = append_formatting(tree, current_element(tree), false);
    return status;
}

#define LXA_ROOT_FORMATTING_START_NAME tree_start_root_formatting
#define LXA_ROOT_FORMATTING_START_VIEW lxa_html_tree_t
#define LXA_ROOT_FORMATTING_START_BODY ensure_body
#define LXA_ROOT_FORMATTING_START_FRAMESET(view) ((view)->frameset_ok)
#define LXA_ROOT_FORMATTING_START_PREPARE tree_root_formatting_prepare
#define LXA_ROOT_FORMATTING_START_ADMIT tree_root_formatting_admit
#include "html/root_formatting_start.inc"
#undef LXA_ROOT_FORMATTING_START_NAME
#undef LXA_ROOT_FORMATTING_START_VIEW
#undef LXA_ROOT_FORMATTING_START_BODY
#undef LXA_ROOT_FORMATTING_START_FRAMESET
#undef LXA_ROOT_FORMATTING_START_PREPARE
#undef LXA_ROOT_FORMATTING_START_ADMIT

#define LXA_ROOT_FOREIGN_START_NAME tree_start_root_foreign
#define LXA_ROOT_FOREIGN_START_VIEW lxa_html_tree_t
#define LXA_ROOT_FOREIGN_START_BODY ensure_body
#define LXA_ROOT_FOREIGN_START_RESERVE tree_root_foreign_reserve
#define LXA_ROOT_FOREIGN_START_RECONSTRUCT reconstruct_formatting
#define LXA_ROOT_FOREIGN_START_INSERT open_container_ns
#define LXA_ROOT_FOREIGN_START_POP pop_element
#include "html/root_foreign_start.inc"
#undef LXA_ROOT_FOREIGN_START_NAME
#undef LXA_ROOT_FOREIGN_START_VIEW
#undef LXA_ROOT_FOREIGN_START_BODY
#undef LXA_ROOT_FOREIGN_START_RESERVE
#undef LXA_ROOT_FOREIGN_START_RECONSTRUCT
#undef LXA_ROOT_FOREIGN_START_INSERT
#undef LXA_ROOT_FOREIGN_START_POP

#define LXA_ROOT_TABLE_EXEC_NAME tree_execute_table_start
#define LXA_ROOT_TABLE_EXEC_VIEW tree_foster_text_view_t
#define LXA_ROOT_TABLE_EXEC_TEMPLATE(view, token) begin_template((view)->tree, token)
#define LXA_ROOT_TABLE_EXEC_TEXT(view, token, group) \
    begin_root_table_text((view)->tree, (view)->tokenizer, token, group)
#define LXA_ROOT_TABLE_EXEC_PLAINTEXT(view, token) \
    begin_root_plaintext((view)->tree, (view)->tokenizer, token)
#define LXA_ROOT_TABLE_EXEC_XMP(view, token) begin_root_xmp((view)->tree, (view)->tokenizer, token)
#define LXA_ROOT_TABLE_EXEC_FOSTER_TEXT(view, token, group) \
    begin_root_foster_text((view)->tree, (view)->tokenizer, token, group)
#define LXA_ROOT_TABLE_EXEC_GROUP(view, container) tree_recover_foster_group((view)->tree, container)
#define LXA_ROOT_TABLE_EXEC_FOREIGN(view, token) foster_foreign((view)->tree, token)
#define LXA_ROOT_TABLE_EXEC_HTML(view, token, keep_open) foster_element((view)->tree, token, !(keep_open))
#include "html/root_table_start_execute.inc"
#undef LXA_ROOT_TABLE_EXEC_NAME
#undef LXA_ROOT_TABLE_EXEC_VIEW
#undef LXA_ROOT_TABLE_EXEC_TEMPLATE
#undef LXA_ROOT_TABLE_EXEC_TEXT
#undef LXA_ROOT_TABLE_EXEC_PLAINTEXT
#undef LXA_ROOT_TABLE_EXEC_XMP
#undef LXA_ROOT_TABLE_EXEC_FOSTER_TEXT
#undef LXA_ROOT_TABLE_EXEC_GROUP
#undef LXA_ROOT_TABLE_EXEC_FOREIGN
#undef LXA_ROOT_TABLE_EXEC_HTML

static lxa_status_t
begin_root_text(tree_foster_text_view_t *view, const lxa_html_token_t *token)
{
    lxa_html_tree_t *tree = view->tree;
    lxa_html_root_text_plan_t plan;
    lxa_status_t status = lxa_html_tree_root_text_plan(tree->mode, token->name, &plan);
    if (status != LXA_OK) return status;
    status = tree_prepare_root_text(tree, &plan, &tree->mode);
    if (status != LXA_OK) return status;
    lxa_dom_ref_t parent = plan.body ? current_element(tree)
        : tree->mode == LXA_TREE_HEAD_NOSCRIPT ? tree->head_noscript : tree->head;
    status = begin_text_element(tree, view->tokenizer, parent, token);
    if (status == LXA_OK && plan.skip_newline) tree->skip_pre_newline = true;
    return status;
}

#define LXA_ROOT_SPECIAL_EXEC_NAME tree_execute_special_start
#define LXA_ROOT_SPECIAL_EXEC_VIEW tree_foster_text_view_t
#define LXA_ROOT_SPECIAL_EXEC_BODY(view) ensure_body((view)->tree)
#define LXA_ROOT_SPECIAL_EXEC_HEAD(view) ensure_head((view)->tree)
#define LXA_ROOT_SPECIAL_EXEC_MODE(view) ((view)->tree->mode)
#define LXA_ROOT_SPECIAL_EXEC_TEMPLATE(view, token) begin_template((view)->tree, token)
#define LXA_ROOT_SPECIAL_EXEC_NOSCRIPT(view, token) tree_start_head_noscript((view)->tree, token)
#define LXA_ROOT_SPECIAL_EXEC_FORM(view, token) \
    tree_start_root_form((view)->tree, token, &(view)->tree->mode)
#define LXA_ROOT_SPECIAL_EXEC_TEXT begin_root_text
#define LXA_ROOT_SPECIAL_EXEC_XMP(view, token) begin_root_xmp((view)->tree, (view)->tokenizer, token)
#define LXA_ROOT_SPECIAL_EXEC_PLAINTEXT(view, token) \
    begin_root_plaintext((view)->tree, (view)->tokenizer, token)
#include "html/root_special_start_execute.inc"
#undef LXA_ROOT_SPECIAL_EXEC_NAME
#undef LXA_ROOT_SPECIAL_EXEC_VIEW
#undef LXA_ROOT_SPECIAL_EXEC_BODY
#undef LXA_ROOT_SPECIAL_EXEC_HEAD
#undef LXA_ROOT_SPECIAL_EXEC_MODE
#undef LXA_ROOT_SPECIAL_EXEC_TEMPLATE
#undef LXA_ROOT_SPECIAL_EXEC_NOSCRIPT
#undef LXA_ROOT_SPECIAL_EXEC_FORM
#undef LXA_ROOT_SPECIAL_EXEC_TEXT
#undef LXA_ROOT_SPECIAL_EXEC_XMP
#undef LXA_ROOT_SPECIAL_EXEC_PLAINTEXT

static lxa_status_t
tree_start_frameset_eligible(tree_foster_text_view_t *view, bool *eligible)
{
    lxa_html_tree_t *tree = view->tree;
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) tree->open_elements.bytes.data;
    size_t length = lxa_vector_length(&tree->open_elements);
    *eligible = lxa_html_tree_frameset_body_eligible(tree->frameset_ok,
        tree->body.handle, length, length == 0 ? LXA_HANDLE_NONE : open[0].handle);
    return LXA_OK;
}

#define LXA_ROOT_START_PRELUDE_NAME tree_start_prelude
#define LXA_ROOT_START_PRELUDE_VIEW tree_foster_text_view_t
#define LXA_ROOT_START_PRELUDE_MODE(view) ((view)->tree->mode)
#define LXA_ROOT_START_PRELUDE_GROUP(view) tree_recover_root_group((view)->tree)
#define LXA_ROOT_START_PRELUDE_ELIGIBLE tree_start_frameset_eligible
#define LXA_ROOT_START_PRELUDE_FRAMESET(view, token) begin_root_frameset((view)->tree, token, true)
#define LXA_ROOT_START_PRELUDE_ROOT(view, action, token) \
    tree_dispatch_root_start((view)->tree, action, token, &(view)->tree->mode)
#include "html/root_start_prelude.inc"
#undef LXA_ROOT_START_PRELUDE_NAME
#undef LXA_ROOT_START_PRELUDE_VIEW
#undef LXA_ROOT_START_PRELUDE_MODE
#undef LXA_ROOT_START_PRELUDE_GROUP
#undef LXA_ROOT_START_PRELUDE_ELIGIBLE
#undef LXA_ROOT_START_PRELUDE_FRAMESET
#undef LXA_ROOT_START_PRELUDE_ROOT

static lxa_status_t
tree_execute_ordinary_start(lxa_html_tree_t *tree, const lxa_html_token_t *token)
{
    lxa_status_t status;
    lxa_html_root_ordinary_start_action_t action;
    status = lxa_html_tree_root_ordinary_start_action(token->name,
        tree->nodes->budget, &tree->nodes->limit_reason, &action);
    if (status != LXA_OK) return status;
    if (action == LXA_HTML_ROOT_ORDINARY_HEAD_VOID)
        return tree_start_root_void(tree, token, true, false, &tree->mode);
    if (action == LXA_HTML_ROOT_ORDINARY_BODY_VOID)
        return tree_start_root_void(tree, token, false,
            lxa_html_tree_body_void_reconstructs(token->name), &tree->mode);
    if (action == LXA_HTML_ROOT_ORDINARY_TABLE_PART)
        return open_table_part(tree, token);
    if (action == LXA_HTML_ROOT_ORDINARY_FOREIGN)
        return tree_start_root_foreign(tree, token, &tree->mode);
    if (action == LXA_HTML_ROOT_ORDINARY_FORMATTING)
        return tree_start_root_formatting(tree, token, &tree->mode);
    if (action == LXA_HTML_ROOT_ORDINARY_CONTAINER) {
        bool newline;
        status = tree_start_root_container(tree, token, &tree->mode, &newline);
        if (status == LXA_OK && newline) tree->skip_pre_newline = true;
        return status;
    }
    assert(false);
    return LXA_ERROR_INTERNAL;
}

static lxa_status_t
tree_root_token_select(tree_foster_text_view_t *view, const lxa_html_token_t *token, bool *ignore)
{
    const lxa_html_tree_scope_view_t scope = {view->tree,
        (const lxa_dom_ref_t *) view->tree->open_elements.bytes.data,
        lxa_vector_length(&view->tree->open_elements)};
    return tree_execute_root_select(&scope, token->kind, token->name, ignore);
}

static lxa_status_t
tree_root_start_table_query(tree_foster_text_view_t *view, bool *table, bool *group)
{
    *table = *group = false;
    lxa_html_tree_t *tree = view->tree;
    if (lxa_vector_length(&tree->open_elements) <= 1) return LXA_OK;
    lxa_span_t top;
    lxa_status_t status = lxa_dom_nodes_element_name(tree->nodes, current_element(tree), &top);
    if (status == LXA_OK) {
        *table = lxa_html_tree_table_context(top);
        *group = named(top, "colgroup");
    }
    return status;
}

static lxa_status_t
tree_root_start_table(tree_foster_text_view_t *view, const lxa_html_token_t *token, bool group)
{
    lxa_html_root_table_start_action_t action;
    lxa_status_t status = lxa_html_tree_root_table_start_action(token->name,
        view->tree->nodes->budget, &view->tree->nodes->limit_reason, &action);
    return status == LXA_OK ? tree_execute_table_start(view, token, action, group) : status;
}

#define LXA_ROOT_START_EXEC_NAME tree_execute_root_start
#define LXA_ROOT_START_EXEC_VIEW tree_foster_text_view_t
#define LXA_ROOT_START_EXEC_PRELUDE tree_start_prelude
#define LXA_ROOT_START_EXEC_IGNORE(view) ((void) (view), LXA_OK)
#define LXA_ROOT_START_EXEC_MODE(view) ((view)->tree->mode)
#define LXA_ROOT_START_EXEC_HIDDEN(view, token, out) insert_hidden_table_input((view)->tree, token, out)
#define LXA_ROOT_START_EXEC_SELECT tree_root_token_select
#define LXA_ROOT_START_EXEC_TABLE_QUERY tree_root_start_table_query
#define LXA_ROOT_START_EXEC_TABLE tree_root_start_table
#define LXA_ROOT_START_EXEC_SPECIAL tree_execute_special_start
#define LXA_ROOT_START_EXEC_ORDINARY(view, token) tree_execute_ordinary_start((view)->tree, token)
#include "html/root_start_execute.inc"
#undef LXA_ROOT_START_EXEC_NAME
#undef LXA_ROOT_START_EXEC_VIEW
#undef LXA_ROOT_START_EXEC_PRELUDE
#undef LXA_ROOT_START_EXEC_IGNORE
#undef LXA_ROOT_START_EXEC_MODE
#undef LXA_ROOT_START_EXEC_HIDDEN
#undef LXA_ROOT_START_EXEC_SELECT
#undef LXA_ROOT_START_EXEC_TABLE_QUERY
#undef LXA_ROOT_START_EXEC_TABLE
#undef LXA_ROOT_START_EXEC_SPECIAL
#undef LXA_ROOT_START_EXEC_ORDINARY

static lxa_status_t
tree_root_misc_insert(lxa_html_tree_t *tree, const lxa_html_token_t *token,
    lxa_html_root_misc_destination_t destination)
{
    lxa_dom_ref_t parent;
    switch (destination) {
    case LXA_HTML_ROOT_MISC_IGNORE: return LXA_OK;
    case LXA_HTML_ROOT_MISC_DOCUMENT: parent = tree->document; break;
    case LXA_HTML_ROOT_MISC_HTML: parent = tree->html; break;
    case LXA_HTML_ROOT_MISC_HEAD: parent = tree->head; break;
    case LXA_HTML_ROOT_MISC_HEAD_NOSCRIPT: parent = tree->head_noscript; break;
    case LXA_HTML_ROOT_MISC_CURRENT: parent = current_element(tree); break;
    default:
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return append_comment_or_pi(tree, parent, token);
}

#define LXA_ROOT_MISC_NAME tree_execute_root_misc
#define LXA_ROOT_MISC_VIEW lxa_html_tree_t
#define LXA_ROOT_MISC_HTML ensure_html
#define LXA_ROOT_MISC_INSERT tree_root_misc_insert
#include "html/root_misc_execute.inc"
#undef LXA_ROOT_MISC_NAME
#undef LXA_ROOT_MISC_VIEW
#undef LXA_ROOT_MISC_HTML
#undef LXA_ROOT_MISC_INSERT

static lxa_status_t
tree_initial_doctype_insert(lxa_html_tree_t *tree, const lxa_html_token_t *token)
{
    const lxa_dom_doctype_data_t declaration = {
        .name = token->name, .public_id = token->public_id, .system_id = token->system_id,
        .has_public_id = token->has_public_id, .has_system_id = token->has_system_id,
        .force_quirks = token->force_quirks
    };
    lxa_dom_ref_t child;
    lxa_status_t status = lxa_dom_nodes_append_doctype(tree->nodes, declaration, &child);
    if (status == LXA_OK)
        status = insert_constructed(tree, tree->document, child, (lxa_dom_ref_t){0});
    return status;
}

#define LXA_ROOT_INITIAL_NAME tree_execute_initial
#define LXA_ROOT_INITIAL_VIEW lxa_html_tree_t
#define LXA_ROOT_INITIAL_RESOLVE(view, token, result) \
    lxa_html_tree_doctype_mode(token, (view)->nodes->budget, \
        &(view)->nodes->limit_reason, result)
#define LXA_ROOT_INITIAL_INSERT tree_initial_doctype_insert
#define LXA_ROOT_INITIAL_DOCUMENT_MODE(view) ((view)->document_mode)
#define LXA_ROOT_INITIAL_MODE(view) ((view)->mode)
#include "html/root_initial_execute.inc"
#undef LXA_ROOT_INITIAL_NAME
#undef LXA_ROOT_INITIAL_VIEW
#undef LXA_ROOT_INITIAL_RESOLVE
#undef LXA_ROOT_INITIAL_INSERT
#undef LXA_ROOT_INITIAL_DOCUMENT_MODE
#undef LXA_ROOT_INITIAL_MODE

static lxa_status_t
tree_template_foreign_end_route(lxa_html_tree_t *tree, const lxa_html_token_t *token,
    lxa_html_foreign_root_route_t *route)
{
    if (lxa_vector_length(&tree->template_elements) <= template_boundary(tree)) {
        *route = LXA_HTML_FOREIGN_ROOT_HTML;
        return LXA_OK;
    }
    lxa_dom_namespace_t ns;
    lxa_status_t status = lxa_dom_nodes_element_namespace(tree->nodes, template_parent(tree), &ns);
    if (status != LXA_OK) return status;
    if (ns == LXA_DOM_NS_HTML) {
        *route = LXA_HTML_FOREIGN_ROOT_HTML;
        return LXA_OK;
    }
    return lxa_html_foreign_root_plan(ns, LXA_HTML_FOREIGN_NONE, token,
        tree->nodes->budget, &tree->nodes->limit_reason, route);
}

static lxa_status_t
tree_template_foreign_end_breakout(lxa_html_tree_t *tree, const lxa_html_token_t *token)
{
    (void) token;
    size_t keep;
    lxa_status_t status = plan_foreign_breakout(tree, &tree->template_elements,
        template_boundary(tree), &keep);
    return status == LXA_OK ? template_pop_elements(tree, keep) : status;
}

static lxa_status_t
tree_template_foreign_end_consume(lxa_html_tree_t *tree, const lxa_html_token_t *token,
    bool *reprocess)
{
    size_t keep;
    lxa_status_t status = plan_foreign_end(tree, &tree->template_elements,
        template_boundary(tree), token->name, &keep, reprocess);
    if (status == LXA_OK && !*reprocess) status = template_pop_elements(tree, keep);
    return status;
}

#define LXA_ROOT_FOREIGN_DISPATCH_NAME tree_dispatch_template_foreign_end
#define LXA_ROOT_FOREIGN_DISPATCH_VIEW lxa_html_tree_t
#define LXA_ROOT_FOREIGN_DISPATCH_ROUTE tree_template_foreign_end_route
#define LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT tree_template_foreign_end_breakout
#define LXA_ROOT_FOREIGN_DISPATCH_CONSUME tree_template_foreign_end_consume
#include "html/root_foreign_dispatch.inc"
#undef LXA_ROOT_FOREIGN_DISPATCH_NAME
#undef LXA_ROOT_FOREIGN_DISPATCH_VIEW
#undef LXA_ROOT_FOREIGN_DISPATCH_ROUTE
#undef LXA_ROOT_FOREIGN_DISPATCH_BREAKOUT
#undef LXA_ROOT_FOREIGN_DISPATCH_CONSUME

#define LXA_TEMPLATE_END_DISPATCH_NAME tree_dispatch_template_end
#define LXA_TEMPLATE_END_DISPATCH_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_END_DISPATCH_FOREIGN tree_dispatch_template_foreign_end
#define LXA_TEMPLATE_END_DISPATCH_BR(view) lxa_html_tree_template_end_br_start( \
    (view)->template_insertion_mode, \
    lxa_vector_length(&(view)->template_elements) > template_boundary(view))
#define LXA_TEMPLATE_END_DISPATCH_CLOSE(view) close_template(view, true)
#define LXA_TEMPLATE_END_DISPATCH_BOUNDARY template_end_formatting_boundary
#include "html/template_end_dispatch.inc"
#undef LXA_TEMPLATE_END_DISPATCH_NAME
#undef LXA_TEMPLATE_END_DISPATCH_VIEW
#undef LXA_TEMPLATE_END_DISPATCH_FOREIGN
#undef LXA_TEMPLATE_END_DISPATCH_BR
#undef LXA_TEMPLATE_END_DISPATCH_CLOSE
#undef LXA_TEMPLATE_END_DISPATCH_BOUNDARY

#define LXA_ROOT_END_EXEC_NAME tree_execute_root_end
#define LXA_ROOT_END_EXEC_VIEW tree_foster_text_view_t
#define LXA_ROOT_END_EXEC_MODE(view) ((view)->tree->mode)
#define LXA_ROOT_END_EXEC_PREBODY(view, action) tree_prebody_end((view)->tree, action, &(view)->tree->mode)
#define LXA_ROOT_END_EXEC_PRELUDE(view, token, out) \
    tree_end_prelude((view)->tree, (view)->tree->mode, (token)->name, out)
#define LXA_ROOT_END_EXEC_SELECT tree_root_token_select
#define LXA_ROOT_END_EXEC_HEAD(view, action) tree_head_end((view)->tree, action, &(view)->tree->mode)
#define LXA_ROOT_END_EXEC_BODY(view, token) tree_body_end_token((view)->tree, (token)->name, \
    lxa_html_tree_root_body_end_plan((token)->name), &(view)->tree->mode)
#define LXA_ROOT_END_EXEC_START tree_execute_root_start
#define LXA_ROOT_END_EXEC_IGNORE(view) ((void) (view))
#define LXA_ROOT_END_EXEC_BR(view) ((void) (view))
#include "html/root_end_execute.inc"
#undef LXA_ROOT_END_EXEC_NAME
#undef LXA_ROOT_END_EXEC_VIEW
#undef LXA_ROOT_END_EXEC_MODE
#undef LXA_ROOT_END_EXEC_PREBODY
#undef LXA_ROOT_END_EXEC_PRELUDE
#undef LXA_ROOT_END_EXEC_SELECT
#undef LXA_ROOT_END_EXEC_HEAD
#undef LXA_ROOT_END_EXEC_BODY
#undef LXA_ROOT_END_EXEC_START
#undef LXA_ROOT_END_EXEC_IGNORE
#undef LXA_ROOT_END_EXEC_BR

#define LXA_ROOT_DATA_EXEC_NAME tree_execute_root_data
#define LXA_ROOT_DATA_EXEC_VIEW lxa_html_tree_t
#define LXA_ROOT_DATA_EXEC_MODE(view) ((view)->mode)
#define LXA_ROOT_DATA_EXEC_PREBODY prepare_prebody_text
#define LXA_ROOT_DATA_EXEC_PREPARE tree_prepare_root_data
#define LXA_ROOT_DATA_EXEC_EMIT(view, bytes, action) insert_data_text( \
    view, current_element(view), bytes, false, action == LXA_HTML_ROOT_DATA_TABLE)
#include "html/root_data_execute.inc"
#undef LXA_ROOT_DATA_EXEC_NAME
#undef LXA_ROOT_DATA_EXEC_VIEW
#undef LXA_ROOT_DATA_EXEC_MODE
#undef LXA_ROOT_DATA_EXEC_PREBODY
#undef LXA_ROOT_DATA_EXEC_PREPARE
#undef LXA_ROOT_DATA_EXEC_EMIT

#define LXA_TEMPLATE_DATA_NAME tree_execute_template_data
#define LXA_TEMPLATE_DATA_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_DATA_VALID(view) ((view)->template_fragment.handle == LXA_HANDLE_NONE \
    ? LXA_ERROR_INTERNAL : LXA_OK)
#define LXA_TEMPLATE_DATA_TABLE(view) (lxa_vector_length(&(view)->template_elements) \
    == template_boundary(view) && lxa_html_tree_template_table_context((view)->template_insertion_mode))
#define LXA_TEMPLATE_DATA_RECONSTRUCT reconstruct_formatting
#define LXA_TEMPLATE_DATA_EMIT(view, bytes, table) insert_data_text( \
    view, table ? (view)->template_fragment : template_parent(view), bytes, false, table)
#include "html/template_data_execute.inc"
#undef LXA_TEMPLATE_DATA_NAME
#undef LXA_TEMPLATE_DATA_VIEW
#undef LXA_TEMPLATE_DATA_VALID
#undef LXA_TEMPLATE_DATA_TABLE
#undef LXA_TEMPLATE_DATA_RECONSTRUCT
#undef LXA_TEMPLATE_DATA_EMIT

static lxa_status_t
tree_root_token_after_body(tree_foster_text_view_t *view,
    const lxa_html_token_t *token, lxa_html_after_body_action_t *action)
{
    lxa_status_t status = lxa_html_tree_after_body_action(token, action);
    if (status == LXA_OK) status = tree_execute_after_body(view->tree, token, *action);
    return status;
}

#define LXA_ROOT_TOKEN_NAME tree_execute_root_token
#define LXA_ROOT_TOKEN_VIEW tree_foster_text_view_t
#define LXA_ROOT_TOKEN_MODE(view) ((view)->tree->mode)
#define LXA_ROOT_TOKEN_MISC(view, token) tree_execute_root_misc((view)->tree, token, (view)->tree->mode)
#define LXA_ROOT_TOKEN_AFTER_BODY tree_root_token_after_body
#define LXA_ROOT_TOKEN_END tree_execute_root_end
#define LXA_ROOT_TOKEN_START tree_execute_root_start
#define LXA_ROOT_TOKEN_DATA(view, bytes) tree_execute_root_data((view)->tree, bytes)
#include "html/root_token_execute.inc"
#undef LXA_ROOT_TOKEN_NAME
#undef LXA_ROOT_TOKEN_VIEW
#undef LXA_ROOT_TOKEN_MODE
#undef LXA_ROOT_TOKEN_MISC
#undef LXA_ROOT_TOKEN_AFTER_BODY
#undef LXA_ROOT_TOKEN_END
#undef LXA_ROOT_TOKEN_START
#undef LXA_ROOT_TOKEN_DATA

static lxa_status_t
tree_root_noscript_dispatch(tree_foster_text_view_t *view,
    const lxa_html_token_t **token, lxa_html_token_t *adjusted, bool *handled)
{
    return tree_dispatch_head_noscript(view->tree, token, adjusted, handled);
}

static lxa_status_t
tree_root_foreign_dispatch(tree_foster_text_view_t *view,
    const lxa_html_token_t *token, bool *handled)
{
    tree_foreign_execute_view_t foreign_view = {view->tree, LXA_DOM_NS_HTML};
    return tree_dispatch_root_foreign(&foreign_view, token, handled);
}

#define LXA_ROOT_DISPATCH_NAME tree_dispatch_root
#define LXA_ROOT_DISPATCH_VIEW tree_foster_text_view_t
#define LXA_ROOT_DISPATCH_MODE(view) ((view)->tree->mode)
#define LXA_ROOT_DISPATCH_HAS_BODY(view) ((view)->tree->body.handle != LXA_HANDLE_NONE)
#define LXA_ROOT_DISPATCH_NOSCRIPT tree_root_noscript_dispatch
#define LXA_ROOT_DISPATCH_FRAMESET(view, token) begin_root_frameset((view)->tree, token, false)
#define LXA_ROOT_DISPATCH_FOREIGN tree_root_foreign_dispatch
#define LXA_ROOT_DISPATCH_INITIAL(view, token) tree_execute_initial((view)->tree, token, LXA_HTML_INITIAL_DOCTYPE)
#define LXA_ROOT_DISPATCH_HTML tree_execute_root_token
#include "html/root_dispatch.inc"
#undef LXA_ROOT_DISPATCH_NAME
#undef LXA_ROOT_DISPATCH_VIEW
#undef LXA_ROOT_DISPATCH_MODE
#undef LXA_ROOT_DISPATCH_HAS_BODY
#undef LXA_ROOT_DISPATCH_NOSCRIPT
#undef LXA_ROOT_DISPATCH_FRAMESET
#undef LXA_ROOT_DISPATCH_FOREIGN
#undef LXA_ROOT_DISPATCH_INITIAL
#undef LXA_ROOT_DISPATCH_HTML

#define LXA_TOKEN_INTAKE_NAME tree_execute_intake
#define LXA_TOKEN_INTAKE_VIEW lxa_html_tree_t
#define LXA_TOKEN_INTAKE_MODE(view) ((view)->mode)
#define LXA_TOKEN_INTAKE_COMMENT(view, token) append_comment_or_pi(view, (view)->document, token)
#define LXA_TOKEN_INTAKE_QUIRKS(view, token) tree_execute_initial(view, token, LXA_HTML_INITIAL_QUIRKS_REPROCESS)
#define LXA_TOKEN_INTAKE_TEXT(view, token) tree_text_token(view, token, &(view)->mode, NULL)
#define LXA_TOKEN_INTAKE_FLUSH(view) ((view)->mode == LXA_TREE_TEMPLATE_TEXT \
    ? flush_template_table_text(view) : flush_table_text(view))
#define LXA_TOKEN_INTAKE_NEWLINE(view) ((view)->skip_pre_newline)
#include "html/token_intake_execute.inc"
#undef LXA_TOKEN_INTAKE_NAME
#undef LXA_TOKEN_INTAKE_VIEW
#undef LXA_TOKEN_INTAKE_MODE
#undef LXA_TOKEN_INTAKE_COMMENT
#undef LXA_TOKEN_INTAKE_QUIRKS
#undef LXA_TOKEN_INTAKE_TEXT
#undef LXA_TOKEN_INTAKE_FLUSH
#undef LXA_TOKEN_INTAKE_NEWLINE

static lxa_status_t
tree_finish_templates(lxa_html_tree_t *tree)
{
    size_t work;
    lxa_status_t status = lxa_size_add(lxa_vector_length(&tree->template_stack),
        lxa_vector_length(&tree->template_elements), &work);
    if (status == LXA_OK) status = lxa_size_add(work, 1, &work);
    if (status == LXA_OK)
        status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, work, &tree->nodes->limit_reason);
    if (status != LXA_OK) return status;
    while (tree->mode == LXA_TREE_TEMPLATE_TEXT) {
        status = close_template(tree, false);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

#define LXA_EOF_EXEC_NAME tree_execute_eof
#define LXA_EOF_EXEC_VIEW lxa_html_tree_t
#define LXA_EOF_EXEC_MODE(view) ((view)->mode)
#define LXA_EOF_EXEC_TEMPLATES tree_finish_templates
#define LXA_EOF_EXEC_ROOT(view) tree_finish_root(view, &(view)->mode)
#include "html/eof_execute.inc"
#undef LXA_EOF_EXEC_NAME
#undef LXA_EOF_EXEC_VIEW
#undef LXA_EOF_EXEC_MODE
#undef LXA_EOF_EXEC_TEMPLATES
#undef LXA_EOF_EXEC_ROOT

static lxa_status_t
tree_template_foreign_namespace(lxa_html_tree_t *tree, lxa_dom_namespace_t *ns)
{
    if (lxa_vector_length(&tree->template_elements) <= template_boundary(tree)) {
        *ns = LXA_DOM_NS_HTML;
        return LXA_OK;
    }
    return lxa_dom_nodes_element_namespace(tree->nodes, template_parent(tree), ns);
}

static lxa_status_t
tree_template_foreign_data(lxa_html_tree_t *tree, const lxa_html_token_t *token, bool integrated)
{
    return integrated ? tree_execute_template_data(tree, token->data)
        : insert_data_text(tree, template_parent(tree), token->data, true, false);
}

static lxa_status_t
tree_template_foreign_table(lxa_html_tree_t *tree, const lxa_html_token_t *token, bool *handled)
{
    const lxa_html_tree_scope_view_t view = {
        tree, (const lxa_dom_ref_t *) tree->template_elements.bytes.data,
        lxa_vector_length(&tree->template_elements)
    };
    lxa_html_template_mathml_table_action_t action;
    lxa_status_t status = tree_template_mathml_table_plan(&view, template_boundary(tree),
        tree->template_insertion_mode, token->name, &action);
    if (status != LXA_OK) return status;
    switch (action) {
    case LXA_HTML_TEMPLATE_MATHML_TABLE_CONTINUE:
        break;
    case LXA_HTML_TEMPLATE_MATHML_TABLE_PART:
        status = named(token->name, "col") ? template_open_column(tree, token)
                                         : template_open_table_part(tree, token);
        break;
    case LXA_HTML_TEMPLATE_MATHML_TABLE_POP_IGNORE:
        status = template_pop_elements(tree, template_boundary(tree));
        break;
    default:
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    if (status == LXA_OK) *handled = action != LXA_HTML_TEMPLATE_MATHML_TABLE_CONTINUE;
    return status;
}

#define LXA_TEMPLATE_FOREIGN_DISPATCH_NAME tree_dispatch_template_foreign
#define LXA_TEMPLATE_FOREIGN_DISPATCH_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_FOREIGN_DISPATCH_NAMESPACE tree_template_foreign_namespace
#define LXA_TEMPLATE_FOREIGN_DISPATCH_CURRENT_NAME(view, out) \
    lxa_dom_nodes_element_name((view)->nodes, template_parent(view), out)
#define LXA_TEMPLATE_FOREIGN_DISPATCH_CONTEXT(view, ns, name, out) \
    element_foreign_context(view, template_parent(view), ns, name, out)
#define LXA_TEMPLATE_FOREIGN_DISPATCH_START template_open_foreign
#define LXA_TEMPLATE_FOREIGN_DISPATCH_DATA tree_template_foreign_data
#define LXA_TEMPLATE_FOREIGN_DISPATCH_TABLE tree_template_foreign_table
#define LXA_TEMPLATE_FOREIGN_DISPATCH_FONT(view, token, font) \
    lxa_html_tree_font_breakout(token, (view)->nodes->budget, &(view)->nodes->limit_reason, font)
#define LXA_TEMPLATE_FOREIGN_DISPATCH_BREAKOUT tree_template_foreign_end_breakout
#include "html/template_foreign_dispatch.inc"
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_NAME
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_VIEW
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_NAMESPACE
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_CURRENT_NAME
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_CONTEXT
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_START
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_DATA
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_TABLE
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_FONT
#undef LXA_TEMPLATE_FOREIGN_DISPATCH_BREAKOUT

typedef struct {
    lxa_html_tree_t *tree;
    lxa_html_tokenizer_t *tokenizer;
} tree_template_table_start_view_t;

static lxa_status_t
tree_template_table_void_preflight(tree_template_table_start_view_t *view)
{
    lxa_status_t status = lxa_budget_check(view->tree->nodes->budget, LXA_BUDGET_NODES,
        1, &view->tree->nodes->limit_reason);
    return status == LXA_OK ? lxa_budget_check(view->tree->nodes->budget, LXA_BUDGET_WORK,
        2, &view->tree->nodes->limit_reason) : status;
}

static lxa_status_t
tree_template_table_text_start(tree_template_table_start_view_t *view,
    const lxa_html_token_t *token, bool foster)
{
    if (!foster) return begin_template_xmp(view->tree, view->tokenizer, token);
    bool plaintext = named(token->name, "plaintext");
    lxa_dom_ref_t child;
    lxa_status_t status = template_foster_element(view->tree, token, plaintext, &child, LXA_DOM_NS_HTML);
    if (status == LXA_OK)
        status = plaintext ? set_tokenizer_text_mode(view->tree, view->tokenizer, token)
                           : enter_text_mode(view->tree, view->tokenizer, token, child);
    if (status == LXA_OK && named(token->name, "textarea")) view->tree->skip_pre_newline = true;
    return status;
}

#define LXA_TEMPLATE_TABLE_START_NAME tree_execute_template_table_start
#define LXA_TEMPLATE_TABLE_START_VIEW tree_template_table_start_view_t
#define LXA_TEMPLATE_TABLE_START_POP(view) template_pop_elements((view)->tree, \
    lxa_vector_length(&(view)->tree->template_elements) - 1)
#define LXA_TEMPLATE_TABLE_START_COLUMN(view, token) template_open_column((view)->tree, token)
#define LXA_TEMPLATE_TABLE_START_PART(view, token) template_open_table_part((view)->tree, token)
#define LXA_TEMPLATE_TABLE_START_FOSTER(view, token, keep, ns) \
    template_foster_element((view)->tree, token, keep, NULL, ns)
#define LXA_TEMPLATE_TABLE_START_TEXT_CHECK(view) \
    ((view)->tokenizer == NULL ? LXA_ERROR_ARGUMENT : LXA_OK)
#define LXA_TEMPLATE_TABLE_START_TEXT tree_template_table_text_start
#define LXA_TEMPLATE_TABLE_START_VOID_PREFLIGHT tree_template_table_void_preflight
#define LXA_TEMPLATE_TABLE_START_NEWLINE(view) ((view)->tree->skip_pre_newline)
#define LXA_TEMPLATE_TABLE_START_SUPPORTED(view, name, out) lxa_html_tree_supported_container(name, \
    (view)->tree->nodes->budget, &(view)->tree->nodes->limit_reason, out)
#include "html/template_table_start_execute.inc"
#undef LXA_TEMPLATE_TABLE_START_NAME
#undef LXA_TEMPLATE_TABLE_START_VIEW
#undef LXA_TEMPLATE_TABLE_START_POP
#undef LXA_TEMPLATE_TABLE_START_COLUMN
#undef LXA_TEMPLATE_TABLE_START_PART
#undef LXA_TEMPLATE_TABLE_START_FOSTER
#undef LXA_TEMPLATE_TABLE_START_TEXT_CHECK
#undef LXA_TEMPLATE_TABLE_START_TEXT
#undef LXA_TEMPLATE_TABLE_START_VOID_PREFLIGHT
#undef LXA_TEMPLATE_TABLE_START_NEWLINE
#undef LXA_TEMPLATE_TABLE_START_SUPPORTED

static lxa_status_t
tree_template_prepare_name(lxa_html_tree_t *tree, lxa_span_t *name)
{
    size_t length = lxa_vector_length(&tree->template_elements);
    const lxa_dom_ref_t *open = (const lxa_dom_ref_t *) tree->template_elements.bytes.data;
    lxa_status_t status = lxa_budget_charge(tree->nodes->budget, LXA_BUDGET_WORK, 1,
        &tree->nodes->limit_reason);
    return status == LXA_OK ? lxa_dom_nodes_element_name(tree->nodes, open[length - 1], name) : status;
}

#define LXA_TEMPLATE_PREPARE_START_NAME tree_prepare_template_start
#define LXA_TEMPLATE_PREPARE_START_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_PREPARE_START_CURRENT(view) \
    (lxa_vector_length(&(view)->template_elements) > template_boundary(view))
#define LXA_TEMPLATE_PREPARE_START_NAME_READ tree_template_prepare_name
#define LXA_TEMPLATE_PREPARE_START_POP(view) \
    template_pop_elements(view, lxa_vector_length(&(view)->template_elements) - 1)
#define LXA_TEMPLATE_PREPARE_START_MODE(view, token) template_start_mode(view, token, LXA_DOM_NS_HTML)
#define LXA_TEMPLATE_PREPARE_START_MODE_GET(view) ((view)->template_insertion_mode)
#define LXA_TEMPLATE_PREPARE_START_TABLE(view, found) \
    table_in_scope(view, &(view)->template_elements, template_boundary(view), found)
#include "html/template_prepare_start.inc"
#undef LXA_TEMPLATE_PREPARE_START_NAME
#undef LXA_TEMPLATE_PREPARE_START_VIEW
#undef LXA_TEMPLATE_PREPARE_START_CURRENT
#undef LXA_TEMPLATE_PREPARE_START_NAME_READ
#undef LXA_TEMPLATE_PREPARE_START_POP
#undef LXA_TEMPLATE_PREPARE_START_MODE
#undef LXA_TEMPLATE_PREPARE_START_MODE_GET
#undef LXA_TEMPLATE_PREPARE_START_TABLE

static lxa_status_t
tree_template_select(lxa_html_tree_t *tree, const lxa_html_token_t *token, bool *ignore)
{
    size_t keep;
    lxa_status_t status = plan_select_token(tree, &tree->template_elements,
        template_boundary(tree), token, &keep, ignore);
    return status == LXA_OK ? template_pop_elements(tree, keep) : status;
}

#define LXA_TEMPLATE_HTML_PRELUDE_NAME tree_execute_template_html_prelude
#define LXA_TEMPLATE_HTML_PRELUDE_VIEW lxa_html_tree_t
#define LXA_TEMPLATE_HTML_PRELUDE_PREPARE tree_prepare_template_start
#define LXA_TEMPLATE_HTML_PRELUDE_GROUP tree_execute_template_column_group
#define LXA_TEMPLATE_HTML_PRELUDE_HIDDEN insert_hidden_table_input
#define LXA_TEMPLATE_HTML_PRELUDE_SELECT tree_template_select
#include "html/template_html_prelude.inc"
#undef LXA_TEMPLATE_HTML_PRELUDE_NAME
#undef LXA_TEMPLATE_HTML_PRELUDE_VIEW
#undef LXA_TEMPLATE_HTML_PRELUDE_PREPARE
#undef LXA_TEMPLATE_HTML_PRELUDE_GROUP
#undef LXA_TEMPLATE_HTML_PRELUDE_HIDDEN
#undef LXA_TEMPLATE_HTML_PRELUDE_SELECT

static lxa_status_t
consume(lxa_html_tree_t *tree,
        lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token)
{
    lxa_status_t status;
    lxa_html_token_t adjusted;
    lxa_dom_ref_t child;
    bool template_ordinary;
    if (!lxa_html_tree_token_shape_valid(token)) return LXA_ERROR_ARGUMENT;
    bool intake_handled;
    status = tree_execute_intake(tree, &token, &adjusted, &intake_handled);
    if (status != LXA_OK || intake_handled) return status;
    if (tree->mode == LXA_TREE_TEMPLATE_TEXT) {
reprocess_template_token:
        if (token->kind == LXA_HTML_DOCTYPE) return LXA_OK;
        if (token->kind == LXA_HTML_COMMENT
            || token->kind == LXA_HTML_PROCESSING_INSTRUCTION)
            return append_comment_or_pi(tree, template_parent(tree), token);
        if (token->kind == LXA_HTML_START_TAG || token->kind == LXA_HTML_TEXT) {
            lxa_html_template_foreign_action_t action;
            status = tree_dispatch_template_foreign(tree, token, &action);
            if (status != LXA_OK || action == LXA_HTML_TEMPLATE_FOREIGN_CONSUMED) return status;
            if (action == LXA_HTML_TEMPLATE_FOREIGN_REPROCESS) goto reprocess_template_token;
        }
        {
            bool handled;
            status = tree_dispatch_template_end(tree, &token, &adjusted, &handled);
            if (status != LXA_OK || handled) return status;
        }
        {
            bool handled;
            status = tree_execute_template_html_prelude(tree, token, &handled);
            if (status != LXA_OK || handled) return status;
        }
        if (token->kind == LXA_HTML_START_TAG && named(token->name, "form")) {
            bool handled;
            status = tree_template_form_token(tree, token, &handled);
            if (status != LXA_OK || handled) return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && (named(token->name, "svg") || named(token->name, "math"))) {
            bool handled;
            status = tree_template_html_foreign_start(tree, token, &handled);
            if (status != LXA_OK || handled) return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "col")
            && lxa_vector_length(&tree->template_elements)
               == template_boundary(tree))
            return template_open_column(tree, token);
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "table")) {
            lxa_html_template_table_action_t action;
            status = tree_template_table_token(tree, &action);
            if (status != LXA_OK) return status;
            if (action == LXA_HTML_TEMPLATE_TABLE_IGNORE) return LXA_OK;
            if (action == LXA_HTML_TEMPLATE_TABLE_REPROCESS) goto reprocess_template_token;
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
            lxa_html_template_current_action_t current_action =
                lxa_html_tree_template_current_action(top, ns,
                    last == template_boundary(tree), token->kind, token->name);
            if (current_action == LXA_HTML_TEMPLATE_CURRENT_REPROCESS_START) {
                if (lxa_html_tree_template_html_text_element(token->name)
                    && tokenizer == NULL) return LXA_ERROR_ARGUMENT;
                status = lxa_budget_check(tree->nodes->budget,
                    LXA_BUDGET_NODES, 1, &tree->nodes->limit_reason);
                if (status != LXA_OK) return status;
                status = template_pop_elements(tree, last);
                if (status != LXA_OK) return status;
                goto reprocess_template_token;
            }
            if (current_action != LXA_HTML_TEMPLATE_CURRENT_CONTINUE) {
                if (current_action == LXA_HTML_TEMPLATE_CURRENT_REPROCESS_END) {
                    status = template_pop_elements(tree, last);
                    if (status != LXA_OK) return status;
                    goto reprocess_template_token;
                }
                if (token->kind == LXA_HTML_TEXT) {
                    return insert_data_text(tree, template_parent(tree),
                                            token->data, false, true);
                }
                if (token->kind == LXA_HTML_START_TAG) {
                    tree_template_table_start_view_t view = {tree, tokenizer};
                    bool handled;
                    status = tree_execute_template_table_start(&view, token, named(top, "colgroup"), &handled);
                    if (status != LXA_OK || handled) return status;
                }
                if (token->kind == LXA_HTML_END_TAG) {
                    switch (lxa_html_tree_template_table_end_action(token->name,
                        named(top, "colgroup"))) {
                        case LXA_HTML_TEMPLATE_TABLE_END_CONTINUE: break;
                        case LXA_HTML_TEMPLATE_TABLE_END_IGNORE:
                            return LXA_OK; /* The table bounds end-tag scope. */
                        case LXA_HTML_TEMPLATE_TABLE_END_POP_IGNORE:
                            return template_pop_elements(tree, last);
                        case LXA_HTML_TEMPLATE_TABLE_END_PARAGRAPH:
                            /* No paragraph is in scope; insert a closed one. */
                            adjusted = *token;
                            adjusted.kind = LXA_HTML_START_TAG;
                            if (named(top, "colgroup")) {
                                status = template_pop_elements(tree, last);
                                if (status != LXA_OK) return status;
                            }
                            return template_foster_element(tree, &adjusted, false,
                                                           NULL, LXA_DOM_NS_HTML);
                        case LXA_HTML_TEMPLATE_TABLE_END_ORDINARY: {
                            size_t index;
                            status = plan_ordinary_end_target(tree,
                                &tree->template_elements,
                                template_boundary(tree), token->name, &index);
                            if (status != LXA_OK) return status;
                            return template_close_elements(tree, index);
                        }
                        case LXA_HTML_TEMPLATE_TABLE_END_FORMATTING:
                            status = lxa_budget_charge(tree->nodes->budget,
                                LXA_BUDGET_WORK, 1, &tree->nodes->limit_reason);
                            if (status == LXA_OK) status = template_pop_elements(tree, last);
                            if (status != LXA_OK) return status;
                            goto reprocess_template_token;
                    }
                }
            }
            else if (ns == LXA_DOM_NS_HTML
                     && (named(top, "caption")
                      || named(top, "td") || named(top, "th"))
                     && token->kind == LXA_HTML_START_TAG
                     && (lxa_html_tree_table_section(token->name)
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
                || lxa_html_tree_table_section(token->name)
                || named(token->name, "tr")
                || named(token->name, "td")
                || named(token->name, "th")))
            return template_open_table_part(tree, token);
        if (token->kind == LXA_HTML_END_TAG)
            return template_body_end(tree, token->name);
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "template"))
            return begin_template(tree, token);
        if (token->kind == LXA_HTML_TEXT)
            return tree_execute_template_data(tree, token->data);
        if (token->kind == LXA_HTML_START_TAG && lxa_html_tree_head_void(token->name))
            return append_void(tree, template_parent(tree), token);
        if (token->kind == LXA_HTML_START_TAG
            && (lxa_html_tree_body_void(token->name) || named(token->name, "image"))) {
            if (lxa_html_tree_body_void_reconstructs(token->name)) {
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
            if (status == LXA_OK)
                status = template_start_mode(tree, token, LXA_DOM_NS_HTML);
            return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && lxa_html_tree_returning_text_element(token->name)
            && !named(token->name, "textarea")) {
            status = begin_text_element(tree, tokenizer, template_parent(tree),
                token);
            if (status == LXA_OK)
                status = template_start_mode(tree, token, LXA_DOM_NS_HTML);
            return status;
        }
        if (token->kind == LXA_HTML_START_TAG
            && named(token->name, "textarea")) {
            status = begin_text_element(tree, tokenizer, template_parent(tree),
                                        token);
            if (status == LXA_OK) {
                tree->skip_pre_newline = true;
                status = template_start_mode(tree, token, LXA_DOM_NS_HTML);
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
            template_ordinary = lxa_html_tree_template_container_start(token->name);
            if (!template_ordinary) {
                status = lxa_html_tree_supported_container(token->name,
                    tree->nodes->budget, &tree->nodes->limit_reason, &template_ordinary);
                if (status != LXA_OK) return status;
            }
        }
        if (template_ordinary) {
            size_t count, close_at, drop = SIZE_MAX;
            lxa_dom_ref_t parent, before = {0};
            bool formatting = lxa_html_tree_formatting_tag(token->name);
            bool marker = lxa_html_tree_formatting_marker_tag(token->name);
            if (named(token->name, "plaintext") && tokenizer == NULL)
                return LXA_ERROR_ARGUMENT;
            if (formatting) status = prepare_formatting_start(tree, token, &drop);
            else if (lxa_html_tree_template_container_reconstructs(token->name))
                status = reconstruct_formatting(tree);
            if (status == LXA_OK && marker) status = reserve_formatting(tree, 1);
            if (status != LXA_OK) return status;
            const lxa_html_tree_scope_view_t view = {
                tree, (const lxa_dom_ref_t *) tree->template_elements.bytes.data,
                lxa_vector_length(&tree->template_elements)
            };
            lxa_html_template_container_plan_t recovery;
            status = tree_template_container_plan(&view, template_boundary(tree), token->name,
                tree->document_mode == LXA_HTML_QUIRKS, &recovery);
            if (status != LXA_OK) return status;
            close_at = recovery.keep;
            if (recovery.foster) {
                status = template_pop_elements(tree, close_at);
                if (status != LXA_OK) return status;
                return template_foster_element(tree, token, true, NULL, LXA_DOM_NS_HTML);
            }
            status = lxa_size_add(close_at, 1, &count);
            if (status == LXA_OK)
                status = check_template_elements(tree, count);
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
            if (status == LXA_OK)
                status = template_start_mode(tree, token, LXA_DOM_NS_HTML);
            if (status == LXA_OK && named(token->name, "plaintext"))
                status = set_tokenizer_text_mode(tree, tokenizer, token);
            return status;
        }
        if (token->kind != LXA_HTML_EOF) {
            assert(false);
            return LXA_ERROR_INTERNAL;
        }
        return tree_execute_eof(tree);
    }
    if (token->kind == LXA_HTML_EOF) return tree_execute_eof(tree);
    if (lxa_html_tree_frameset_mode(tree->mode)) {
        lxa_html_frameset_action_t action;
        status = lxa_html_tree_frameset_action(tree->mode, token->kind,
            token->name, &action);
        if (status != LXA_OK) return status;
        tree_frameset_execute_view_t view = {tree, tokenizer};
        return tree_execute_frameset(&view, token, action);
    }
    tree_foster_text_view_t view = {tree, tokenizer, {0}};
    return tree_dispatch_root(&view, &token, &adjusted);
}

lxa_status_t
lxa_html_tree_on_token(void *context,
    lxa_html_tokenizer_t *tokenizer, const lxa_html_token_t *token)
{
    lxa_html_tree_t *tree = context;
    if (tree == NULL || tree->nodes == NULL) return LXA_ERROR_ARGUMENT;
    if (tree->failure != LXA_OK) return tree->failure;
    if (!lxa_html_tree_root_mode_valid(tree->mode)) {
        assert(false);
        tree->failure = LXA_ERROR_INTERNAL;
        return tree->failure;
    }
    if (tree->mode == LXA_TREE_DONE) return LXA_ERROR_ARGUMENT;
    if ((lxa_html_tree_mode_has_template(tree->mode, tree->text_context.return_mode)
         != (tree->template_fragment.handle != LXA_HANDLE_NONE))
        || (unsigned) tree->document_mode > LXA_HTML_QUIRKS
        || ((tree->mode == LXA_TREE_TEXT)
            != (tree->text_context.plan.mode != LXA_HTML_DATA))
        || (tree->mode == LXA_TREE_TEXT
            && (tree->text_element.handle == LXA_HANDLE_NONE
                || !lxa_html_text_context_valid(&tree->text_context)))) {
        assert(false);
        tree->failure = LXA_ERROR_INTERNAL;
        return tree->failure;
    }
    if (tree->template_fragment.handle != LXA_HANDLE_NONE
        && ((unsigned) tree->template_insertion_mode > LXA_TEMPLATE_BODY
            || !template_boundary_valid(tree)
            || !lxa_html_tree_template_return_mode_valid(tree->template_return_mode))) {
        assert(false);
        tree->failure = LXA_ERROR_INTERNAL;
        return tree->failure;
    }
    tree->failure = consume(tree, tokenizer, token);
    if (tree->failure == LXA_OK && tokenizer != NULL
        && token != NULL && token->kind != LXA_HTML_EOF) {
        bool in_template = tree->template_fragment.handle != LXA_HANDLE_NONE;
        const lxa_vector_t *elements = in_template
            ? &tree->template_elements : &tree->open_elements;
        const lxa_html_tree_scope_view_t view = {
            tree, (const lxa_dom_ref_t *) elements->bytes.data,
            lxa_vector_length(elements)
        };
        lxa_dom_namespace_t ns;
        tree->failure = tree_current_namespace(&view,
            in_template ? template_boundary(tree) : 0, &ns);
        if (tree->failure == LXA_OK)
            tree->failure = lxa_html_tree_namespace_feedback(tokenizer, ns);
    }
    /* A rejected store reference or token from our tokenizer is a pipeline
     * failure, not an invalid caller argument. */
    if (tree->failure == LXA_ERROR_ARGUMENT)
        tree->failure = LXA_ERROR_INTERNAL;
    return tree->failure;
}
