#ifndef LIBLEXA_HTML_TREE_CATEGORIES_H
#define LIBLEXA_HTML_TREE_CATEGORIES_H

#include "dom/namespace.h"
#include "html/tree_policy.h"
#include "html-categories.h"

/* Names are normalized HTML or adjusted foreign names. Predicates borrow
 * spans; stack owners retain traversal, WORK charges and mutation policy.
 */
typedef enum {
    LXA_HTML_SCOPE_NORMAL = 1,
    LXA_HTML_SCOPE_LIST_ITEM = 2,
    LXA_HTML_SCOPE_BUTTON = 4,
    LXA_HTML_SCOPE_TABLE = 8
} lxa_html_scope_kind_t;

static inline const char *
lxa_html_tree_namespace_name(lxa_dom_namespace_t ns)
{
    return ns == LXA_DOM_NS_HTML ? "html"
        : ns == LXA_DOM_NS_SVG ? "svg"
        : ns == LXA_DOM_NS_MATHML ? "mathml" : NULL;
}

static inline bool
lxa_html_tree_special_element(lxa_span_t name, lxa_dom_namespace_t ns)
{
    const char *namespace = lxa_html_tree_namespace_name(ns);
    if (namespace == NULL) return false;
    for (size_t i = 0; i < LIBLEXA_HTML_SPECIAL_COUNT; ++i) {
        const liblexa_html_special_tag *tag = &liblexa_html_special_tags[i];
        if (strcmp(tag->namespace_name, namespace) == 0
            && lxa_html_tree_name_equals(name, tag->tag, strlen(tag->tag)))
            return true;
    }
    return false;
}

/* Formatting and implied-end predicates require HTML-rule dispatch. */
static inline bool
lxa_html_tree_formatting_tag(lxa_span_t name)
{
    for (size_t i = 0; i < LIBLEXA_HTML_FORMATTING_COUNT; ++i) {
        const char *tag = liblexa_html_formatting_tags[i];
        if (lxa_html_tree_name_equals(name, tag, strlen(tag))) return true;
    }
    return false;
}

static inline bool
lxa_html_tree_formatting_marker_tag(lxa_span_t name)
{
#define LXA_MARKER_NAMED(text) lxa_html_tree_name_equals(name, text, sizeof(text) - 1)
    return LXA_MARKER_NAMED("td") || LXA_MARKER_NAMED("th")
        || LXA_MARKER_NAMED("caption") || LXA_MARKER_NAMED("object")
        || LXA_MARKER_NAMED("marquee") || LXA_MARKER_NAMED("applet");
#undef LXA_MARKER_NAMED
}

static inline bool
lxa_html_tree_scope_boundary(lxa_span_t name, lxa_dom_namespace_t ns,
                             lxa_html_scope_kind_t scope)
{
    const char *namespace = lxa_html_tree_namespace_name(ns);
    if (namespace == NULL) return false;
    for (size_t i = 0; i < LIBLEXA_HTML_SCOPE_BOUNDARY_COUNT; ++i) {
        const liblexa_html_scope_boundary *tag = &liblexa_html_scope_boundaries[i];
        if ((tag->mask & scope) != 0
            && strcmp(tag->namespace_name, namespace) == 0
            && lxa_html_tree_name_equals(name, tag->tag, strlen(tag->tag)))
            return true;
    }
    return false;
}

static inline bool
lxa_html_tree_implied_end_tag(lxa_span_t name)
{
    for (size_t i = 0; i < LIBLEXA_HTML_IMPLIED_END_COUNT; ++i) {
        const char *tag = liblexa_html_implied_end_tags[i];
        if (lxa_html_tree_name_equals(name, tag, strlen(tag))) return true;
    }
    return false;
}

#endif
